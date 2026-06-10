#ifndef MAILBOX_SYSTEM_H
#define MAILBOX_SYSTEM_H

#include "ServiceRegistry.h"

// Backward-compat shim: MailboxSystem delegates to ServiceRegistry.
// All enum types and ServiceMessage are defined in ServiceRegistry.h.

class MailboxSystem {
public:
    static MailboxSystem& getInstance() {
        static MailboxSystem instance;
        return instance;
    }

    bool registerMailbox(ServiceId id, UBaseType_t depth = 8) {
        return g_registry.registerMailbox(id, depth);
    }

    bool send(ServiceId target, const ServiceMessage& msg) {
        return g_registry.send(target, msg);
    }

    bool receive(ServiceId id, ServiceMessage& msg, TickType_t timeout) {
        return g_registry.receive(id, msg, timeout);
    }

    bool tryReceive(ServiceId id, ServiceMessage& msg) {
        return g_registry.tryReceive(id, msg);
    }

    QueueHandle_t getQueue(ServiceId id) const {
        return g_registry.getQueue(id);
    }

private:
    MailboxSystem() = default;
};

#endif
