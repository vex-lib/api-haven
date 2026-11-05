#ifndef APP_BROKER_H
#define APP_BROKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app/app_provider.h"
#include "c23/constructor.h"

// app/app_broker.h — bounded app/automation action seam (L2 behavior).
//
// Each action gets its own slot in a fixed array (BitPool discipline):
// APP_MAX_JOBS slots, a count, BUSY_FULL when full — never an unbounded
// grow. Per-slot timeoutMs bounds every driver wait (Rule 27: the driver
// degrades to TIMEOUT, the seam never blocks); teardown stays bounded
// via cancel (Rule 26).
//
// Actions execute synchronously inside the bounded driver call, so poll
// reports the stored verdict and cancel only bites while RUNNING. No
// osascript, no IPC, no sockets here: execution runs behind the
// injected AppDriverTable, implemented in vexspoke/R3. No threads, no
// allocation, no vendor SDKs inside api-haven (Rule 17).

#define APP_MAX_JOBS 16

// Action lifecycle. BUSY_FULL is a table verdict (lastStatus only — a
// job slot itself is never BUSY_FULL).
typedef enum AppStatus {
    APP_STATUS_IDLE,      // free slot / rejected action / cancelled
    APP_STATUS_RUNNING,   // accepted, driver owns the call
    APP_STATUS_DONE,      // driver completed, outLen valid
    APP_STATUS_TIMEOUT,   // driver bounded-wait fired, frame dropped
    APP_STATUS_BUSY_FULL, // action rejected: all 16 slots RUNNING
} AppStatus;

// SLOT RECORD — one bounded action slot (Rule 3 co-location). Borrowed
// target row; output lands in caller-owned dest (dest-last, Rule 9).
typedef struct AppJob {
    uint32_t jobId;                  // slot index + 1; stable per slot
    const AppProviderSlot *target;   // borrowed descriptor row
    AppStatus status;                // lifecycle state
    uint64_t timeoutMs;              // per-slot bound handed to driver
    void *driverHandle;              // opaque driver job; NULL when idle
    size_t outLen;                   // Bytes written into the caller dest
} AppJob;

// Driver seam — implemented outside api-haven (vexspoke/R3). The wait
// inside runActionFn takes timeoutMs and degrades (drop, TIMEOUT) rather
// than blocking unbounded (Rule 27). outBuf/outCap/outLen are dest-last
// per Rule 9; credentials arrive via driver ctx, never the arena.
typedef bool (*AppRunActionFn)(void *driverCtx, const AppProviderSlot *target,
                               const char *action, const char *paramsJson,
                               size_t paramsLen, uint64_t timeoutMs,
                               char *outBuf, size_t outCap, size_t *outLen);

typedef struct AppDriverTable {
    AppRunActionFn runActionFn; // NULL = driver unbound, actions degrade
} AppDriverTable;

typedef struct AppBroker {
    AppJob jobs[APP_MAX_JOBS];       // fixed slots, BitPool discipline
    uint32_t jobCount;               // slots ever issued, capped at 16
    uint64_t timeoutMs;              // default bound for new actions
    const AppProviderSlot *target;   // bound target row; NULL = unbound
    void *driver;                    // opaque driver ctx, never touched
    AppDriverTable table;            // injected run fn
    AppStatus lastStatus;            // verdict of the last action call
} AppBroker;

// --- Constructors (value structs, zero heap — Harness precedent) ---
AppBroker AppBroker_0(void);                   // target NULL, 100ms default
AppBroker AppBroker_1(uint64_t timeoutMs);     // target NULL, given default
#define AppBroker(...) CONSTRUCTOR_DISPATCH(AppBroker, __VA_ARGS__)

// --- Core functions ---
// Executes one action synchronously inside the bounded driver call:
// DONE + true on success, TIMEOUT + false on driver failure, BUSY_FULL
// + false when all 16 slots RUNNING. Unbound target/driver, NULL/empty
// action, or NULL params degrade to false (lastStatus IDLE) without
// consuming a slot. outJobId is dest-last per Rule 9 (NULL-tolerant).
bool AppBroker_action(AppBroker *self, const char *action,
                      const char *paramsJson, size_t paramsLen,
                      char *outBuf, size_t outCap, uint32_t *outJobId);
// Reports the stored slot verdict (the driver call already completed
// inside action). IDLE on NULL self or unknown jobId.
AppStatus AppBroker_poll(AppBroker *self, uint32_t jobId);
// Bounded teardown hook per slot (Rule 26): RUNNING to IDLE. False on
// NULL self, unknown jobId, or a slot not RUNNING.
bool AppBroker_cancel(AppBroker *self, uint32_t jobId);

// --- Setters ---
void AppBroker_setTarget(AppBroker *self, const AppProviderSlot *target);
void AppBroker_setDriver(AppBroker *self, void *driver, AppDriverTable table);
void AppBroker_setTimeout(AppBroker *self, uint64_t timeoutMs);

// --- Getters (Rule 24, null-safe) ---
const AppProviderSlot *AppBroker_getTarget(const AppBroker *self);
void *AppBroker_getDriver(const AppBroker *self);
uint64_t AppBroker_getTimeout(const AppBroker *self);
bool AppBroker_isRunning(const AppBroker *self);
uint32_t AppBroker_getJobCount(const AppBroker *self);
const AppJob *AppBroker_getJobAt(const AppBroker *self, uint32_t i);
AppStatus AppBroker_getJobStatus(const AppBroker *self, uint32_t jobId);
AppStatus AppBroker_getLastStatus(const AppBroker *self);

#endif
