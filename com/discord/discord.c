#include "com/discord/discord.h"
#include "annotation/annotation.h"
#include "net/http.h"
#include "net/json.h"
#include "net/url.h"
#include "nio/mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

;;DEFINITION
/** The Discord webhook client: text and rich-embed payloads rendered off-heap
 * and transmitted through the vexspoke HTTP client. It exists as the vendor
 * driver over the shared transport — it owns no socket and no buffer, and every
 * payload is written into caller-owned storage. */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: DiscordWebhook (com/discord/discord)
 * ============================================================================
 * High-performance, zero-allocation Discord Webhook API for the VexGraph engine.
 * Formulates off-heap JSON payloads (text-only and rich embeds) and transmits
 * them to Discord servers using the vexspoke HTTPClient.
 *
 * STRUCT FIELDS (Mirroring com/discord/discord.h):
 * ----------------------------------------------------------------------------
 *   const char *title;         // Embed card headline
 *   const char *description;   // Embed body markdown
 *   i32     color;         // Hex color integer (e.g. 0x00FF00)
 *   const char *imageUrl;      // Optional image attachment/embed URL
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - DiscordWebhook_sendText(webhookUrl, content, username, avatarUrl)
 *   - DiscordWebhook_sendEmbed(webhookUrl, content, username, avatarUrl, embed)
 * ============================================================================
 */

/** Splits a webhook URL into bounded scheme, host, port, and path outputs. */
static bool parseUrl(const char *url, char *scheme, bytes schemeCap,
                     char *host, bytes hostCap, int *port,
                     char *path, bytes pathCap) {
    if (!url || url[0] == '\0') return false;

    const char *p = url;
    const char *schemeEnd = strstr(p, "://");
    if (schemeEnd) {
        bytes sLen = (bytes)(schemeEnd - p);
        if (sLen >= schemeCap) return false;
        memcpy(scheme, p, sLen);
        scheme[sLen] = '\0';
        p = schemeEnd + 3;
    } else {
        strncpy(scheme, "http", schemeCap - 1);
        scheme[schemeCap - 1] = '\0';
    }

    *port = Url_defaultPort(scheme);

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    if (colon && (!slash || colon < slash)) {
        bytes hLen = (bytes)(colon - p);
        if (hLen >= hostCap) return false;
        memcpy(host, p, hLen);
        host[hLen] = '\0';
        *port = atoi(colon + 1);
    } else if (slash) {
        bytes hLen = (bytes)(slash - p);
        if (hLen >= hostCap) return false;
        memcpy(host, p, hLen);
        host[hLen] = '\0';
    } else {
        bytes hLen = strlen(p);
        if (hLen >= hostCap) return false;
        memcpy(host, p, hLen);
        host[hLen] = '\0';
    }

    if (slash) {
        strncpy(path, slash, pathCap - 1);
        path[pathCap - 1] = '\0';
    } else {
        strncpy(path, "/", pathCap - 1);
        path[pathCap - 1] = '\0';
    }

    return true;
}

/** Appends an optional JSON string field while tracking field separators. */
static bool appendStringField(char *buf, bytes cap, bytes *offset, const char *key, const char *val, bool *first) {
    if (!val) return true;
    bytes o = *offset;
    if (!(*first)) {
        if (o + 1 >= cap) return false;
        buf[o++] = ',';
    }
    *first = false;
    int n = snprintf(buf + o, cap - o, "\"%s\":", key);
    if (n < 0 || o + (bytes)n >= cap) return false;
    o += (bytes)n;
    int64_t w = Json_writeString(buf + o, cap - o, val);
    if (w < 0) return false;
    o += (bytes)w;
    *offset = o;
    return true;
}

/** Sends a Discord text webhook payload and reports whether the response is 2xx. */
bool DiscordWebhook_sendText(const char *webhookUrl, const char *content,
                             const char *username, const char *avatarUrl) {
    if (!webhookUrl || webhookUrl[0] == '\0') return false;

    char scheme[16] = {0};
    char host[256] = {0};
    char path[512] = {0};
    int port = 80;

    if (!parseUrl(webhookUrl, scheme, sizeof(scheme), host, sizeof(host), &port, path, sizeof(path))) {
        return false;
    }

    char jsonBuf[4096];
    bytes offset = 0;
    jsonBuf[offset++] = '{';
    bool first = true;

    if (content) {
        if (!appendStringField(jsonBuf, sizeof(jsonBuf), &offset, "content", content, &first))
            return false;
    }
    if (username) {
        if (!appendStringField(jsonBuf, sizeof(jsonBuf), &offset, "username", username, &first))
            return false;
    }
    if (avatarUrl) {
        if (!appendStringField(jsonBuf, sizeof(jsonBuf), &offset, "avatar_url", avatarUrl, &first))
            return false;
    }

    if (offset + 2 >= sizeof(jsonBuf)) return false;
    jsonBuf[offset++] = '}';
    jsonBuf[offset] = '\0';

    HttpHeader headers[2] = {
        { .name = "Content-Type", .value = "application/json" },
        { .name = "Connection",   .value = "close" }
    };

    HttpRequest req = {
        .scheme = scheme,
        .method = "POST",
        .host = host,
        .port = port,
        .path = path,
        .headers = headers,
        .headerCount = 2,
        .body = jsonBuf,
        .bodyLen = offset,
        .timeoutMs = 5000
    };

    char respBody[2048];
    HttpResponse resp = { 0 };
    resp.body = respBody;
    resp.bodyCap = sizeof(respBody);
    Http_perform(&req, &resp);
    return resp.ok && resp.status >= 200 && resp.status < 300;
}

