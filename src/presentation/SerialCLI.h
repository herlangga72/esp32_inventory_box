#ifndef SERIAL_CLI_H
#define SERIAL_CLI_H

#include <Arduino.h>

class SerialCLI {
public:
    SerialCLI();
    void begin();
    void handle();
    
private:
    String buffer;
    
    void parseCommand(const String& cmd);
    void printHelp();
    void cmdStatus();
    void cmdCalibrate();
    void cmdTools();
    void cmdUsers();
    void cmdLogs();
    void cmdLogLevel(const String& arg);
    void cmdReboot();
    void cmdWifi();
    void cmdMemory();
};

#endif // SERIAL_CLI_H