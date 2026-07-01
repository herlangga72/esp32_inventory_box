#include "DisplayManager.h"
#include "../domain/events/EventBus.h"
#include "../domain/services/DoorService.h"
#include "../domain/services/StateManager.h"
#include "../hal/Lcd1602Driver.h"
#include "../utils/LogManager.h"
#include "../data/ToolRepository.h"
#include "../data/StorageManager.h"
#include <WiFi.h>
#include <cstring>

extern StorageManager storage;

// ---- LCD 16x2 helpers ----
static void lcdPad(char* buf, size_t len) {
    size_t pos = strlen(buf);
    while (pos < len) buf[pos++] = ' ';
    buf[len] = '\0';
}

static void lcdDrawStatus(Lcd1602Driver* lcd, const DisplayManagerMemory* mem) {
    lcd->clear();
    char row0[17];
    snprintf(row0, sizeof(row0), "DOOR:%s W:%.0fg",
             mem->doorOpen ? "OPEN" : "CLSD", mem->displayWeight);
    lcdPad(row0, 16);
    lcd->setCursor(0, 0); lcd->print(row0);

    char row1[17];
    snprintf(row1, sizeof(row1), "U:%s",
             strlen(mem->displayUser) > 0 ? mem->displayUser : "None");
    lcdPad(row1, 16);
    lcd->setCursor(0, 1); lcd->print(row1);
}

static void lcdDrawContents(Lcd1602Driver* lcd, const DisplayManagerMemory* mem) {
    (void)mem;
    lcd->clear();
    StateManagerMemory* sm = g_registry.getStateManager();
    int count = sm_getContentCount(sm);
    const int32_t* ids = sm_getContents(sm);

    if (count == 0) {
        lcd->setCursor(0, 0); lcd->print("Box empty       ");
        lcd->setCursor(0, 1); lcd->print("                ");
        return;
    }

    ToolRepositoryMemory* tr = g_registry.getToolRepository();
    char name0[16] = "?", name1[16] = "?";
    Tool* t = tr_findById(tr, &storage, ids[0]);
    if (t) snprintf(name0, sizeof(name0), "%s", t->name);
    if (count >= 2) {
        t = tr_findById(tr, &storage, ids[1]);
        if (t) snprintf(name1, sizeof(name1), "%s", t->name);
    }

    char row0[17];
    snprintf(row0, sizeof(row0), "1.%.14s", name0);
    lcdPad(row0, 16);
    lcd->setCursor(0, 0); lcd->print(row0);

    char row1[17];
    if (count > 2) snprintf(row1, sizeof(row1), "+%d more       ", count - 1);
    else snprintf(row1, sizeof(row1), "2.%.14s", name1);
    lcdPad(row1, 16);
    lcd->setCursor(0, 1); lcd->print(row1);
}

static void lcdDrawInfo(Lcd1602Driver* lcd, const DisplayManagerMemory* mem) {
    lcd->clear();
    char row0[17];
    snprintf(row0, sizeof(row0), "B:%.0f D:%+.0f", mem->displayBaseline, mem->displayDelta);
    lcdPad(row0, 16);
    lcd->setCursor(0, 0); lcd->print(row0);

    char row1[17];
    if (WiFi.isConnected()) snprintf(row1, sizeof(row1), "WiFi:%ddBm STA", static_cast<int>(WiFi.RSSI()));
    else snprintf(row1, sizeof(row1), "WiFi:OFFLINE    ");
    lcdPad(row1, 16);
    lcd->setCursor(0, 1); lcd->print(row1);
}

// ======================================================================

void dm_dispatchMessage(DisplayManagerMemory* mem, const ServiceMessage& msg) {
    switch (static_cast<DisplayMsgType>(msg.type)) {
        case DisplayMsgType::STATE_CHANGED: break;
        case DisplayMsgType::WEIGHT_UPDATE:
            mem->displayWeight = msg.f2.f1;
            mem->displayDelta  = msg.f2.f2;
            break;
        case DisplayMsgType::TOOL_PLACED:
            mem->displayContentCount++;
            strncpy(mem->notificationText, "Tool placed", sizeof(mem->notificationText) - 1);
            mem->notificationText[sizeof(mem->notificationText) - 1] = '\0';
            mem->notificationEndMs = millis() + 3000;
            break;
        case DisplayMsgType::TOOL_REMOVED:
            if (mem->displayContentCount > 0) mem->displayContentCount--;
            strncpy(mem->notificationText, "Tool removed", sizeof(mem->notificationText) - 1);
            mem->notificationText[sizeof(mem->notificationText) - 1] = '\0';
            mem->notificationEndMs = millis() + 3000;
            break;
        case DisplayMsgType::NOTIFICATION:
            strncpy(mem->notificationText, reinterpret_cast<const char*>(msg.raw), sizeof(mem->notificationText) - 1);
            mem->notificationText[sizeof(mem->notificationText) - 1] = '\0';
            mem->notificationEndMs = millis() + 3000;
            break;
        case DisplayMsgType::USER_LOGIN:
            snprintf(mem->displayUser, sizeof(mem->displayUser), "UID:%d", static_cast<int>(msg.u4.u1));
            break;
        case DisplayMsgType::USER_LOGOUT: mem->displayUser[0] = '\0'; break;
        case DisplayMsgType::SLEEP: mem->awake = false; break;
        case DisplayMsgType::WAKE:  mem->awake = true;  break;
        default: break;
    }
}

void dm_update(DisplayManagerMemory* mem, Lcd1602Driver* lcd) {
    // Drain mailbox
    ServiceMessage mbMsg;
    while (g_registry.tryReceive(ServiceId::DISPLAY_MANAGER, mbMsg)) {
        dm_dispatchMessage(mem, mbMsg);
    }

    if (!lcd || !lcd->isInitialized()) {
        mem->healthy = false;
        return;
    }
    mem->healthy = true;

    if (!mem->awake) return;

    // Auto-cycle screens every 5s
    static uint32_t lastSwitch = 0;
    if (millis() - lastSwitch > 5000) {
        mem->currentScreen = (mem->currentScreen + 1) % 3;
        lastSwitch = millis();
    }

    mem->doorOpen = ds_isDoorOpen(g_registry.getDoorService());

    switch (mem->currentScreen) {
        case 0: lcdDrawStatus(lcd, mem); break;
        case 1: lcdDrawContents(lcd, mem); break;
        case 2: lcdDrawInfo(lcd, mem); break;
    }
    mem->lastRefreshMs = millis();
}
