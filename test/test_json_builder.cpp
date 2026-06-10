#include <unity.h>
#include "../src/utils/JsonBuilder.h"
#include <cstring>

static JsonBuilder jb;

void test_jb_empty_obj() {
    jb.clear();
    jb.startObj(); jb.endObj();
    TEST_ASSERT_TRUE(jb.ok());
    TEST_ASSERT_EQUAL_STRING("{}", jb.str());
}

void test_jb_clear_twice() {
    jb.clear();
    jb.startObj(); jb.endObj();
    jb.clear();
    jb.startObj(); jb.endObj();
    TEST_ASSERT_EQUAL_STRING("{}", jb.str());
}

void test_jb_single_string() {
    jb.clear();
    jb.startObj();
    jb.addStr("name", "herlangga");
    jb.endObj();
    TEST_ASSERT_EQUAL_STRING("{\"name\":\"herlangga\"}", jb.str());
}

void test_jb_two_strings() {
    jb.clear();
    jb.startObj();
    jb.addStr("ssid", "MyWiFi");
    jb.addStr("pass", "secret123");
    jb.endObj();
    TEST_ASSERT_EQUAL_STRING("{\"ssid\":\"MyWiFi\",\"pass\":\"secret123\"}", jb.str());
}

void test_jb_string_escape_quotes() {
    jb.clear();
    jb.startObj();
    jb.addStr("key", "val\"ue");
    jb.endObj();
    TEST_ASSERT_EQUAL_STRING("{\"key\":\"val\\\"ue\"}", jb.str());
}

void test_jb_string_escape_backslash() {
    jb.clear();
    jb.startObj();
    jb.addStr("path", "C:\\dir\\file");
    jb.endObj();
    TEST_ASSERT_EQUAL_STRING("{\"path\":\"C:\\\\dir\\\\file\"}", jb.str());
}

void test_jb_string_escape_newline_tab() {
    jb.clear();
    jb.startObj();
    jb.addStr("text", "line1\nline2\tindent");
    jb.endObj();
    TEST_ASSERT_EQUAL_STRING("{\"text\":\"line1\\nline2\\tindent\"}", jb.str());
}

void test_jb_single_int() {
    jb.clear();
    jb.startObj();
    jb.addInt("count", 42);
    jb.endObj();
    TEST_ASSERT_EQUAL_STRING("{\"count\":42}", jb.str());
}

void test_jb_negative_int() {
    jb.clear();
    jb.startObj();
    jb.addInt("rssi", -67);
    jb.endObj();
    TEST_ASSERT_EQUAL_STRING("{\"rssi\":-67}", jb.str());
}

void test_jb_zero_int() {
    jb.clear();
    jb.startObj();
    jb.addInt("zero", 0);
    jb.endObj();
    TEST_ASSERT_EQUAL_STRING("{\"zero\":0}", jb.str());
}

void test_jb_single_float() {
    jb.clear();
    jb.startObj();
    jb.addFlt("weight", 3.5, 1);
    jb.endObj();
    TEST_ASSERT_EQUAL_STRING("{\"weight\":3.5}", jb.str());
}

void test_jb_float_two_decimals() {
    jb.clear();
    jb.startObj();
    jb.addFlt("delta", 0.15, 2);
    jb.endObj();
    TEST_ASSERT_EQUAL_STRING("{\"delta\":0.15}", jb.str());
}

void test_jb_bool_true() {
    jb.clear();
    jb.startObj();
    jb.addBool("active", true);
    jb.endObj();
    TEST_ASSERT_EQUAL_STRING("{\"active\":true}", jb.str());
}

void test_jb_bool_false() {
    jb.clear();
    jb.startObj();
    jb.addBool("active", false);
    jb.endObj();
    TEST_ASSERT_EQUAL_STRING("{\"active\":false}", jb.str());
}

void test_jb_empty_array() {
    jb.clear();
    jb.startObj();
    jb.startArr("items");
    jb.endArr();
    jb.endObj();
    TEST_ASSERT_EQUAL_STRING("{\"items\":[]}", jb.str());
}

void test_jb_array_of_strings() {
    jb.clear();
    jb.startObj();
    jb.startArr("tools");
    jb.addArrStr("hammer");
    jb.addArrStr("wrench");
    jb.endArr();
    jb.endObj();
    TEST_ASSERT_EQUAL_STRING("{\"tools\":[\"hammer\",\"wrench\"]}", jb.str());
}

void test_jb_array_of_ints() {
    jb.clear();
    jb.startObj();
    jb.startArr("ids");
    jb.addArrInt(1);
    jb.addArrInt(2);
    jb.addArrInt(3);
    jb.endArr();
    jb.endObj();
    TEST_ASSERT_EQUAL_STRING("{\"ids\":[1,2,3]}", jb.str());
}

void test_jb_array_of_objects() {
    jb.clear();
    jb.startObj();
    jb.startArr("networks");
    jb.startArrObj();
    jb.addStr("ssid", "WiFi1");
    jb.addInt("rssi", -50);
    jb.endObj();
    jb.startArrObj();
    jb.addStr("ssid", "WiFi2");
    jb.addInt("rssi", -70);
    jb.endObj();
    jb.endArr();
    jb.endObj();
    TEST_ASSERT_EQUAL_STRING("{\"networks\":[{\"ssid\":\"WiFi1\",\"rssi\":-50},{\"ssid\":\"WiFi2\",\"rssi\":-70}]}", jb.str());
}

void test_jb_status_response() {
    jb.clear();
    jb.startObj();
    jb.addFlt("weight", 1250.5, 1);
    jb.addFlt("baseline", 1200.0, 1);
    jb.addFlt("delta", 50.5, 1);
    jb.addBool("connected", true);
    jb.addStr("state", "idle");
    jb.addInt("contentCount", 3);
    jb.endObj();
    TEST_ASSERT_TRUE(jb.ok());
    const char* out = jb.str();
    TEST_ASSERT_NOT_NULL(strstr(out, "\"weight\":1250.5"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"connected\":true"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"state\":\"idle\""));
}

void test_jb_overflow_detection() {
    jb.clear();
    jb.startObj();
    for (int i = 0; i < 200; i++) {
        char key[16];
        snprintf(key, sizeof(key), "k%d", i);
        jb.addStr(key, "valuevaluevaluevaluevalue");
        if (!jb.ok()) break;
    }
    TEST_ASSERT_FALSE(jb.ok());
}

void test_jb_len_matches_strlen() {
    jb.clear();
    jb.startObj();
    jb.addStr("key", "value");
    jb.endObj();
    TEST_ASSERT_EQUAL(strlen(jb.str()), jb.len());
}
