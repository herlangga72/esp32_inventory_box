#include "DisplayManager.h"
#include "../domain/events/EventBus.h"
#include "../domain/services/DoorService.h"
#include "../domain/services/StateManager.h"
#include "../hal/SSD1306Driver.h"
#include "../hal/Lcd1602Driver.h"
#include "../utils/LogManager.h"
#include "../data/ToolRepository.h"
#include "../data/StorageManager.h"
#include <WiFi.h>
#include <cstring>

extern StorageManager storage;

// ---- Screen constants (match Screen enum) ----
enum : uint8_t { DM_STATUS, DM_EVENT_LOG, DM_SETTINGS, DM_CALIBRATION, DM_ERROR };

// ---- Drawing helpers ----
static void dm_drawCenteredText(SSD1306Driver* oled, int y, const char* text) {
    int len = strlen(text);
    int x = (128 - len * 6) / 2;
    if (x < 0) x = 0;
    oled->setCursor(x, y);
    oled->print(text);
}

static void dm_drawNotification(SSD1306Driver* oled, const DisplayManagerMemory* mem) {
    if (millis() >= mem->notificationEndMs) return;
    oled->fillRect(0, 58, 128, 6, 1);
    oled->setTextColor(0);
    oled->setCursor(4, 59);
    oled->print(mem->notificationText);
    oled->setTextColor(1);
}

// ---- Screen drawing functions ----

static void dm_drawStatusScreen(SSD1306Driver* oled, const DisplayManagerMemory* mem) {
    oled->clear();

    // Weight - large font
    char weightStr[16];
    snprintf(weightStr, sizeof(weightStr), "%.1f g", mem->displayWeight);
    dm_drawCenteredText(oled, 0, weightStr);

    // Baseline / Delta
    oled->setTextSize(1);
    char infoStr[32];
    snprintf(infoStr, sizeof(infoStr), "B: %.1f  D: %+.1f",
             mem->displayBaseline, mem->displayDelta);
    oled->setCursor(16, 20);
    oled->print(infoStr);

    // Contents count
    oled->setCursor(0, 32);
    oled->print("Contents: ");
    oled->print(mem->displayContentCount);

    // User
    oled->setCursor(0, 44);
    oled->print("User: ");
    oled->print(strlen(mem->displayUser) > 0 ? mem->displayUser : "None");

    // WiFi RSSI
    oled->setCursor(0, 56);
    oled->print("WiFi: ");
    if (WiFi.isConnected()) {
        oled->print(WiFi.RSSI());
        oled->print(" dBm");
    } else {
        oled->print("Offline");
    }
}

static void dm_drawEventLogScreen(SSD1306Driver* oled, const DisplayManagerMemory* mem) {
    (void)mem;
    oled->clear();

    oled->setTextSize(1);
    oled->setCursor(0, 0);
    oled->print("Event Log");

    // Show recent events (placeholder — real impl reads from log repo)
    oled->setCursor(0, 16);
    oled->print("Recent events...");

    oled->setCursor(0, 56);
    oled->print("[Button] Back");
}

static void dm_drawSettingsScreen(SSD1306Driver* oled, const DisplayManagerMemory* mem) {
    (void)mem;
    oled->clear();

    oled->setTextSize(1);
    oled->setCursor(0, 0);
    oled->print("Settings");

    oled->setCursor(0, 16);
    oled->print("1. Calibrate");
    oled->setCursor(0, 28);
    oled->print("2. WiFi Config");
    oled->setCursor(0, 40);
    oled->print("3. Sleep Mode");

    oled->setCursor(0, 56);
    oled->print("[Button] Back");
}

static void dm_drawCalibrationScreen(SSD1306Driver* oled, const DisplayManagerMemory* mem) {
    (void)mem;
    oled->clear();

    oled->setTextSize(1);
    dm_drawCenteredText(oled, 20, "Calibrating...");
    dm_drawCenteredText(oled, 36, "Place empty box");

    // Progress bar outline
    oled->drawRect(20, 50, 88, 8, 1);

    oled->setCursor(0, 56);
    oled->print("[Button] Cancel");
}

static void dm_drawErrorScreen(SSD1306Driver* oled, const DisplayManagerMemory* mem) {
    oled->clear();

    oled->setTextSize(1);
    dm_drawCenteredText(oled, 20, "ERROR");
    dm_drawCenteredText(oled, 32,
        mem->notificationText[0] ? mem->notificationText : "System Error");

    oled->setCursor(0, 56);
    oled->print("[Button] Retry");
}

// ======================================================================
// LCD 16x2 SCREEN DRAWING — 2 rows × 16 chars
// ======================================================================

static void dm_lcdPadRight(char* buf, size_t len) {
    // pad with spaces to clear leftover chars from previous write
    size_t pos = strlen(buf);
    while (pos < len) buf[pos++] = ' ';
    buf[len] = '\0';
}

