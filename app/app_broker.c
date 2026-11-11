#include "annotation/annotation.h"
#include "app/app_broker.h"

#include <stddef.h>

;;DEFINITION
/** The bounded app/automation action seam. It exists so a driver call cannot
 * outlive its slot: each action owns one entry in a fixed jobs[] array, a
 * per-slot timeout bounds the driver wait, and a per-slot cancel gives teardown
 * its bounded join. Preconditions (target, driver, action) are validated before
 * a slot is issued, so a rejected action consumes nothing. No osascript, no IPC,
 * no sockets, no threads, no allocation — execution runs behind the injected
 * AppDriverTable. */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: AppBroker (app/app_broker)
 * ============================================================================
 * Bounded app/automation action seam: each action owns one slot in a
 * fixed jobs[APP_MAX_JOBS] array (BitPool discipline) — a count,
 * BUSY_FULL when all 16 slots RUNNING, never an unbounded grow.
 * Per-slot timeoutMs bounds the driver call (Rule 27 drop-degrade);
 * per-slot cancel gives teardown its bounded join (Rule 26).
 * Preconditions (target, driver, action) are validated BEFORE a slot is
 * issued, so rejected actions never consume slots. No osascript, no IPC,
 * no sockets, no threads, no allocation — execution runs behind the
 * injected AppDriverTable (vexspoke/R3).
 *
 * STRUCT FIELDS (Mirroring app/app_broker.h — exactly this file's
 * class):
 * ----------------------------------------------------------------------------
 *   AppJob jobs[16];                // fixed slots, BitPool discipline
 *   u32 jobCount;              // slots ever issued, capped at 16
 *   u64 timeoutMs;             // default bound for new actions (ms)
 *   const AppProviderSlot *target;  // bound target row; nullptr = unbound
 *   void *driver;                   // opaque driver ctx, never touched
 *   AppDriverTable table;           // injected run fn
 *   AppStatus lastStatus;           // verdict of the last action call
 *
 * SLOT RECORD (AppJob — Rule 3 co-location, zero behavior of its own;
 * all behavior hangs off this class):
 * ----------------------------------------------------------------------------
 *   u32 jobId;                 // slot index + 1; stable per slot
 *   const AppProviderSlot *target;  // borrowed descriptor row
 *   AppStatus status;               // lifecycle state
 *   u64 timeoutMs;             // per-slot bound for the driver
 *   void *driverHandle;             // opaque driver job; nullptr when idle
 *   bytes outLen;                  // Bytes written into the caller dest
 *
 * PRIVATE HELPERS (none — slot scan is inline in AppBroker_action):
 * ----------------------------------------------------------------------------
 *   (none)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - AppBroker_0()            : target nullptr, driver nullptr, 100ms default
 *   - AppBroker_1(timeoutMs)   : target nullptr, driver nullptr, given default
 *
 * Core Functions:
 *   - AppBroker_action(self, action, paramsJson, paramsLen, dest, cap,
 *     outJobId) : sync bounded exec into a free/fresh slot (dest-last)
 *   - AppBroker_poll(self, jobId)   : stored slot verdict, never blocks
 *   - AppBroker_cancel(self, jobId) : bounded per-slot teardown to IDLE
 *
 * Setters:
 *   - AppBroker_setTarget(self, target)
 *   - AppBroker_setDriver(self, driver, table)
 *   - AppBroker_setTimeout(self, timeoutMs)
 *
 * Getters (Rule 24; null-safe):
 *   - AppBroker_getTarget / getDriver / getTimeout / isRunning /
 *     getJobCount / getJobAt / getJobStatus / getLastStatus
 * ============================================================================
 */
;;PLATFORM_EXCLUSIVE("macOS")
;;INTENTION("missing targets degrade to UNAVAILABLE via the unbound-driver verdict (IDLE + false) — AppDetect pattern, never a crash when the app or helper is absent")
;;INTENTION("fixed slots + count + BUSY_FULL instead of a growable queue — BitPool discipline; teardown stays bounded per Rule 26/27")
;;INTENTION("no osascript/IPC/sockets/threads/allocation inside api-haven — execution is one injected vexspoke/R3 fn; unbound driver degrades to false — Rule 17 api-haven clause")

#define APP_DEFAULT_TIMEOUT_MS 100

// CONSTRUCTORS

/** Creates a broker with no bound driver and the default timeout. */
AppBroker AppBroker_0() {
    AppBroker self = { 0 };
    self.timeoutMs = APP_DEFAULT_TIMEOUT_MS;
    self.lastStatus = APP_STATUS_IDLE;
    return self;
}

/** Creates a broker with no bound driver and the supplied default timeout. */
AppBroker AppBroker_1(u64 timeoutMs) {
    AppBroker self = { 0 };
    self.timeoutMs = timeoutMs;
    self.lastStatus = APP_STATUS_IDLE;
    return self;
}

// CORE FUNCTIONS

