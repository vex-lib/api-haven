#ifndef API_CLIENT_H
#define API_CLIENT_H

#include "vex.h"

typedef struct APIClientTelemetry {
    u64 frameCount;
    i32 state;
    i32 activePipelines;
    u64 totalAllocations;
} APIClientTelemetry;

bool APIClient_sendTelemetry(string endpointUrl, const APIClientTelemetry *telemetry);

#endif
