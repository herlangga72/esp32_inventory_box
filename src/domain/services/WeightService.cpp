#include "WeightService.h"
#include "../events/EventBus.h"
#include "../../config/Config.h"

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

static float applyMovingAverage(WeightServiceMemory* mem, int32_t raw) {
    // Subtract the oldest reading from the running sum
    mem->filterSum -= mem->readings[mem->filterIndex];

    // Add the new reading (scaled by calibration factor)
    mem->readings[mem->filterIndex] = raw * mem->calibrationFactor;
    mem->filterSum += mem->readings[mem->filterIndex];

    // Advance the circular index
    mem->filterIndex = (mem->filterIndex + 1) % Config::FILTER_SIZE;

    return mem->filterSum / Config::FILTER_SIZE;
}

static void processWeight(WeightServiceMemory* mem) {
    float delta = mem->currentWeight - mem->baseline;

    // Send WEIGHT_CHANGE to StateManager (delta in f2.f1, currentWeight in f2.f2)
    ServiceMessage sm = ServiceMessage::cmd(
        ServiceId::STATE_MANAGER,
        static_cast<uint8_t>(StateMsgType::WEIGHT_CHANGE));
    sm.f2.f1 = delta;
    sm.f2.f2 = mem->currentWeight;
    g_registry.send(ServiceId::STATE_MANAGER, sm);

    // Send WEIGHT_UPDATE to DisplayManager (currentWeight in f2.f1, delta in f2.f2)
    ServiceMessage dm = ServiceMessage::cmd(
        ServiceId::DISPLAY_MANAGER,
        static_cast<uint8_t>(DisplayMsgType::WEIGHT_UPDATE));
    dm.f2.f1 = mem->currentWeight;
    dm.f2.f2 = delta;
    g_registry.send(ServiceId::DISPLAY_MANAGER, dm);

    // Publish WEIGHT_UPDATED to EventBus for backward compatibility
    EventPayload event;
    event.type = DomainEvent::WEIGHT_UPDATED;
    event.timestamp = millis();
    event.data.weight.weight   = mem->currentWeight;
    event.data.weight.delta    = delta;
    event.data.weight.baseline = mem->baseline;
    EventBus::getInstance()->publish(event);
}

// ---------------------------------------------------------------------------
// Public free-function API
// ---------------------------------------------------------------------------

void ws_onRawReading(WeightServiceMemory* mem, int32_t raw) {
    mem->previousWeight = mem->currentWeight;
    mem->currentWeight  = applyMovingAverage(mem, raw);
    mem->readingsTaken++;

    // Calibration mode: accumulate samples until the requested count is reached
    if (mem->calibrating && mem->calibrationSamples > 0) {
        mem->calibrationSum += mem->currentWeight;
        mem->calibrationSamples--;

        if (mem->calibrationSamples == 0) {
            mem->baseline    = mem->calibrationSum / mem->totalCalSamples;
            mem->calibrating = 0;

            // Publish CALIBRATION_COMPLETE event
            EventPayload event;
            event.type = DomainEvent::CALIBRATION_COMPLETE;
            event.timestamp = millis();
            event.data.generic.value = mem->baseline;
            EventBus::getInstance()->publish(event);
        }
        return;  // Don't publish WEIGHT_UPDATED during calibration
    }

    processWeight(mem);
}

void ws_update(WeightServiceMemory* mem) {
    ServiceMessage msg;
    while (g_registry.tryReceive(ServiceId::WEIGHT_SERVICE, msg)) {
        switch (static_cast<WeightMsgType>(msg.type)) {
            case WeightMsgType::SET_BASELINE:
                mem->baseline = msg.f2.f1;
                break;

            case WeightMsgType::START_CALIBRATION:
                mem->calibrating       = 1;
                mem->calibrationSamples = msg.u4.u1;
                mem->totalCalSamples   = msg.u4.u1;
                mem->calibrationSum    = 0;
                break;

            case WeightMsgType::TARE:
                mem->baseline = 0;
                break;

            default:
                break;
        }
        mem->messagesProcessed++;
    }
}

float ws_getCurrentWeight(const WeightServiceMemory* mem) {
    return mem->currentWeight;
}

float ws_getBaseline(const WeightServiceMemory* mem) {
    return mem->baseline;
}

float ws_getDelta(const WeightServiceMemory* mem) {
    return mem->currentWeight - mem->baseline;
}
