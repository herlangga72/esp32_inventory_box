#include "StateManager.h"
#include "WeightService.h"
#include "MotionService.h"
#include "MatchingService.h"
#include "../../data/ToolRepository.h"
#include "../../data/LogRepository.h"
#include "../events/Events.h"
#include "config/Config.h"
#include "../../utils/LogManager.h"

StateManager::StateManager(EventBus* events)
    : events(events), weightService(nullptr), motionService(nullptr),
      toolRepo(nullptr), logRepo(nullptr), stateStartTime(0) {}

void StateManager::begin() {
    boxState.state = BoxStateMachine::INIT;
    matchingService.setToolRepository(toolRepo);
    transition(BoxStateMachine::IDLE);
}

void StateManager::update() {
    // Monitor weight changes
    if (weightService && boxState.state == BoxStateMachine::IDLE) {
        float delta = weightService->getDelta();
        if (abs(delta) > Config::WEIGHT_THRESHOLD_GRAMS) {
            onWeightChange(delta);
        }
    }
}

void StateManager::setWeightService(WeightService* ws) {
    weightService = ws;
}

void StateManager::setMotionService(MotionService* ms) {
    motionService = ms;
}

void StateManager::setToolRepository(ToolRepository* tr) {
    toolRepo = tr;
    matchingService.setToolRepository(tr);
}

void StateManager::setLogRepository(LogRepository* lr) {
    logRepo = lr;
}

void StateManager::onWeightChange(float delta) {
    switch (boxState.state) {
        case BoxStateMachine::IDLE:
            if (abs(delta) > Config::WEIGHT_THRESHOLD_GRAMS) {
                transition(BoxStateMachine::ANALYZING);
            }
            break;
            
        case BoxStateMachine::ANALYZING:
            // Wait for settling timer
            break;
            
        case BoxStateMachine::TOOL_PLACED:
            if (delta < -Config::WEIGHT_THRESHOLD_GRAMS) {
                transition(BoxStateMachine::REMOVING);
            }
            break;
            
        default:
            break;
    }
}

void StateManager::onMotionDetected(MotionType motion) {
    if (motion == MotionType::SETTLED) {
        // Motion stopped - settle weight
        if (boxState.state == BoxStateMachine::ANALYZING) {
            // Try to match weight
            if (weightService && toolRepo) {
                float delta = weightService->getDelta();
                Tool* matched = matchingService.matchByWeight(delta);
                if (matched) {
                    onToolMatched(matched->id);
                } else {
                    onUnknownWeight(delta);
                }
            }
        }
    }
    
    // Update box state based on motion
    if (motion == MotionType::TILT || motion == MotionType::FREE_FALL) {
        // Box was opened or moved significantly
        logStateEvent("LID_OPEN");
    }
}

void StateManager::onToolMatched(int toolId) {
    boxState.addTool(toolId);
    transition(BoxStateMachine::TOOL_PLACED);
    logStateEvent("TOOL_PLACED");
    
    // Publish event
    EventPayload event;
    event.type = DomainEvent::TOOL_PLACED;
    event.timestamp = millis();
    event.data.tool.toolId = toolId;
    events->publish(event);
}

void StateManager::onUnknownWeight(float weight) {
    transition(BoxStateMachine::UNKNOWN_ITEM);
    logStateEvent("UNKNOWN_ITEM");
    
    EventPayload event;
    event.type = DomainEvent::TOOL_UNKNOWN;
    event.timestamp = millis();
    event.data.generic.value = weight;
    events->publish(event);
}

void StateManager::onUserLogin(int userId) {
    boxState.currentUserId = userId;
    boxState.sessionStart = millis();
    
    EventPayload event;
    event.type = DomainEvent::USER_LOGIN;
    event.timestamp = millis();
    event.data.user.userId = userId;
    events->publish(event);
    
    logStateEvent("USER_LOGIN");
}

void StateManager::onUserLogout() {
    // Calculate usage time
    if (boxState.currentUserId > 0 && boxState.sessionStart > 0) {
        unsigned long usage = (millis() - boxState.sessionStart) / 1000;
        // Record usage in UserRepository
        
        EventPayload event;
        event.type = DomainEvent::USER_LOGOUT;
        event.timestamp = millis();
        event.data.user.userId = boxState.currentUserId;
        events->publish(event);
    }
    
    boxState.currentUserId = 0;
    boxState.sessionStart = 0;
    logStateEvent("USER_LOGOUT");
}

