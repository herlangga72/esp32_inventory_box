#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <Arduino.h>
#include <functional>
#include <map>
#include <vector>
#include "Events.h"

using EventHandler = std::function<void(const EventPayload&)>;
using SubscriptionToken = int;

class EventBus {
public:
    static EventBus* getInstance();

    SubscriptionToken subscribe(DomainEvent type, EventHandler handler);
    void unsubscribe(SubscriptionToken token);
    void publish(const EventPayload& event);
    void publish(DomainEvent type);

    void clear();

private:
    struct SubEntry {
        DomainEvent type;
        int index;
    };

    static EventBus* instance;

    std::map<DomainEvent, std::vector<EventHandler>> subscribers;
    std::map<SubscriptionToken, SubEntry> tokenMap;
    int nextToken = 1;

    EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
};

#endif // EVENT_BUS_H
