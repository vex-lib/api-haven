# api-haven — Repo-Local Living Preferences

## Constitution Link

gist.md (`../../metadata/gists/preferences/gist.md`) is the compressed law list; all universal laws bind this repository. Read one on demand: `./tools/prefs law "<title>"`. Universal laws are inherited, never restated.

Tier: core (R3 API Connector). Owns: connector contracts, telemetry loops, asset provider descriptors. Borrows: Vexspoke CPU/behavior, Relational Engine memory/storage.

## Exclusive Laws

| Law Title | Scope | Enforcement |
| **Asset Sourcing Law** | R3 API Connector asset providers | Mandatory for `api-haven` |
| **Quarantined Attic Isolation Law** | R3 API Connector `attic/` | Mandatory for `api-haven` |
| **Zero-Allocation Telemetry Law** | R3 API Connector telemetry | Mandatory for `api-haven` |

### Asset Sourcing Law

* Wire only blessed public APIs and source-offered direct-download URLs; sources without public search APIs are catalog-only via curated static manifests and hand-verified URLs, or excluded.
* Never scrape interfaces (HTML/JSON) to fake search or bypass auth.
* Public-search providers: Unsplash, Pexels, Pixabay, Openverse, Wikimedia Commons, Sketchfab, Freesound, Poly Haven, AmbientCG, OpenGameArt, Google Custom Search JSON API.
* Pinterest, raw Google Images, Kenney, Quaternius and itch.io packs lack public search APIs: catalog-only or excluded.
* Every result is one normalized AssetRow: provider slug, id, title, author, license family, preview/download URLs, attribution, dimensions/duration, size; UI never sees provider-specific shapes.
* License is a field, not a footnote: every row carries a license family; render attribution before import; project export fails closed on UNKNOWN.
* Downloads land in the cache (`AssetBroker_downloadToCache` streams to `VexHome_cache(<subsystem>)` with bounded timeouts); track cache files as shim state; close before `Memory_freeAll`; no exec or writes outside cache.
* The UI seam is fn-pointers: Darling hosts AssetBrowser and never includes api-haven; R5 binds an AssetSource fn-pointer table (opaque handle + callbacks).
* MCP surface (`McpServer`) hosts `asset_source_lookup`/`asset_search`/`asset_download`; writes stay cache-confined; timeouts bounded; no exec.
* API keys come via vexspoke Keychain or `ASSET_KEY_<SLUG>` env rendered by ApiAuth; never arena, preferences or repo.

### Quarantined Attic Isolation Law

* Legacy Java and off-heap prototypes stay permanently quarantined in `attic/`.
* Production C23 never includes, links or references files in `attic/`.
* `src/api` and `src/com` never include headers or references from `attic/`.
* Production and workspace indexing configurations ignore `attic/` entirely.

### Zero-Allocation Telemetry Law

* Telemetry feeds, SSE and MCP serialization use preallocated circular buffers and arena memory.
* Zero steady-state heap allocations.
* Serialize inbound/outbound packets in preallocated slots.
* Overflow triggers structured backpressure or packet drops, never emergency reallocations.

## Repo-Local Extensions

* None — no managed exceptions to universal laws.

## Readiness Cross-Reference

* Feature readiness: api-haven readiness gist.
* Open blockers: ecosystem blockers gist.
* Proof lawbook: `tests/test-preferences.md` — `./tools/prefs test "<title>"`.
* Evidence ledger: `metadata/gists/test-checklist.md` — `python3 tools/test_checklist.py check`.
