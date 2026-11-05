#include "com/slack/slack.h"

#include <stdio.h>
#include <string.h>

#include "annotation/annotation.h"
#include "api/rest.h"
#include "net/http.h"
#include "net/json.h"

;;DEFINITION
/** The Slack webhook driver: it POSTs {"text","username","icon_url"} through the
 * Rest core and owns no transport of its own. It exists as the second vendor on
 * api/rest.h — proof that the core carries a vendor with zero transport code
 * added beside it. No state is retained between calls. */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: SlackWebhook (com/slack/slack)
 * ============================================================================
 * POSTs {"text","username","icon_url"} to a Slack incoming webhook URL via
 * the Rest core. First driver on api/rest.h: proves the core carries a
 * second vendor with zero transport code of its own.
 *
 * STRUCT FIELDS: none — procedural driver over Rest_postJson.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - SlackWebhook_sendText(webhookUrl, text, username, iconUrl)
 * ============================================================================
 */

/** Appends one optional JSON string field, updating the comma state and offset. */
static bool appendField(char *buf, size_t cap, size_t *offset,
                        const char *key, const char *val, bool *first) {
    if (!val)
        return true;
    size_t o = *offset;
    if (!(*first)) {
        if (o + 1 >= cap)
            return false;
        buf[o++] = ',';
    }
    *first = false;
    int n = snprintf(buf + o, cap - o, "\"%s\":", key);
    if (n < 0 || o + (size_t)n >= cap)
        return false;
    o += (size_t)n;
    int64_t w = Json_writeString(buf + o, cap - o, val);
    if (w < 0)
        return false;
    o += (size_t)w;
    *offset = o;
    return true;
}

// CORE FUNCTIONS
/** Sends a Slack incoming-webhook text payload and reports a 2xx response. */
bool SlackWebhook_sendText(const char *webhookUrl, const char *text,
                           const char *username, const char *iconUrl) {
    if (!webhookUrl || webhookUrl[0] == '\0')
        return false;
    if (!text)
        return false;
    char jsonBuf[4096];
    size_t offset = 0;
    jsonBuf[offset++] = '{';
    bool first = true;
    if (!appendField(jsonBuf, sizeof(jsonBuf), &offset, "text", text, &first))
        return false;
    if (!appendField(jsonBuf, sizeof(jsonBuf), &offset, "username", username, &first))
        return false;
    if (!appendField(jsonBuf, sizeof(jsonBuf), &offset, "icon_url", iconUrl, &first))
        return false;
    if (offset + 2 >= sizeof(jsonBuf))
        return false;
    jsonBuf[offset++] = '}';
    jsonBuf[offset] = '\0';
    char respBody[2048];
    HttpResponse resp = { 0 };
    resp.body = respBody;
    resp.bodyCap = sizeof(respBody);
    if (!Rest_postJson(webhookUrl, nullptr, jsonBuf, offset, &resp))
        return false;
    return resp.status >= 200 && resp.status < 300;
}
