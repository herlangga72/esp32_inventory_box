#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include "../domain/events/EventBus.h"

class StateManager;
class ToolRepository;
class UserRepository;
class LogRepository;
class WeightService;
class WiFiManager;
class SystemStatus;

class WebServerManager {
public:
    WebServerManager(EventBus* events);
    
    void begin();
    void handle();
    
    void setStateManager(StateManager* sm);
    void setToolRepository(ToolRepository* tr);
    void setUserRepository(UserRepository* ur);
    void setLogRepository(LogRepository* lr);
    void setWeightService(WeightService* ws);
    void setWiFiManager(WiFiManager* wm);
    void setSystemStatus(SystemStatus* ss);
    
    void notifyClients(const char* event, const char* data = nullptr);

private:
    ::WebServer server;
    EventBus* events;
    
    StateManager* stateManager;
    ToolRepository* toolRepo;
    UserRepository* userRepo;
    LogRepository* logRepo;
    WeightService* weightService;
    WiFiManager* wifiManager;
    SystemStatus* systemStatus;
    
    // Route handlers
    void handleStatus();
    void handleTools();
    void handleToolById();
    void handleUsers();
    void handleUserLogin();
    void handleUserLogout();
    void handleLogs();
    void handleCalibrate();
    void handleConfig();
    void handleRoot();
    void handleDiagnostics();
    void handleRestart();
    void handleWiFiStatus();
    void handleWiFiConfig();
    
    // Helpers
    void sendJson(const String& json);
    void sendError(int code, const char* message);
    String stateToString(int state);
    void handleStaticFile(const char* path, const char* mimeType);
};

#endif // WEB_SERVER_H