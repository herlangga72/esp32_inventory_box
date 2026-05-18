#include "EventBus.h"

EventBus* EventBus::instance = nullptr;

EventBus* EventBus::getInstance() {
    if (!instance) {
        instance = new EventBus();
    }
    return instance;
}

void EventBus::subscribe(DomainEvent type, EventHandler handler) {
    subscribers[type].push_back(handler);
}

void EventBus::unsubscribe(DomainEvent type, EventHandler handler) {
    auto it = subscribers.find(type);
    if (it != subscribers.end()) {
        auto& handlers = it->second;
        handlers.erase(
            std::remove_if(handlers.begin(), handlers.end(),
                [&handler](const EventHandler& h) {
                    return h.target_type() == handler.target_type();
                }),
            handlers.end()
        );
    }
}

void EventBus::publish(const EventPayload& event) {
    auto it = subscribers.find(event.type);
    if (it != subscribers.end()) {
        for (auto& handler : it->second) {
            handler(event);
        }
    }
}

void EventBus::publish(DomainEvent type) {
    EventPayload event;
    event.type = type;
    event.timestamp = millis();
    publish(event);
}

void EventBus::clear() {
    subscribers.clear();
}