void StateManager::onCalibration(float baseline) {
    boxState.baselineGrams = baseline;
    if (weightService) {
        weightService->setBaseline(baseline);
    }
    transition(BoxStateMachine::IDLE);
    logStateEvent("CALIBRATED");
    
    EventPayload event;
    event.type = DomainEvent::CALIBRATION_COMPLETE;
    event.timestamp = millis();
    events->publish(event);
}

void StateManager::enterSleep() {
    transition(BoxStateMachine::SLEEP);
    
    EventPayload event;
    event.type = DomainEvent::SLEEP_ENTER;
    event.timestamp = millis();
    events->publish(event);
    
    logStateEvent("SLEEP");
}

void StateManager::wake() {
    transition(BoxStateMachine::IDLE);
    
    EventPayload event;
    event.type = DomainEvent::SLEEP_WAKE;
    event.timestamp = millis();
    events->publish(event);
    
    logStateEvent("WAKE");
}

void StateManager::handleWakeReason() {
    // Check wake cause and act accordingly
    if (motionService && motionService->isMotionDetected()) {
        // Motion wake - might be box opened
        if (motionService->isLidOpen()) {
            onMotionDetected(MotionType::TILT);
        }
    }
}

BoxStateMachine StateManager::getCurrentState() {
    return boxState.state;
}

BoxState* StateManager::getState() {
    return &boxState;
}

float StateManager::getBaseline() {
    return boxState.baselineGrams;
}

void StateManager::setBaseline(float baseline) {
    boxState.baselineGrams = baseline;
    if (weightService) {
        weightService->setBaseline(baseline);
    }
}

int StateManager::getCurrentUserId() {
    return boxState.currentUserId;
}

void StateManager::setCurrentUserId(int userId) {
    boxState.currentUserId = userId;
}

void StateManager::transition(BoxStateMachine newState) {
    if (!isValidTransition(boxState.state, newState)) {
        LOG_INFO("STATE", "Invalid state transition: %d -> %d\n", (int)boxState.state, (int)newState);
        return;
    }
    
    BoxStateMachine oldState = boxState.state;
    boxState.state = newState;
    stateStartTime = millis();
    
    // Publish state change
    EventPayload event;
    event.type = DomainEvent::STATE_CHANGED;
    event.timestamp = millis();
    event.data.state.fromState = (int)oldState;
    event.data.state.toState = (int)newState;
    events->publish(event);
    
    LOG_INFO("STATE", "State: %d -> %d\n", (int)oldState, (int)newState);
}

bool StateManager::isValidTransition(BoxStateMachine from, BoxStateMachine to) {
    // Allow most transitions
    switch (from) {
        case BoxStateMachine::INIT:
            return to == BoxStateMachine::IDLE;
        case BoxStateMachine::IDLE:
            return to == BoxStateMachine::ANALYZING || 
                   to == BoxStateMachine::SLEEP;
        case BoxStateMachine::ANALYZING:
            return to == BoxStateMachine::TOOL_PLACED || 
                   to == BoxStateMachine::UNKNOWN_ITEM ||
                   to == BoxStateMachine::IDLE;
        case BoxStateMachine::TOOL_PLACED:
            return to == BoxStateMachine::REMOVING ||
                   to == BoxStateMachine::IDLE;
        case BoxStateMachine::REMOVING:
            return to == BoxStateMachine::IDLE;
        case BoxStateMachine::UNKNOWN_ITEM:
            return to == BoxStateMachine::IDLE;
        case BoxStateMachine::SLEEP:
            return to == BoxStateMachine::IDLE;
        default:
            return true;
    }
}

void StateManager::logStateEvent(const char* event) {
    // Log via RTOS queue → Serial + SPIFFS file
    float w = weightService ? weightService->getCurrentWeight() : 0;
    float d = weightService ? weightService->getDelta() : 0;
    LOG_INFO("STATE", "%s uid=%d w=%.1f d=%.1f",
        event, boxState.currentUserId, w, d);
}