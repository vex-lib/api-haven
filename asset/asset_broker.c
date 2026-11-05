#include "annotation/annotation.h"
#include "asset/asset_broker.h"

#include <string.h>

;;DEFINITION
/** The download-to-cache seam. It exists so a stream arrives as copies the caller
 * can afford: the host feeds 64KiB chunk Bytes and the broker copies them into
 * caller-owned dest at the usedLen cursor, one 100ms budget and one cancel flag
 * per chunk. A dead source drops one chunk and teardown still joins; dest
 * overflow reports truncation instead of dropping silently. A value struct — no
 * sockets, no threads, no allocation, no exec, no vendor SDK. */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: AssetBroker (asset/asset_broker)
 * chunks, caller-owned dest, per-chunk budget + cancel, zero alloc)
 * ============================================================================
 * Streams one download into the VexHome cache as bounded 64KiB chunk
 * copies: the host feeds srcChunk Bytes (decoded through the R1
 * ProcessSpawn shape, never fetched here) and this broker copies them
 * into the caller-owned dest buffer at the *usedLen cursor. Each chunk
 * runs under its own 100ms budget with a cancel flag — a dead source
 * drops one chunk and the teardown still joins (Rule 27). Dest
 * overflow returns false with the truncation flag set (Rule 35.3,
 * never silent). Cache confinement: dest buffers live under
 * VexHome_cache(cacheSub) and close before Memory_freeAll (Rule 26);
 * the broker itself is a value struct — no sockets, no threads, no
 * allocation, no exec, no vendor SDK.
 *
 * STRUCT FIELDS (Mirroring asset/asset_broker.h — exactly this file's
 * class):
 * ----------------------------------------------------------------------------
 *   char cacheSub[ASSET_BROKER_CACHE_SUB_CAP]; // VexHome_cache slot (64)
 *   uint64_t chunkBudgetMs;                    // per-chunk bound (dflt 100)
 *   bool cancelled;                            // cancel flag: copy degrades
 *   uint32_t bytesCopied;                      // lifetime copied Bytes
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors (value structs, no allocation):
 *   - AssetBroker_0()
 *   - AssetBroker_1(cacheSub)
 *
 * Core Functions:
 *   - AssetBroker_copyChunk(self, srcChunk, chunkLen, dest, destCap,
 *                           usedLen, outTruncated)
 *   - AssetBroker_cachePath(self, fileName, out, outCap)
 *   - AssetBroker_reset(self)
 *   - AssetBroker_cancel(self)
 *
 * Setters:
 *   - AssetBroker_setCacheSub(self, sub, outTruncated)
 *   - AssetBroker_setChunkBudget(self, chunkBudgetMs)
 *   - AssetBroker_setCancelled(self, cancelled)
 *   - AssetBroker_setBytesCopied(self, bytesCopied)
 *
 * Getters:
 *   - AssetBroker_getCacheSub(self)
 *   - AssetBroker_getChunkBudget(self)
 *   - AssetBroker_isCancelled(self)
 *   - AssetBroker_getBytesCopied(self)
 * ============================================================================
 */
;;INTENTION("per-chunk budgets, never a whole-file budget: a 100ms slice bounds one 64KiB copy — a stall drops the chunk and the join still lands (Rule 27)")
;;INTENTION("cache-confined by construction: the broker shapes relative cache paths only — VexHome_cache resolution and file close-before-free stay with the caller (Rule 26)")

// CONSTRUCTORS

/** Creates a broker value using the default cache subdirectory and budget. */
AssetBroker AssetBroker_0(void) {
    AssetBroker broker;
    memset(&broker, 0, sizeof(broker));
    const char *dflt = "assets";
    memcpy(broker.cacheSub, dflt, strlen(dflt) + 1);
    broker.chunkBudgetMs = ASSET_BROKER_CHUNK_BUDGET_MS;
    return broker;
}

/** Creates a broker value configured with the supplied cache subdirectory. */
AssetBroker AssetBroker_1(const char *cacheSub) {
    AssetBroker broker = AssetBroker();
    if (cacheSub) {
        size_t n = strlen(cacheSub);
        if (n >= ASSET_BROKER_CACHE_SUB_CAP)
            n = ASSET_BROKER_CACHE_SUB_CAP - 1;
        memcpy(broker.cacheSub, cacheSub, n);
        broker.cacheSub[n] = '\0';
    }
    return broker;
}

// CORE FUNCTIONS

