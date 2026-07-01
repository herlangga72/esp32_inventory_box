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
#include "../kernel/WiFiManager.h"
#include "../kernel/SystemStatus.h"
#include "../data/StorageManager.h"
#include "../utils/LogManager.h"
#include "../utils/JsonBuilder.h"
#include "../utils/JsonParser.h"

extern StorageManager storage;
extern WiFiManager wifiManager;  // global from main.cpp, used directly via extern

// Registry shorthand
#define RSTATUS    g_registry.getSystemStatus()
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
    : events(events), logRepo(nullptr), fpDriver(nullptr), serverClient(nullptr) {}

void WebServerManager::logRequest() {
    LOG_INFO("WEB", "%s %s", server.method() == HTTP_GET ? "GET" :
        server.method() == HTTP_POST ? "POST" :
        server.method() == HTTP_PUT ? "PUT" : "DELETE", server.uri().c_str());
}

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
    server.on("/api/users", HTTP_GET, [this]() { handleUsers(); });
    server.on("/api/users", HTTP_POST, [this]() { handleUsers(); });
    server.on("/api/users/login", HTTP_POST, [this]() { handleUserLogin(); });
    server.on("/api/users/logout", HTTP_POST, [this]() { handleUserLogout(); });
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

    server.onNotFound([this]() {
        String uri = server.uri();
        if (uri.startsWith("/api/tools/")) { handleToolById(); return; }
        if (uri.startsWith("/api/users/")) {
            if (server.method() == HTTP_DELETE && !uri.endsWith("/login") && !uri.endsWith("/logout")) { handleUserDelete(); return; }
            if (server.method() == HTTP_GET) { handleUserById(); return; }
            if (server.method() == HTTP_PUT) { handleUserById(); return; }
        }
    });
    server.begin();
    LOG_INFO("WEB", "Started");
}

void WebServerManager::handle() {
    // LIFO: if backlogged (3+ pending client cycles), drop oldest by killing current
    static int s_backlog = 0;
    WiFiClient cur = server.client();
    if (cur) {
        if (++s_backlog > 3) { cur.stop(); s_backlog = 0; return; }
    } else {
        s_backlog = 0;
    }
    server.handleClient();
}
bool WebServerManager::hasActiveClient() { return server.client().connected(); }

// ---- Root ----

void WebServerManager::handleRoot() {
    logRequest();
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
    logRequest();
    JsonBuilder jb;
    jb.startObj();

    jb.addBool("c", wifiManager.isConnected());
    jb.addBool("a", wifiManager.isAPMode());

    jb.addStr("ip", wifiManager.getIP());
    jb.addInt("rss", wifiManager.getRSSI());
    jb.addStr("ss", wifiManager.getSSID());

    {
        const char* statusStr = "UNKNOWN";
        ComponentStatus overall = ss_getOverallStatus(RSTATUS);
        if (overall == ComponentStatus::OK) statusStr = "OK";
        else if (overall == ComponentStatus::WARNING) statusStr = "WARNING";
        else if (overall == ComponentStatus::ERROR) statusStr = "ERROR";
        jb.addStr("st", statusStr);
        jb.addBool("err", ss_hasErrors(RSTATUS));
    }

    {
        jb.addStr("s", stateToString((int)sm_getCurrentState(RSTATE)).c_str());
        jb.addInt("cnt", sm_getContentCount(RSTATE));
        jb.addInt("uid", sm_getCurrentUserId(RSTATE));

        if (sm_getCurrentUserId(RSTATE) > 0) {
            User* u = ur_findById(RUSERS, &storage, sm_getCurrentUserId(RSTATE));
            jb.addStr("un", u ? u->name : "");
        } else {
            jb.addStr("un", "");
        }

        jb.startArr("cl");
        auto* st = RSTATE;
        for (int i = 0; i < sm_getContentCount(RSTATE); i++) {
            Tool* t = tr_findById(RTOOLS, &storage, sm_getContents(RSTATE)[i]);
            if (t) {
                jb.startArrObj();
                jb.addInt("i", t->id);
                jb.addStr("n", t->name);
                jb.addFlt("w", t->weightGrams);
                jb.endObj();
            }
        }
        jb.endArr();
    }

    jb.addFlt("w", ws_getCurrentWeight(RWEIGHT));
        jb.addFlt("bl", ws_getBaseline(RWEIGHT));
        jb.addFlt("d", ws_getDelta(RWEIGHT));

    jb.addInt("up", (int)millis());
    jb.addInt("h", ESP.getFreeHeap());
    jb.endObj();

    sendJson(jb.str());
}

