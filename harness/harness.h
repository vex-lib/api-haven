#ifndef HARNESS_H
#define HARNESS_H

#include "vex.h"
#include "harness/engine_provider.h"

/**
 * harness/harness.h, bounded CLI-engine job seam (L2 behavior).
 *
 * Each job gets its own slot in a fixed array (BitPool discipline):
 * HARNESS_MAX_JOBS slots, a count, BUSY_FULL when full, never an
 * unbounded grow. Per-slot timeoutMs bounds every driver wait (Rule 27:
 * the driver degrades to TIMEOUT, the seam never blocks); teardown
 * joins bounded via the driver's cancel path (Rule 26).
 *
 * No exec here: spawn/poll/cancel run behind the injected
 * HarnessDriverTable, implemented in vexspoke/R3. No popen, system,
 * fork, exec, threads, or vendor SDKs inside api-haven (Rule 17).
 */

#define HARNESS_MAX_JOBS 16

/**
 * Job lifecycle. BUSY_FULL is a table verdict (lastStatus only, a job
 * slot itself is never BUSY_FULL).
 *
 * HARNESS_STATUS_IDLE      - free slot / rejected run / cancelled
 * HARNESS_STATUS_RUNNING   - accepted, driver owns the handle
 * HARNESS_STATUS_DONE      - driver reported completion
 * HARNESS_STATUS_TIMEOUT   - driver bounded-wait fired, frame dropped
 * HARNESS_STATUS_BUSY_FULL - run rejected: all 16 slots RUNNING
 */
typedef enum HarnessStatus {
    HARNESS_STATUS_IDLE,
    HARNESS_STATUS_RUNNING,
    HARNESS_STATUS_DONE,
    HARNESS_STATUS_TIMEOUT,
    HARNESS_STATUS_BUSY_FULL,
} HarnessStatus;

/**
 * SLOT RECORD, one bounded job slot (Rule 3 co-location). Borrowed
 * engine row; driver-owned handle closed via cancel before teardown.
 *
 * u32 jobId                        - slot index + 1; stable per slot
 * const EngineProviderSlot *engine - borrowed descriptor row
 * HarnessStatus status             - lifecycle state
 * u64 timeoutMs                    - per-slot bound handed to driver
 * any driverHandle                 - opaque driver job; nullptr when idle
 * bytes outLen                     - driver-reported output length
 */
typedef struct HarnessJob {
    u32 jobId;
    const EngineProviderSlot *engine;
    HarnessStatus status;
    u64 timeoutMs;
    any driverHandle;
    bytes outLen;
} HarnessJob;

/**
 * Driver seam, implemented outside api-haven (vexspoke/R3). Every wait
 * inside these fns takes timeoutMs and degrades (drop frame, TIMEOUT)
 * instead of blocking unbounded (Rule 27). Zero steady-state allocation.
 */
typedef bool (*HarnessSpawnFn)(any driverCtx, const EngineProviderSlot *engine,
                               string prompt, bytes promptLen,
                               u64 timeoutMs, any *outHandle);
typedef HarnessStatus (*HarnessPollFn)(any driverCtx, any handle,
                                       u64 timeoutMs);
typedef fn (*HarnessCancelFn)(any driverCtx, any handle);

typedef struct HarnessDriverTable {
    HarnessSpawnFn spawnFn;
    HarnessPollFn pollFn;
    HarnessCancelFn cancelFn;
} HarnessDriverTable;

/**
 * HarnessJob jobs[HARNESS_MAX_JOBS] - fixed slots, BitPool discipline
 * u32 jobCount                      - slots ever issued, capped at MAX
 * u64 timeoutMs                     - default bound for new jobs
 * const EngineProviderSlot *engine  - bound engine row; nullptr = unbound
 * any driver                        - opaque driver ctx, never touched
 * HarnessDriverTable table          - injected driver fns
 * HarnessStatus lastStatus          - verdict of the last run call
 */
typedef struct Harness {
    HarnessJob jobs[HARNESS_MAX_JOBS];
    u32 jobCount;
    u64 timeoutMs;
    const EngineProviderSlot *engine;
    any driver;
    HarnessDriverTable table;
    HarnessStatus lastStatus;
} Harness;

/** --- Constructors (value structs, zero heap, AiChat precedent) --- */
Harness Harness_0(); /** engine nullptr, 100ms default */
Harness Harness_1(u64 timeoutMs); /** engine nullptr, given default */
#define Harness(...) CONSTRUCTOR_DISPATCH(Harness, __VA_ARGS__)

/**
 * --- Core functions ---
 * Finds a free slot (IDLE/DONE/TIMEOUT reused) or issues a fresh one;
 * BUSY_FULL + false when all 16 RUNNING. Unbound engine/driver,
 * nullptr/empty prompt, or spawn failure degrade to false (lastStatus
 * IDLE). outJobId is dest-last per Rule 9 (nullptr-tolerant).
 */
bool Harness_run(Harness *self, string prompt, bytes promptLen,
                 u64 timeoutMs, u32 *outJobId);
/**
 * Bounded status check: hands the slot timeout to pollFn; nullptr pollFn
 * reports the stored status. IDLE on nullptr self or unknown jobId.
 */
HarnessStatus Harness_poll(Harness *self, u32 jobId);
/**
 * Bounded teardown hook per slot (Rule 26): cancelFn, then IDLE.
 * False on nullptr self, unknown jobId, or a slot not RUNNING.
 */
bool Harness_cancel(Harness *self, u32 jobId);

/** --- Setters --- */
fn Harness_setEngine(Harness *self, const EngineProviderSlot *engine);
fn Harness_setDriver(Harness *self, any driver, HarnessDriverTable table);
fn Harness_setTimeout(Harness *self, u64 timeoutMs);

/** --- Getters (Rule 24, null-safe) --- */
const EngineProviderSlot *Harness_getEngine(const Harness *self);
any Harness_getDriver(const Harness *self);
u64 Harness_getTimeout(const Harness *self);
bool Harness_isRunning(const Harness *self);
u32 Harness_getJobCount(const Harness *self);
const HarnessJob *Harness_getJobAt(const Harness *self, u32 i);
HarnessStatus Harness_getJobStatus(const Harness *self, u32 jobId);
HarnessStatus Harness_getLastStatus(const Harness *self);

#endif