static void dm_lcdDrawStatus(Lcd1602Driver* lcd, const DisplayManagerMemory* mem) {
    lcd->clear();

    // Row 0: "DOOR:OPEN W:450g" or "DOOR:CLSD W:450g"
    char row0[17];
    snprintf(row0, sizeof(row0), "DOOR:%s W:%.0fg",
             mem->doorOpen ? "OPEN" : "CLSD",
             mem->displayWeight);
    dm_lcdPadRight(row0, 16);
    lcd->setCursor(0, 0);
    lcd->print(row0);

    // Row 1: "U:username      "
    char row1[17];
    snprintf(row1, sizeof(row1), "U:%s",
             strlen(mem->displayUser) > 0 ? mem->displayUser : "None");
    dm_lcdPadRight(row1, 16);
    lcd->setCursor(0, 1);
    lcd->print(row1);
}

static void dm_lcdDrawContents(Lcd1602Driver* lcd, const DisplayManagerMemory* mem) {
    (void)mem;
    lcd->clear();

    // Read contents directly from StateManagerMemory
    StateManagerMemory* sm = g_registry.getStateManager();
    int count = sm_getContentCount(sm);
    const int32_t* ids = sm_getContents(sm);

    if (count == 0) {
        char row0[17];
        snprintf(row0, sizeof(row0), "Box empty       ");
        lcd->setCursor(0, 0);
        lcd->print(row0);
        lcd->setCursor(0, 1);
        lcd->print("                ");
        return;
    }

    // Look up tool names from repo cache
    ToolRepositoryMemory* tr = g_registry.getToolRepository();
    char name0[16] = "?";
    char name1[16] = "?";

    Tool* t = tr_findById(tr, &storage, ids[0]);
    if (t) snprintf(name0, sizeof(name0), "%s", t->name);

    if (count >= 2) {
        t = tr_findById(tr, &storage, ids[1]);
        if (t) snprintf(name1, sizeof(name1), "%s", t->name);
    }

    // Row 0: "1.Wrench        "
    char row0[17];
    snprintf(row0, sizeof(row0), "1.%.14s", name0);
    dm_lcdPadRight(row0, 16);
    lcd->setCursor(0, 0);
    lcd->print(row0);

    // Row 1: "2.Screwdriver   " or "+2 more       "
    char row1[17];
    if (count > 2) {
        snprintf(row1, sizeof(row1), "+%d more       ", count - 1);
    } else {
        snprintf(row1, sizeof(row1), "2.%.14s", name1);
    }
    dm_lcdPadRight(row1, 16);
    lcd->setCursor(0, 1);
    lcd->print(row1);
}

static void dm_lcdDrawInfo(Lcd1602Driver* lcd, const DisplayManagerMemory* mem) {
    lcd->clear();

    // Row 0: "B:1000 D:+23    "
    char row0[17];
    snprintf(row0, sizeof(row0), "B:%.0f D:%+.0f",
             mem->displayBaseline, mem->displayDelta);
    dm_lcdPadRight(row0, 16);
    lcd->setCursor(0, 0);
    lcd->print(row0);

    // Row 1: "WiFi:-45dBm STA "
    char row1[17];
    if (WiFi.isConnected()) {
        snprintf(row1, sizeof(row1), "WiFi:%ddBm STA",
                 static_cast<int>(WiFi.RSSI()));
    } else {
        snprintf(row1, sizeof(row1), "WiFi:OFFLINE    ");
    }
    dm_lcdPadRight(row1, 16);
    lcd->setCursor(0, 1);
    lcd->print(row1);
}

// ======================================================================

void dm_dispatchMessage(DisplayManagerMemory* mem, const ServiceMessage& msg) {
    switch (static_cast<DisplayMsgType>(msg.type)) {
        case DisplayMsgType::STATE_CHANGED:
            // Cache state change (b0=from, b1=to) — reserved for future use
            break;

        case DisplayMsgType::WEIGHT_UPDATE:
            mem->displayWeight   = msg.f2.f1;
            mem->displayDelta    = msg.f2.f2;
            // Baseline preserved from EventBus subscription or previous state
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
            strncpy(mem->notificationText,
                    reinterpret_cast<const char*>(msg.raw),
                    sizeof(mem->notificationText) - 1);
            mem->notificationText[sizeof(mem->notificationText) - 1] = '\0';
            mem->notificationEndMs = millis() + 3000;
            break;

        case DisplayMsgType::USER_LOGIN:
            snprintf(mem->displayUser, sizeof(mem->displayUser),
                     "UID:%d", static_cast<int>(msg.u4.u1));
            break;

        case DisplayMsgType::USER_LOGOUT:
            mem->displayUser[0] = '\0';
            break;

        case DisplayMsgType::SLEEP:
            mem->awake = false;
            break;

        case DisplayMsgType::WAKE:
            mem->awake = true;
            break;

        default:
            break;
    }
}

// ======================================================================
// PERIODIC UPDATE — 1Hz refresh, health check, draw
// ======================================================================

