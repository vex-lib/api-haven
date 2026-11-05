#ifndef COM_DISCORD_DISCORD_H
#define COM_DISCORD_DISCORD_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct DiscordEmbed {
    const char *title;
    const char *description;
    int32_t     color;
    const char *imageUrl;
} DiscordEmbed;

/**
 * Sends a simple text-only message to a Discord Webhook.
 * Returns true if transmitted successfully and status 2xx received.
 */
bool DiscordWebhook_sendText(const char *webhookUrl, const char *content,
                             const char *username, const char *avatarUrl);

/**
 * Sends a rich embed message to a Discord Webhook.
 * Returns true if transmitted successfully and status 2xx received.
 */
bool DiscordWebhook_sendEmbed(const char *webhookUrl, const char *content,
                              const char *username, const char *avatarUrl,
                              const DiscordEmbed *embed);

#endif
