#ifndef ASSET_PROVIDER_H
#define ASSET_PROVIDER_H

#include <stdint.h>

// asset/asset_provider.h — the external asset-source directory (L1
// descriptors), mirroring the ai/ provider pattern under Rule 34.
//
// One behavior class owns the catalog of blessed asset sources: the
// canonical slug, verbatim display name, the blessed public search-API
// base (NULL = catalog-only row with a hand-verified curated manifest —
// scraping another service's HTML/JSON to fake search is a defect,
// always), the dominant license family, and one hand-verified sample
// row (title, author, preview + download URLs) proving the broker path
// without touching the network. Pure descriptors: no scraping, no exec,
// no vendor SDK, no includes beyond the standard library.

// SLOT RECORD — one row of the asset-source directory (Rule 3
// co-location). Immutable: rows are static const data in
// asset_provider.c; all behavior hangs off the AssetProvider table
// class (Rule 33 Tier-2 waiver, same as the ai/database directories).
typedef struct AssetProviderSlot {
    const char *slug;           // canonical key, e.g. "unsplash"
    const char *displayName;    // human label, e.g. "Unsplash"
    const char *apiBase;        // blessed search-API base; NULL = catalog-only
    const char *licenseFamily;  // dominant license, e.g. "CC0"; never NULL
    const char *sampleTitle;    // hand-verified sample asset title
    const char *sampleAuthor;   // sample attribution (rendered before import)
    const char *samplePreview;  // sample preview URL (hand-verified)
    const char *sampleDownload; // sample download URL (hand-verified)
    const char *note;           // caveat; NULL when none
} AssetProviderSlot;

// The table class — a singleton handle; state lives in static const rows
// in asset_provider.c.
typedef struct AssetProvider {
    uint32_t reserved; // signature/marker; no mutable state
} AssetProvider;

// --- Constructor ---
// Returns the shared directory handle (static, zero-init, never NULL).
AssetProvider *AssetProvider_shared(void);

// --- Core functions ---
// Total rows. Null-safe: 0 on NULL self.
uint32_t AssetProvider_count(const AssetProvider *self);
// Row at index i (source order). NULL when out of range.
const AssetProviderSlot *AssetProvider_at(const AssetProvider *self, uint32_t i);
// Row by canonical slug. NULL when absent. Linear scan (~12 rows).
const AssetProviderSlot *AssetProvider_get(const AssetProvider *self, const char *slug);

// --- Getters (Rule 24; null-safe; setters omitted — immutable rows) ---
const char *AssetProvider_getSlug(const AssetProvider *self, const AssetProviderSlot *slot);
const char *AssetProvider_getDisplayName(const AssetProvider *self, const AssetProviderSlot *slot);
const char *AssetProvider_getApiBase(const AssetProvider *self, const AssetProviderSlot *slot);
const char *AssetProvider_getLicenseFamily(const AssetProvider *self, const AssetProviderSlot *slot);
const char *AssetProvider_getSampleTitle(const AssetProvider *self, const AssetProviderSlot *slot);
const char *AssetProvider_getSampleAuthor(const AssetProvider *self, const AssetProviderSlot *slot);
const char *AssetProvider_getSamplePreview(const AssetProvider *self, const AssetProviderSlot *slot);
const char *AssetProvider_getSampleDownload(const AssetProvider *self, const AssetProviderSlot *slot);
const char *AssetProvider_getNote(const AssetProvider *self, const AssetProviderSlot *slot);

#endif
