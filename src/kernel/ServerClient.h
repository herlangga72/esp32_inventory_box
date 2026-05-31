#ifndef SERVER_CLIENT_H
#define SERVER_CLIENT_H

#include <Arduino.h>
#include <WiFiClient.h>

class ServerClient {
public:
    ServerClient();

    void begin(const char* serverUrl, const char* authToken = nullptr);
    void update();                           // state machine tick (heartbeat, retries)

    // Primary access check
    bool isServerReachable();
    int  checkAccess(int fpId);              // returns -1=error, 0=deny, 1=allow
    const char* getLastReason();
    const char* getLastUserName();
    int  getLastUserId();

    // Health
    bool sendHeartbeat();

    // Log sync (batch JSON)
    bool syncAccessLogs(const char* jsonBatch);

    // Configuration
    void setServerUrl(const char* url);
    void setAuthToken(const char* token);
    const char* getServerUrl() const;
    const char* getAuthToken() const;

    // Connection stats
    unsigned long getLastResponseTime() const;
    unsigned long getServerFailDuration() const;  // how long server been unreachable
    bool isConfigured() const;

private:
    WiFiClient client;

    char serverUrl[128];
    char authToken[64];
    bool reachable;
    bool configured;

    // Last access check result
    char lastReason[64];
    char lastUserName[32];
    int  lastUserId;

    // Timing
    unsigned long lastHeartbeat;
    unsigned long lastResponseTime;
    unsigned long serverFailStart;   // when server first became unreachable
    unsigned long heartbeatInterval;

    // HTTP helpers
    String httpPost(const char* path, const char* jsonBody, int timeoutMs);
    String httpGet(const char* path, int timeoutMs);
    int  parseAccessResponse(const String& response);
    bool parseHealthResponse(const String& response);

    // Connection
    bool connectToServer();
};

#endif // SERVER_CLIENT_H