/** Sends a Discord webhook payload with optional embed fields. */
bool DiscordWebhook_sendEmbed(const char *webhookUrl, const char *content,
                              const char *username, const char *avatarUrl,
                              const DiscordEmbed *embed) {
    if (!webhookUrl || webhookUrl[0] == '\0') return false;

    char scheme[16] = {0};
    char host[256] = {0};
    char path[512] = {0};
    int port = 80;

    if (!parseUrl(webhookUrl, scheme, sizeof(scheme), host, sizeof(host), &port, path, sizeof(path))) {
        return false;
    }

    char jsonBuf[8192];
    bytes offset = 0;
    jsonBuf[offset++] = '{';
    bool first = true;

    if (content) {
        if (!appendStringField(jsonBuf, sizeof(jsonBuf), &offset, "content", content, &first))
            return false;
    }
    if (username) {
        if (!appendStringField(jsonBuf, sizeof(jsonBuf), &offset, "username", username, &first))
            return false;
    }
    if (avatarUrl) {
        if (!appendStringField(jsonBuf, sizeof(jsonBuf), &offset, "avatar_url", avatarUrl, &first))
            return false;
    }

    if (embed) {
        if (!first) {
            if (offset + 1 >= sizeof(jsonBuf)) return false;
            jsonBuf[offset++] = ',';
        }
        first = false;

        const char *embedsKey = "\"embeds\":[{";
        bytes kLen = strlen(embedsKey);
        if (offset + kLen >= sizeof(jsonBuf)) return false;
        memcpy(jsonBuf + offset, embedsKey, kLen);
        offset += kLen;

        bool embedFirst = true;
        if ((*embed).title) {
            if (!appendStringField(jsonBuf, sizeof(jsonBuf), &offset, "title", (*embed).title, &embedFirst))
                return false;
        }
        if ((*embed).description) {
            if (!appendStringField(jsonBuf, sizeof(jsonBuf), &offset, "description", (*embed).description, &embedFirst))
                return false;
        }
        if ((*embed).color != 0) {
            if (!embedFirst) {
                if (offset + 1 >= sizeof(jsonBuf)) return false;
                jsonBuf[offset++] = ',';
            }
            embedFirst = false;
            int n = snprintf(jsonBuf + offset, sizeof(jsonBuf) - offset, "\"color\":%d", (*embed).color);
            if (n < 0 || offset + (bytes)n >= sizeof(jsonBuf)) return false;
            offset += (bytes)n;
        }
        if ((*embed).imageUrl) {
            if (!embedFirst) {
                if (offset + 1 >= sizeof(jsonBuf)) return false;
                jsonBuf[offset++] = ',';
            }
            embedFirst = false;
            const char *imgKey = "\"image\":{\"url\":";
            bytes ikLen = strlen(imgKey);
            if (offset + ikLen >= sizeof(jsonBuf)) return false;
            memcpy(jsonBuf + offset, imgKey, ikLen);
            offset += ikLen;

            int64_t w = Json_writeString(jsonBuf + offset, sizeof(jsonBuf) - offset, (*embed).imageUrl);
            if (w < 0) return false;
            offset += (bytes)w;

            if (offset + 2 >= sizeof(jsonBuf)) return false;
            jsonBuf[offset++] = '}';
        }

        if (offset + 3 >= sizeof(jsonBuf)) return false;
        jsonBuf[offset++] = '}';
        jsonBuf[offset++] = ']';
    }

    if (offset + 2 >= sizeof(jsonBuf)) return false;
    jsonBuf[offset++] = '}';
    jsonBuf[offset] = '\0';

    HttpHeader headers[2] = {
        { .name = "Content-Type", .value = "application/json" },
        { .name = "Connection",   .value = "close" }
    };

    HttpRequest req = {
        .scheme = scheme,
        .method = "POST",
        .host = host,
        .port = port,
        .path = path,
        .headers = headers,
        .headerCount = 2,
        .body = jsonBuf,
        .bodyLen = offset,
        .timeoutMs = 5000
    };

    char respBody[2048];
    HttpResponse resp = { 0 };
    resp.body = respBody;
    resp.bodyCap = sizeof(respBody);
    Http_perform(&req, &resp);
    return resp.ok && resp.status >= 200 && resp.status < 300;
}
