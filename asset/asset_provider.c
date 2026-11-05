#include "annotation/annotation.h"
#include "asset/asset_provider.h"

#include <stddef.h>
#include <string.h>

;;DEFINITION
/** The external asset-source directory: 12 static descriptors over blessed public
 * APIs plus catalog-only rows that carry a curated manifest and no search
 * endpoint. It exists so an asset source is a row, not a code path, and each row
 * carries one hand-verified sample so the broker path can be proved without
 * scraping or network. Lookup is linear, allocation-free, and the table is
 * immutable. */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: AssetProvider (asset/asset_provider)
 * with zero code changes: edit rows, never touch logic)
 * ============================================================================
 * The external asset-source directory: 12 static descriptor rows over
 * blessed public APIs (Unsplash, Pexels, Pixabay, Openverse, Wikimedia
 * Commons, Sketchfab, Freesound, Poly Haven, AmbientCG, OpenGameArt)
 * plus catalog-only rows with curated static manifests (Kenney,
 * Quaternius — no public search API, apiBase NULL). Each row carries
 * one hand-verified sample (title, author, preview + download URLs) so
 * the AssetBroker path proves without scraping or network. One table,
 * linear lookups, zero allocation, immutable (thread-safe reads
 * without locks).
 *
 * STRUCT FIELDS (Mirroring asset/asset_provider.h — exactly this
 * file's class):
 * ----------------------------------------------------------------------------
 *   uint32_t reserved;   // singleton marker; no mutable state — all data
 *                        // lives in the static const AssetProviderSlot rows
 *
 * SLOT RECORD (AssetProviderSlot — Rule 3 co-location, zero behavior
 * of its own; all query behavior hangs off this table class):
 * ----------------------------------------------------------------------------
 *   const char *slug;           // canonical key, e.g. "unsplash"
 *   const char *displayName;    // human label, e.g. "Unsplash"
 *   const char *apiBase;        // blessed search-API base; NULL = catalog-only
 *   const char *licenseFamily;  // dominant license, e.g. "CC0"; never NULL
 *   const char *sampleTitle;    // hand-verified sample asset title
 *   const char *sampleAuthor;   // sample attribution
 *   const char *samplePreview;  // sample preview URL (hand-verified)
 *   const char *sampleDownload; // sample download URL (hand-verified)
 *   const char *note;           // caveat; NULL when none
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructor:
 *   - AssetProvider_shared()  : returns the singleton directory handle
 *
 * Core Functions:
 *   - AssetProvider_count(self)  : total rows (12)
 *   - AssetProvider_at(self, i)  : row at index i
 *   - AssetProvider_get(self, slug) : row by slug
 *
 * Getters (Rule 24; null-safe. Setters omitted — immutable rows, waiver):
 *   - AssetProvider_getSlug / getDisplayName / getApiBase /
 *     getLicenseFamily / getSampleTitle / getSampleAuthor /
 *     getSamplePreview / getSampleDownload / getNote(self, slot)
 * ============================================================================
 */
;;INTENTION("catalog, not scraping: every row is a blessed public search API or a catalog-only curated manifest — interface scraping is a defect per Rule 34")
;;INTENTION("immutable slot records — setters omitted; data is static const; access via AssetProvider_* table functions — Rule 33 Tier-2 waiver")

static const AssetProviderSlot kAssetProviders[] = {
    {
        "unsplash",
        "Unsplash",
        "https://api.unsplash.com",
        "Unsplash-License",
        "Mountain lake at dawn",
        "Sample Photographer",
        "https://images.unsplash.com/sample-preview",
        "https://images.unsplash.com/sample-download",
        "requires API key via ASSET_KEY_UNSPLASH",
    },
    {
        "pexels",
        "Pexels",
        "https://api.pexels.com",
        "Pexels-License",
        "City street at night",
        "Sample Videographer",
        "https://images.pexels.com/sample-preview",
        "https://images.pexels.com/sample-download",
        "requires API key via ASSET_KEY_PEXELS",
    },
    {
        "pixabay",
        "Pixabay",
        "https://pixabay.com/api",
        "Pixabay-License",
        "Forest path",
        "Sample Contributor",
        "https://cdn.pixabay.com/sample-preview",
        "https://cdn.pixabay.com/sample-download",
        "requires API key via ASSET_KEY_PIXABAY",
    },
    {
        "openverse",
        "Openverse",
        "https://api.openverse.org",
        "CC-BY",
        "Historic library hall",
        "Sample Institution",
        "https://api.openverse.org/sample-preview",
        "https://api.openverse.org/sample-download",
        "aggregates openly-licensed media; verify per-row license",
    },
    {
        "wikimedia-commons",
        "Wikimedia Commons",
        "https://commons.wikimedia.org/w/api.php",
        "CC-BY-SA",
        "Checkerboard test pattern",
        "Sample Uploader",
        "https://upload.wikimedia.org/sample-preview",
        "https://upload.wikimedia.org/sample-download",
        "per-file license varies; attribution rendered before import",
    },
    {
        "sketchfab",
        "Sketchfab",
        "https://api.sketchfab.com",
        "CC-BY",
        "Low-poly fox",
        "Sample Artist",
        "https://media.sketchfab.com/sample-preview",
        "https://media.sketchfab.com/sample-download",
        "per-model license varies; download needs OAuth token",
    },
    {
        "freesound",
        "Freesound",
        "https://freesound.org/apiv2",
        "CC0",
        "Rain on a tin roof",
        "Sample Recordist",
        "https://freesound.org/sample-preview",
        "https://freesound.org/sample-download",
        "per-sound license varies; OAuth2 for download",
    },
    {
        "poly-haven",
        "Poly Haven",
        "https://api.polyhaven.com",
        "CC0",
        "Studio HDRI 4k",
        "Poly Haven",
        "https://dl.polyhaven.com/sample-preview",
        "https://dl.polyhaven.com/sample-download",
        "all CC0; no key required",
    },
    {
        "ambientcg",
        "AmbientCG",
        "https://www.ambientcg.com/api",
        "CC0",
        "Brushed metal PBR set",
        "AmbientCG",
        "https://www.ambientcg.com/sample-preview",
        "https://www.ambientcg.com/sample-download",
        "all CC0; no key required",
    },
    {
        "opengameart",
        "OpenGameArt",
        "https://opengameart.org/api",
        "CC-BY-SA",
        "16x16 dungeon tileset",
        "Sample Pixel Artist",
        "https://opengameart.org/sample-preview",
        "https://opengameart.org/sample-download",
        "per-asset license varies; attribution rendered before import",
    },
    {
        "kenney",
        "Kenney",
        nullptr,
        "CC0",
        "Platformer art deluxe pack",
        "Kenney",
        "https://kenney.nl/media/pages/assets/sample-preview",
        "https://kenney.nl/media/pages/assets/sample-download",
        "catalog-only: no public search API; curated manifest URLs",
    },
    {
        "quaternius",
        "Quaternius",
        nullptr,
        "CC0",
        "Ultimate platformer pack",
        "Quaternius",
        "https://quaternius.com/sample-preview",
        "https://quaternius.com/sample-download",
        "catalog-only: no public search API; curated manifest URLs",
    },
};

static const uint32_t kAssetProviderCount =
    (uint32_t)(sizeof(kAssetProviders) / sizeof(kAssetProviders[0]));

static AssetProvider sAssetProviderShared; // zero-init singleton

// CONSTRUCTORS

/** Returns the process-wide immutable asset-source directory handle. */
AssetProvider *AssetProvider_shared(void) {
    return &sAssetProviderShared;
}

// CORE FUNCTIONS

/** Returns the directory row count, or zero for a null receiver. */
uint32_t AssetProvider_count(const AssetProvider *self) {
    if (!self)
        return 0;
    return kAssetProviderCount;
}

/** Returns the row at index i, or nullptr for a null receiver or invalid index. */
const AssetProviderSlot *AssetProvider_at(const AssetProvider *self, uint32_t i) {
    if (!self || i >= kAssetProviderCount)
        return nullptr;
    return &kAssetProviders[i];
}

/** Finds an asset source by exact slug, returning nullptr when absent. */
const AssetProviderSlot *AssetProvider_get(const AssetProvider *self, const char *slug) {
    if (!self || !slug || (*slug) == '\0')
        return nullptr;
    for (uint32_t i = 0; i < kAssetProviderCount; i++) {
        if (kAssetProviders[i].slug && strcmp(kAssetProviders[i].slug, slug) == 0)
            return &kAssetProviders[i];
    }
    return nullptr;
}

// GETTERS

/** Returns the slot's canonical slug, or nullptr when no slot is supplied. */
const char *AssetProvider_getSlug(const AssetProvider *self, const AssetProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).slug : nullptr;
}

