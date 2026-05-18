#ifndef STATE_MANAGER_H
#define STATE_MANAGER_H

#include <Arduino.h>
#include "../events/EventBus.h"
#include "../events/Events.h"
#include "../entities/BoxState.h"

class WeightService;
class MotionService;
class ToolRepository;
class LogRepository;

class StateManager {
public:
    StateManager(EventBus* events);
    
    void begin();
    void update();
    
    void setWeightService(WeightService* ws);
    void setMotionService(MotionService* ms);
    void setToolRepository(ToolRepository* tr);
    void setLogRepository(LogRepository* lr);
    
    // State transitions
    void onWeightChange(float delta);
    void onMotionDetected(MotionType motion);
    void onToolMatched(int toolId);
    void onUnknownWeight(float weight);
    void onUserLogin(int userId);
    void onUserLogout();
    void onCalibration(float baseline);
    
    void enterSleep();
    void wake();
    void handleWakeReason();
    
    BoxStateMachine getCurrentState();
    BoxState* getState();
    
    float getBaseline();
    void setBaseline(float baseline);
    
    int getCurrentUserId();
    void setCurrentUserId(int userId);

private:
    EventBus* events;
    WeightService* weightService;
    MotionService* motionService;
    ToolRepository* toolRepo;
    LogRepository* logRepo;
    
    BoxState boxState;
    unsigned long stateStartTime;
    
    void transition(BoxStateMachine newState);
    bool isValidTransition(BoxStateMachine from, BoxStateMachine to);
    void logStateEvent(const char* event);
};

#endif // STATE_MANAGER_H