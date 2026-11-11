#include "annotation/annotation.h"
#include "harness/harness.h"

#include <stddef.h>

;;DEFINITION
/** The bounded CLI-engine job seam. It exists so a driver wait cannot outlive its
 * slot: each job owns one entry in a fixed jobs[] array, a per-slot timeout
 * bounds every wait, and a per-slot cancel gives teardown its bounded join.
 * Preconditions (engine, driver, prompt) are validated before a slot is issued,
 * so a rejected run consumes nothing. No exec, no threads, no allocation —
 * spawn/poll/cancel run behind the injected HarnessDriverTable. */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Harness (harness/harness)
 * ============================================================================
 * Bounded CLI-engine job seam: each job owns one slot in a fixed
 * jobs[HARNESS_MAX_JOBS] array (BitPool discipline) — a count, BUSY_FULL
 * when all 16 slots RUNNING, never an unbounded grow. Per-slot timeoutMs
 * bounds every driver wait (Rule 27 drop-degrade); per-slot cancel gives
 * teardown its bounded join (Rule 26). Preconditions (engine, driver,
 * prompt) are validated BEFORE a slot is issued, so rejected runs never
 * consume slots. No exec, no threads, no allocation — spawn/poll/cancel
 * run behind the injected HarnessDriverTable (vexspoke/R3).
 *
 * STRUCT FIELDS (Mirroring harness/harness.h — exactly this file's
 * class):
 * ----------------------------------------------------------------------------
 *   HarnessJob jobs[16];               // fixed slots, BitPool discipline
 *   u32 jobCount;                 // slots ever issued, capped at 16
 *   u64 timeoutMs;                // default bound for new jobs (ms)
 *   const EngineProviderSlot *engine;  // bound engine row; NULL = unbound
 *   void *driver;                      // opaque driver ctx, never touched
 *   HarnessDriverTable table;          // injected spawn/poll/cancel fns
 *   HarnessStatus lastStatus;          // verdict of the last run call
 *
 * SLOT RECORD (HarnessJob — Rule 3 co-location, zero behavior of its
 * own; all behavior hangs off this class):
 * ----------------------------------------------------------------------------
 *   u32 jobId;                    // slot index + 1; stable per slot
 *   const EngineProviderSlot *engine;  // borrowed descriptor row
 *   HarnessStatus status;              // lifecycle state
 *   u64 timeoutMs;                // per-slot bound for the driver
 *   void *driverHandle;                // opaque driver job; NULL when idle
 *   bytes outLen;                     // driver-reported output length
 *
 * PRIVATE HELPERS (none — slot scan is inline in Harness_run):
 * ----------------------------------------------------------------------------
 *   (none)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Harness_0()            : engine NULL, driver NULL, 100ms default
 *   - Harness_1(timeoutMs)   : engine NULL, driver NULL, given default
 *
 * Core Functions:
 *   - Harness_run(self, prompt, promptLen, timeoutMs, outJobId) : issue
 *     a job into a free/fresh slot; BUSY_FULL + false when full (dest-last)
 *   - Harness_poll(self, jobId)   : bounded status via pollFn (or stored)
 *   - Harness_cancel(self, jobId) : bounded per-slot teardown to IDLE
 *
 * Setters:
 *   - Harness_setEngine(self, engine)
 *   - Harness_setDriver(self, driver, table)
 *   - Harness_setTimeout(self, timeoutMs)
 *
 * Getters (Rule 24; null-safe):
 *   - Harness_getEngine / getDriver / getTimeout / isRunning /
 *     getJobCount / getJobAt / getJobStatus / getLastStatus
 * ============================================================================
 */
;;INTENTION("fixed slots + count + BUSY_FULL instead of a growable queue — BitPool discipline; teardown stays bounded per Rule 26/27")
;;INTENTION("no exec/threads/allocation inside api-haven — spawn/poll/cancel are injected vexspoke/R3 fns; unbound driver degrades to false, never crashes — Rule 17 api-haven clause")

#define HARNESS_DEFAULT_TIMEOUT_MS 100

// CONSTRUCTORS

/** Creates a harness with no bound driver and the default timeout. */
Harness Harness_0(void) {
    Harness self = { 0 };
    self.timeoutMs = HARNESS_DEFAULT_TIMEOUT_MS;
    self.lastStatus = HARNESS_STATUS_IDLE;
    return self;
}

/** Creates a harness with no bound driver and the supplied default timeout. */
Harness Harness_1(u64 timeoutMs) {
    Harness self = { 0 };
    self.timeoutMs = timeoutMs;
    self.lastStatus = HARNESS_STATUS_IDLE;
    return self;
}

// CORE FUNCTIONS

