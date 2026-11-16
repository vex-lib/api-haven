#include "vex.h"
#include "app/app_broker.h"


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
 *   any driver;                   // opaque driver ctx, never touched
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
 *   any driverHandle;             // opaque driver job; nullptr when idle
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

;;GETTER
static inline AppDriverTable AppBroker_table(const AppBroker *self) { return (*self).table; }
;;SETTER
static inline fn AppBroker_setLastStatus(AppBroker *self, AppStatus lastStatus) { (*self).lastStatus = lastStatus; }
;;SETTER
static inline fn AppBroker_setJobCount(AppBroker *self, u32 jobCount) { (*self).jobCount = jobCount; }
;;GETTER
static inline AppJob *AppBroker_jobs(AppBroker *self) { return (*self).jobs; }
;;GETTER
static inline u32 AppJob_jobId(const AppJob *self) { return (*self).jobId; }
;;SETTER
static inline fn AppJob_setJobId(AppJob *self, u32 jobId) { (*self).jobId = jobId; }
;;SETTER
static inline fn AppJob_setTarget(AppJob *self, const AppProviderSlot *target) { (*self).target = target; }
;;GETTER
static inline AppStatus AppJob_status(const AppJob *self) { return (*self).status; }
;;SETTER
static inline fn AppJob_setStatus(AppJob *self, AppStatus status) { (*self).status = status; }
;;SETTER
static inline fn AppJob_setTimeoutMs(AppJob *self, u64 timeoutMs) { (*self).timeoutMs = timeoutMs; }
;;SETTER
static inline fn AppJob_setDriverHandle(AppJob *self, any driverHandle) { (*self).driverHandle = driverHandle; }
;;SETTER
static inline fn AppJob_setOutLen(AppJob *self, bytes outLen) { (*self).outLen = outLen; }

/** Runs one action through the injected driver and records its bounded result. */
bool AppBroker_action(AppBroker *self, const char *action,
                      const char *paramsJson, bytes paramsLen,
                      char *outBuf, bytes outCap, u32 *outJobId) {
    if (!self)
        return false;
    AppDriverTable table = AppBroker_table(self);
    any driver = AppBroker_getDriver(self);
    const AppProviderSlot *target = AppBroker_getTarget(self);
    if (!target || !driver || !table.runActionFn || !action || (*action) == '\0') {
        AppBroker_setLastStatus(self, APP_STATUS_IDLE);
        return false;
    }
    i32 freeIndex = -1;
    u32 jobCount = AppBroker_getJobCount(self);
    AppJob *jobs = AppBroker_jobs(self);
    for (u32 i = 0; i < jobCount; i++) {
        AppJob *job = &jobs[i];
        if (AppJob_status(job) != APP_STATUS_RUNNING) {
            freeIndex = (i32) i;
            break;
        }
    }
    if (freeIndex < 0) {
        if (jobCount >= (u32) APP_MAX_JOBS) {
            AppBroker_setLastStatus(self, APP_STATUS_BUSY_FULL);
            return false;
        }
        freeIndex = (i32) jobCount;
        AppBroker_setJobCount(self, jobCount + 1);
    }
    u64 bound = AppBroker_getTimeout(self);
    AppJob *slot = &jobs[(u32) freeIndex];
    u32 jobId = (u32) freeIndex + 1;
    AppJob_setJobId(slot, jobId);
    AppJob_setTarget(slot, target);
    AppJob_setStatus(slot, APP_STATUS_RUNNING);
    AppJob_setTimeoutMs(slot, bound);
    AppJob_setDriverHandle(slot, nullptr);
    AppJob_setOutLen(slot, 0);
    bytes produced = 0;
    bool ok = table.runActionFn(driver, target, action, paramsJson, paramsLen,
                                bound, outBuf, outCap, &produced);
    if (!ok) {
        AppJob_setStatus(slot, APP_STATUS_TIMEOUT);
        AppJob_setOutLen(slot, 0);
        AppBroker_setLastStatus(self, APP_STATUS_TIMEOUT);
        if (outJobId)
            *outJobId = jobId;
        return false;
    }
    AppJob_setStatus(slot, APP_STATUS_DONE);
    AppJob_setOutLen(slot, produced);
    AppBroker_setLastStatus(self, APP_STATUS_DONE);
    if (outJobId)
        (*outJobId) = jobId;
    return true;
}

