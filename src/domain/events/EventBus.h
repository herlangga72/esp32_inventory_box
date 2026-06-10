#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <Arduino.h>
#include "Events.h"

// Fixed-size subscriber: raw function pointer + context (no heap, no std::function)
struct EventSubscriber {
    void (*fn)(const EventPayload&, void*);
    void* ctx;
};

using SubscriptionToken = int;
static const int EVENT_BUS_MAX_EVENTS = 24;
static const int EVENT_BUS_MAX_SUBS_PER_EVENT = 4;
static const int EVENT_BUS_MAX_TOKENS = 16;

// Subscriber tracking entry (for unsubscribe)
struct SubEntry {
    DomainEvent type;
    uint8_t index;      // index within that event's subscriber array
};

class EventBus {
public:
    static EventBus* getInstance();

    SubscriptionToken subscribe(DomainEvent type,
                                void (*fn)(const EventPayload&, void*),
                                void* ctx = nullptr);
    void unsubscribe(SubscriptionToken token);
    void publish(const EventPayload& event);
    void publish(DomainEvent type);
    void clear();

private:
    static EventBus s_instance;          // static instance — no new()

    // Fixed-size subscriber arrays — no std::map, no std::vector
    EventSubscriber subscribers[EVENT_BUS_MAX_EVENTS][EVENT_BUS_MAX_SUBS_PER_EVENT];
    uint8_t subscriberCounts[EVENT_BUS_MAX_EVENTS];  // count per event type

    // Fixed-size token registry
    SubEntry tokenEntries[EVENT_BUS_MAX_TOKENS];
    SubscriptionToken tokenSlots[EVENT_BUS_MAX_TOKENS]; // token -> index
    uint8_t tokenCount;

    int nextToken;

    EventBus();
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
};

#endif // EVENT_BUS_H