/** Copies one bounded source chunk to dest at usedLen, reporting capacity truncation. */
bool AssetBroker_copyChunk(AssetBroker *self, const uint8_t *srcChunk, size_t chunkLen,
                           uint8_t *dest, size_t destCap, uint32_t *usedLen,
                           bool *outTruncated) {
    if (self == nullptr)
        return false;
    if (outTruncated)
        *outTruncated = false;
    if ((*self).cancelled)
        return false;
    if ((*self).chunkBudgetMs == 0)
        return false;
    if (dest == nullptr || usedLen == nullptr || destCap == 0)
        return false;
    if (chunkLen > 0 && srcChunk == nullptr)
        return false;
    if (chunkLen > ASSET_BROKER_CHUNK_CAP)
        return false;
    if ((*usedLen) > destCap)
        return false;
    if ((size_t)(*usedLen) + chunkLen > destCap) {
        if (outTruncated)
            *outTruncated = true;
        return false;
    }
    if (chunkLen > 0)
        memcpy(dest + (*usedLen), srcChunk, chunkLen);
    (*usedLen) += (uint32_t)chunkLen;
    (*self).bytesCopied += (uint32_t)chunkLen;
    return true;
}

/** Builds a relative cache path from the configured subdirectory and file name. */
bool AssetBroker_cachePath(const AssetBroker *self, const char *fileName,
                           char *out, size_t outCap) {
    if (self == nullptr)
        return false;
    if (fileName == nullptr || (*fileName) == '\0')
        return false;
    if (out == nullptr || outCap == 0)
        return false;
    const char *prefix = "cache/";
    const char *sub = (*self).cacheSub;
    size_t need = strlen(prefix) + strlen(sub) + 1 + strlen(fileName) + 1;
    if (need > outCap)
        return false;
    memcpy(out, prefix, strlen(prefix) + 1);
    size_t pos = strlen(prefix);
    size_t subLen = strlen(sub);
    memcpy(out + pos, sub, subLen);
    pos += subLen;
    out[pos] = '/';
    pos++;
    size_t nameLen = strlen(fileName);
    memcpy(out + pos, fileName, nameLen + 1);
    return true;
}

/** Clears cancellation and the accumulated copied-byte count. */
void AssetBroker_reset(AssetBroker *self) {
    if (self == nullptr)
        return;
    (*self).cancelled = false;
    (*self).bytesCopied = 0;
}

/** Marks future chunk copies as cancelled. */
void AssetBroker_cancel(AssetBroker *self) {
    if (self == nullptr)
        return;
    (*self).cancelled = true;
}

// SETTERS

/** Replaces the cache subdirectory only when the full name fits its field. */
bool AssetBroker_setCacheSub(AssetBroker *self, const char *sub, bool *outTruncated) {
    if (self == nullptr)
        return false;
    if (outTruncated)
        *outTruncated = false;
    if (sub == nullptr || (*sub) == '\0')
        return false;
    size_t n = strlen(sub);
    if (n >= ASSET_BROKER_CACHE_SUB_CAP) {
        if (outTruncated)
            *outTruncated = true;
        return false;
    }
    memcpy((*self).cacheSub, sub, n + 1);
    return true;
}

/** Sets the per-chunk time budget used by the broker contract. */
void AssetBroker_setChunkBudget(AssetBroker *self, uint64_t chunkBudgetMs) {
    if (self == nullptr)
        return;
    (*self).chunkBudgetMs = chunkBudgetMs;
}

/** Sets the broker's cancellation flag. */
void AssetBroker_setCancelled(AssetBroker *self, bool cancelled) {
    if (self == nullptr)
        return;
    (*self).cancelled = cancelled;
}

/** Sets the reported cumulative number of bytes copied. */
void AssetBroker_setBytesCopied(AssetBroker *self, uint32_t bytesCopied) {
    if (self == nullptr)
        return;
    (*self).bytesCopied = bytesCopied;
}

// GETTERS

/** Returns a borrowed view of the configured cache subdirectory. */
const char *AssetBroker_getCacheSub(const AssetBroker *self) {
    return self ? (*self).cacheSub : nullptr;
}

/** Returns the per-chunk budget, or zero for a null receiver. */
uint64_t AssetBroker_getChunkBudget(const AssetBroker *self) {
    if (self == nullptr)
        return 0;
    return (*self).chunkBudgetMs;
}

/** Reports whether future copy operations are cancelled. */
bool AssetBroker_isCancelled(const AssetBroker *self) {
    if (self == nullptr)
        return false;
    return (*self).cancelled;
}

/** Returns the cumulative copied-byte count, or zero for a null receiver. */
uint32_t AssetBroker_getBytesCopied(const AssetBroker *self) {
    if (self == nullptr)
        return 0;
    return (*self).bytesCopied;
}
