#include "ServerClient.h"
#include "../config/Config.h"
#include "../utils/LogManager.h"
#include "../utils/JsonBuilder.h"
#include "../utils/JsonParser.h"

ServerClient::ServerClient()
    : reachable(false), configured(false),
      lastUserId(0),
      lastHeartbeat(0), lastResponseTime(0), serverFailStart(0),
      heartbeatInterval(60000) {
    serverUrl[0] = '\0';
    authToken[0] = '\0';
    lastReason[0] = '\0';
    lastUserName[0] = '\0';
}

void ServerClient::begin(const char* url, const char* token) {
    if (url && url[0]) {
        strncpy(serverUrl, url, sizeof(serverUrl) - 1);
        serverUrl[sizeof(serverUrl) - 1] = '\0';
        configured = true;
    }
    if (token && token[0]) {
        strncpy(authToken, token, sizeof(authToken) - 1);
        authToken[sizeof(authToken) - 1] = '\0';
    }
    LOG_INFO("SERVER", "INIT url=%s configured=%d", serverUrl, configured);
}

void ServerClient::update() {
    if (!configured) return;

    // Periodic heartbeat
    if (millis() - lastHeartbeat >= heartbeatInterval) {
        bool wasReachable = reachable;
        reachable = sendHeartbeat();
        lastHeartbeat = millis();

        if (reachable && !wasReachable) {
            LOG_INFO("SERVER", "RECONNECTED latency=%dms", lastResponseTime);
            serverFailStart = 0;
        } else if (!reachable && wasReachable) {
            LOG_WARN("SERVER", "UNREACHABLE url=%s", serverUrl);
            serverFailStart = millis();
        }
    }
}

bool ServerClient::isServerReachable() {
    return reachable;
}

int ServerClient::checkAccess(int fpId) {
    if (!configured || serverUrl[0] == '\0') {
        lastReason[0] = '\0';
        strncpy(lastReason, "server not configured", sizeof(lastReason) - 1);
        return -1;
    }

    // Build JSON request body
    JsonBuilder jb;
    jb.startObj();
    jb.addStr("deviceId", "inventory-box-01");
    jb.addInt("fpId", fpId);
    jb.addInt("timestamp", (int)time(nullptr));
    jb.endObj();
    String body(jb.str());

    unsigned long startMs = millis();
    String response = httpPost("/api/access/check", body.c_str(), SERVER_TIMEOUT_MS);
    lastResponseTime = millis() - startMs;

    if (response.length() == 0) {
        reachable = false;
        if (serverFailStart == 0) serverFailStart = millis();
        lastReason[0] = '\0';
        strncpy(lastReason, "server unreachable", sizeof(lastReason) - 1);
        return -1;
    }

    reachable = true;
    serverFailStart = 0;
    return parseAccessResponse(response);
}

bool ServerClient::sendHeartbeat() {
    if (!configured || serverUrl[0] == '\0') return false;

    char path[256];
    snprintf(path, sizeof(path), "/api/device/heartbeat?deviceId=inventory-box-01&uptime=%lu&freeHeap=%d",
             millis() / 1000, ESP.getFreeHeap());

    unsigned long startMs = millis();
    String response = httpGet(path, 3000);
    lastResponseTime = millis() - startMs;

    return parseHealthResponse(response);
}

bool ServerClient::syncAccessLogs(const char* jsonBatch) {
    if (!configured || !reachable) return false;

    String response = httpPost("/api/access/sync-logs", jsonBatch, 10000);
    if (response.length() == 0) return false;

    // Check for success response
    return response.indexOf("\"received\"") > 0;
}

// ---- Getters ----

const char* ServerClient::getLastReason() {
    return lastReason;
}

const char* ServerClient::getLastUserName() {
    return lastUserName;
}

int ServerClient::getLastUserId() {
    return lastUserId;
}

unsigned long ServerClient::getLastResponseTime() const {
    return lastResponseTime;
}

unsigned long ServerClient::getServerFailDuration() const {
    if (serverFailStart == 0) return 0;
    return millis() - serverFailStart;
}

bool ServerClient::isConfigured() const {
    return configured && serverUrl[0] != '\0';
}

// ---- Configuration ----

void ServerClient::setServerUrl(const char* url) {
    if (url) {
        strncpy(serverUrl, url, sizeof(serverUrl) - 1);
        serverUrl[sizeof(serverUrl) - 1] = '\0';
        configured = (serverUrl[0] != '\0');
    }
}

void ServerClient::setAuthToken(const char* token) {
    if (token) {
        strncpy(authToken, token, sizeof(authToken) - 1);
        authToken[sizeof(authToken) - 1] = '\0';
    }
}

const char* ServerClient::getServerUrl() const {
    return serverUrl;
}

const char* ServerClient::getAuthToken() const {
    return authToken;
}

// ---- HTTP Helpers ----

bool ServerClient::connectToServer() {
    // Parse host and port from serverUrl
    String url(serverUrl);
    url.trim();

    // Strip http:// prefix
    String host = url;
    int port = 80;
    if (host.startsWith("http://")) {
        host = host.substring(7);
    }
    if (host.startsWith("https://")) {
        host = host.substring(8);
        port = 443;
    }

    // Split host:port
    int colonIdx = host.indexOf(':');
    int slashIdx = host.indexOf('/');
    if (colonIdx > 0 && (slashIdx < 0 || colonIdx < slashIdx)) {
        String portStr = host.substring(colonIdx + 1);
        if (slashIdx > 0) {
            portStr = host.substring(colonIdx + 1, slashIdx);
        }
        port = portStr.toInt();
        host = host.substring(0, colonIdx);
    } else if (slashIdx > 0) {
        host = host.substring(0, slashIdx);
    }

    return client.connect(host.c_str(), port);
}