// ---- Tools ----

void WebServerManager::handleTools() {
    logRequest();
    if (server.method() == HTTP_GET) {
        Tool tools[Config::MAX_TOOLS];
        int toolCount = tr_findAll(RTOOLS, &storage, tools, Config::MAX_TOOLS);

        JsonBuilder jb;
        jb.startObj();
        jb.startArr("tl");
        for (int _i = 0; _i < toolCount; _i++) {
            auto& tool = tools[_i];
            jb.startArrObj();
            jb.addInt("i", tool.id);
            jb.addStr("n", tool.name);
            jb.addFlt("w", tool.weightGrams);
            jb.addFlt("tol", tool.toleranceGrams);
            jb.addBool("act", tool.active);
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

        int id = tr_create(RTOOLS, &storage, &tool);
            LOG_INFO("WEB", "POST /api/tools id=%d name=%s", id, tool.name);
            JsonBuilder jb;
            jb.startObj();
            jb.addBool("ok", true);
            jb.addInt("i", id);
            jb.endObj();
            sendJson(jb.str());
    }
}

void WebServerManager::handleToolById() {
    logRequest();
    String path = server.uri();
    int id = path.substring(path.lastIndexOf('/') + 1).toInt();

    if (server.method() == HTTP_GET) {
        Tool* tool = tr_findById(RTOOLS, &storage, id);
        if (tool) {
            JsonBuilder jb;
            jb.startObj();
            jb.addInt("i", tool->id);
            jb.addStr("n", tool->name);
            jb.addFlt("w", tool->weightGrams);
            jb.addFlt("tol", tool->toleranceGrams);
            jb.addBool("act", tool->active);
            jb.endObj();
            sendJson(jb.str());
        } else {
            LOG_ERROR("WEB", "GET /api/tools/%d — not found", id);
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

        Tool* tool = tr_findById(RTOOLS, &storage, id);
        if (tool) {
            if (req.name[0]) tool->setName(req.name);
            tool->weightGrams = req.weight;
            tool->toleranceGrams = req.tolerance;
            tr_update(RTOOLS, &storage, id, tool);
            LOG_INFO("WEB", "PUT /api/tools/%d name=%s", id, tool->name);
            sendJson("{\"success\":true}");
        } else {
            LOG_ERROR("WEB", "PUT /api/tools/%d — not found", id);
            sendError(404, "Tool not found");
        }

    } else if (server.method() == HTTP_DELETE) {
        if ( tr_remove(RTOOLS, &storage, id)) {
            LOG_INFO("WEB", "DELETE /api/tools/%d", id);
            sendJson("{\"success\":true}");
        } else {
            LOG_ERROR("WEB", "DELETE /api/tools/%d — not found", id);
            sendError(404, "Tool not found");
        }
    }
}

// ---- Users ----

void WebServerManager::handleUsers() {
    logRequest();
    if (server.method() == HTTP_GET) {
        User users[Config::MAX_USERS];
        int userCount = ur_findAll(RUSERS, &storage, users, Config::MAX_USERS);

        JsonBuilder jb;
        jb.startObj();
        jb.startArr("us");
        for (int _i = 0; _i < userCount; _i++) {
            auto& user = users[_i];
            jb.startArrObj();
            jb.addInt("i", user.id);
            jb.addStr("n", user.name);
            jb.addBool("act", user.active);
            jb.addInt("fp", user.fpId);
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

        int id = ur_create(RUSERS, &storage, &user);
        LOG_INFO("WEB", "POST /api/users id=%d name=%s", id, user.name);
        JsonBuilder jb;
        jb.startObj();
        jb.addBool("ok", true);
        jb.addInt("i", id);
        jb.endObj();
        sendJson(jb.str());
    }
}

void WebServerManager::handleUserLogin() {
    logRequest();
    LoginReq req; memset(&req, 0, sizeof(req));
    JField fields[] = {
        {"pin", JField::T_STR, req.pin, sizeof(req.pin)},
    };
    String err;
    if (!jsonParse(server.arg("plain").c_str(), fields, 1, err) || req.pin[0] == '\0') {
        sendError(400, "PIN required"); return;
    }

    User* user = ur_authenticate(RUSERS, &storage, req.pin);
    if (user) {
        ServiceMessage _sm = ServiceMessage::cmd(ServiceId::STATE_MANAGER, (uint8_t)StateMsgType::USER_LOGIN);
        _sm.u4.u1 = (uint16_t)(user->id);
        g_registry.send(ServiceId::STATE_MANAGER, _sm);

        JsonBuilder jb;
        jb.startObj();
        jb.addBool("ok", true);
        jb.addInt("uid", user->id);
        jb.addStr("n", user->name);
        jb.endObj();
        LOG_INFO("WEB", "POST /api/users/login uid=%d name=%s", user->id, user->name);
        sendJson(jb.str());
    } else {
        LOG_ERROR("WEB", "POST /api/users/login — invalid PIN");
        sendError(401, "Invalid PIN");
    }
}

void WebServerManager::handleUserLogout() {
    logRequest();
    g_registry.sendCmd(ServiceId::STATE_MANAGER, (uint8_t)StateMsgType::USER_LOGOUT);
    LOG_INFO("WEB", "POST /api/users/logout");
    sendJson("{\"success\":true}");
}

void WebServerManager::handleUserById() {
    logRequest();
    int id = server.uri().substring(server.uri().lastIndexOf('/') + 1).toInt();
    if (id <= 0) { sendError(400, "Invalid user ID"); return; }

    if (server.method() == HTTP_GET) {
        User* user = ur_findById(RUSERS, &storage, id);
        if (!user) { sendError(404, "User not found"); return; }

        JsonBuilder jb;
        jb.startObj();
        jb.addInt("i", user->id);
        jb.addStr("n", user->name);
        jb.addStr("pin", user->pin);
        jb.addBool("act", user->active);
        jb.addInt("fp", user->fpId);
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
        LOG_INFO("WEB", "PUT /api/users/%d", id);
        sendJson("{\"success\":true}");
    } else {
        LOG_ERROR("WEB", "PUT /api/users/%d — update failed", id);
        sendError(500, "Update failed");
    }
}

void WebServerManager::handleUserDelete() {
    logRequest();
    int id = server.uri().substring(server.uri().lastIndexOf('/') + 1).toInt();
    if ( ur_remove(RUSERS, &storage, id)) {
        LOG_INFO("WEB", "DELETE /api/users/%d", id);
        sendJson("{\"success\":true}");
    } else {
        LOG_ERROR("WEB", "DELETE /api/users/%d — not found", id);
        sendError(404, "User not found");
    }
}

// ---- Logs ----

void WebServerManager::handleLogs() {
    logRequest();
    int limit = server.hasArg("limit") ? server.arg("limit").toInt() : 50;
    int offset = server.hasArg("offset") ? server.arg("offset").toInt() : 0;

    // Read from end: get last N entries, newest first
    int cap = (limit > 10) ? 10 : limit;
    auto* logBuf = new LogEntry[cap];
    int total = logRepo ? logRepo->count() : 0;
    int tailOffset = (total > cap) ? total - cap : 0;
    int logCount = logRepo ? logRepo->findFiltered(logBuf, cap, cap, tailOffset, 0) : 0;

    JsonBuilder jb;
    jb.startObj();
    jb.startArr("lg");
    // Reverse: newest entries first
    for (int li = logCount - 1; li >= 0; li--) {
        auto& entry = logBuf[li];
        jb.startArrObj();
        jb.addInt("ts", (int)entry.timestamp);
        jb.addInt("level", entry.severity);
        jb.addStr("tag", entry.event);
        jb.addStr("msg", entry.message);
        jb.addInt("uid", entry.userId);
        jb.addInt("tid", entry.toolId);
        jb.addFlt("w", entry.weightGrams);
        jb.endObj();
    }
    jb.endArr();
    jb.addInt("total", logRepo ? logRepo->count() : 0);
    jb.addInt("dropped", logRepo ? logRepo->getDropped() : 0);
    jb.addInt("fileSize", logRepo ? (int)logRepo->fileSize() : 0);
    jb.endObj();

    sendJson(jb.str());
    delete[] logBuf;
}

void WebServerManager::handleLogsDownload() {
    logRequest();
    if (!logRepo) { sendError(500, "Log repository not available"); return; }
    char csvBuf[4096];
    int csvLen = logRepo->downloadCSV(csvBuf, sizeof(csvBuf));
    server.send(200, "text/csv", String(csvBuf));
}

void WebServerManager::handleLogsClear() {
    logRequest();
    if (logRepo) {
        logRepo->clear();
        sendJson("{\"success\":true}");
    } else {
        sendError(500, "Log repository not available");
    }
}

// ---- Contents ----

void WebServerManager::handleContentsClear() {
    logRequest();
    sm_clearContents(RSTATE);
    sendJson("{\"success\":true}");
}

// ---- Calibrate ----

void WebServerManager::handleCalibrate() {
    logRequest();
    {
        float baseline = ws_getBaseline(RWEIGHT);
        ServiceMessage _sm = ServiceMessage::cmd(ServiceId::STATE_MANAGER, (uint8_t)StateMsgType::CALIBRATION);
        _sm.f2.f1 = baseline;
        g_registry.send(ServiceId::STATE_MANAGER, _sm);
        storage.putFloat("bl", baseline);

        JsonBuilder jb;
        jb.startObj();
        jb.addBool("ok", true);
        jb.addFlt("bl", baseline);
        jb.endObj();
        sendJson(jb.str());
    }
}

// ---- Config ----

void WebServerManager::handleConfig() {
    logRequest();
    if (server.method() == HTTP_GET) {
        JsonBuilder jb;
        jb.startObj();
        jb.addStr("dn", "ESP32-Inventory-Box");
        jb.addStr("ss", WiFi.SSID().c_str());
        jb.addInt("rss", WiFi.RSSI());
        jb.addInt("h", ESP.getFreeHeap());
        jb.addInt("up", (int)millis());
        jb.addInt("ll", (int)logGetLevel());
        // Actual config values from NVS (with defaults matching Config.h)
        jb.addFlt("th", storage.getFloat("cfg_threshold", 2.0f));
        jb.addInt("st", storage.getInt("cfg_settling", 3000));
        jb.addFlt("mt", storage.getFloat("cfg_motion", 0.15f));
        jb.addInt("ls", storage.getInt("cfg_light", 30000));
        jb.addInt("ds", storage.getInt("cfg_deep", 300000));
        jb.addFlt("dt", storage.getFloat("cfg_tolerance", 2.0f));
        jb.addInt("mc", storage.getInt("cfg_maxcontents", 10));
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
            {"ll",         JField::T_INT,  &c.logLevel},
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
    logRequest();
    JsonBuilder jb;
    jb.startObj();

    {
        ComponentStatus overall = ss_getOverallStatus(RSTATUS);
        const char* statusStr = "UNKNOWN";
        if (overall == ComponentStatus::OK) statusStr = "OK";
        else if (overall == ComponentStatus::WARNING) statusStr = "WARNING";
        else if (overall == ComponentStatus::ERROR) statusStr = "ERROR";

        jb.addStr("os", statusStr);
        jb.addInt("up", (int)ss_getUptime(RSTATUS));
        jb.addInt("te", ss_getErrorCount(RSTATUS));
        jb.addStr("le", ss_getLastError(RSTATUS));
        jb.addInt("oc", ss_getOKCount(RSTATUS));
        jb.addInt("wc", ss_getWarningCount(RSTATUS));
        jb.addInt("ec", ss_getErrorComponentCount(RSTATUS));

        jb.startArr("comp");
        for (int ci = 0; ci < RSTATUS->componentCount; ci++) {
            auto& comp = RSTATUS->components[ci];
            jb.startArrObj();
            jb.addStr("n", comp.name);

            const char* s = "UNKNOWN";
            if (comp.status == ComponentStatus::OK) s = "OK";
            else if (comp.status == ComponentStatus::WARNING) s = "WARNING";
            else if (comp.status == ComponentStatus::ERROR) s = "ERROR";
            jb.addStr("status", s);

            jb.addStr("le", comp.lastError);
            jb.addInt("ec", comp.errorCount);
            jb.endObj();
        }
        jb.endArr();
    }

    jb.endObj();
    sendJson(jb.str());
}

// ---- Restart ----

void WebServerManager::handleRestart() {
    logRequest();
    LOG_INFO("WEB", "Restart requested via API");
    sendJson("{\"success\":true,\"message\":\"Restarting...\"}");
    delay(500);
    ESP.restart();
}

// ---- WiFi ----

void WebServerManager::handleWiFiStatus() {
    logRequest();
    JsonBuilder jb;
    jb.startObj();
    jb.addBool("c", wifiManager.isConnected());
    jb.addBool("a", wifiManager.isAPMode());

    jb.addStr("ip", wifiManager.getIP());
    jb.addStr("ss", wifiManager.getSSID());
    jb.addInt("rss", wifiManager.getRSSI());
    jb.addStr("st", wifiManager.getState());
    jb.addBool("reconnecting", wifiManager.isReconnecting());

    jb.endObj();
    sendJson(jb.str());
}

void WebServerManager::handleWiFiConfig() {
    logRequest();
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

    wifiManager.setCredentials(req.ssid, req.pass);
    LOG_INFO("WEB", "POST /api/wifi ssid=%s", req.ssid);
    sendJson("{\"success\":true,\"message\":\"Credentials saved. Rebooting...\"}");
    delay(1500);
    ESP.restart();
}

void WebServerManager::handleWiFiScan() {
    logRequest();
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
        jb.addStr("ss", ssid.c_str());
        jb.addInt("rss", WiFi.RSSI(i));
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
    logRequest();
    JsonBuilder jb;
    jb.startObj();

    if (true) {
        jb.addStr("s", ac_getStateName(RACCESS));
        jb.addStr("lastEvent", ac_getLastEvent(RACCESS));
        jb.addInt("lastFpId", ac_getLastFpId(RACCESS));
        jb.addBool("localFallback", ac_isLocalFallbackEnabled(RACCESS));
        jb.addInt("sst", (serverClient->isConfigured() ? (serverClient->isServerReachable()?0:1) : 2));
        jb.addInt("sfd", (int)ac_getServerFailDuration(RACCESS, serverClient));
        jb.addInt("sl", (int)ac_getLastServerResponseTime(RACCESS, serverClient));
        jb.addBool("enrolling", ac_isEnrolling(RACCESS));
        if (ac_isEnrolling(RACCESS)) {
            jb.addInt("enrollStep", ac_getEnrollStep(RACCESS));
            jb.addInt("enrollFpId", ac_getEnrollingFpId(RACCESS));
        }
    } else {
        jb.addStr("s", "unavailable");
        jb.addStr("lastEvent", "");
        jb.addInt("lastFpId", 0);
        jb.addBool("localFallback", false);
        jb.addInt("sst", 2);
    }

    jb.endObj();
    sendJson(jb.str());
}

void WebServerManager::handleAccessServerConfig() {
    logRequest();
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
        if (body.indexOf("\"localFallback\":true") > 0) {
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
    logRequest();
    String uri = server.uri();

    if (uri.indexOf("/status") > 0 || server.method() == HTTP_GET) {
        // Enrollment status poll
        JsonBuilder jb;
        jb.startObj();
        if (true) {
            jb.addBool("enr", ac_isEnrolling(RACCESS));
            jb.addInt("stp", ac_getEnrollStep(RACCESS));
            jb.addInt("fp", ac_getEnrollingFpId(RACCESS));

            const char* stepStr = "idle";
            switch (ac_getEnrollStep(RACCESS)) {
            case -2: stepStr = "failed"; break;
            case -1: stepStr = "idle"; break;
            case 0:  stepStr = "waiting"; break;
            case 1:  stepStr = "second_capture"; break;
            case 2:  stepStr = "complete"; break;
            }
            jb.addStr("sn", stepStr);
        } else {
            jb.addBool("enrolling", false);
            jb.addInt("step", -1);
            jb.addInt("fp", 0);
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
        jb.addBool("ok", true);
        jb.addInt("fp", req.fpId);
        jb.addInt("uid", req.userId);
        jb.addStr("message", "Enrollment started. Place finger on sensor.");
        jb.endObj();
        sendJson(jb.str());
    } else {
        sendError(500, "Enrollment failed — sensor busy or unavailable");
    }
}

void WebServerManager::handleFingerprintDelete() {
    logRequest();
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
    logRequest();
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
            jb.addStr("s", state);
        } else {
            jb.addStr("s", "unavailable");
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
    jb.addStr("msg", message);
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

void WebServerManager::setLogRepository(LogRepository* lr) { logRepo = lr; }
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

void web_setLogRepository(LogRepository* lr) {
    if (s_webServer) s_webServer->setLogRepository(lr);
}

void web_handle() {
    if (!s_webServer) return;
    UBaseType_t before = uxTaskGetStackHighWaterMark(NULL);
    if (before < 512) {
        static uint32_t lastWarn = 0;
        if (millis() - lastWarn > 10000) {
            LOG_WARN("WEB", "stack low (%d), dropped", before);
            lastWarn = millis();
        }
        return;
    }
    s_webServer->handle();
    UBaseType_t after = uxTaskGetStackHighWaterMark(NULL);
    // Log if this call consumed significant stack
    if (before - after > 1000) {
        LOG_WARN("WEB", "stack consumed %d (was %d, now %d)", before - after, before, after);
    }
    // Log health every 60s
    static uint32_t lastLog = 0;
    static UBaseType_t minStack = 9999;
    if (after < minStack) minStack = after;
    if (millis() - lastLog > 60000) {
        LOG_INFO("WEB", "stack before=%d after=%d min=%d", before, after, minStack);
        lastLog = millis();
    }
}
