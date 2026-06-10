#include "StateManager.h"
#include "MatchingService.h"
#include "../../data/ToolRepository.h"
#include "../events/EventBus.h"
#include "../../utils/LogManager.h"
#include "../../config/Config.h"

// External storage instance for NVS persistence
extern StorageManager storage;

// ======================================================================
// Internal helpers
// ======================================================================

// State constants matching BoxStateMachine enum (BooleStateMachine.h)
enum : uint8_t {
    ST_INIT         = 0,
    ST_IDLE         = 1,
    ST_ANALYZING    = 2,
    ST_TOOL_PLACED  = 3,
    ST_REMOVING     = 4,
    ST_UNKNOWN_ITEM = 5,
    ST_CALIBRATING  = 6,
    ST_ERROR        = 7,
    ST_SLEEP        = 8
};

static bool isValidTransition(uint8_t from, uint8_t to) {
    switch (from) {
        case ST_INIT:
            return to == ST_IDLE;
        case ST_IDLE:
            return to == ST_ANALYZING || to == ST_SLEEP;
        case ST_ANALYZING:
            return to == ST_TOOL_PLACED || to == ST_UNKNOWN_ITEM || to == ST_IDLE;
        case ST_TOOL_PLACED:
            return to == ST_REMOVING || to == ST_IDLE;
        case ST_REMOVING:
            return to == ST_IDLE;
        case ST_UNKNOWN_ITEM:
            return to == ST_IDLE;
        case ST_SLEEP:
            return to == ST_IDLE;
        default:
            return true;
    }
}

static void transition(StateManagerMemory* mem, uint8_t newState) {
    if (!isValidTransition(mem->state, newState)) {
        LOG_INFO("STATE", "Invalid state transition: %d -> %d\n",
                 mem->state, newState);
        return;
    }

    uint8_t oldState = mem->state;
    mem->state = newState;
    mem->stateStartMs = millis();

    // Publish state change via EventBus (backward compat)
    EventPayload event;
    event.type = DomainEvent::STATE_CHANGED;
    event.timestamp = millis();
    event.data.state.fromState = (int)oldState;
    event.data.state.toState   = (int)newState;
    EventBus::getInstance()->publish(event);

    // Notify DisplayManager via mailbox
    ServiceMessage dm = ServiceMessage::cmd(ServiceId::DISPLAY_MANAGER,
        static_cast<uint8_t>(DisplayMsgType::STATE_CHANGED));
    dm.bytes.b0 = oldState;
    dm.bytes.b1 = newState;
    g_registry.send(ServiceId::DISPLAY_MANAGER, dm);

    LOG_INFO("STATE", "State: %d -> %d\n", (int)oldState, (int)newState);
}

static bool addToContents(StateManagerMemory* mem, int32_t toolId) {
    if (mem->contentCount >= Config::MAX_CONTENTS) return false;
    for (int i = 0; i < mem->contentCount; i++) {
        if (mem->contents[i] == toolId) return false;  // already present
    }
    mem->contents[mem->contentCount++] = toolId;
    return true;
}

static bool removeFromContents(StateManagerMemory* mem, int32_t toolId) {
    for (int i = 0; i < mem->contentCount; i++) {
        if (mem->contents[i] == toolId) {
            for (int j = i; j < mem->contentCount - 1; j++) {
                mem->contents[j] = mem->contents[j + 1];
            }
            mem->contentCount--;
            return true;
        }
    }
    return false;
}

// ======================================================================
// Public API
// ======================================================================

void sm_init(StateManagerMemory* mem, StorageManager* storage) {
    memset(mem, 0, sizeof(StateManagerMemory));
    mem->baselineGrams = 0.0f;
    mem->state         = ST_INIT;
    transition(mem, ST_IDLE);
    LOG_INFO("STATE", "StateManager initialized (baseline=0)\n");
}