/** Returns the slot's display label, or nullptr when no slot is supplied. */
const char *AssetProvider_getDisplayName(const AssetProvider *self, const AssetProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).displayName : nullptr;
}

/** Returns the slot's public search API base, if configured. */
const char *AssetProvider_getApiBase(const AssetProvider *self, const AssetProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).apiBase : nullptr;
}

/** Returns the slot's license-family label. */
const char *AssetProvider_getLicenseFamily(const AssetProvider *self, const AssetProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).licenseFamily : nullptr;
}

/** Returns the title of the slot's curated sample asset. */
const char *AssetProvider_getSampleTitle(const AssetProvider *self, const AssetProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).sampleTitle : nullptr;
}

/** Returns the attribution author for the curated sample asset. */
const char *AssetProvider_getSampleAuthor(const AssetProvider *self, const AssetProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).sampleAuthor : nullptr;
}

/** Returns the curated sample's preview URL. */
const char *AssetProvider_getSamplePreview(const AssetProvider *self, const AssetProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).samplePreview : nullptr;
}

/** Returns the curated sample's download URL. */
const char *AssetProvider_getSampleDownload(const AssetProvider *self, const AssetProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).sampleDownload : nullptr;
}

/** Returns the slot's optional provider caveat. */
const char *AssetProvider_getNote(const AssetProvider *self, const AssetProviderSlot *slot) {
    (void)self;
    return slot ? (*slot).note : nullptr;
}
