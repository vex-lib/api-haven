#ifndef API_HAVEN_WS_FANOUT_H
#define API_HAVEN_WS_FANOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "c23/constructor.h"

// api/haven_ws_fanout.h — the HavenWsFanout class (R2 fan-out registry).
//
// A fixed fan-out table over opaque socket handles: at most 16 slots,
// each pairing a borrowed void* handle with its WsSource fn-table
// {connect, poll, send, close}. A host caller invokes HavenWsFanout_pollStep
// with a millisecond budget — zero pthread_*, zero socket syscalls in this
// file. A real socket adapter is not implemented in this workspace.

// Max fan-out slots (fixed array, no allocation, Rule 10 friendly).
#define HAVEN_WS_FANOUT_MAX 16u

// Opaque transport seam: a host caller supplies callbacks here. FrameInbox is
// only a bounded byte inbox, not a socket transport. Handles are borrowed views
// (detach-only, never freed or closed by the registry — close runs only
// when the host explicitly calls the source close fn via detach path).
typedef struct HavenWsSource {
    bool (*connect)(void *handle);
    uint32_t (*poll)(void *handle, uint64_t budgetMs);
    bool (*send)(void *handle, const uint8_t *Bytes, uint32_t len);
    void (*close)(void *handle);
} HavenWsSource;

// SLOT RECORD — one fan-out row (Rule 3 co-location, zero behavior of
// its own; all behavior hangs off the HavenWsFanout table class).
typedef struct HavenWsSlot {
    void *handle;                 // borrowed transport handle (NULL = free)
    const HavenWsSource *source;  // fn-table bound at attach (NULL = free)
} HavenWsSlot;

typedef struct HavenWsFanout {
    HavenWsSlot slots[HAVEN_WS_FANOUT_MAX]; // fixed fan-out rows, no alloc
    uint32_t count;                         // live rows (0..FANOUT_MAX)
} HavenWsFanout;

// Empty registry (zero rows).
HavenWsFanout *HavenWsFanout_0(void);
#define HavenWsFanout(...) CONSTRUCTOR_DISPATCH(HavenWsFanout, __VA_ARGS__)

// Release the registry block (null-safe no-op; handles are borrowed —
// detach first per Rule 26, never freed here).
void HavenWsFanout_free(HavenWsFanout *fanout);

// Bind a borrowed handle + source table into a free slot. False on NULL
// args, full table, or duplicate handle.
bool HavenWsFanout_attach(HavenWsFanout *fanout, void *handle,
                          const HavenWsSource *source);

// Unbind a handle (slot freed, handle NOT closed/freed — borrowed view).
// False on NULL args or unknown handle.
bool HavenWsFanout_detach(HavenWsFanout *fanout, void *handle);

// One caller-driven fan-out step: poll each live slot with an even budget
// slice (budgetMs / count, min 1ms), summing delivered frames. Returns
// frames delivered; 0 when empty, NULL, or zero budget.
uint32_t HavenWsFanout_pollStep(HavenWsFanout *fanout, uint64_t budgetMs);

// Getters only (Rule 24; null-safe. Setters omitted — rows are managed
// by attach/detach, same immutable-row waiver as the provider tables).
uint32_t HavenWsFanout_getCount(const HavenWsFanout *fanout);
void *HavenWsFanout_getHandle(const HavenWsFanout *fanout, uint32_t i);

#endif
