#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include "../domain/events/EventBus.h"
#include "../kernel/ServiceRegistry.h"
#include "../data/LogRepository.h"
#include "../kernel/WiFiManager.h"

class FingerprintDriver;
class ServerClient;

class WebServerManager {
public:
    WebServerManager(EventBus* events);

    void begin();
    void handle();

    void setLogRepository(LogRepository* lr);
    void setFingerprintDriver(FingerprintDriver* fp);
    void setServerClient(ServerClient* sc);

    void notifyClients(const char* event, const char* data = nullptr);
    bool hasActiveClient();

private:
    ::WebServer server;
    EventBus* events;

    LogRepository* logRepo;
    FingerprintDriver* fpDriver;
    ServerClient* serverClient;

    // WiFi scan cache — avoids flicker between async scan cycles
    String lastScanJson;
    uint32_t lastScanMs;

    void handleStatus();
    void handleTools();
    void handleToolById();
    void handleUsers();
    void handleUserLogin();
    void handleUserLogout();
    void handleLogs();
    void handleLogsDownload();
    void handleLogsClear();
    void handleCalibrate();
    void handleConfig();
    void handleRoot();
    void handleDiagnostics();
    void handleRestart();
    void handleWiFiStatus();
    void handleWiFiConfig();
    void handleUserDelete();
    void handleUserById();
    void handleWiFiScan();

    void handleAccessStatus();
    void handleAccessServerConfig();
    void handleFingerprintEnroll();
    void handleFingerprintDelete();
    void handleDoorControl();

    void handleContentsClear();

    void sendJson(const String& json);
    void sendError(int code, const char* message);
    String stateToString(int state);
    void handleStaticFile(const char* path, const char* mimeType);
    void logRequest();
};

// Global wrappers used by main.cpp
void web_begin();
void web_stop();
void web_handle();
void web_setLogRepository(LogRepository* lr);

#endif // WEB_SERVER_H