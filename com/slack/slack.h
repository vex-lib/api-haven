#ifndef COM_SLACK_SLACK_H
#define COM_SLACK_SLACK_H

#include <stdbool.h>

// com/slack/slack.h — Slack incoming-webhook driver (Shape A).
//
// Sister to com/discord (same webhook shape, Slack payload keys), but built
// on the Rest core instead of raw Http_perform: the first driver proving
// api/rest.h + api/auth.h. Webhook URLs carry their own secret; no ApiAuth
// needed (pass nullptr).

bool SlackWebhook_sendText(const char *webhookUrl, const char *text,
                           const char *username, const char *iconUrl);

#endif
