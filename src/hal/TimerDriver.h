#ifndef TIMER_DRIVER_H
#define TIMER_DRIVER_H

// ESP-IDF native time/delay — replaces Arduino delay/millis/micros/yield.

void tmrDelay(unsigned long ms);
void tmrYield();
unsigned long tmrMillis();
unsigned long tmrMicros();

#endif // TIMER_DRIVER_H
