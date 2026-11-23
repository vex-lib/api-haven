#ifndef ASSET_PROVIDER_H
#define ASSET_PROVIDER_H

#include "vex.h"

/**
 * asset/asset_provider.h, the external asset-source directory (L1
 * descriptors), mirroring the ai/ provider pattern under Rule 34.
 *
 * One behavior class owns the catalog of blessed asset sources: the
 * canonical slug, verbatim display name, the blessed public search-API
 * base (nullptr = catalog-only row with a hand-verified curated manifest,
 * scraping another service's HTML/JSON to fake search is a defect,
 * always), the dominant license family, and one hand-verified sample
 * row (title, author, preview + download URLs) proving the broker path
 * without touching the network. Pure descriptors: no scraping, no exec,
 * no vendor SDK, no includes beyond the standard library.
 */

/**
 * SLOT RECORD, one row of the asset-source directory (Rule 3
 * co-location). Immutable: rows are static const data in
 * asset_provider.c; all behavior hangs off the AssetProvider table
 * class (Rule 33 Tier-2 waiver, same as the ai/database directories).
 *
 * string slug           - canonical key, e.g. "unsplash"
 * string displayName    - human label, e.g. "Unsplash"
 * string apiBase        - blessed search-API base; nullptr = catalog-only
 * string licenseFamily  - dominant license, e.g. "CC0"; never nullptr
 * string sampleTitle    - hand-verified sample asset title
 * string sampleAuthor   - sample attribution (rendered before import)
 * string samplePreview  - sample preview URL (hand-verified)
 * string sampleDownload - sample download URL (hand-verified)
 * string note           - caveat; nullptr when none
 */
typedef struct AssetProviderSlot {
    string slug;
    string displayName;
    string apiBase;
    string licenseFamily;
    string sampleTitle;
    string sampleAuthor;
    string samplePreview;
    string sampleDownload;
    string note;
} AssetProviderSlot;

/**
 * The table class, a singleton handle; state lives in static const rows
 * in asset_provider.c.
 *
 * u32 reserved - signature/marker; no mutable state
 */
typedef struct AssetProvider {
    u32 reserved;
} AssetProvider;

/**
 * --- Constructor ---
 * Returns the shared directory handle (static, zero-init, never nullptr).
 */
AssetProvider *AssetProvider_shared();

/**
 * --- Core functions ---
 * Total rows. Null-safe: 0 on nullptr self.
 */
u32 AssetProvider_count(const AssetProvider *self);
/** Row at index i (source order). nullptr when out of range. */
const AssetProviderSlot *AssetProvider_at(const AssetProvider *self, u32 i);
/** Row by canonical slug. nullptr when absent. Linear scan (~12 rows). */
const AssetProviderSlot *AssetProvider_get(const AssetProvider *self, string slug);

/** --- Getters (Rule 24; null-safe; setters omitted, immutable rows) --- */
string AssetProvider_getSlug(const AssetProvider *self, const AssetProviderSlot *slot);
string AssetProvider_getDisplayName(const AssetProvider *self, const AssetProviderSlot *slot);
string AssetProvider_getApiBase(const AssetProvider *self, const AssetProviderSlot *slot);
string AssetProvider_getLicenseFamily(const AssetProvider *self, const AssetProviderSlot *slot);
string AssetProvider_getSampleTitle(const AssetProvider *self, const AssetProviderSlot *slot);
string AssetProvider_getSampleAuthor(const AssetProvider *self, const AssetProviderSlot *slot);
string AssetProvider_getSamplePreview(const AssetProvider *self, const AssetProviderSlot *slot);
string AssetProvider_getSampleDownload(const AssetProvider *self, const AssetProviderSlot *slot);
string AssetProvider_getNote(const AssetProvider *self, const AssetProviderSlot *slot);

#endif
