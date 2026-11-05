#ifndef HARNESS_H
#define HARNESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "harness/engine_provider.h"
#include "c23/constructor.h"

// harness/harness.h — bounded CLI-engine job seam (L2 behavior).
//
// Each job gets its own slot in a fixed array (BitPool discipline):
// HARNESS_MAX_JOBS slots, a count, BUSY_FULL when full — never an
// unbounded grow. Per-slot timeoutMs bounds every driver wait (Rule 27:
// the driver degrades to TIMEOUT, the seam never blocks); teardown
// joins bounded via the driver's cancel path (Rule 26).
//
// No exec here: spawn/poll/cancel run behind the injected
// HarnessDriverTable, implemented in vexspoke/R3. No popen, system,
// fork, exec, threads, or vendor SDKs inside api-haven (Rule 17).

#define HARNESS_MAX_JOBS 16

// Job lifecycle. BUSY_FULL is a table verdict (lastStatus only — a job
// slot itself is never BUSY_FULL).
typedef enum HarnessStatus {
    HARNESS_STATUS_IDLE,      // free slot / rejected run / cancelled
    HARNESS_STATUS_RUNNING,   // accepted, driver owns the handle
    HARNESS_STATUS_DONE,      // driver reported completion
    HARNESS_STATUS_TIMEOUT,   // driver bounded-wait fired, frame dropped
    HARNESS_STATUS_BUSY_FULL, // run rejected: all 16 slots RUNNING
} HarnessStatus;

// SLOT RECORD — one bounded job slot (Rule 3 co-location). Borrowed
// engine row; driver-owned handle closed via cancel before teardown.
typedef struct HarnessJob {
    uint32_t jobId;                    // slot index + 1; stable per slot
    const EngineProviderSlot *engine;  // borrowed descriptor row
    HarnessStatus status;              // lifecycle state
    uint64_t timeoutMs;                // per-slot bound handed to driver
    void *driverHandle;                // opaque driver job; NULL when idle
    size_t outLen;                     // driver-reported output length
} HarnessJob;

// Driver seam — implemented outside api-haven (vexspoke/R3). Every wait
// inside these fns takes timeoutMs and degrades (drop frame, TIMEOUT)
// instead of blocking unbounded (Rule 27). Zero steady-state allocation.
typedef bool (*HarnessSpawnFn)(void *driverCtx, const EngineProviderSlot *engine,
                               const char *prompt, size_t promptLen,
                               uint64_t timeoutMs, void **outHandle);
typedef HarnessStatus (*HarnessPollFn)(void *driverCtx, void *handle,
                                       uint64_t timeoutMs);
typedef void (*HarnessCancelFn)(void *driverCtx, void *handle);

typedef struct HarnessDriverTable {
    HarnessSpawnFn spawnFn; // NULL = driver unbound, runs degrade to false
    HarnessPollFn pollFn;   // NULL = poll reports stored status only
    HarnessCancelFn cancelFn; // NULL = cancel just marks the slot IDLE
} HarnessDriverTable;

typedef struct Harness {
    HarnessJob jobs[HARNESS_MAX_JOBS]; // fixed slots, BitPool discipline
    uint32_t jobCount;                 // slots ever issued, capped at MAX
    uint64_t timeoutMs;                // default bound for new jobs
    const EngineProviderSlot *engine;  // bound engine row; NULL = unbound
    void *driver;                      // opaque driver ctx, never touched
    HarnessDriverTable table;          // injected driver fns
    HarnessStatus lastStatus;          // verdict of the last run call
} Harness;

// --- Constructors (value structs, zero heap — AiChat precedent) ---
Harness Harness_0(void);                        // engine NULL, 100ms default
Harness Harness_1(uint64_t timeoutMs);          // engine NULL, given default
#define Harness(...) CONSTRUCTOR_DISPATCH(Harness, __VA_ARGS__)

// --- Core functions ---
// Finds a free slot (IDLE/DONE/TIMEOUT reused) or issues a fresh one;
// BUSY_FULL + false when all 16 RUNNING. Unbound engine/driver,
// NULL/empty prompt, or spawn failure degrade to false (lastStatus
// IDLE). outJobId is dest-last per Rule 9 (NULL-tolerant).
bool Harness_run(Harness *self, const char *prompt, size_t promptLen,
                 uint64_t timeoutMs, uint32_t *outJobId);
// Bounded status check: hands the slot timeout to pollFn; NULL pollFn
// reports the stored status. IDLE on NULL self or unknown jobId.
HarnessStatus Harness_poll(Harness *self, uint32_t jobId);
// Bounded teardown hook per slot (Rule 26): cancelFn, then IDLE.
// False on NULL self, unknown jobId, or a slot not RUNNING.
bool Harness_cancel(Harness *self, uint32_t jobId);

// --- Setters ---
void Harness_setEngine(Harness *self, const EngineProviderSlot *engine);
void Harness_setDriver(Harness *self, void *driver, HarnessDriverTable table);
void Harness_setTimeout(Harness *self, uint64_t timeoutMs);

// --- Getters (Rule 24, null-safe) ---
const EngineProviderSlot *Harness_getEngine(const Harness *self);
void *Harness_getDriver(const Harness *self);
uint64_t Harness_getTimeout(const Harness *self);
bool Harness_isRunning(const Harness *self);
uint32_t Harness_getJobCount(const Harness *self);
const HarnessJob *Harness_getJobAt(const Harness *self, uint32_t i);
HarnessStatus Harness_getJobStatus(const Harness *self, uint32_t jobId);
HarnessStatus Harness_getLastStatus(const Harness *self);

#endif
