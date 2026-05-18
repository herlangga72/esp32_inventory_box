#include "WebServer.h"
#include "../domain/services/StateManager.h"
#include "../data/ToolRepository.h"
#include "../data/UserRepository.h"
#include "../data/LogRepository.h"
#include "../domain/services/WeightService.h"
#include "../domain/entities/BoxState.h"
#include "../kernel/WiFiManager.h"
#include "../kernel/SystemStatus.h"

extern StorageManager storage;

WebServerManager::WebServerManager(EventBus* events)
    : events(events), stateManager(nullptr), toolRepo(nullptr),
      userRepo(nullptr), logRepo(nullptr), weightService(nullptr), 
      wifiManager(nullptr), systemStatus(nullptr) {}

void WebServerManager::begin() {
    // Main routes
    server.on("/", HTTP_GET, [this]() { handleRoot(); });
    server.on("/index.html", HTTP_GET, [this]() { handleRoot(); });
    
    // Static assets
    server.on("/styles.css", HTTP_GET, [this]() { handleStaticFile("/styles.css", "text/css"); });;
    server.on("/app.js", HTTP_GET, [this]() { handleStaticFile("/app.js", "application/javascript"); });;
    
    // Page routes
    server.on("/pages/dashboard.html", HTTP_GET, [this]() { handleStaticFile("/pages/dashboard.html", "text/html"); });;
    server.on("/pages/tools.html", HTTP_GET, [this]() { handleStaticFile("/pages/tools.html", "text/html"); });;
    server.on("/pages/users.html", HTTP_GET, [this]() { handleStaticFile("/pages/users.html", "text/html"); });;
    server.on("/pages/logs.html", HTTP_GET, [this]() { handleStaticFile("/pages/logs.html", "text/html"); });;
    server.on("/pages/diagnostics.html", HTTP_GET, [this]() { handleStaticFile("/pages/diagnostics.html", "text/html"); });;
    server.on("/pages/config.html", HTTP_GET, [this]() { handleStaticFile("/pages/config.html", "text/html"); });;
    server.on("/pages/wifi.html", HTTP_GET, [this]() { handleStaticFile("/pages/wifi.html", "text/html"); });;
    
    // API routes
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
    server.on("/api/logs", HTTP_GET, [this]() { handleLogs(); });
    server.on("/api/calibrate", HTTP_POST, [this]() { handleCalibrate(); });
    server.on("/api/config", HTTP_GET, [this]() { handleConfig(); });
    server.on("/api/config", HTTP_POST, [this]() { handleConfig(); });
    server.on("/api/wifi", HTTP_GET, [this]() { handleWiFiStatus(); });
    server.on("/api/wifi", HTTP_POST, [this]() { handleWiFiConfig(); });
    server.on("/api/diagnostics", HTTP_GET, [this]() { handleDiagnostics(); });
    server.on("/api/restart", HTTP_POST, [this]() { handleRestart(); });
    
    server.begin();
    Serial.println("[WebServer] Started");
}

void WebServerManager::handle() {
    server.handleClient();
}

