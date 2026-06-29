#include "WebServer.h"
#include <SPIFFS.h>
#include "../hal/InterruptManager.h"
#include "../domain/services/StateManager.h"
#include "../data/ToolRepository.h"
#include "../data/UserRepository.h"
#include "../data/LogRepository.h"
#include "../domain/services/WeightService.h"
#include "../domain/services/AccessController.h"
#include "../kernel/ServerClient.h"
#include "../domain/entities/BoxState.h"
#include "../kernel/WiFiManager.h"
#include "../kernel/SystemStatus.h"
#include "../data/StorageManager.h"
#include "../utils/LogManager.h"
#include "../utils/JsonBuilder.h"
#include "../utils/JsonParser.h"

extern StorageManager storage;

// Registry shorthand
#define RSTATUS    g_registry.getSystemStatus()
#define RSTATE     g_registry.getStateManager()
#define RSTATE     g_registry.getStateManager()
#define RWEIGHT    g_registry.getWeightService()
#define RACCESS    g_registry.getAccessController()
#define RTOOLS     g_registry.getToolRepository()
#define RUSERS     g_registry.getUserRepository()

// ---- Request structs ----

struct CreateToolReq  { char name[32]; float weight; float tolerance; };
struct CreateUserReq  { char name[32]; char pin[8]; int fpId; };
struct LoginReq       { char pin[8]; };
struct WifiConfigReq  { char ssid[33]; char pass[65]; };

WebServerManager::WebServerManager(EventBus* events)
    : events(events), stateManager(nullptr), toolRepo(nullptr),
      userRepo(nullptr), logRepo(nullptr), weightService(nullptr),
      wifiManager(nullptr), systemStatus(nullptr),
      fpDriver(nullptr), serverClient(nullptr) {}

void WebServerManager::begin() {
    server.on("/", HTTP_GET, [this]() { handleRoot(); });
    server.on("/index.html", HTTP_GET, [this]() { handleRoot(); });

    server.on("/styles.css", HTTP_GET, [this]() { handleStaticFile("/styles.css", "text/css"); });
    server.on("/app.js", HTTP_GET, [this]() { handleStaticFile("/app.js", "application/javascript"); });

    server.on("/pages/dashboard.html", HTTP_GET, [this]() { handleStaticFile("/pages/dashboard.html", "text/html"); });
    server.on("/pages/tools.html", HTTP_GET, [this]() { handleStaticFile("/pages/tools.html", "text/html"); });
    server.on("/pages/users.html", HTTP_GET, [this]() { handleStaticFile("/pages/users.html", "text/html"); });
    server.on("/pages/logs.html", HTTP_GET, [this]() { handleStaticFile("/pages/logs.html", "text/html"); });
    server.on("/pages/diagnostics.html", HTTP_GET, [this]() { handleStaticFile("/pages/diagnostics.html", "text/html"); });
    server.on("/pages/config.html", HTTP_GET, [this]() { handleStaticFile("/pages/config.html", "text/html"); });
    server.on("/pages/wifi.html", HTTP_GET, [this]() { handleStaticFile("/pages/wifi.html", "text/html"); });

    server.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    server.on("/api/tools", HTTP_GET, [this]() { handleTools(); });
    server.on("/api/tools", HTTP_POST, [this]() { handleTools(); });
    server.on("/api/tools/*", HTTP_GET, [this]() { handleToolById(); });
    server.on("/api/tools/*", HTTP_PUT, [this]() { handleToolById(); });
    server.on("/api/tools/*", HTTP_DELETE, [this]() { handleToolById(); });
    server.on("/api/users", HTTP_GET, [this]() { handleUsers(); });
    server.on("/api/users", HTTP_POST, [this]() { handleUsers(); });
    server.on("/api/users/login", HTTP_POST, [this]() { handleUserLogin(); });
    server.on("/api/users/logout", HTTP_POST, [this]() { handleUserLogout(); });
    server.on("/api/users/*", HTTP_GET, [this]() { handleUserById(); });
    server.on("/api/users/*", HTTP_PUT, [this]() { handleUserById(); });
    server.on("/api/users/*", HTTP_DELETE, [this]() { handleUserDelete(); });
    server.on("/api/logs", HTTP_GET, [this]() { handleLogs(); });
    server.on("/api/logs/download", HTTP_GET, [this]() { handleLogsDownload(); });
    server.on("/api/logs/clear", HTTP_POST, [this]() { handleLogsClear(); });
    server.on("/api/calibrate", HTTP_POST, [this]() { handleCalibrate(); });
    server.on("/api/config", HTTP_GET, [this]() { handleConfig(); });
    server.on("/api/config", HTTP_POST, [this]() { handleConfig(); });
    server.on("/api/wifi", HTTP_GET, [this]() { handleWiFiStatus(); });
    server.on("/api/wifi", HTTP_POST, [this]() { handleWiFiConfig(); });
    server.on("/api/diagnostics", HTTP_GET, [this]() { handleDiagnostics(); });
    server.on("/api/restart", HTTP_POST, [this]() { handleRestart(); });
    server.on("/scan", HTTP_GET, [this]() { handleWiFiScan(); });

    // Contents management
    server.on("/api/contents/clear", HTTP_POST, [this]() { handleContentsClear(); });

    // Access control routes
    server.on("/api/access/status", HTTP_GET, [this]() { handleAccessStatus(); });
    server.on("/api/access/server", HTTP_GET, [this]() { handleAccessServerConfig(); });
    server.on("/api/access/server", HTTP_POST, [this]() { handleAccessServerConfig(); });
    server.on("/api/fingerprint/enroll", HTTP_POST, [this]() { handleFingerprintEnroll(); });
    server.on("/api/fingerprint/enroll/status", HTTP_GET, [this]() { handleFingerprintEnroll(); });
    server.on("/api/fingerprint/delete", HTTP_POST, [this]() { handleFingerprintDelete(); });
    server.on("/api/door", HTTP_GET, [this]() { handleDoorControl(); });
    server.on("/api/door/unlock", HTTP_POST, [this]() { handleDoorControl(); });

    server.begin();
    LOG_INFO("WEB", "Started");
}

