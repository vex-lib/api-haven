#ifndef ASSET_BROKER_H
#define ASSET_BROKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "c23/constructor.h"

// asset/asset_broker.h — the AssetBroker class: bounded chunked-copy
// downloader into the VexHome cache (L2 behavior, R2 api-haven).
//
// Downloads land in the cache, never anywhere else: the host resolves
// VexHome_cache(<subsystem>) and hands this broker caller-owned chunk
// Bytes plus a caller-owned dest buffer. Each chunk copies under a
// per-chunk 100ms budget with a cancel flag — never a whole-file
// budget (Rule 27: a dead source drops one chunk, not the teardown).
// Truncation is never silent: dest overflow returns false and sets the
// flag (Rule 35.3). Zero sockets, zero threads, zero allocation, no
// exec, no vendor SDK — this file only copies caller-supplied chunks. No
// network transport or socket adapter is implemented here.

// Per-chunk copy ceiling: one chunk never exceeds 64KiB.
#define ASSET_BROKER_CHUNK_CAP 65536u
// Per-chunk budget default (Rule 27): one chunk never blocks past 100ms.
#define ASSET_BROKER_CHUNK_BUDGET_MS 100u
// Cache subdirectory label ceiling (VexHome_cache subsystem slot).
#define ASSET_BROKER_CACHE_SUB_CAP 64u

typedef struct AssetBroker {
    char cacheSub[ASSET_BROKER_CACHE_SUB_CAP]; // VexHome_cache subsystem slot
    uint64_t chunkBudgetMs;                    // per-chunk bound (default 100)
    bool cancelled;                            // cancel flag: copy degrades false
    uint32_t bytesCopied;                      // lifetime copied Bytes (counter)
} AssetBroker;

// --- Constructors (value structs, no allocation) ---
AssetBroker AssetBroker_0(void);                    // "assets" sub, 100ms
AssetBroker AssetBroker_1(const char *cacheSub);    // named cache slot
#define AssetBroker(...) CONSTRUCTOR_DISPATCH(AssetBroker, __VA_ARGS__)

// --- Core functions ---
// Bounded chunked copy: one srcChunk (chunkLen Bytes) into dest at the
// *usedLen cursor (in/out, Bytes already resident). Returns true on a
// copied chunk. Drop-degrade false on NULL self/args, cancelled, zero
// budget, chunkLen past ASSET_BROKER_CHUNK_CAP, or dest overflow — the
// overflow path sets the flag (dest-last, Rule 9). NULL flag degrades.
bool AssetBroker_copyChunk(AssetBroker *self, const uint8_t *srcChunk, size_t chunkLen,
                           uint8_t *dest, size_t destCap, uint32_t *usedLen,
                           bool *outTruncated);
// Render the cache-confined relative path "cache/<sub>/<fileName>"
// into caller-owned out (NUL always). False on NULL args, zero cap, or
// overflow. The host joins it under VexHome_cache(cacheSub); files are
// closed before Memory_freeAll per Rule 26. Dest-last per Rule 9.
bool AssetBroker_cachePath(const AssetBroker *self, const char *fileName,
                           char *out, size_t outCap);
// Drop the cancel latch and zero the byte counter (slot stays bound).
void AssetBroker_reset(AssetBroker *self);
// Latch the cancel flag (copies degrade to false until reset).
void AssetBroker_cancel(AssetBroker *self);

// --- Setters ---
// Cache slot copy with a loud truncation flag (Rule 35.3): false +
// flag when sub overflows the slot (slot keeps its old value).
bool AssetBroker_setCacheSub(AssetBroker *self, const char *sub, bool *outTruncated);
void AssetBroker_setChunkBudget(AssetBroker *self, uint64_t chunkBudgetMs);
void AssetBroker_setCancelled(AssetBroker *self, bool cancelled);
void AssetBroker_setBytesCopied(AssetBroker *self, uint32_t bytesCopied);

// --- Getters (Rule 24, null-safe) ---
// Borrowed cache slot, valid until the slot changes or the broker is destroyed.
const char *AssetBroker_getCacheSub(const AssetBroker *self);
uint64_t AssetBroker_getChunkBudget(const AssetBroker *self);
bool AssetBroker_isCancelled(const AssetBroker *self);
uint32_t AssetBroker_getBytesCopied(const AssetBroker *self);

#endif
