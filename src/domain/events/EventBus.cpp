#include "EventBus.h"
#include <cstring>

EventBus EventBus::s_instance;

EventBus::EventBus() : tokenCount(0), nextToken(1) {
    memset(subscribers, 0, sizeof(subscribers));
    memset(subscriberCounts, 0, sizeof(subscriberCounts));
    memset(tokenEntries, 0, sizeof(tokenEntries));
    memset(tokenSlots, 0, sizeof(tokenSlots));
}

EventBus* EventBus::getInstance() {
    return &s_instance;
}

SubscriptionToken EventBus::subscribe(DomainEvent type,
                                       void (*fn)(const EventPayload&, void*),
                                       void* ctx) {
    uint8_t evtIdx = static_cast<uint8_t>(type);
    if (evtIdx >= EVENT_BUS_MAX_EVENTS) return 0;

    uint8_t count = subscriberCounts[evtIdx];
    if (count >= EVENT_BUS_MAX_SUBS_PER_EVENT) return 0;
    if (tokenCount >= EVENT_BUS_MAX_TOKENS) return 0;

    // Add subscriber
    subscribers[evtIdx][count].fn = fn;
    subscribers[evtIdx][count].ctx = ctx;
    subscriberCounts[evtIdx]++;

    // Register token
    SubscriptionToken token = nextToken++;
    if (nextToken <= 0) nextToken = 1;  // wrap guard

    uint8_t slot = tokenCount++;
    tokenSlots[slot] = token;
    tokenEntries[slot].type = type;
    tokenEntries[slot].index = count;

    return token;
}

void EventBus::unsubscribe(SubscriptionToken token) {
    // Linear scan — 16 tokens max, negligible cost
    for (uint8_t i = 0; i < tokenCount; i++) {
        if (tokenSlots[i] == token) {
            uint8_t evtIdx = static_cast<uint8_t>(tokenEntries[i].type);
            uint8_t idx = tokenEntries[i].index;

            if (idx < subscriberCounts[evtIdx]) {
                // Null out the slot
                subscribers[evtIdx][idx].fn = nullptr;
                subscribers[evtIdx][idx].ctx = nullptr;
            }

            // Compact token array (swap last into this slot)
            tokenCount--;
            if (i < tokenCount) {
                tokenSlots[i] = tokenSlots[tokenCount];
                tokenEntries[i] = tokenEntries[tokenCount];
            }
            return;
        }
    }
}

void EventBus::publish(const EventPayload& event) {
    static int publishDepth = 0;
    if (publishDepth > 5) return;  // recursion guard

    uint8_t evtIdx = static_cast<uint8_t>(event.type);
    if (evtIdx >= EVENT_BUS_MAX_EVENTS) return;

    publishDepth++;
    uint8_t count = subscriberCounts[evtIdx];
    for (uint8_t i = 0; i < count; i++) {
        if (subscribers[evtIdx][i].fn) {
            subscribers[evtIdx][i].fn(event, subscribers[evtIdx][i].ctx);
        }
    }
    publishDepth--;
}

void EventBus::publish(DomainEvent type) {
    EventPayload event;
    event.type = type;
    event.timestamp = millis();
    publish(event);
}

void EventBus::clear() {
    memset(subscribers, 0, sizeof(subscribers));
    memset(subscriberCounts, 0, sizeof(subscriberCounts));
    memset(tokenEntries, 0, sizeof(tokenEntries));
    memset(tokenSlots, 0, sizeof(tokenSlots));
    tokenCount = 0;
    nextToken = 1;
}