void WebServerManager::handle() { server.handleClient(); }

// ---- Root ----

void WebServerManager::handleRoot() {
    spiffsLock();
    if (SPIFFS.exists("/index.html")) {
        File file = SPIFFS.open("/index.html", "r");
        server.streamFile(file, "text/html");
        file.close();
    } else {
        server.send(200, "text/html",
            "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
            "<title>ESP32 Inventory Box</title>"
            "<style>body{font-family:Arial;text-align:center;padding:50px;background:#1a3c6e;color:white;}"
            "h1{font-size:3em;margin-bottom:20px;}"
            ".btn{background:#27ae60;color:white;border:none;padding:15px 30px;font-size:1.2em;border-radius:5px;cursor:pointer;margin-top:20px;}"
            "</style></head><body>"
            "<h1>ESP32 Inventory Box</h1>"
            "<p>Web UI not found. Please upload SPIFFS files.</p>"
            "<button class='btn' onclick='fetch(\"/api/restart\",{method:\"POST\"})'>Restart System</button>"
            "</body></html>");
    }
    spiffsUnlock();
}

// ---- Status ----

void WebServerManager::handleStatus() {
    JsonBuilder jb;
    jb.startObj();

    jb.addBool("connected", wifiManager && wifiManager->isConnected());
    jb.addBool("apMode", wifiManager && wifiManager->isAPMode());

    if (wifiManager) {
        jb.addStr("ipAddress", wifiManager->getIP());
        jb.addInt("wifiRssi", wifiManager->getRSSI());
        jb.addStr("wifiSSID", wifiManager->getSSID());
    } else {
        jb.addStr("ipAddress", "--");
        jb.addInt("wifiRssi", 0);
        jb.addStr("wifiSSID", "--");
    }

    if (systemStatus) {
        const char* statusStr = "UNKNOWN";
        ComponentStatus overall = ss_getOverallStatus(RSTATUS);
        if (overall == ComponentStatus::OK) statusStr = "OK";
        else if (overall == ComponentStatus::WARNING) statusStr = "WARNING";
        else if (overall == ComponentStatus::ERROR) statusStr = "ERROR";
        jb.addStr("systemStatus", statusStr);
        jb.addBool("hasErrors", ss_hasErrors(RSTATUS));
    } else {
        jb.addStr("systemStatus", "UNKNOWN");
        jb.addBool("hasErrors", false);
    }

    {
        jb.addStr("state", stateToString((int)sm_getCurrentState(RSTATE)).c_str());
        jb.addInt("contents", sm_getContentCount(RSTATE));
        jb.addInt("currentUser", sm_getCurrentUserId(RSTATE));

        if (sm_getCurrentUserId(RSTATE) > 0 && userRepo) {
            User* u = ur_findById(RUSERS, &storage, sm_getCurrentUserId(RSTATE));
            jb.addStr("currentUserName", u ? u->name : "");
        } else {
            jb.addStr("currentUserName", "");
        }

        jb.startArr("contentsList");
        auto* st = RSTATE;
        for (int i = 0; i < sm_getContentCount(RSTATE); i++) {
            Tool* t = toolRepo ? tr_findById(RTOOLS, &storage, sm_getContents(RSTATE)[i]) : nullptr;
            if (t) {
                jb.startArrObj();
                jb.addInt("id", t->id);
                jb.addStr("name", t->name);
                jb.addFlt("weight", t->weightGrams);
                jb.endObj();
            }
        }
        jb.endArr();
    }

    if (weightService) {
        jb.addFlt("weight", ws_getCurrentWeight(RWEIGHT));
        jb.addFlt("baseline", ws_getBaseline(RWEIGHT));
        jb.addFlt("delta", ws_getDelta(RWEIGHT));
    }

    jb.addInt("uptime", (int)millis());
    jb.addInt("freeHeap", ESP.getFreeHeap());
    jb.endObj();

    sendJson(jb.str());
}

