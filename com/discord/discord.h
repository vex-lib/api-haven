#ifndef COM_DISCORD_DISCORD_H
#define COM_DISCORD_DISCORD_H

#include "vex.h"

typedef struct DiscordEmbed {
    string title;
    string description;
    i32     color;
    string imageUrl;
} DiscordEmbed;

/**
 * Sends a simple text-only message to a Discord Webhook.
 * Returns true if transmitted successfully and status 2xx received.
 */
bool DiscordWebhook_sendText(string webhookUrl, string content,
                             string username, string avatarUrl);

/**
 * Sends a rich embed message to a Discord Webhook.
 * Returns true if transmitted successfully and status 2xx received.
 */
bool DiscordWebhook_sendEmbed(string webhookUrl, string content,
                              string username, string avatarUrl,
                              const DiscordEmbed *embed);

#endif
