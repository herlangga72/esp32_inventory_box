#include "SerialCLI.h"
#include <WiFi.h>
#include "../utils/LogManager.h"
#include "../data/StorageManager.h"
#include "../data/ToolRepository.h"
#include "../data/UserRepository.h"
#include "../data/LogRepository.h"

extern StorageManager storage;
extern LogRepository logRepo;

SerialCLI::SerialCLI() : buffer("") {}

void SerialCLI::begin() {
    Serial.println("\n=== ESP32 Inventory Box CLI ===");
    printHelp();
}

void SerialCLI::handle() {
    while (Serial.available()) {
        char c = Serial.read();
        
        if (c == '\n' || c == '\r') {
            if (buffer.length() > 0) {
                Serial.println("> " + buffer);
                parseCommand(buffer);
                buffer = "";
                Serial.print("\n> ");
            }
        } else {
            buffer += c;
        }
    }
}

void SerialCLI::parseCommand(const String& cmd) {
    String command = cmd;
    command.trim();
    command.toLowerCase();
    
    if (command == "help" || command == "?") {
        printHelp();
    }
    else if (command == "status") {
        cmdStatus();
    }
    else if (command == "calibrate") {
        cmdCalibrate();
    }
    else if (command.startsWith("tools")) {
        cmdTools();
    }
    else if (command.startsWith("users")) {
        cmdUsers();
    }
    else if (command.startsWith("logs")) {
        cmdLogs();
    }
    else if (command.startsWith("log level")) {
        String arg = command.substring(10);
        cmdLogLevel(arg);
    }
    else if (command == "reboot" || command == "restart") {
        cmdReboot();
    }
    else if (command.startsWith("wifi")) {
        cmdWifi();
    }
    else if (command == "mem" || command == "memory") {
        cmdMemory();
    }
    else if (command.length() > 0) {
        Serial.println("Unknown command. Type 'help' for commands.");
    }
}

void SerialCLI::printHelp() {
    Serial.println("\nAvailable commands:");
    Serial.println("  help      - Show this help");
    Serial.println("  status    - Show system status");
    Serial.println("  calibrate - Start calibration");
    Serial.println("  tools     - List all tools");
    Serial.println("  users     - List all users");
    Serial.println("  logs      - Show recent logs");
    Serial.println("  wifi      - WiFi status");
    Serial.println("  mem       - Memory info");
    Serial.println("  reboot    - Reboot device");
}

void SerialCLI::cmdStatus() {
    Serial.println("\n=== System Status ===");
    Serial.printf("Uptime: %lu ms\n", millis());
    Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("CPU Freq: %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("WiFi RSSI: %d dBm\n", WiFi.RSSI());
    Serial.printf("Tool Count: %d\n", tr_count(g_registry.getToolRepository(), &storage));
    Serial.printf("User Count: %d\n", ur_count(g_registry.getUserRepository(), &storage));
    Serial.printf("Log Count: %d\n", logRepo.count());
    Serial.printf("Baseline: %.1f g\n", storage.getFloat("baseline", 0));
}

void SerialCLI::cmdCalibrate() {
    Serial.println("\nCalibration: Place empty box, then restart.");
}

void SerialCLI::cmdTools() {
    Serial.println("\n=== Tools ===");
    Tool tools[Config::MAX_TOOLS];
    int toolCount = tr_findAll(g_registry.getToolRepository(), &storage, tools, Config::MAX_TOOLS);

    if (toolCount == 0) {
        Serial.println("No tools registered.");
        return;
    }

    for (int ti = 0; ti < toolCount; ti++) { auto& tool = tools[ti];
        Serial.printf("  [%d] %s - %.1fg %s\n",
            tool.id,
            tool.name,
            tool.weightGrams,
            tool.active ? "" : " (inactive)"
        );
    }
}

void SerialCLI::cmdUsers() {
    Serial.println("\n=== Users ===");
    User users[Config::MAX_USERS];
    int userCount = ur_findAll(g_registry.getUserRepository(), &storage, users, Config::MAX_USERS);

    if (userCount == 0) {
        Serial.println("No users registered.");
        return;
    }

    for (int ui = 0; ui < userCount; ui++) { auto& user = users[ui];
        Serial.printf("  [%d] %s (PIN: %s) %s\n",
            user.id,
            user.name,
            user.pin,
            user.active ? "" : " (inactive)"
        );
    }
}

void SerialCLI::cmdLogs() {
    Serial.println("\n=== Recent Logs ===");
    LogEntry logBuf[20];
    int logCount = logRepo.findAll(logBuf, 20, 20, 0);

    const char* lvl[] = {"NONE", "ERR", "WARN", "INFO", "DBG"};
    if (logCount == 0) {
        Serial.println("No logs.");
        return;
    }

    for (int i = 0; i < logCount; i++) {
        auto& entry = logBuf[i];
        Serial.printf("  [%lu] [%s] [%s] %s\n",
            (unsigned long)entry.timestamp,
            (entry.severity >= 0 && entry.severity <= 4) ? lvl[entry.severity] : "?",
            entry.event,
            entry.message
        );
    }
    Serial.printf("  --- %d total, %d dropped ---\n",
        logRepo.count(), logRepo.getDropped());
}

void SerialCLI::cmdLogLevel(const String& arg) {
    if (arg.length() == 0) {
        const char* names[] = {"NONE", "ERROR", "WARN", "INFO", "DEBUG"};
        Serial.printf("Current log level: %s\n", names[logGetLevel()]);
        return;
    }
    String a = arg;
    a.toLowerCase();
    if (a == "none")  logSetLevel(LOG_NONE);
    else if (a == "error") logSetLevel(LOG_ERROR);
    else if (a == "warn")  logSetLevel(LOG_WARN);
    else if (a == "info")  logSetLevel(LOG_INFO);
    else if (a == "debug") logSetLevel(LOG_DEBUG);
    else { Serial.println("Invalid level. Use: none, error, warn, info, debug"); return; }
    Serial.printf("Log level set to: %s\n", a.c_str());
}

void SerialCLI::cmdReboot() {
    Serial.println("\nRebooting...");
    delay(100);
    ESP.restart();
}

void SerialCLI::cmdWifi() {
    Serial.println("\n=== WiFi Status ===");
    Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
}

void SerialCLI::cmdMemory() {
    Serial.println("\n=== Memory ===");
    Serial.printf("Free Heap: %d bytes\n", ESP.getHeapSize());
    Serial.printf("Used Heap: %d bytes\n", ESP.getHeapSize() - ESP.getFreeHeap());
    Serial.printf("Min Free: %d bytes\n", ESP.getMinFreeHeap());
}