// ---- Tools ----

void WebServerManager::handleTools() {
    if (server.method() == HTTP_GET) {
        Tool tools[Config::MAX_TOOLS];
        int toolCount = tr_findAll(RTOOLS, &storage, tools, Config::MAX_TOOLS);

        JsonBuilder jb;
        jb.startObj();
        jb.startArr("tools");
        for (int _i = 0; _i < toolCount; _i++) {
            auto& tool = tools[_i];
            jb.startArrObj();
            jb.addInt("id", tool.id);
            jb.addStr("name", tool.name);
            jb.addFlt("weight", tool.weightGrams);
            jb.addFlt("tolerance", tool.toleranceGrams);
            jb.addBool("active", tool.active);
            jb.endObj();
        }
        jb.endArr();
        jb.endObj();

        sendJson(jb.str());

    } else if (server.method() == HTTP_POST) {
        CreateToolReq req; memset(&req, 0, sizeof(req)); req.tolerance = Config::DEFAULT_TOLERANCE;
        JField fields[] = {
            {"name",      JField::T_STR, req.name, sizeof(req.name)},
            {"weight",    JField::T_FLT, &req.weight},
            {"tolerance", JField::T_FLT, &req.tolerance},
        };
        String err;
        if (!jsonParse(server.arg("plain").c_str(), fields, 3, err)) {
            sendError(400, "Invalid JSON"); return;
        }

        Tool tool;
        if (req.name[0]) tool.setName(req.name);
        tool.weightGrams = req.weight;
        tool.toleranceGrams = req.tolerance;

        if (toolRepo) {
            int id = tr_create(RTOOLS, &storage, &tool);

            JsonBuilder jb;
            jb.startObj();
            jb.addBool("success", true);
            jb.addInt("id", id);
            jb.endObj();
            sendJson(jb.str());
        } else {
            sendError(500, "Tool repository not available");
        }
    }
}

void WebServerManager::handleToolById() {
    String path = server.uri();
    int id = path.substring(path.lastIndexOf('/') + 1).toInt();

    if (server.method() == HTTP_GET) {
        Tool* tool = toolRepo ? tr_findById(RTOOLS, &storage, id) : nullptr;
        if (tool) {
            JsonBuilder jb;
            jb.startObj();
            jb.addInt("id", tool->id);
            jb.addStr("name", tool->name);
            jb.addFlt("weight", tool->weightGrams);
            jb.addFlt("tolerance", tool->toleranceGrams);
            jb.addBool("active", tool->active);
            jb.endObj();
            sendJson(jb.str());
        } else {
            sendError(404, "Tool not found");
        }

    } else if (server.method() == HTTP_PUT) {
        CreateToolReq req; memset(&req, 0, sizeof(req)); req.tolerance = Config::DEFAULT_TOLERANCE;
        JField fields[] = {
            {"name",      JField::T_STR, req.name, sizeof(req.name)},
            {"weight",    JField::T_FLT, &req.weight},
            {"tolerance", JField::T_FLT, &req.tolerance},
        };
        String err;
        if (!jsonParse(server.arg("plain").c_str(), fields, 3, err)) {
            sendError(400, "Invalid JSON"); return;
        }

        Tool* tool = toolRepo ? tr_findById(RTOOLS, &storage, id) : nullptr;
        if (tool) {
            if (req.name[0]) tool->setName(req.name);
            tool->weightGrams = req.weight;
            tool->toleranceGrams = req.tolerance;
            tr_update(RTOOLS, &storage, id, tool);
            sendJson("{\"success\":true}");
        } else {
            sendError(404, "Tool not found");
        }

    } else if (server.method() == HTTP_DELETE) {
        if ( tr_remove(RTOOLS, &storage, id)) {
            sendJson("{\"success\":true}");
        } else {
            sendError(404, "Tool not found");
        }
    }
}

// ---- Users ----