/** Returns the stored status for a job without waiting for driver work. */
AppStatus AppBroker_poll(AppBroker *self, u32 jobId) {
    if (!self || jobId == 0)
        return APP_STATUS_IDLE;
    u32 jobCount = AppBroker_getJobCount(self);
    for (u32 i = 0; i < jobCount; i++) {
        const AppJob *job = AppBroker_getJobAt(self, i);
        if (job && AppJob_jobId(job) == jobId)
            return AppJob_status(job);
    }
    return APP_STATUS_IDLE;
}

/** Cancels a running job slot and returns it to the idle state. */
bool AppBroker_cancel(AppBroker *self, u32 jobId) {
    if (!self || jobId == 0)
        return false;
    u32 jobCount = AppBroker_getJobCount(self);
    AppJob *jobs = AppBroker_jobs(self);
    for (u32 i = 0; i < jobCount; i++) {
        AppJob *job = &jobs[i];
        if (AppJob_jobId(job) != jobId)
            continue;
        if (AppJob_status(job) != APP_STATUS_RUNNING)
            return false;
        AppJob_setStatus(job, APP_STATUS_IDLE);
        AppJob_setDriverHandle(job, nullptr);
        return true;
    }
    return false;
}

// SETTERS

/** Sets the borrowed automation target row used for subsequent actions. */
;;SETTER
fn AppBroker_setTarget(AppBroker *self, const AppProviderSlot *target) {
    if (!self)
        return;
    (*self).target = target;
}

/** Binds the opaque driver context and its action callback table. */
;;SETTER
fn AppBroker_setDriver(AppBroker *self, any driver, AppDriverTable table) {
    if (!self)
        return;
    (*self).driver = driver;
    (*self).table = table;
}

/** Sets the default per-action timeout in milliseconds. */
;;SETTER
fn AppBroker_setTimeout(AppBroker *self, u64 timeoutMs) {
    if (!self)
        return;
    (*self).timeoutMs = timeoutMs;
}

// GETTERS

/** Returns the borrowed target row, or nullptr when unset. */
;;GETTER
const AppProviderSlot *AppBroker_getTarget(const AppBroker *self) {
    return self ? (*self).target : nullptr;
}

/** Returns the opaque driver context, or nullptr for a null receiver. */
;;GETTER
any AppBroker_getDriver(const AppBroker *self) {
    return self ? (*self).driver : nullptr;
}

/** Returns the configured timeout, or zero for a null receiver. */
;;GETTER
u64 AppBroker_getTimeout(const AppBroker *self) {
    if (!self)
        return 0;
    return (*self).timeoutMs;
}

/** Reports whether any recorded job currently has running status. */
bool AppBroker_isRunning(const AppBroker *self) {
    if (!self)
        return false;
    u32 jobCount = AppBroker_getJobCount(self);
    for (u32 i = 0; i < jobCount; i++) {
        const AppJob *job = AppBroker_getJobAt(self, i);
        if (job && AppJob_status(job) == APP_STATUS_RUNNING)
            return true;
    }
    return false;
}

/** Returns the number of job slots issued so far. */
;;GETTER
u32 AppBroker_getJobCount(const AppBroker *self) {
    if (!self)
        return 0;
    return (*self).jobCount;
}

/** Returns the job record at index i, or nullptr when out of range. */
;;GETTER
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
    u32 jobCount = AppBroker_getJobCount(self);
    for (u32 i = 0; i < jobCount; i++) {
        const AppJob *job = AppBroker_getJobAt(self, i);
        if (job && AppJob_jobId(job) == jobId)
            return AppJob_status(job);
    }
    return APP_STATUS_IDLE;
}

/** Returns the last action result, or idle for a null receiver. */
;;GETTER
AppStatus AppBroker_getLastStatus(const AppBroker *self) {
    if (!self)
        return APP_STATUS_IDLE;
    return (*self).lastStatus;
}
