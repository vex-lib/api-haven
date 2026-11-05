#include "annotation/annotation.h"

// api-haven is the CLI/MCP-facing layer: it CONSUMES vexspoke's R1
// system probes (AppDetect, CaptureTool, ProcessProbe) instead of
// owning them — PREFERENCES.md Rule 17 (api-haven connector clause:
// descriptor registries + fn-pointer client shapes, never re-implemented
// OS probes). This test proves the seam: the probes resolve and behave
// through api_haven's link against vexspoke.

#include "system/app_detect.h"
#include "system/capture_tool.h"
#include "system/process_probe.h"

#include <stdio.h>
#include <string.h>

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: ProbeConsumerTest (src/probe_consumer_test/test.c)
 * LEVEL: L2 — Behavior verification (headless; seam proof, no network)
 * ============================================================================
 * Executable proof that the vexspoke system probes are reachable and
 * functional from the api-haven layer: app registry breadth (new rows:
 * t3-code, grok, gemini, ...), capture registry presence, live liveness
 * via the test's own basename, and null-safety spot checks.
 *
 * Exit code 0 = all checks green; 1 = at least one check failed.
 * ============================================================================
 */

static int sFailures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            sFailures++;                                               \
        }                                                              \
    } while (0)

/* Verifies api-haven can consume Vexspoke app, capture, and process probes through the R2 seam. */
int main(int argc, const char **argv) {
    // --- vexspoke AppDetect through the api_haven seam ------------------------
    AppDetect *detect = AppDetect_shared();
    CHECK(detect != nullptr);
    const uint32_t appCount = AppDetect_count(detect);
    printf("AppDetect_count (via api_haven) = %u\n", appCount);
    CHECK(appCount >= 12);
    CHECK(AppDetect_get(detect, "opencode") != nullptr);
    CHECK(AppDetect_get(detect, "codex") != nullptr);
    CHECK(AppDetect_get(detect, "claude") != nullptr);
    CHECK(AppDetect_get(detect, "t3") != nullptr);
    CHECK(AppDetect_get(detect, "grok") != nullptr);
    CHECK(AppDetect_get(detect, "gemini") != nullptr);

    // --- vexspoke CaptureTool through the seam ---------------------------------
    CaptureTool *tools = CaptureTool_shared();
    CHECK(tools != nullptr);
    const uint32_t captureCount = CaptureTool_count(tools);
    printf("CaptureTool_count (via api_haven) = %u\n", captureCount);
    CHECK(captureCount >= 12);
    const CaptureSlot *obs = CaptureTool_get(tools, "obs");
    CHECK(obs != nullptr);
    CHECK(obs && CaptureTool_getKind(tools, obs) == CAPTURE_KIND_SCREEN);
    CHECK(CaptureTool_get(tools, "quicktime-player") != nullptr);

    // --- live liveness: self-truthy, driver rows never run ---------------------
    const char *selfName = argc > 0 && argv[0] ? strrchr(argv[0], '/') : nullptr;
    selfName = selfName ? selfName + 1 : (argc > 0 ? argv[0] : nullptr);
    ProcessProbe *probe = ProcessProbe_shared();
    CHECK(selfName && ProcessProbe_isRunning(probe, selfName));
    CHECK(!ProcessProbe_isRunning(probe, "definitely-not-a-proc-xyzzy"));
    const CaptureSlot *blackhole = CaptureTool_get(tools, "blackhole");
    CHECK(blackhole && !CaptureTool_isRunning(tools, blackhole));

    // reported, not asserted — machine-state dependent
    printf("isInstalled(opencode) = %s\n",
           AppDetect_isInstalled(detect, AppDetect_get(detect, "opencode")) ? "yes" : "no");
    printf("isRunning(obs) = %s\n", CaptureTool_isRunning(tools, obs) ? "yes" : "no");

    // --- null-safety spot checks -----------------------------------------------
    CHECK(AppDetect_count(nullptr) == 0);
    CHECK(CaptureTool_count(nullptr) == 0);
    CHECK(AppDetect_isRunning(nullptr, nullptr) == false);
    CHECK(CaptureTool_isInstalled(nullptr, nullptr) == false);
    CHECK(ProcessProbe_isRunning(nullptr, nullptr) == false);
    CHECK(ProcessProbe_isDriverLoaded(nullptr, nullptr, nullptr) == false);

    if (sFailures == 0) {
        printf("probe_consumer_test: ALL CHECKS PASSED (vexspoke probes reachable)\n");
        return 0;
    }
    printf("probe_consumer_test: %d FAILURES\n", sFailures);
    return 1;
}