void WebServerManager::handleUsers() {
    if (server.method() == HTTP_GET) {
        User users[Config::MAX_USERS];
        int userCount = ur_findAll(RUSERS, &storage, users, Config::MAX_USERS);

        JsonBuilder jb;
        jb.startObj();
        jb.startArr("users");
        for (int _i = 0; _i < userCount; _i++) {
            auto& user = users[_i];
            jb.startArrObj();
            jb.addInt("id", user.id);
            jb.addStr("name", user.name);
            jb.addBool("active", user.active);
            jb.addInt("fpId", user.fpId);
            jb.endObj();
        }
        jb.endArr();
        jb.endObj();

        sendJson(jb.str());

    } else if (server.method() == HTTP_POST) {
        CreateUserReq req; memset(&req, 0, sizeof(req));
        req.fpId = -1;
        JField fields[] = {
            {"name", JField::T_STR, req.name, sizeof(req.name)},
            {"pin",  JField::T_STR, req.pin,  sizeof(req.pin)},
            {"fpId", JField::T_INT, &req.fpId},
        };
        String err;
        if (!jsonParse(server.arg("plain").c_str(), fields, 3, err)) {
            sendError(400, "Invalid JSON"); return;
        }

        User user;
        if (req.name[0]) user.setName(req.name);
        if (req.pin[0]) user.setPin(req.pin);
        if (req.fpId >= 0) user.fpId = req.fpId;

        if (userRepo) {
            int id = ur_create(RUSERS, &storage, &user);

            JsonBuilder jb;
            jb.startObj();
            jb.addBool("success", true);
            jb.addInt("id", id);
            jb.endObj();
            sendJson(jb.str());
        } else {
            sendError(500, "User repository not available");
        }
    }
}

void WebServerManager::handleUserLogin() {
    LoginReq req; memset(&req, 0, sizeof(req));
    JField fields[] = {
        {"pin", JField::T_STR, req.pin, sizeof(req.pin)},
    };
    String err;
    if (!jsonParse(server.arg("plain").c_str(), fields, 1, err) || req.pin[0] == '\0') {
        sendError(400, "PIN required"); return;
    }

    User* user = userRepo ? ur_authenticate(RUSERS, &storage, req.pin) : nullptr;
    if (user) {
        ServiceMessage _sm = ServiceMessage::cmd(ServiceId::STATE_MANAGER, (uint8_t)StateMsgType::USER_LOGIN);
        _sm.u4.u1 = (uint16_t)(user->id);
        g_registry.send(ServiceId::STATE_MANAGER, _sm);

        JsonBuilder jb;
        jb.startObj();
        jb.addBool("success", true);
        jb.addInt("userId", user->id);
        jb.addStr("name", user->name);
        jb.endObj();
        sendJson(jb.str());
    } else {
        sendError(401, "Invalid PIN");
    }
}

void WebServerManager::handleUserLogout() {
    g_registry.sendCmd(ServiceId::STATE_MANAGER, (uint8_t)StateMsgType::USER_LOGOUT);
    sendJson("{\"success\":true}");
}

void WebServerManager::handleUserById() {
    int id = server.uri().substring(server.uri().lastIndexOf('/') + 1).toInt();
    if (id <= 0) { sendError(400, "Invalid user ID"); return; }

    if (server.method() == HTTP_GET) {
        User* user = ur_findById(RUSERS, &storage, id);
        if (!user) { sendError(404, "User not found"); return; }

        JsonBuilder jb;
        jb.startObj();
        jb.addInt("id", user->id);
        jb.addStr("name", user->name);
        jb.addStr("pin", user->pin);
        jb.addBool("active", user->active);
        jb.addInt("fpId", user->fpId);
        jb.addInt("usageSec", (int)user->totalUsageSeconds);
        jb.addInt("sessions", user->sessionCount);
        jb.addInt("placements", user->toolPlacements);
        jb.addInt("removals", user->toolRemovals);
        jb.endObj();
        sendJson(jb.str());
        return;
    }

    // PUT: update user fields
    struct { char name[32]; char pin[8]; int fpId; bool hasFpId; } req;
    memset(&req, 0, sizeof(req));
    req.fpId = -1;
    JField fields[] = {
        {"name", JField::T_STR, req.name, sizeof(req.name)},
        {"pin",  JField::T_STR, req.pin,  sizeof(req.pin)},
        {"fpId", JField::T_INT, &req.fpId},
    };
    String err;
    if (!jsonParse(server.arg("plain").c_str(), fields, 3, err)) {
        sendError(400, "Invalid JSON"); return;
    }

    User* user = ur_findById(RUSERS, &storage, id);
    if (!user) { sendError(404, "User not found"); return; }

    if (req.name[0]) user->setName(req.name);
    if (req.pin[0]) user->setPin(req.pin);
    if (req.fpId >= 0) user->fpId = req.fpId;

    if (ur_update(RUSERS, &storage, id, user)) {
        sendJson("{\"success\":true}");
    } else {
        sendError(500, "Update failed");
    }
}

void WebServerManager::handleUserDelete() {
    int id = server.uri().substring(server.uri().lastIndexOf('/') + 1).toInt();
    if ( ur_remove(RUSERS, &storage, id)) {
        sendJson("{\"success\":true}");
    } else {
        sendError(404, "User not found");
    }
}

// ---- Logs ----

