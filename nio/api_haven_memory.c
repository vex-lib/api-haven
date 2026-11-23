#include "vex.h"
#include "nio/api_haven_memory.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: ApiHavenMemory (api-haven/nio/api_haven_memory.c)
 * ============================================================================
 * Per-library MemoryArena owner for API Haven, backed by Relational Engine's
 * MemoryPool. Starts at zero OS pages and grows on demand until OOM.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: ApiHavenMemory (nio/api_haven_memory)
 * ============================================================================
 * Lazily creates and shuts down API Haven's dedicated MemoryArena.
 * ============================================================================
 */

static MemoryArena *s_apiHavenArena = nullptr;

/** Lazily create and return API Haven's page-pooled MemoryArena. */
MemoryArena *ApiHaven_arena() {
    if (s_apiHavenArena == nullptr)
        s_apiHavenArena = MemoryArena();
    return s_apiHavenArena;
}

/** Destroy API Haven's MemoryArena and return all pages to MemoryPool. */
fn ApiHaven_arenaShutdown() {
    if (s_apiHavenArena != nullptr) {
        MemoryArena_destroy(s_apiHavenArena);
        s_apiHavenArena = nullptr;
    }
}