/** Starts a bounded driver job in an available slot and returns its job id. */
bool Harness_run(Harness *self, const char *prompt, bytes promptLen,
                 u64 timeoutMs, u32 *outJobId) {
    if (!self)
        return false;
    HarnessDriverTable table = (*self).table;
    void *driver = (*self).driver;
    const EngineProviderSlot *engine = (*self).engine;
    if (!engine || !driver || !table.spawnFn || !prompt || promptLen == 0) {
        (*self).lastStatus = HARNESS_STATUS_IDLE;
        return false;
    }
    i32 freeIndex = -1;
    for (u32 i = 0; i < (*self).jobCount; i++) {
        HarnessJob *job = &(*self).jobs[i];
        if ((*job).status != HARNESS_STATUS_RUNNING) {
            freeIndex = (i32) i;
            break;
        }
    }
    if (freeIndex < 0) {
        if ((*self).jobCount >= (u32) HARNESS_MAX_JOBS) {
            (*self).lastStatus = HARNESS_STATUS_BUSY_FULL;
            return false;
        }
        freeIndex = (i32) (*self).jobCount;
        (*self).jobCount++;
    }
    u64 bound = timeoutMs != 0 ? timeoutMs : (*self).timeoutMs;
    void *handle = nullptr;
    bool ok = table.spawnFn(driver, engine, prompt, promptLen, bound, &handle);
    HarnessJob *slot = &(*self).jobs[(u32) freeIndex];
    if (!ok) {
        (*slot).status = HARNESS_STATUS_IDLE;
        (*slot).driverHandle = nullptr;
        (*self).lastStatus = HARNESS_STATUS_IDLE;
        return false;
    }
    (*slot).jobId = (u32) freeIndex + 1;
    (*slot).engine = engine;
    (*slot).status = HARNESS_STATUS_RUNNING;
    (*slot).timeoutMs = bound;
    (*slot).driverHandle = handle;
    (*slot).outLen = 0;
    (*self).lastStatus = HARNESS_STATUS_RUNNING;
    if (outJobId)
        (*outJobId) = (*slot).jobId;
    return true;
}

/** Polls the selected driver's job and updates the stored slot status. */
HarnessStatus Harness_poll(Harness *self, u32 jobId) {
    if (!self || jobId == 0)
        return HARNESS_STATUS_IDLE;
    for (u32 i = 0; i < (*self).jobCount; i++) {
        HarnessJob *job = &(*self).jobs[i];
        if ((*job).jobId != jobId)
            continue;
        HarnessDriverTable table = (*self).table;
        if (!table.pollFn)
            return (*job).status;
        HarnessStatus now = table.pollFn((*self).driver, (*job).driverHandle,
                                        (*job).timeoutMs);
        if (now != HARNESS_STATUS_RUNNING && now != HARNESS_STATUS_BUSY_FULL) {
            (*job).status = now;
            if (now == HARNESS_STATUS_IDLE)
                (*job).driverHandle = nullptr;
        }
        return (*job).status;
    }
    return HARNESS_STATUS_IDLE;
}

/** Cancels a running job through the driver callback and clears its handle. */
bool Harness_cancel(Harness *self, u32 jobId) {
    if (!self || jobId == 0)
        return false;
    for (u32 i = 0; i < (*self).jobCount; i++) {
        HarnessJob *job = &(*self).jobs[i];
        if ((*job).jobId != jobId)
            continue;
        if ((*job).status != HARNESS_STATUS_RUNNING)
            return false;
        HarnessDriverTable table = (*self).table;
        if (table.cancelFn)
            table.cancelFn((*self).driver, (*job).driverHandle);
        (*job).status = HARNESS_STATUS_IDLE;
        (*job).driverHandle = nullptr;
        return true;
    }
    return false;
}

// SETTERS

/** Sets the borrowed engine descriptor used by subsequent runs. */
void Harness_setEngine(Harness *self, const EngineProviderSlot *engine) {
    if (!self)
        return;
    (*self).engine = engine;
}

/** Binds the opaque driver context and spawn/poll/cancel callback table. */
void Harness_setDriver(Harness *self, void *driver, HarnessDriverTable table) {
    if (!self)
        return;
    (*self).driver = driver;
    (*self).table = table;
}

/** Sets the default timeout used when a run supplies no override. */
void Harness_setTimeout(Harness *self, u64 timeoutMs) {
    if (!self)
        return;
    (*self).timeoutMs = timeoutMs;
}

// GETTERS

/** Returns the borrowed engine descriptor, or nullptr when unset. */
const EngineProviderSlot *Harness_getEngine(const Harness *self) {
    return self ? (*self).engine : nullptr;
}

/** Returns the opaque driver context, or nullptr for a null receiver. */
void *Harness_getDriver(const Harness *self) {
    return self ? (*self).driver : nullptr;
}

/** Returns the default timeout, or zero for a null receiver. */
u64 Harness_getTimeout(const Harness *self) {
    if (!self)
        return 0;
    return (*self).timeoutMs;
}

/** Reports whether any recorded job currently has running status. */
bool Harness_isRunning(const Harness *self) {
    if (!self)
        return false;
    for (u32 i = 0; i < (*self).jobCount; i++) {
        const HarnessJob *job = &(*self).jobs[i];
        if ((*job).status == HARNESS_STATUS_RUNNING)
            return true;
    }
    return false;
}

/** Returns the number of job slots issued so far. */
u32 Harness_getJobCount(const Harness *self) {
    if (!self)
        return 0;
    return (*self).jobCount;
}

/** Returns the job record at index i, or nullptr when out of range. */
const HarnessJob *Harness_getJobAt(const Harness *self, u32 i) {
    if (!self)
        return nullptr;
    if (i >= (*self).jobCount)
        return nullptr;
    return &(*self).jobs[i];
}

/** Returns the stored status for jobId, or idle when the id is unknown. */
HarnessStatus Harness_getJobStatus(const Harness *self, u32 jobId) {
    if (!self || jobId == 0)
        return HARNESS_STATUS_IDLE;
    for (u32 i = 0; i < (*self).jobCount; i++) {
        const HarnessJob *job = &(*self).jobs[i];
        if ((*job).jobId == jobId)
            return (*job).status;
    }
    return HARNESS_STATUS_IDLE;
}

/** Returns the most recently recorded run result. */
HarnessStatus Harness_getLastStatus(const Harness *self) {
    if (!self)
        return HARNESS_STATUS_IDLE;
    return (*self).lastStatus;
}