void WebServerManager::handleLogs() {
    int limit = server.hasArg("limit") ? server.arg("limit").toInt() : 50;
    int offset = server.hasArg("offset") ? server.arg("offset").toInt() : 0;

    LogEntry logBuf[50];
    int logCount = logRepo ? logRepo->findFiltered(logBuf, 50, limit, offset, 0) : 0;

    JsonBuilder jb;
    jb.startObj();
    jb.startArr("logs");
    for (int li = 0; li < logCount; li++) {
        auto& entry = logBuf[li];
        jb.startArrObj();
        jb.addInt("ts", (int)entry.timestamp);
        jb.addInt("level", entry.severity);
        jb.addStr("tag", entry.event);
        jb.addStr("msg", entry.message);
        jb.addInt("userId", entry.userId);
        jb.addInt("toolId", entry.toolId);
        jb.addFlt("weight", entry.weightGrams);
        jb.endObj();
    }
    jb.endArr();
    jb.addInt("total", logRepo ? logRepo->count() : 0);
    jb.addInt("dropped", logRepo ? logRepo->getDropped() : 0);
    jb.addInt("fileSize", logRepo ? (int)logRepo->fileSize() : 0);
    jb.endObj();

    sendJson(jb.str());
}

void WebServerManager::handleLogsDownload() {
    if (!logRepo) { sendError(500, "Log repository not available"); return; }
    char csvBuf[4096];
    int csvLen = logRepo->downloadCSV(csvBuf, sizeof(csvBuf));
    server.send(200, "text/csv", String(csvBuf));
}

void WebServerManager::handleLogsClear() {
    if (logRepo) {
        logRepo->clear();
        sendJson("{\"success\":true}");
    } else {
        sendError(500, "Log repository not available");
    }
}

// ---- Contents ----

void WebServerManager::handleContentsClear() {
    sm_clearContents(RSTATE);
    sendJson("{\"success\":true}");
}

// ---- Calibrate ----

void WebServerManager::handleCalibrate() {
    {
        float baseline = ws_getBaseline(RWEIGHT);
        ServiceMessage _sm = ServiceMessage::cmd(ServiceId::STATE_MANAGER, (uint8_t)StateMsgType::CALIBRATION);
        _sm.f2.f1 = baseline;
        g_registry.send(ServiceId::STATE_MANAGER, _sm);
        storage.putFloat("baseline", baseline);

        JsonBuilder jb;
        jb.startObj();
        jb.addBool("success", true);
        jb.addFlt("baseline", baseline);
        jb.endObj();
        sendJson(jb.str());
    }
}

// ---- Config ----

void WebServerManager::handleConfig() {
    if (server.method() == HTTP_GET) {
        JsonBuilder jb;
        jb.startObj();
        jb.addStr("deviceName", "ESP32-Inventory-Box");
        jb.addStr("wifiSSID", WiFi.SSID().c_str());
        jb.addInt("wifiRssi", WiFi.RSSI());
        jb.addInt("freeHeap", ESP.getFreeHeap());
        jb.addInt("uptime", (int)millis());
        jb.addInt("logLevel", (int)logGetLevel());
        jb.endObj();
        sendJson(jb.str());
    } else {
        // POST: parse optional config fields
        struct CfgReq { float threshold; int settlingTime; float motionThreshold;
                        int lightSleep; int deepSleep; float defaultTolerance;
                        int maxContents; int logLevel; bool factoryReset; };
        CfgReq c;
        c.threshold = -1; c.settlingTime = -1; c.motionThreshold = -1;
        c.lightSleep = -1; c.deepSleep = -1; c.defaultTolerance = -1;
        c.maxContents = -1; c.logLevel = -1; c.factoryReset = false;

        JField fields[] = {
            {"threshold",        JField::T_FLT,  &c.threshold},
            {"settlingTime",     JField::T_INT,  &c.settlingTime},
            {"motionThreshold",  JField::T_FLT,  &c.motionThreshold},
            {"lightSleepTimeout",JField::T_INT,  &c.lightSleep},
            {"deepSleepTimeout", JField::T_INT,  &c.deepSleep},
            {"defaultTolerance", JField::T_FLT,  &c.defaultTolerance},
            {"maxContents",      JField::T_INT,  &c.maxContents},
            {"logLevel",         JField::T_INT,  &c.logLevel},
            {"factoryReset",     JField::T_BOOL, &c.factoryReset},
        };
        String err;
        if (!jsonParse(server.arg("plain").c_str(), fields, 9, err)) {
            sendError(400, "Invalid JSON"); return;
        }

        if (c.threshold >= 0)        storage.putFloat("cfg_threshold", c.threshold);
        if (c.settlingTime >= 0)     storage.putInt("cfg_settling", c.settlingTime);
        if (c.motionThreshold >= 0)  storage.putFloat("cfg_motion", c.motionThreshold);
        if (c.lightSleep >= 0)       storage.putInt("cfg_light", c.lightSleep);
        if (c.deepSleep >= 0)        storage.putInt("cfg_deep", c.deepSleep);
        if (c.defaultTolerance >= 0) storage.putFloat("cfg_tolerance", c.defaultTolerance);
        if (c.maxContents >= 0)      storage.putInt("cfg_maxcontents", c.maxContents);
        if (c.logLevel >= 0)         logSetLevel((LogLevel)c.logLevel);
        if (c.factoryReset)          { storage.clear(); ESP.restart(); }

        sendJson("{\"success\":true}");
    }
}