String ServerClient::httpPost(const char* path, const char* jsonBody, int timeoutMs) {
    if (!connectToServer()) return "";

    // Parse host for Host header
    String url(serverUrl);
    url.trim();
    String host = url;
    if (host.startsWith("http://")) host = host.substring(7);
    else if (host.startsWith("https://")) host = host.substring(8);
    int slashIdx = host.indexOf('/');
    if (slashIdx > 0) host = host.substring(0, slashIdx);
    int colonIdx = host.indexOf(':');
    if (colonIdx > 0) host = host.substring(0, colonIdx);

    char req[512];
    int written = snprintf(req, sizeof(req),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n",
        path, host.c_str(), (int)strlen(jsonBody));

    if (authToken[0] && written > 0 && written < (int)sizeof(req) - 1) {
        written += snprintf(req + written, sizeof(req) - written,
            "Authorization: Bearer %s\r\n", authToken);
    }

    if (written > 0 && written < (int)sizeof(req) - 1) {
        snprintf(req + written, sizeof(req) - written,
            "Connection: close\r\n\r\n");
    }

    client.print(req);
    client.print(jsonBody);

    // Read response with timeout
    unsigned long start = millis();
    while (!client.available()) {
        if (millis() - start > (unsigned long)timeoutMs) {
            client.stop();
            return "";
        }
        delay(10);
    }

    char buf[1024];
    size_t bufLen = 0;
    memset(buf, 0, sizeof(buf));
    unsigned long readStart = millis();
    while (client.connected() || client.available()) {
        if (client.available()) {
            char c = client.read();
            if (bufLen < sizeof(buf) - 1) {
                buf[bufLen++] = c;
            }
            readStart = millis();
        }
        if (millis() - readStart > 2000) break;  // idle timeout
    }
    client.stop();

    // Extract body (after \r\n\r\n)
    const char* bodyStart = strstr(buf, "\r\n\r\n");
    if (bodyStart) {
        return String(bodyStart + 4);
    }
    return "";
}

String ServerClient::httpGet(const char* path, int timeoutMs) {
    if (!connectToServer()) return "";

    String url(serverUrl);
    url.trim();
    String host = url;
    if (host.startsWith("http://")) host = host.substring(7);
    else if (host.startsWith("https://")) host = host.substring(8);
    int slashIdx = host.indexOf('/');
    if (slashIdx > 0) host = host.substring(0, slashIdx);
    int colonIdx = host.indexOf(':');
    if (colonIdx > 0) host = host.substring(0, colonIdx);

    char req[512];
    int written = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n",
        path, host.c_str());

    if (authToken[0] && written > 0 && written < (int)sizeof(req) - 1) {
        written += snprintf(req + written, sizeof(req) - written,
            "Authorization: Bearer %s\r\n", authToken);
    }

    if (written > 0 && written < (int)sizeof(req) - 1) {
        snprintf(req + written, sizeof(req) - written,
            "Connection: close\r\n\r\n");
    }

    client.print(req);

    unsigned long start = millis();
    while (!client.available()) {
        if (millis() - start > (unsigned long)timeoutMs) {
            client.stop();
            return "";
        }
        delay(10);
    }

    char buf[1024];
    size_t bufLen = 0;
    memset(buf, 0, sizeof(buf));
    unsigned long readStart = millis();
    while (client.connected() || client.available()) {
        if (client.available()) {
            char c = client.read();
            if (bufLen < sizeof(buf) - 1) {
                buf[bufLen++] = c;
            }
            readStart = millis();
        }
        if (millis() - readStart > 2000) break;
    }
    client.stop();

    const char* bodyStart = strstr(buf, "\r\n\r\n");
    if (bodyStart) {
        return String(bodyStart + 4);
    }
    return "";
}

int ServerClient::parseAccessResponse(const String& response) {
    // Parse JSON: {"allowed":true/false,"reason":"...","userName":"...","userId":N}
    struct { bool allowed; char reason[64]; char userName[32]; int userId; } resp;
    memset(&resp, 0, sizeof(resp));

    JField fields[] = {
        {"allowed",  JField::T_BOOL, &resp.allowed,  0},
        {"reason",   JField::T_STR,  resp.reason,    sizeof(resp.reason)},
        {"userName", JField::T_STR,  resp.userName,  sizeof(resp.userName)},
        {"userId",   JField::T_INT,  &resp.userId,   0},
    };
    String err;
    if (!jsonParse(response.c_str(), fields, 4, err)) {
        strncpy(lastReason, "parse error", sizeof(lastReason) - 1);
        return -1;
    }

    strncpy(lastReason, resp.reason, sizeof(lastReason) - 1);
    strncpy(lastUserName, resp.userName, sizeof(lastUserName) - 1);
    lastUserId = resp.userId;

    return resp.allowed ? 1 : 0;
}

bool ServerClient::parseHealthResponse(const String& response) {
    if (response.length() == 0) return false;
    struct { char status[16]; } resp;
    memset(&resp, 0, sizeof(resp));
    JField fields[] = {
        {"status", JField::T_STR, resp.status, sizeof(resp.status)},
    };
    String err;
    if (!jsonParse(response.c_str(), fields, 1, err)) return false;
    return strcmp(resp.status, "ok") == 0;
}
