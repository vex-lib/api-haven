# api-haven — Repo-Local Living Preferences
> Repo-local preferences governed by the Living Documentation Law.
> Universal constitution: `gist.md` is the compressed law list; `PREFERENCES.md` is its rationale.

## 0. Constitution Link (supreme)
- [gist.md](https://gist.github.com/vex-graph/4132a6c45cb6d3797c3e8eff2e94035a#file-gist-md) — the compressed law list, mirrored in this workspace at `../../metadata/gists/preferences/gist.md`. It is the only normative law text; `../../../metadata/gists/preferences/PREFERENCES.md` carries the rationale and is never ingested whole. Numbers are slice addresses inside one file, never law identity: cite by Title.
- All universal laws bind this repository. Read one on demand from the workspace root: `./tools/prefs law "<title>"`, `./tools/prefs section "<section>"`, `./tools/prefs repo <name>`.
- This document codifies **exclusive** preferences for `api-haven` (R3 API Connector). R3 borrows Vexspoke CPU computation/behavior and Relational Engine memory/storage contracts. The default production build resolves migrated IO/NIO from RE, preserving the native Memory ABI; this is not a Rust allocator rewrite. Network/connector semantics remain API-owned.

## 1. Repo-Local Law Index (Binding Matrix)

Universal laws are inherited from the compressed constitution (`../../metadata/gists/preferences/gist.md`); this table indexes the additional laws specific to this repository.

| Law Title | Scope | Enforcement |
| :--- | :--- | :--- |
| **Asset Sourcing Law — Legal-Sense, First-Class In-App Marketplace Policy** | R3 API Connector | Mandatory for `api-haven` |
| **Quarantined Attic Isolation Law** | R3 API Connector | Mandatory for `api-haven` |
| **Zero-Allocation Telemetry Law** | R3 API Connector | Mandatory for `api-haven` |

## 2. Exclusive Repo-Local Laws (FULL PROSE RESTATEMENT)

### Asset Sourcing Law — Legal-Sense, First-Class In-App Marketplace Policy

#### Definition:
Each external image, 3D, texture or audio source is an AssetProvider descriptor row. Wire only blessed public APIs and source-offered direct-download URLs. Sources without public search APIs are catalog-only via curated static manifests and hand-verified URLs, or excluded. Never scrape interfaces (HTML/JSON) to fake search or bypass auth.

#### The Why:
Scraping creates legal exposure, broken trust and brittle integrations. A first-class marketplace needs explicit contracts, normalized shapes and license-aware flows, not reverse-engineered endpoints.

#### The Rule:
- **Catalog, not scraping.** Public-search providers: Unsplash, Pexels, Pixabay, Openverse, Wikimedia Commons, Sketchfab, Freesound, Poly Haven, AmbientCG, OpenGameArt, Google Custom Search JSON API. Pinterest, raw Google Images, Kenney, Quaternius and itch.io packs lack public search APIs: catalog-only with curated static manifests and hand-verified URLs, or excluded. Scraping is prohibited.
- **One normalized contract.** Each result is an AssetRow: provider slug, id, title, author, license family, preview/download URLs, attribution, dimensions/duration and size. UI never sees provider-specific shapes.
- **License is a field, not a footnote.** Every row carries a license family; render attribution before import; project export fails closed on UNKNOWN.
- **Downloads land in the cache.** AssetBroker_download streams to VexHome_cache(<subsystem>) with bounded timeouts (Bounded Wait Law). Track cache files as shim state and close before Memory_freeAll (Vertical Integration Law (Teardown)); no exec or writes outside cache.
- **The UI seam is fn-pointers.** Darling hosts AssetBrowser and never includes api-haven (Vertical Integration Law); R5 binds an AssetSource fn-pointer table (opaque handle + callbacks; Conflict Triage Law).
- **MCP surface.** McpServer hosts asset_source_lookup / asset_search / asset_download; writes stay cache-confined, timeouts bounded, no exec.
- **Credentials.** API keys come via vexspoke Keychain or ASSET_KEY_<SLUG> env rendered by ApiAuth; never arena, preferences or repo.

---

### Quarantined Attic Isolation Law

#### Definition:
Legacy Java and off-heap prototypes stay permanently quarantined in `attic/`. Production C23 must never include, link or reference files in `attic/`.

#### The Why:
Historical context must not pollute C23 compilation or confuse static analysis.

#### The Rule:
1. **No Attic Includes:** `src/api` and `src/com` never include headers or references from `attic/`.
2. **Build Isolation:** Production and workspace indexing configurations ignore `attic/` entirely.

---

### Zero-Allocation Telemetry Law

#### Definition:
Telemetry feeds, SSE and MCP serialization use preallocated circular buffers and arena memory, with zero steady-state heap allocations.

#### The Why:
Network telemetry must avoid allocator churn and thread stalls in high-throughput pipelines.

#### The Rule:
1. **Ring Buffering:** Serialize inbound/outbound packets in preallocated slots.
2. **Bounded Capacity:** Overflow triggers structured backpressure or packet drops, never emergency reallocations.

---

## 3. Repo-Local Extensions (managed, per the Conflict Triage Law)

;;INTENTION("R3 API Surface: POSIX/macOS native connectors; quarantined attic; zero runtime allocation in telemetry loops.")

---

## 4. Readiness Cross-Reference (Living Documentation Law)

- Feature readiness matrix: [api-haven](https://gist.github.com/vex-graph/6943f92acb931b25dad1073c46da6ce7#file-api-haven-md).
- Open blockers and deferred decisions: [ecosystem blockers Gist](https://gist.github.com/vex-graph/e921fa188eebbd0c68c4e59646109887).