// ---- Diagnostics ----

void WebServerManager::handleDiagnostics() {
    JsonBuilder jb;
    jb.startObj();

    if (systemStatus) {
        ComponentStatus overall = ss_getOverallStatus(RSTATUS);
        const char* statusStr = "UNKNOWN";
        if (overall == ComponentStatus::OK) statusStr = "OK";
        else if (overall == ComponentStatus::WARNING) statusStr = "WARNING";
        else if (overall == ComponentStatus::ERROR) statusStr = "ERROR";

        jb.addStr("overallStatus", statusStr);
        jb.addInt("uptime", (int)ss_getUptime(RSTATUS));
        jb.addInt("totalErrors", ss_getErrorCount(RSTATUS));
        jb.addStr("lastError", ss_getLastError(RSTATUS));
        jb.addInt("okCount", ss_getOKCount(RSTATUS));
        jb.addInt("warningCount", ss_getWarningCount(RSTATUS));
        jb.addInt("errorCount", ss_getErrorComponentCount(RSTATUS));

        jb.startArr("components");
        for (int ci = 0; ci < RSTATUS->componentCount; ci++) {
            auto& comp = RSTATUS->components[ci];
            jb.startArrObj();
            jb.addStr("name", comp.name);

            const char* s = "UNKNOWN";
            if (comp.status == ComponentStatus::OK) s = "OK";
            else if (comp.status == ComponentStatus::WARNING) s = "WARNING";
            else if (comp.status == ComponentStatus::ERROR) s = "ERROR";
            jb.addStr("status", s);

            jb.addStr("lastError", comp.lastError);
            jb.addInt("errorCount", comp.errorCount);
            jb.endObj();
        }
        jb.endArr();
    } else {
        jb.addStr("overallStatus", "UNKNOWN");
        jb.addInt("uptime", 0);
        jb.addInt("totalErrors", 0);
        jb.addStr("lastError", "SystemStatus not available");
        jb.startArr("components");
        jb.endArr();
    }

    jb.endObj();
    sendJson(jb.str());
}

// ---- Restart ----

void WebServerManager::handleRestart() {
    LOG_INFO("WEB", "Restart requested via API");
    sendJson("{\"success\":true,\"message\":\"Restarting...\"}");
    delay(500);
    ESP.restart();
}

// ---- WiFi ----

void WebServerManager::handleWiFiStatus() {
    JsonBuilder jb;
    jb.startObj();
    jb.addBool("connected", wifiManager && wifiManager->isConnected());
    jb.addBool("apMode", wifiManager && wifiManager->isAPMode());

    if (wifiManager) {
        jb.addStr("ip", wifiManager->getIP());
        jb.addStr("ssid", wifiManager->getSSID());
        jb.addInt("rssi", wifiManager->getRSSI());
    }

    jb.endObj();
    sendJson(jb.str());
}

void WebServerManager::handleWiFiConfig() {
    if (server.method() != HTTP_POST) return;

    WifiConfigReq req; memset(&req, 0, sizeof(req));
    JField fields[] = {
        {"ssid",     JField::T_STR, req.ssid, sizeof(req.ssid)},
        {"password", JField::T_STR, req.pass, sizeof(req.pass)},
    };
    String err;
    if (!jsonParse(server.arg("plain").c_str(), fields, 2, err) || req.ssid[0] == '\0') {
        sendError(400, "SSID required"); return;
    }

    if (wifiManager) {
        wifiManager->setCredentials(req.ssid, req.pass);
        sendJson("{\"success\":true,\"message\":\"Credentials saved. Rebooting...\"}");
        delay(1500);
        ESP.restart();
    } else {
        sendError(500, "WiFi manager not available");
    }
}

void WebServerManager::handleWiFiScan() {
    int n = WiFi.scanComplete();

    if (n == WIFI_SCAN_FAILED) {
        WiFi.scanNetworks(true);
        // Serve cached results while new scan runs
        if (lastScanJson.length() > 0) {
            sendJson(lastScanJson);
        } else {
            sendJson("{\"networks\":[]}");
        }
        return;
    }

    if (n == WIFI_SCAN_RUNNING) {
        // Serve cached results while scan is in progress
        if (lastScanJson.length() > 0) {
            sendJson(lastScanJson);
        } else {
            sendJson("{\"networks\":[]}");
        }
        return;
    }

    // Scan complete — build JSON, cache it, start next scan
    JsonBuilder jb;
    jb.startObj();
    jb.startArr("networks");
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        jb.startArrObj();
        jb.addStr("ssid", ssid.c_str());
        jb.addInt("rssi", WiFi.RSSI(i));
        jb.endObj();
    }
    jb.endArr();
    jb.endObj();

    lastScanJson = jb.str();
    lastScanMs = millis();
    WiFi.scanDelete();
    // Kick off next scan immediately so cache is fresh
    WiFi.scanNetworks(true);

    sendJson(lastScanJson);
}

