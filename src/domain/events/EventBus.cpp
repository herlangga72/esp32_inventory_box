#include "EventBus.h"

EventBus* EventBus::instance = nullptr;

EventBus* EventBus::getInstance() {
    if (!instance) {
        instance = new EventBus();
    }
    return instance;
}

SubscriptionToken EventBus::subscribe(DomainEvent type, EventHandler handler) {
    auto& handlers = subscribers[type];
    handlers.push_back(handler);
    int idx = handlers.size() - 1;
    SubscriptionToken token = nextToken++;
    tokenMap[token] = {type, idx};
    return token;
}

void EventBus::unsubscribe(SubscriptionToken token) {
    auto it = tokenMap.find(token);
    if (it == tokenMap.end()) return;

    auto& entry = it->second;
    auto& handlers = subscribers[entry.type];

    if (entry.index < (int)handlers.size()) {
        handlers[entry.index] = nullptr;
    }
    tokenMap.erase(it);
}

void EventBus::publish(const EventPayload& event) {
    static int publishDepth = 0;
    if (publishDepth > 5) {
        // Prevents stack overflow from recursive event chains
        return;
    }
    publishDepth++;

    auto it = subscribers.find(event.type);
    if (it != subscribers.end()) {
        for (auto& handler : it->second) {
            if (handler) handler(event);
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
    subscribers.clear();
    tokenMap.clear();
    nextToken = 1;
}
