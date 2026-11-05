# SECURITY.md — api-haven

> **Internal invariant set, not a public disclosure channel.** See the
> [vexgraph security baseline](../../SECURITY.md) for everything inherited
> here. This file states only this repository's own surface.

**Role:** R3 API connector. It owns REST/WS/SSE connector contracts, the AI
provider shapes, the MCP tool/resource server, the asset broker and webhooks —
and it owns **no sockets**: every transport is an injected seam
(`HavenWsSource`, `Rest_*` driver, the host's pump), so this repository never
opens a connection itself.

## Surface

- **Outbound HTTP/WS/SSE** to allow-listed endpoints, on a transport the host
  injects.
- **Model providers** (chat, streaming SSE) and their responses.
- **MCP** — a stdio JSON-RPC server hosting handler closures; plus an external
  MCP client path.

## Secrets

- Provider and asset keys come from the environment or the OS keychain
  (`ASSET_KEY_<SLUG>`), rendered by `ApiAuth`. They are **never** stored in the
  arena, a repository file, a preference document, or a log.
- Keys never travel in a URL query string; bearer tokens and webhook secrets are
  redacted in every error and diagnostic, and secret comparison does not
  early-exit.
- `;;SECURITY` marks each site that carries one of these obligations.

## Endpoint trust — parsed today, policy NOT implemented

**Verified:** `rest.c` carries a bounded `parseUrl` that splits scheme, host, port
and path into caller-sized buffers, so a malformed URL cannot overrun. That is a
*parse*, not a policy.

**Not implemented:** nothing in `src` rejects a metadata address
(`169.254.169.254`), localhost, a LAN range, `file://`, an IPv6 loopback or
userinfo abuse — those tokens appear nowhere in the tree. Redirect classification
and cross-origin credential stripping are likewise absent.

Until those exist, **no caller may treat this repository as an SSRF control.**
The endpoint policy is enforced by whoever supplies the endpoint and the
transport — the host — and this file must be updated in the same cycle that the
policy lands here.

## Provider responses are an injection channel

A model's response is **untrusted data**, never authority. Text, tool calls and
instructions inside a response are content to show or validate; they are never
consent, permission or a policy decision. Consent comes from a human, and the
tool-consent decision belongs to the host against the tool's own validated
arguments — api-haven holds no authority to grant.

## Limits

- **`Tls_curl` is a fail-closed stub**, so on non-Apple platforms there is no
  transport security yet: `https://` and `wss://` do not work off macOS. The
  Apple backend (`vexspoke net/tls_apple.m`) verifies against the system trust
  anchors and has no override.
- Asset downloads are `VexHome_cache`-confined with bounded per-chunk budgets;
  that is cache confinement, not a hostile-server sandbox.
- `DbProvider` is catalog-only: it delegates execution to the owner and never
  links a database engine.