// ---- Access Control ----

void WebServerManager::handleAccessStatus() {
    JsonBuilder jb;
    jb.startObj();

    if (true) {
        jb.addStr("state", ac_getStateName(RACCESS));
        jb.addStr("lastEvent", ac_getLastEvent(RACCESS));
        jb.addInt("lastFpId", ac_getLastFpId(RACCESS));
        jb.addBool("localFallback", ac_isLocalFallbackEnabled(RACCESS));
        jb.addInt("serverStatus", (serverClient->isConfigured() ? (serverClient->isServerReachable()?0:1) : 2));
        jb.addInt("serverFailDuration", (int)ac_getServerFailDuration(RACCESS, serverClient));
        jb.addInt("serverLatency", (int)ac_getLastServerResponseTime(RACCESS, serverClient));
        jb.addBool("enrolling", ac_isEnrolling(RACCESS));
        if (ac_isEnrolling(RACCESS)) {
            jb.addInt("enrollStep", ac_getEnrollStep(RACCESS));
            jb.addInt("enrollFpId", ac_getEnrollingFpId(RACCESS));
        }
    } else {
        jb.addStr("state", "unavailable");
        jb.addStr("lastEvent", "");
        jb.addInt("lastFpId", 0);
        jb.addBool("localFallback", false);
        jb.addInt("serverStatus", 2);
    }

    jb.endObj();
    sendJson(jb.str());
}

void WebServerManager::handleAccessServerConfig() {
    if (server.method() == HTTP_GET) {
        JsonBuilder jb;
        jb.startObj();
        if (serverClient) {
            jb.addStr("serverUrl", serverClient->getServerUrl());
            jb.addStr("serverToken", serverClient->getAuthToken()[0] ? "***hidden***" : "");
            jb.addBool("configured", serverClient->isConfigured());
            jb.addBool("reachable", serverClient->isServerReachable());
        } else {
            jb.addStr("serverUrl", "");
            jb.addStr("serverToken", "");
            jb.addBool("configured", false);
            jb.addBool("reachable", false);
        }
        if (true) {
            jb.addBool("localFallback", ac_isLocalFallbackEnabled(RACCESS));
        }
        jb.endObj();
        sendJson(jb.str());
    } else {
        // POST: update server config
        struct { char url[128]; char token[64]; } req;
        memset(&req, 0, sizeof(req));
        bool localFallback = false;

        JField fields[] = {
            {"serverUrl",     JField::T_STR, req.url, sizeof(req.url)},
            {"serverToken",   JField::T_STR, req.token, sizeof(req.token)},
        };
        String err;
        jsonParse(server.arg("plain").c_str(), fields, 2, err);

        // Parse localFallback from raw body (simple check)
        String body = server.arg("plain");
        if (body.indexOf("\"localFallback\":true") > 0 || body.indexOf("\"localFallback\":true") > 0) {
            localFallback = true;
        } else if (body.indexOf("\"localFallback\":false") > 0) {
            localFallback = false;
        }

        if (req.url[0]) {
            if (serverClient) serverClient->setServerUrl(req.url);
            storage.putString("cfg_server_url", req.url);
        }
        if (req.token[0]) {
            if (serverClient) serverClient->setAuthToken(req.token);
            storage.putString("cfg_server_token", req.token);
        }
        // localFallback handled via registry

        sendJson("{\"success\":true}");
    }
}

void WebServerManager::handleFingerprintEnroll() {
    String uri = server.uri();

    if (uri.indexOf("/status") > 0 || server.method() == HTTP_GET) {
        // Enrollment status poll
        JsonBuilder jb;
        jb.startObj();
        if (true) {
            jb.addBool("enrolling", ac_isEnrolling(RACCESS));
            jb.addInt("step", ac_getEnrollStep(RACCESS));
            jb.addInt("fpId", ac_getEnrollingFpId(RACCESS));

            const char* stepStr = "idle";
            switch (ac_getEnrollStep(RACCESS)) {
            case -2: stepStr = "failed"; break;
            case -1: stepStr = "idle"; break;
            case 0:  stepStr = "waiting"; break;
            case 1:  stepStr = "second_capture"; break;
            case 2:  stepStr = "complete"; break;
            }
            jb.addStr("stepName", stepStr);
        } else {
            jb.addBool("enrolling", false);
            jb.addInt("step", -1);
            jb.addInt("fpId", 0);
            jb.addStr("stepName", "unavailable");
        }
        jb.endObj();
        sendJson(jb.str());
        return;
    }

    // POST: start enrollment
    struct { int fpId; int userId; } req;
    memset(&req, 0, sizeof(req));
    req.userId = -1;
    JField fields[] = {
        {"fpId",   JField::T_INT, &req.fpId},
        {"userId", JField::T_INT, &req.userId},
    };
    String err;
    if (!jsonParse(server.arg("plain").c_str(), fields, 2, err) || req.fpId <= 0) {
        sendError(400, "fpId required (1-127)");
        return;
    }

    // Auto-link fpId to user if userId provided
    if (req.userId > 0) {
        User* u = ur_findById(RUSERS, &storage, req.userId);
        if (u) {
            u->fpId = req.fpId;
            ur_update(RUSERS, &storage, req.userId, u);
        }
    }

    if (ac_beginEnrollment(RACCESS, fpDriver, req.fpId)) {
        JsonBuilder jb;
        jb.startObj();
        jb.addBool("success", true);
        jb.addInt("fpId", req.fpId);
        jb.addInt("userId", req.userId);
        jb.addStr("message", "Enrollment started. Place finger on sensor.");
        jb.endObj();
        sendJson(jb.str());
    } else {
        sendError(500, "Enrollment failed — sensor busy or unavailable");
    }
}

