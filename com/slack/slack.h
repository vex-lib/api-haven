#ifndef COM_SLACK_SLACK_H
#define COM_SLACK_SLACK_H

#include "vex.h"

/**
 * com/slack/slack.h, Slack incoming-webhook driver (Shape A).
 *
 * Sister to com/discord (same webhook shape, Slack payload keys), but built
 * on the Rest core instead of raw Http_perform: the first driver proving
 * api/rest.h + api/auth.h. Webhook URLs carry their own secret; no ApiAuth
 * needed (pass nullptr).
 */

bool SlackWebhook_sendText(string webhookUrl, string text,
                           string username, string iconUrl);

#endif