void WebServerManager::handleRoot() {
    // Serve the index.html from SPIFFS or return simple page
    if (SPIFFS.exists("/index.html")) {
        File file = SPIFFS.open("/index.html", "r");
        server.streamFile(file, "text/html");
        file.close();
    } else {
        server.send(200, "text/html", R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>ESP32 Inventory Box</title>
    <style>
        body { font-family: Arial; text-align: center; padding: 50px; background: #1a3c6e; color: white; }
        h1 { font-size: 3em; margin-bottom: 20px; }
        p { font-size: 1.2em; opacity: 0.8; }
        .error { background: #e74c3c; padding: 20px; border-radius: 8px; margin: 20px auto; max-width: 400px; }
        .restart-btn { 
            background: #27ae60; color: white; border: none; padding: 15px 30px;
            font-size: 1.2em; border-radius: 5px; cursor: pointer; margin-top: 20px;
        }
        .restart-btn:hover { background: #219a52; }
    </style>
</head>
<body>
    <h1>ESP32 Inventory Box</h1>
    <p>Web UI not found. Please upload SPIFFS files.</p>
    <button class="restart-btn" onclick="doRestart()">Restart System</button>
    <script>
        function doRestart() {
            fetch('/api/restart', { method: 'POST' });
        }
    </script>
</body>
</html>
        )");
    }
}

void WebServerManager::handleStatus() {
    String json = "{";
    
    // Connection status
    json += "\"connected\":" + String((wifiManager && wifiManager->isConnected()) ? "true" : "false") + ",";
    json += "\"apMode\":" + String((wifiManager && wifiManager->isAPMode()) ? "true" : "false") + ",";
    
    if (wifiManager) {
        json += "\"ipAddress\":\"" + wifiManager->getIP() + "\",";
        json += "\"wifiRssi\":" + String(wifiManager->getRSSI()) + ",";
        json += "\"wifiSSID\":\"" + wifiManager->getSSID() + "\",";
    } else {
        json += "\"ipAddress\":\"--\",";
        json += "\"wifiRssi\":0,";
        json += "\"wifiSSID\":\"--\",";
    }
    
    // System status summary
    if (systemStatus) {
        json += "\"systemStatus\":\"" + 
            (systemStatus->getOverallStatus() == ComponentStatus::OK ? "OK" :
             systemStatus->getOverallStatus() == ComponentStatus::WARNING ? "WARNING" : "ERROR") + "\",";
        json += "\"hasErrors\":" + String(systemStatus->hasErrors() ? "true" : "false") + ",";
    } else {
        json += "\"systemStatus\":\"UNKNOWN\",";
        json += "\"hasErrors\":false,";
    }
    
    // State
    if (stateManager) {
        json += "\"state\":\"" + stateToString((int)stateManager->getCurrentState()) + "\",";
        json += "\"contents\":" + String(stateManager->getState()->contentCount) + ",";
        json += "\"currentUser\":" + String(stateManager->getCurrentUserId()) + ",";
    }
    
    // Weight
    if (weightService) {
        json += "\"weight\":" + String(weightService->getCurrentWeight(), 1) + ",";
        json += "\"baseline\":" + String(weightService->getBaseline(), 1) + ",";
        json += "\"delta\":" + String(weightService->getDelta(), 1) + ",";
    }
    
    // System info
    json += "\"uptime\":" + String(millis()) + ",";
    json += "\"freeHeap\":" + String(ESP.getFreeHeap());
    
    json += "}";
    sendJson(json);
}

void WebServerManager::handleTools() {
    if (server.method() == HTTP_GET) {
        auto tools = toolRepo ? toolRepo->findAll() : std::vector<Tool>();
        
        String json = "{\"tools\":[";
        bool first = true;
        for (auto& tool : tools) {
            if (!first) json += ",";
            first = false;
            json += "{\"id\":" + String(tool.id) + 
                    ",\"name\":\"" + String(tool.name) + "\"," +
                    "\"weight\":" + String(tool.weightGrams, 1) + "," +
                    "\"tolerance\":" + String(tool.toleranceGrams, 1) + "," +
                    "\"active\":" + String(tool.active ? "true" : "false") + "}";
        }
        json += "]}";
        sendJson(json);
        
    } else if (server.method() == HTTP_POST) {
        String body = server.arg("plain");
        
        Tool tool;
        char name[32] = {0};
        float weight = 0;
        float tolerance = 2.0;
        
        // Extract name
        int nameStart = body.indexOf("\"name\":\"") + 8;
        if (nameStart > 7) {
            int nameEnd = body.indexOf("\"", nameStart);
            body.substring(nameStart, nameEnd).toCharArray(name, sizeof(name));
            tool.setName(name);
        }
        
        // Extract weight
        int weightStart = body.indexOf("\"weight\":") + 9;
        if (weightStart > 8) {
            int weightEnd = body.indexOf("}", weightStart);
            if (weightEnd < 0) weightEnd = body.indexOf(",", weightStart);
            if (weightEnd < 0) weightEnd = body.indexOf("\"", weightStart);
            weight = body.substring(weightStart, weightEnd).toFloat();
            tool.weightGrams = weight;
        }
        
        // Extract tolerance
        int tolStart = body.indexOf("\"tolerance\":") + 11;
        if (tolStart > 10) {
            int tolEnd = body.indexOf("}", tolStart);
            if (tolEnd < 0) tolEnd = body.indexOf(",", tolStart);
            if (tolEnd < 0) tolEnd = body.indexOf("\"", tolStart);
            tolerance = body.substring(tolStart, tolEnd).toFloat();
            tool.toleranceGrams = tolerance;
        }
        
        if (toolRepo) {
            int id = toolRepo->create(&tool);
            sendJson("{\"success\":true,\"id\":" + String(id) + "}");
        } else {
            sendError(500, "Tool repository not available");
        }
    }
}

void WebServerManager::handleToolById() {
    String path = server.uri();
    int idStart = path.lastIndexOf('/') + 1;
    int id = path.substring(idStart).toInt();
    
    if (server.method() == HTTP_GET) {
        Tool* tool = toolRepo ? toolRepo->findById(id) : nullptr;
        if (tool) {
            String json = "{\"id\":" + String(tool->id) + 
                    ",\"name\":\"" + String(tool->name) + "\"," +
                    "\"weight\":" + String(tool->weightGrams, 1) + "," +
                    "\"tolerance\":" + String(tool->toleranceGrams, 1) + "," +
                    "\"active\":" + String(tool->active ? "true" : "false") + "}";
            sendJson(json);
        } else {
            sendError(404, "Tool not found");
        }
        
    } else if (server.method() == HTTP_PUT) {
        String body = server.arg("plain");
        Tool* tool = toolRepo ? toolRepo->findById(id) : nullptr;
        
        if (tool) {
            // Extract name
            int nameStart = body.indexOf("\"name\":\"") + 8;
            if (nameStart > 7) {
                int nameEnd = body.indexOf("\"", nameStart);
                char name[32] = {0};
                body.substring(nameStart, nameEnd).toCharArray(name, sizeof(name));
                tool->setName(name);
            }
            
            // Extract weight
            int weightStart = body.indexOf("\"weight\":") + 9;
            if (weightStart > 8) {
                int weightEnd = body.indexOf("}", weightStart);
                if (weightEnd < 0) weightEnd = body.indexOf(",", weightStart);
                tool->weightGrams = body.substring(weightStart, weightEnd).toFloat();
            }
            
            toolRepo->update(tool);
            sendJson("{\"success\":true}");
        } else {
            sendError(404, "Tool not found");
        }
        
    } else if (server.method() == HTTP_DELETE) {
        if (toolRepo && toolRepo->remove(id)) {
            sendJson("{\"success\":true}");
        } else {
            sendError(404, "Tool not found");
        }
    }
}

void WebServerManager::handleUsers() {
    if (server.method() == HTTP_GET) {
        auto users = userRepo ? userRepo->findAll() : std::vector<User>();
        
        String json = "{\"users\":[";
        bool first = true;
        for (auto& user : users) {
            if (!first) json += ",";
            first = false;
            json += "{\"id\":" + String(user.id) + 
                    ",\"name\":\"" + String(user.name) + "\"," +
                    "\"active\":" + String(user.active ? "true" : "false") + "}";
        }
        json += "]}";
        sendJson(json);
        
    } else if (server.method() == HTTP_POST) {
        String body = server.arg("plain");
        
        User user;
        char name[32] = {0};
        char pin[5] = {0};
        
        // Extract name
        int nameStart = body.indexOf("\"name\":\"") + 8;
        if (nameStart > 7) {
            int nameEnd = body.indexOf("\"", nameStart);
            body.substring(nameStart, nameEnd).toCharArray(name, sizeof(name));
            user.setName(name);
        }
        
        // Extract pin
        int pinStart = body.indexOf("\"pin\":\"") + 7;
        if (pinStart > 6) {
            int pinEnd = body.indexOf("\"", pinStart);
            body.substring(pinStart, pinEnd).toCharArray(pin, sizeof(pin));
            user.setPin(pin);
        }
        
        if (userRepo) {
            int id = userRepo->create(&user);
            sendJson("{\"success\":true,\"id\":" + String(id) + "}");
        } else {
            sendError(500, "User repository not available");
        }
    }
}

void WebServerManager::handleUserLogin() {
    String body = server.arg("plain");
    
    int pinStart = body.indexOf("\"pin\":\"") + 7;
    char pin[5] = {0};
    if (pinStart > 6) {
        int pinEnd = body.indexOf("\"", pinStart);
        body.substring(pinStart, pinEnd).toCharArray(pin, sizeof(pin));
    }
    
    User* user = userRepo ? userRepo->authenticate(pin) : nullptr;
    if (user) {
        if (stateManager) {
            stateManager->onUserLogin(user->id);
        }
        sendJson("{\"success\":true,\"userId\":" + String(user->id) + 
                ",\"name\":\"" + String(user->name) + "\"}");
    } else {
        sendError(401, "Invalid PIN");
    }
}

void WebServerManager::handleUserLogout() {
    if (stateManager) {
        stateManager->onUserLogout();
    }
    sendJson("{\"success\":true}");
}

void WebServerManager::handleLogs() {
    int limit = server.hasArg("limit") ? server.arg("limit").toInt() : 50;
    int offset = server.hasArg("offset") ? server.arg("offset").toInt() : 0;
    
    auto logs = logRepo ? logRepo->findAll(limit, offset) : std::vector<LogEntry>();
    
    String json = "{\"logs\":[";
    bool first = true;
    for (auto& log : logs) {
        if (!first) json += ",";
        first = false;
        json += "{\"timestamp\":" + String(log.timestamp) + 
                ",\"event\":\"" + String(log.event) + "\"," +
                "\"userId\":" + String(log.userId) + "," +
                "\"toolId\":" + String(log.toolId) + "," +
                "\"weight\":" + String(log.weight) + "}";
    }
    json += "],\"total\":" + String(logRepo ? logRepo->count() : 0) + "}";
    sendJson(json);
}

void WebServerManager::handleCalibrate() {
    if (weightService && stateManager) {
        float baseline = weightService->getBaseline();
        stateManager->setBaseline(baseline);
        
        // Save baseline to storage
        storage.putFloat("baseline", baseline);
        
        sendJson("{\"success\":true,\"baseline\":" + String(baseline, 2) + "}");
    } else {
        sendError(500, "Not ready");
    }
}

void WebServerManager::handleConfig() {
    if (server.method() == HTTP_GET) {
        String json = "{";
        json += "\"deviceName\":\"ESP32-Inventory-Box\",";
        json += "\"wifiSSID\":\"" + String(WiFi.SSID()) + "\",";
        json += "\"wifiRssi\":" + String(WiFi.RSSI()) + ",";
        json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
        json += "\"uptime\":" + String(millis());
        json += "}";
        sendJson(json);
    } else {
        String body = server.arg("plain");
        
        // Parse config updates
        // threshold, settlingTime, motionThreshold, etc.
        
        sendJson("{\"success\":true}");
    }
}

void WebServerManager::handleDiagnostics() {
    String json = "{";
    
    // Overall status
    if (systemStatus) {
        ComponentStatus overall = systemStatus->getOverallStatus();
        json += "\"overallStatus\":\"";
        if (overall == ComponentStatus::OK) json += "OK";
        else if (overall == ComponentStatus::WARNING) json += "WARNING";
        else json += "ERROR";
        json += "\",";
        
        json += "\"uptime\":" + String(systemStatus->getUptime()) + ",";
        json += "\"totalErrors\":" + String(systemStatus->getErrorCount()) + ",";
        json += "\"lastError\":\"" + systemStatus->getLastError() + "\",";
        json += "\"okCount\":" + String(systemStatus->getOKCount()) + ",";
        json += "\"warningCount\":" + String(systemStatus->getWarningCount()) + ",";
        json += "\"errorCount\":" + String(systemStatus->getErrorComponentCount()) + ",";
        
        // Component details
        json += "\"components\":[";
        auto comps = systemStatus->getAllComponents();
        bool first = true;
        for (auto& comp : comps) {
            if (!first) json += ",";
            first = false;
            
            json += "{";
            json += "\"name\":\"" + comp.name + "\",";
            json += "\"status\":\"";
            if (comp.status == ComponentStatus::OK) json += "OK";
            else if (comp.status == ComponentStatus::WARNING) json += "WARNING";
            else if (comp.status == ComponentStatus::ERROR) json += "ERROR";
            else json += "UNKNOWN";
            json += "\",";
            json += "\"lastError\":\"" + comp.lastError + "\",";
            json += "\"errorCount\":" + String(comp.errorCount);
            json += "}";
        }
        json += "]";
    } else {
        json += "\"overallStatus\":\"UNKNOWN\",";
        json += "\"uptime\":0,";
        json += "\"totalErrors\":0,";
        json += "\"lastError\":\"SystemStatus not available\",";
        json += "\"components\":[]";
    }
    
    json += "}";
    sendJson(json);
}

void WebServerManager::handleRestart() {
    Serial.println("[WebServer] Restart requested via API");
    sendJson("{\"success\":true,\"message\":\"Restarting...\"}");
    delay(500);
    ESP.restart();
}

void WebServerManager::handleWiFiStatus() {
    String json = "{";
    json += "\"connected\":" + String((wifiManager && wifiManager->isConnected()) ? "true" : "false") + ",";
    json += "\"apMode\":" + String((wifiManager && wifiManager->isAPMode()) ? "true" : "false") + ",";
    
    if (wifiManager) {
        json += "\"ip\":\"" + wifiManager->getIP() + "\",";
        json += "\"ssid\":\"" + wifiManager->getSSID() + "\",";
        json += "\"rssi\":" + String(wifiManager->getRSSI());
    }
    
    json += "}";
    sendJson(json);
}

void WebServerManager::handleWiFiConfig() {
    if (server.method() == HTTP_POST) {
        String body = server.arg("plain");
        
        String ssid, pass;
        
        int ssidStart = body.indexOf("\"ssid\":\"") + 8;
        if (ssidStart > 7) {
            int ssidEnd = body.indexOf("\"", ssidStart);
            ssid = body.substring(ssidStart, ssidEnd);
        }
        
        int passStart = body.indexOf("\"password\":\"") + 12;
        if (passStart > 11) {
            int passEnd = body.indexOf("\"", passStart);
            pass = body.substring(passStart, passEnd);
        }
        
        if (ssid.length() > 0 && wifiManager) {
            wifiManager->setCredentials(ssid.c_str(), pass.c_str());
            sendJson("{\"success\":true,\"message\":\"Credentials saved. Rebooting...\"}");
            delay(1500);
            ESP.restart();
        } else {
            sendError(400, "Invalid credentials");
        }
    }
}

void WebServerManager::sendJson(const String& json) {
    server.send(200, "application/json", json);
}

void WebServerManager::handleStaticFile(const char* path, const char* mimeType) {
    if (SPIFFS.exists(path)) {
        File file = SPIFFS.open(path, "r");
        server.streamFile(file, mimeType);
        file.close();
    } else {
        server.send(404, "text/plain", "File not found");
    }
}

void WebServerManager::sendError(int code, const char* message) {
    String json = "{\"error\":true,\"code\":" + String(code) + 
                 ",\"message\":\"" + String(message) + "\"}";
    server.send(code, "application/json", json);
}

String WebServerManager::stateToString(int state) {
    const char* states[] = {"INIT", "IDLE", "ANALYZING", "TOOL_PLACED", 
                           "REMOVING", "UNKNOWN", "CALIBRATING", "ERROR", "SLEEP"};
    if (state >= 0 && state < 9) {
        return String(states[state]);
    }
    return "UNKNOWN";
}

void WebServerManager::setStateManager(StateManager* sm) {
    stateManager = sm;
}

void WebServerManager::setToolRepository(ToolRepository* tr) {
    toolRepo = tr;
}

void WebServerManager::setUserRepository(UserRepository* ur) {
    userRepo = ur;
}

void WebServerManager::setLogRepository(LogRepository* lr) {
    logRepo = lr;
}

void WebServerManager::setWeightService(WeightService* ws) {
    weightService = ws;
}

void WebServerManager::setWiFiManager(WiFiManager* wm) {
    wifiManager = wm;
}

void WebServerManager::setSystemStatus(SystemStatus* ss) {
    systemStatus = ss;
}

void WebServerManager::notifyClients(const char* event, const char* data) {
    // For WebSocket or SSE - simplified for now
}