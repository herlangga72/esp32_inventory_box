#include "SerialCLI.h"
#include "../data/StorageManager.h"
#include "../data/ToolRepository.h"
#include "../data/UserRepository.h"
#include "../data/LogRepository.h"

extern StorageManager storage;
extern ToolRepository toolRepo;
extern UserRepository userRepo;
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
    Serial.printf("Tool Count: %d\n", toolRepo.count());
    Serial.printf("User Count: %d\n", userRepo.count());
    Serial.printf("Log Count: %d\n", logRepo.count());
    Serial.printf("Baseline: %.1f g\n", storage.getFloat("baseline", 0));
}

void SerialCLI::cmdCalibrate() {
    Serial.println("\nCalibration: Place empty box, then restart.");
}

void SerialCLI::cmdTools() {
    Serial.println("\n=== Tools ===");
    auto tools = toolRepo.findAll();
    
    if (tools.empty()) {
        Serial.println("No tools registered.");
        return;
    }
    
    for (auto& tool : tools) {
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
    auto users = userRepo.findAll();
    
    if (users.empty()) {
        Serial.println("No users registered.");
        return;
    }
    
    for (auto& user : users) {
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
    auto logs = logRepo.findAll(10, 0);
    
    if (logs.empty()) {
        Serial.println("No logs.");
        return;
    }
    
    for (auto& log : logs) {
        Serial.printf("  [%lu] %s - user:%d tool:%d\n",
            log.timestamp,
            log.event,
            log.userId,
            log.toolId
        );
    }
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