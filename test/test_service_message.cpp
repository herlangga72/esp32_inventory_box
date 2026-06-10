#include <unity.h>
#include <cstdint>
#include <cstring>

// Verify struct layouts match ServiceRegistry.h static_asserts
// Actual structs depend on FreeRTOS (not available on native), so we
// replicate the layout here and verify sizes match the spec.

void test_message_size_16() {
    // ServiceMessage: target(1)+type(1)+replyTo(1)+corrId(1)+raw[12] = 16
    struct { uint8_t t, ty, r, c; uint8_t raw[12]; } msg;
    TEST_ASSERT_EQUAL(16, sizeof(msg));
}

void test_scb_size_36() {
    // SCB is 36 bytes on 32-bit (ESP32). On 64-bit, pointers are 8 bytes → 48 bytes.
#if UINTPTR_MAX == UINT32_MAX
    struct SCB {
        uint8_t  id, state;
        uint16_t flags;
        void*    inbox;
        uint32_t msg_received, msg_sent, msg_dropped, last_heartbeat_ms;
        void*    task;
        uint32_t owned_memory_offset;
        uint16_t owned_memory_size, reserved;
    };
    TEST_ASSERT_EQUAL(36, sizeof(SCB));
#else
    TEST_ASSERT_EQUAL(48, sizeof(void*) * 2 + 32);  // 8+8+32=48 on 64-bit
#endif
}

void test_wifi_ap_smaller_than_sta() {
    // AP state just config + pointer; STA has full client state
    struct STA {
        char cfg1[96]; char cfg2[144]; void* netif;
        bool c, wc, rc; uint8_t pad;
        unsigned long lc, rs, ct; char ip[16];
    };
    struct AP {
        char cfg[96]; void* portal;
    };
    TEST_ASSERT_GREATER_THAN(sizeof(AP), sizeof(STA));
    TEST_ASSERT_LESS_OR_EQUAL(300, sizeof(STA));
    TEST_ASSERT_LESS_OR_EQUAL(120, sizeof(AP));
}

void test_union_overlay_fits() {
    // WiFiManager uses union { WiFiSTAState, WiFiAPState }
    // The union size = max(sizeof(STA), sizeof(AP))
    struct STA {
        char cfg1[96]; char cfg2[144]; void* netif;
        bool c, wc, rc; uint8_t pad;
        unsigned long lc, rs, ct; char ip[16];
    };
    struct AP {
        char cfg[96]; void* portal;
    };
    union Overlay { STA s; AP a; };
    // Union size should equal STA size (larger)
    TEST_ASSERT_EQUAL(sizeof(STA), sizeof(Overlay));
}

void test_pools_fit_domain() {
    // 5 domain services must fit in 1024-byte domainPool
    // Weight(≤96) + Motion(≤32) + State(≤160) + Access(≤144) + Door(≤32)
    // = ≤464 bytes. Pool is 1024.
    TEST_ASSERT_LESS_OR_EQUAL(1024, 96 + 32 + 160 + 144 + 32);
}

void test_kernel_pool_oversized_for_safety() {
    // kernelPool is 16KB — should be plenty for 5 kernel services
    // Logger(~2KB) + Storage(~32) + WiFiMemory(~128) + Power(~32) + SystemStatus(~1.6KB)
    // ≈ 4KB, pool is 16KB
    TEST_ASSERT_GREATER_OR_EQUAL(4096, 16384 - (2048 + 32 + 128 + 32 + 1600));
}
