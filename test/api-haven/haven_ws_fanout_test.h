#ifndef HAVEN_WS_FANOUT_TEST_H
#define HAVEN_WS_FANOUT_TEST_H

#include "vex.h"

/**
 * Mock transport state used by the WebSocket fanout unit tests.
 *
 * u32 pollCount       - total number of poll invocations recorded
 * u64 lastBudget      - budget argument passed to the most recent poll call
 * u32 framesToDeliver - remaining synthetic frames to emit on subsequent polls
 */
typedef struct MockTransport {
    u32 pollCount;
    u64 lastBudget;
    u32 framesToDeliver;
} MockTransport;

#endif