void sm_dispatchMessage(StateManagerMemory* mem, const ServiceMessage& msg) {
    mem->messagesProcessed++;

    switch (static_cast<StateMsgType>(msg.type)) {

    // ---------------------------------------------------------------
    // WEIGHT_CHANGE: f2.f1 = delta, f2.f2 = currentWeight
    // ---------------------------------------------------------------
    case StateMsgType::WEIGHT_CHANGE: {
        float delta         = msg.f2.f1;
        float currentWeight = msg.f2.f2;

        mem->previousWeightGrams = mem->currentWeightGrams;
        mem->currentWeightGrams  = currentWeight;

        switch (mem->state) {
        case ST_IDLE:
            if (abs(delta) > Config::WEIGHT_THRESHOLD_GRAMS) {
                transition(mem, ST_ANALYZING);
            }
            break;

        case ST_ANALYZING:
            // Weight still changing — reset the settling timer
            mem->stateStartMs = millis();
            break;

        case ST_TOOL_PLACED:
            if (delta < -Config::WEIGHT_THRESHOLD_GRAMS) {
                transition(mem, ST_REMOVING);
            }
            break;

        default:
            break;
        }
        break;
    }

    // ---------------------------------------------------------------
    // MOTION_DETECTED: bytes.b0 = MotionType
    // ---------------------------------------------------------------
    case StateMsgType::MOTION_DETECTED: {
        MotionType motion = static_cast<MotionType>(msg.bytes.b0);

        // SETTLED in ANALYZING state — run tool matching
        if (motion == MotionType::SETTLED && mem->state == ST_ANALYZING) {
            ToolRepositoryMemory* toolRepo = g_registry.getToolRepository();
            Tool toolBuf[Config::MAX_TOOLS];
            int toolCount = tr_findAll(toolRepo, &storage, toolBuf, Config::MAX_TOOLS);

            float delta      = mem->currentWeightGrams - mem->baselineGrams;
            int   matchIds[Config::MAX_CONTENTS];
            int   matchCount = ms_matchByWeight(toolBuf, toolCount, delta,
                                   Config::DEFAULT_TOLERANCE, matchIds, Config::MAX_CONTENTS);

            if (matchCount > 0) {
                // Persist match results for diagnostics
                mem->matchCount = matchCount;
                for (int i = 0; i < matchCount && i < Config::MAX_CONTENTS; i++) {
                    mem->matchResults[i] = matchIds[i];
                }

                // First match: primary tool placed
                addToContents(mem, matchIds[0]);
                transition(mem, ST_TOOL_PLACED);
                LOG_INFO("STATE", "TOOL_PLACED uid=%d toolId=%d w=%.1f d=%.1f",
                         mem->currentUserId, matchIds[0],
                         mem->currentWeightGrams, delta);

                EventPayload event;
                event.type          = DomainEvent::TOOL_PLACED;
                event.timestamp     = millis();
                event.data.tool.toolId = matchIds[0];
                EventBus::getInstance()->publish(event);

                ServiceMessage dm = ServiceMessage::cmd(ServiceId::DISPLAY_MANAGER,
                    static_cast<uint8_t>(DisplayMsgType::TOOL_PLACED));
                dm.u2.u1 = static_cast<uint16_t>(matchIds[0]);
                g_registry.send(ServiceId::DISPLAY_MANAGER, dm);

                // Additional matches (if any): add to contents silently
                for (int i = 1; i < matchCount; i++) {
                    addToContents(mem, matchIds[i]);
                    LOG_INFO("STATE", "TOOL_ADDITIONAL uid=%d toolId=%d",
                             mem->currentUserId, matchIds[i]);
                }
            } else {
                // No match — unknown item
                transition(mem, ST_UNKNOWN_ITEM);
                LOG_INFO("STATE", "UNKNOWN_ITEM uid=%d w=%.1f d=%.1f",
                         mem->currentUserId, mem->currentWeightGrams, delta);

                EventPayload event;
                event.type              = DomainEvent::TOOL_UNKNOWN;
                event.timestamp         = millis();
                event.data.generic.value = delta;
                EventBus::getInstance()->publish(event);
            }
        }

        // TILT / FREE_FALL — log lid open
        if (motion == MotionType::TILT || motion == MotionType::FREE_FALL) {
            LOG_INFO("STATE", "LID_OPEN uid=%d", mem->currentUserId);
        }
        break;
    }

    // ---------------------------------------------------------------
    // TOOL_MATCHED: u2.u1 = toolId
    // ---------------------------------------------------------------
    case StateMsgType::TOOL_MATCHED: {
        int32_t toolId = static_cast<int32_t>(msg.u2.u1);
        addToContents(mem, toolId);
        transition(mem, ST_TOOL_PLACED);
        LOG_INFO("STATE", "TOOL_PLACED uid=%d toolId=%d", mem->currentUserId, toolId);

        EventPayload event;
        event.type          = DomainEvent::TOOL_PLACED;
        event.timestamp     = millis();
        event.data.tool.toolId = toolId;
        EventBus::getInstance()->publish(event);

        ServiceMessage dm = ServiceMessage::cmd(ServiceId::DISPLAY_MANAGER,
            static_cast<uint8_t>(DisplayMsgType::TOOL_PLACED));
        dm.u2.u1 = static_cast<uint16_t>(toolId);
        g_registry.send(ServiceId::DISPLAY_MANAGER, dm);
        break;
    }

    // ---------------------------------------------------------------
    // UNKNOWN_WEIGHT: f2.f1 = weight
    // ---------------------------------------------------------------
    case StateMsgType::UNKNOWN_WEIGHT: {
        float weight = msg.f2.f1;
        transition(mem, ST_UNKNOWN_ITEM);
        LOG_INFO("STATE", "UNKNOWN_ITEM uid=%d w=%.1f", mem->currentUserId, weight);

        EventPayload event;
        event.type              = DomainEvent::TOOL_UNKNOWN;
        event.timestamp         = millis();
        event.data.generic.value = weight;
        EventBus::getInstance()->publish(event);
        break;
    }

    // ---------------------------------------------------------------
    // USER_LOGIN: u2.u1 = userId
    // ---------------------------------------------------------------
    case StateMsgType::USER_LOGIN: {
        int32_t userId = static_cast<int32_t>(msg.u2.u1);
        mem->currentUserId = userId;
        mem->sessionStartMs = millis();

        EventPayload event;
        event.type              = DomainEvent::USER_LOGIN;
        event.timestamp         = millis();
        event.data.user.userId  = userId;
        EventBus::getInstance()->publish(event);

        ServiceMessage dm = ServiceMessage::cmd(ServiceId::DISPLAY_MANAGER,
            static_cast<uint8_t>(DisplayMsgType::USER_LOGIN));
        dm.u2.u1 = static_cast<uint16_t>(userId);
        g_registry.send(ServiceId::DISPLAY_MANAGER, dm);

        LOG_INFO("STATE", "USER_LOGIN uid=%d", userId);
        break;
    }

    // ---------------------------------------------------------------
    // USER_LOGOUT: no payload
    // ---------------------------------------------------------------
    case StateMsgType::USER_LOGOUT: {
        if (mem->currentUserId > 0 && mem->sessionStartMs > 0) {
            EventPayload event;
            event.type              = DomainEvent::USER_LOGOUT;
            event.timestamp         = millis();
            event.data.user.userId  = mem->currentUserId;
            EventBus::getInstance()->publish(event);
        }

        mem->currentUserId  = 0;
        mem->sessionStartMs = 0;

        ServiceMessage dm = ServiceMessage::cmd(ServiceId::DISPLAY_MANAGER,
            static_cast<uint8_t>(DisplayMsgType::USER_LOGOUT));
        g_registry.send(ServiceId::DISPLAY_MANAGER, dm);

        LOG_INFO("STATE", "USER_LOGOUT");
        break;
    }

    // ---------------------------------------------------------------
    // CALIBRATION: f2.f1 = baseline
    // ---------------------------------------------------------------
    case StateMsgType::CALIBRATION: {
        float baseline = msg.f2.f1;
        mem->baselineGrams = baseline;

        // Forward baseline to WeightService mailbox
        ServiceMessage wm = ServiceMessage::cmd(ServiceId::WEIGHT_SERVICE,
            static_cast<uint8_t>(WeightMsgType::SET_BASELINE));
        wm.f2.f1 = baseline;
        g_registry.send(ServiceId::WEIGHT_SERVICE, wm);

        transition(mem, ST_IDLE);
        LOG_INFO("STATE", "CALIBRATED baseline=%.1f", baseline);

        EventPayload event;
        event.type      = DomainEvent::CALIBRATION_COMPLETE;
        event.timestamp = millis();
        EventBus::getInstance()->publish(event);
        break;
    }

    // ---------------------------------------------------------------
    // ENTER_SLEEP: no payload
    // ---------------------------------------------------------------
    case StateMsgType::ENTER_SLEEP: {
        transition(mem, ST_SLEEP);

        EventPayload event;
        event.type      = DomainEvent::SLEEP_ENTER;
        event.timestamp = millis();
        EventBus::getInstance()->publish(event);

        ServiceMessage dm = ServiceMessage::cmd(ServiceId::DISPLAY_MANAGER,
            static_cast<uint8_t>(DisplayMsgType::SLEEP));
        g_registry.send(ServiceId::DISPLAY_MANAGER, dm);

        LOG_INFO("STATE", "SLEEP");
        break;
    }

    // ---------------------------------------------------------------
    // WAKE: no payload
    // ---------------------------------------------------------------
    case StateMsgType::WAKE: {
        transition(mem, ST_IDLE);

        EventPayload event;
        event.type      = DomainEvent::SLEEP_WAKE;
        event.timestamp = millis();
        EventBus::getInstance()->publish(event);

        ServiceMessage dm = ServiceMessage::cmd(ServiceId::DISPLAY_MANAGER,
            static_cast<uint8_t>(DisplayMsgType::WAKE));
        g_registry.send(ServiceId::DISPLAY_MANAGER, dm);

        LOG_INFO("STATE", "WAKE");
        break;
    }

    default:
        break;
    }
}

void sm_updatePeriodic(StateManagerMemory* mem) {
    // Time-based ANALYZING -> IDLE transition after settling timeout
    if (mem->state == ST_ANALYZING) {
        if (millis() - mem->stateStartMs > (uint32_t)Config::SETTLING_TIME_MS) {
            transition(mem, ST_IDLE);
        }
    }
}

// ======================================================================
// Read queries
// ======================================================================

int sm_getCurrentState(const StateManagerMemory* mem) {
    return mem->state;
}

int sm_getCurrentUserId(const StateManagerMemory* mem) {
    return mem->currentUserId;
}

float sm_getBaseline(const StateManagerMemory* mem) {
    return mem->baselineGrams;
}

const int32_t* sm_getContents(const StateManagerMemory* mem) {
    return mem->contents;
}

int sm_getContentCount(const StateManagerMemory* mem) {
    return mem->contentCount;
}
