#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <Arduino.h>
#include <functional>
#include <map>
#include <vector>
#include "Events.h"

using EventHandler = std::function<void(const EventPayload&)>;

class EventBus {
public:
    static EventBus* getInstance();
    
    void subscribe(DomainEvent type, EventHandler handler);
    void unsubscribe(DomainEvent type, EventHandler handler);
    void publish(const EventPayload& event);
    void publish(DomainEvent type);
    
    void clear();

private:
    static EventBus* instance;
    
    std::map<DomainEvent, std::vector<EventHandler>> subscribers;
    
    EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
};

#endif // EVENT_BUS_H