/** Runs one action through the injected driver and records its bounded result. */
/** Runs one action through the injected driver and records its bounded result. */
bool AppBroker_action(AppBroker *self, const char *action,
                      const char *paramsJson, bytes paramsLen,
                      char *outBuf, bytes outCap, u32 *outJobId) {
    if (!self)
        return false;
    AppDriverTable table = (*self).table;
    void *driver = (*self).driver;
    const AppProviderSlot *target = (*self).target;
    if (!target || !driver || !table.runActionFn || !action || (*action) == '\0') {
        (*self).lastStatus = APP_STATUS_IDLE;
        return false;
    }
    i32 freeIndex = -1;
    for (u32 i = 0; i < (*self).jobCount; i++) {
        AppJob *job = &(*self).jobs[i];
        if ((*job).status != APP_STATUS_RUNNING) {
            freeIndex = (i32) i;
            break;
        }
    }
    if (freeIndex < 0) {
        if ((*self).jobCount >= (u32) APP_MAX_JOBS) {
            (*self).lastStatus = APP_STATUS_BUSY_FULL;
            return false;
        }
        freeIndex = (i32) (*self).jobCount;
        (*self).jobCount++;
    }
    u64 bound = (*self).timeoutMs;
    AppJob *slot = &(*self).jobs[(u32) freeIndex];
    (*slot).jobId = (u32) freeIndex + 1;
    (*slot).target = target;
    (*slot).status = APP_STATUS_RUNNING;
    (*slot).timeoutMs = bound;
    (*slot).driverHandle = nullptr;
    (*slot).outLen = 0;
    bytes produced = 0;
    bool ok = table.runActionFn(driver, target, action, paramsJson, paramsLen,
                                bound, outBuf, outCap, &produced);
    if (!ok) {
        (*slot).status = APP_STATUS_TIMEOUT;
        (*slot).outLen = 0;
        (*self).lastStatus = APP_STATUS_TIMEOUT;
        if (outJobId)
            *outJobId = (*slot).jobId;
        return false;
    }
    (*slot).status = APP_STATUS_DONE;
    (*slot).outLen = produced;
    (*self).lastStatus = APP_STATUS_DONE;
    if (outJobId)
        (*outJobId) = (*slot).jobId;
    return true;
}

/** Returns the stored status for a job without waiting for driver work. */
AppStatus AppBroker_poll(AppBroker *self, u32 jobId) {
    if (!self || jobId == 0)
        return APP_STATUS_IDLE;
    for (u32 i = 0; i < (*self).jobCount; i++) {
        const AppJob *job = &(*self).jobs[i];
        if ((*job).jobId == jobId)
            return (*job).status;
    }
    return APP_STATUS_IDLE;
}

/** Cancels a running job slot and returns it to the idle state. */
bool AppBroker_cancel(AppBroker *self, u32 jobId) {
    if (!self || jobId == 0)
        return false;
    for (u32 i = 0; i < (*self).jobCount; i++) {
        AppJob *job = &(*self).jobs[i];
        if ((*job).jobId != jobId)
            continue;
        if ((*job).status != APP_STATUS_RUNNING)
            return false;
        (*job).status = APP_STATUS_IDLE;
        (*job).driverHandle = nullptr;
        return true;
    }
    return false;
}

// SETTERS

/** Sets the borrowed automation target row used for subsequent actions. */
void AppBroker_setTarget(AppBroker *self, const AppProviderSlot *target) {
    if (!self)
        return;
    (*self).target = target;
}

/** Binds the opaque driver context and its action callback table. */
void AppBroker_setDriver(AppBroker *self, void *driver, AppDriverTable table) {
    if (!self)
        return;
    (*self).driver = driver;
    (*self).table = table;
}

/** Sets the default per-action timeout in milliseconds. */
void AppBroker_setTimeout(AppBroker *self, u64 timeoutMs) {
    if (!self)
        return;
    (*self).timeoutMs = timeoutMs;
}

// GETTERS

/** Returns the borrowed target row, or nullptr when unset. */
const AppProviderSlot *AppBroker_getTarget(const AppBroker *self) {
    return self ? (*self).target : nullptr;
}

/** Returns the opaque driver context, or nullptr for a null receiver. */
void *AppBroker_getDriver(const AppBroker *self) {
    return self ? (*self).driver : nullptr;
}

/** Returns the configured timeout, or zero for a null receiver. */
u64 AppBroker_getTimeout(const AppBroker *self) {
    if (!self)
        return 0;
    return (*self).timeoutMs;
}

/** Reports whether any recorded job currently has running status. */
bool AppBroker_isRunning(const AppBroker *self) {
    if (!self)
        return false;
    for (u32 i = 0; i < (*self).jobCount; i++) {
        const AppJob *job = &(*self).jobs[i];
        if ((*job).status == APP_STATUS_RUNNING)
            return true;
    }
    return false;
}

/** Returns the number of job slots issued so far. */
u32 AppBroker_getJobCount(const AppBroker *self) {
    if (!self)
        return 0;
    return (*self).jobCount;
}

/** Returns the job record at index i, or nullptr when out of range. */
const AppJob *AppBroker_getJobAt(const AppBroker *self, u32 i) {
    if (!self)
        return nullptr;
    if (i >= (*self).jobCount)
        return nullptr;
    return &(*self).jobs[i];
}

/** Returns the status associated with jobId, or idle when it is unknown. */
AppStatus AppBroker_getJobStatus(const AppBroker *self, u32 jobId) {
    if (!self || jobId == 0)
        return APP_STATUS_IDLE;
    for (u32 i = 0; i < (*self).jobCount; i++) {
        const AppJob *job = &(*self).jobs[i];
        if ((*job).jobId == jobId)
            return (*job).status;
    }
    return APP_STATUS_IDLE;
}

/** Returns the last action result, or idle for a null receiver. */
AppStatus AppBroker_getLastStatus(const AppBroker *self) {
    if (!self)
        return APP_STATUS_IDLE;
    return (*self).lastStatus;
}
