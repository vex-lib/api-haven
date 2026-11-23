#ifndef APP_BROKER_H
#define APP_BROKER_H

#include "vex.h"
#include "app/app_provider.h"

/**
 * app/app_broker.h, bounded app/automation action seam (L2 behavior).
 *
 * Each action gets its own slot in a fixed array (BitPool discipline):
 * APP_MAX_JOBS slots, a count, BUSY_FULL when full, never an unbounded
 * grow. Per-slot timeoutMs bounds every driver wait (Rule 27: the driver
 * degrades to TIMEOUT, the seam never blocks); teardown stays bounded
 * via cancel (Rule 26).
 *
 * Actions execute synchronously inside the bounded driver call, so poll
 * reports the stored verdict and cancel only bites while RUNNING. No
 * osascript, no IPC, no sockets here: execution runs behind the
 * injected AppDriverTable, implemented in vexspoke/R3. No threads, no
 * allocation, no vendor SDKs inside api-haven (Rule 17).
 */

#define APP_MAX_JOBS 16

/**
 * Action lifecycle. BUSY_FULL is a table verdict (lastStatus only, a
 * job slot itself is never BUSY_FULL).
 *
 * APP_STATUS_IDLE      - free slot / rejected action / cancelled
 * APP_STATUS_RUNNING   - accepted, driver owns the call
 * APP_STATUS_DONE      - driver completed, outLen valid
 * APP_STATUS_TIMEOUT   - driver bounded-wait fired, frame dropped
 * APP_STATUS_BUSY_FULL - action rejected: all 16 slots RUNNING
 */
typedef enum AppStatus {
    APP_STATUS_IDLE,
    APP_STATUS_RUNNING,
    APP_STATUS_DONE,
    APP_STATUS_TIMEOUT,
    APP_STATUS_BUSY_FULL,
} AppStatus;

/**
 * SLOT RECORD, one bounded action slot (Rule 3 co-location). Borrowed
 * target row; output lands in caller-owned dest (dest-last, Rule 9).
 *
 * u32 jobId                     - slot index + 1; stable per slot
 * const AppProviderSlot *target - borrowed descriptor row
 * AppStatus status              - lifecycle state
 * u64 timeoutMs                 - per-slot bound handed to driver
 * any driverHandle              - opaque driver job; nullptr when idle
 * bytes outLen                  - Bytes written into the caller dest
 */
typedef struct AppJob {
    u32 jobId;
    const AppProviderSlot *target;
    AppStatus status;
    u64 timeoutMs;
    any driverHandle;
    bytes outLen;
} AppJob;

/**
 * Driver seam, implemented outside api-haven (vexspoke/R3). The wait
 * inside runActionFn takes timeoutMs and degrades (drop, TIMEOUT) rather
 * than blocking unbounded (Rule 27). outBuf/outCap/outLen are dest-last
 * per Rule 9; credentials arrive via driver ctx, never the arena.
 */
typedef bool (*AppRunActionFn)(any driverCtx, const AppProviderSlot *target,
                               string action, string paramsJson,
                               bytes paramsLen, u64 timeoutMs,
                               utf8 *outBuf, bytes outCap, bytes *outLen);

typedef struct AppDriverTable {
    AppRunActionFn runActionFn;
} AppDriverTable;

/**
 * AppJob jobs[APP_MAX_JOBS]     - fixed slots, BitPool discipline
 * u32 jobCount                  - slots ever issued, capped at 16
 * u64 timeoutMs                 - default bound for new actions
 * const AppProviderSlot *target - bound target row; nullptr = unbound
 * any driver                    - opaque driver ctx, never touched
 * AppDriverTable table          - injected run fn
 * AppStatus lastStatus          - verdict of the last action call
 */
typedef struct AppBroker {
    AppJob jobs[APP_MAX_JOBS];
    u32 jobCount;
    u64 timeoutMs;
    const AppProviderSlot *target;
    any driver;
    AppDriverTable table;
    AppStatus lastStatus;
} AppBroker;

/** --- Constructors (value structs, zero heap, Harness precedent) --- */
AppBroker AppBroker_0(); /** target nullptr, 100ms default */
AppBroker AppBroker_1(u64 timeoutMs); /** target nullptr, given default */
#define AppBroker(...) CONSTRUCTOR_DISPATCH(AppBroker, __VA_ARGS__)

/**
 * --- Core functions ---
 * Executes one action synchronously inside the bounded driver call:
 * DONE + true on success, TIMEOUT + false on driver failure, BUSY_FULL
 * + false when all 16 slots RUNNING. Unbound target/driver, nullptr/empty
 * action, or nullptr params degrade to false (lastStatus IDLE) without
 * consuming a slot. outJobId is dest-last per Rule 9 (nullptr-tolerant).
 */
bool AppBroker_action(AppBroker *self, string action,
                      string paramsJson, bytes paramsLen,
                      utf8 *outBuf, bytes outCap, u32 *outJobId);
/**
 * Reports the stored slot verdict (the driver call already completed
 * inside action). IDLE on nullptr self or unknown jobId.
 */
AppStatus AppBroker_poll(AppBroker *self, u32 jobId);
/**
 * Bounded teardown hook per slot (Rule 26): RUNNING to IDLE. False on
 * nullptr self, unknown jobId, or a slot not RUNNING.
 */
bool AppBroker_cancel(AppBroker *self, u32 jobId);

/** --- Setters --- */
fn AppBroker_setTarget(AppBroker *self, const AppProviderSlot *target);
fn AppBroker_setDriver(AppBroker *self, any driver, AppDriverTable table);
fn AppBroker_setTimeout(AppBroker *self, u64 timeoutMs);

/** --- Getters (Rule 24, null-safe) --- */
const AppProviderSlot *AppBroker_getTarget(const AppBroker *self);
any AppBroker_getDriver(const AppBroker *self);
u64 AppBroker_getTimeout(const AppBroker *self);
bool AppBroker_isRunning(const AppBroker *self);
u32 AppBroker_getJobCount(const AppBroker *self);
const AppJob *AppBroker_getJobAt(const AppBroker *self, u32 i);
AppStatus AppBroker_getJobStatus(const AppBroker *self, u32 jobId);
AppStatus AppBroker_getLastStatus(const AppBroker *self);

#endif
