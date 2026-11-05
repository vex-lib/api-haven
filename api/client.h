#ifndef API_CLIENT_H
#define API_CLIENT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct APIClientTelemetry {
    uint64_t frameCount;
    int32_t state;
    int32_t activePipelines;
    uint64_t totalAllocations;
} APIClientTelemetry;

bool APIClient_sendTelemetry(const char *endpointUrl, const APIClientTelemetry *telemetry);

#endif