void dm_update(DisplayManagerMemory* mem, SSD1306Driver* oled, Lcd1602Driver* lcd) {
    // Drain mailbox
    ServiceMessage mbMsg;
    while (g_registry.tryReceive(ServiceId::DISPLAY_MANAGER, mbMsg)) {
        dm_dispatchMessage(mem, mbMsg);
    }

    bool isLCD = (mem->displayType == static_cast<uint8_t>(DisplayType::LCD1602));

    // ---- Health check every 5s (OLED only — LCD has no ping) ----
    if (!isLCD && millis() - mem->lastHealthCheckMs > 5000) {
        mem->lastHealthCheckMs = millis();

        if (oled->ping()) {
            if (!mem->healthy) {
                LOG_INFO("DISP", "recovered - OLED back on I2C bus");
            }
            mem->healthy = true;
        } else if (oled->init()) {
            mem->healthy = true;
            LOG_INFO("DISP", "reinit OK");
        } else {
            mem->healthy = false;
            LOG_INFO("DISP", "reinit FAILED - display offline");
        }
    }

    // ---- Refresh rate: 1Hz OLED, 2Hz LCD ----
    unsigned int interval = isLCD ? 500 : 1000;
    if (millis() - mem->lastRefreshMs < interval) return;

    if (isLCD) {
        // ---- LCD path ----
        if (!lcd || !lcd->isInitialized()) {
            mem->healthy = false;
            return;
        }
        mem->healthy = true;

        // Auto-cycle screens every 5s (3 screens: status / contents / info)
        static uint32_t lastScreenSwitch = 0;
        if (millis() - lastScreenSwitch > 5000) {
            mem->currentScreen = (mem->currentScreen + 1) % 3;
            lastScreenSwitch = millis();
        }

        // Read door state from DoorServiceMemory in registry
        mem->doorOpen = ds_isDoorOpen(g_registry.getDoorService());

        switch (mem->currentScreen) {
            case 0:
                dm_lcdDrawStatus(lcd, mem);
                break;
            case 1:
                dm_lcdDrawContents(lcd, mem);
                break;
            case 2:
            default:
                dm_lcdDrawInfo(lcd, mem);
                break;
        }
        mem->lastRefreshMs = millis();
        return;
    }

    // ---- OLED path ----
    // Apply pending sleep/wake state
    if (!mem->awake) {
        oled->sleep();
        mem->lastRefreshMs = millis();
        return;
    }

    if (!oled->isAwake()) {
        oled->wake();
    }

    if (!mem->healthy) return;

    // One-time EventBus subscription for backward compat
    static bool subscribed = false;
    if (!subscribed) {
        EventBus::getInstance()->subscribe(DomainEvent::WEIGHT_UPDATED,
            [](const EventPayload& e, void* ctx) {
                auto* m = static_cast<DisplayManagerMemory*>(ctx);
                m->displayWeight   = e.data.weight.weight;
                m->displayDelta    = e.data.weight.delta;
                m->displayBaseline = e.data.weight.baseline;
            }, mem);
        EventBus::getInstance()->subscribe(DomainEvent::TOOL_PLACED,
            [](const EventPayload& e, void* ctx) {
                auto* m = static_cast<DisplayManagerMemory*>(ctx);
                m->displayContentCount++;
                strncpy(m->notificationText, "Tool placed", sizeof(m->notificationText) - 1);
                m->notificationText[sizeof(m->notificationText) - 1] = '\0';
                m->notificationEndMs = millis() + 3000;
            }, mem);
        EventBus::getInstance()->subscribe(DomainEvent::TOOL_REMOVED,
            [](const EventPayload& e, void* ctx) {
                auto* m = static_cast<DisplayManagerMemory*>(ctx);
                if (m->displayContentCount > 0) m->displayContentCount--;
                strncpy(m->notificationText, "Tool removed", sizeof(m->notificationText) - 1);
                m->notificationText[sizeof(m->notificationText) - 1] = '\0';
                m->notificationEndMs = millis() + 3000;
            }, mem);
        subscribed = true;
        LOG_INFO("DISP", "EventBus subscriptions registered");
    }

    // Draw current screen
    switch (mem->currentScreen) {
        case DM_STATUS:
            dm_drawStatusScreen(oled, mem);
            break;
        case DM_EVENT_LOG:
            dm_drawEventLogScreen(oled, mem);
            break;
        case DM_SETTINGS:
            dm_drawSettingsScreen(oled, mem);
            break;
        case DM_CALIBRATION:
            dm_drawCalibrationScreen(oled, mem);
            break;
        case DM_ERROR:
            dm_drawErrorScreen(oled, mem);
            break;
        default:
            dm_drawStatusScreen(oled, mem);
            break;
    }

    if (millis() < mem->notificationEndMs) {
        dm_drawNotification(oled, mem);
    }

    oled->display();
    mem->lastRefreshMs = millis();
}