void WebServerManager::handleFingerprintDelete() {
    struct { int fpId; } req;
    memset(&req, 0, sizeof(req));
    JField fields[] = {
        {"fpId", JField::T_INT, &req.fpId},
    };
    String err;
    if (!jsonParse(server.arg("plain").c_str(), fields, 1, err) || req.fpId <= 0) {
        sendError(400, "fpId required");
        return;
    }

    if ( ac_deleteFingerprint(RACCESS, fpDriver, req.fpId)) {
        sendJson("{\"success\":true}");
    } else {
        sendError(500, "Delete failed");
    }
}

void WebServerManager::handleDoorControl() {
    String uri = server.uri();

    if (uri.indexOf("/unlock") > 0) {
        // Remote unlock
        if ( ac_remoteUnlock(RACCESS, g_registry.getDoorService())) {
            sendJson("{\"success\":true,\"message\":\"Door unlocking\"}");
        } else {
            sendError(409, "Door busy or not available");
        }
    } else {
        // GET door status
        JsonBuilder jb;
        jb.startObj();
        // DoorService is accessed via AccessController
        if (true) {
            const char* state = ac_getStateName(RACCESS);
            jb.addStr("state", state);
        } else {
            jb.addStr("state", "unavailable");
        }
        jb.endObj();
        sendJson(jb.str());
    }
}

// ---- Helpers ----

void WebServerManager::sendJson(const String& json) {
    server.send(200, "application/json", json);
}

void WebServerManager::sendError(int code, const char* message) {
    JsonBuilder jb;
    jb.startObj();
    jb.addBool("error", true);
    jb.addInt("code", code);
    jb.addStr("message", message);
    jb.endObj();
    server.send(code, "application/json", jb.str());
}

void WebServerManager::handleStaticFile(const char* path, const char* mimeType) {
    spiffsLock();
    if (SPIFFS.exists(path)) {
        File file = SPIFFS.open(path, "r");
        server.streamFile(file, mimeType);
        file.close();
    } else {
        server.send(404, "text/plain", "File not found");
    }
    spiffsUnlock();
}

String WebServerManager::stateToString(int state) {
    const char* states[] = {"INIT", "IDLE", "ANALYZING", "TOOL_PLACED",
                           "REMOVING", "UNKNOWN", "CALIBRATING", "ERROR", "SLEEP"};
    return (state >= 0 && state < 9) ? String(states[state]) : "UNKNOWN";
}

void WebServerManager::setStateManager(StateManager*) { }
void WebServerManager::setToolRepository(ToolRepository*) { }
void WebServerManager::setUserRepository(UserRepository*) { }
void WebServerManager::setLogRepository(LogRepository*) { }
void WebServerManager::setWeightService(WeightService*) { }
void WebServerManager::setWiFiManager(WiFiManager*) { }
void WebServerManager::setSystemStatus(SystemStatus*) { }
void WebServerManager::setFingerprintDriver(FingerprintDriver* fp) { fpDriver = fp; }
void WebServerManager::setServerClient(ServerClient* sc) { serverClient = sc; }
void WebServerManager::notifyClients(const char* event, const char* data) {}

// ---- Global wrapper functions for main.cpp ----
// Pointer — only created in STA mode. AP mode uses WiFiManager's configPortal only.
static WebServerManager* s_webServer = nullptr;

void web_begin() {
    if (!s_webServer) {
        s_webServer = new WebServerManager(EventBus::getInstance());
    }
    s_webServer->begin();
    LOG_INFO("WEB", "Started (registry-backed)");
}

void web_stop() {
    if (s_webServer) {
        delete s_webServer;
        s_webServer = nullptr;
        LOG_INFO("WEB", "Stopped");
    }
}

void web_handle() {
    if (s_webServer) s_webServer->handle();
}
