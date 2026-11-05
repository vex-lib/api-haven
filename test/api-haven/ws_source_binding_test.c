// MODULE: the R2->R3 WebSocket binding seam. api-haven owns the WsSource shape
// and the fan-out registry; Vexspoke owns the protocol and its socket adapter.
// This owner is the host that can see BOTH layers, so it is where the binding is
// assigned and where the signature compatibility is checked at compile time.
// Scope: attach a Vexspoke client to HavenWsFanout through the fn-table, prove
// frames flow end to end against a forked loopback peer, prove the seam's send
// reaches the peer as a masked text frame, and prove the null/empty paths.
// No live network, no TLS, no wss://, no Discord gateway: the peer is scripted.
#include "api/haven_ws_fanout.h"
#include "net/vex_ws_source.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <unistd.h>

#include "net/url.h"
#include "security/sha1.h"

// The binding itself: assigning these four into the seam's table IS the
// compatibility check. A signature that drifts (even by a const) fails to
// compile here, which is the only place both headers are visible.
static const HavenWsSource kBinding = {
    .connect = VexWsSource_connect,
    .poll = VexWsSource_poll,
    .send = VexWsSource_send,
    .close = VexWsSource_close,
};

_Static_assert(_Generic(kBinding.connect, bool (*)(void*) : 1, default : 0),
               "connect must be bool(void*)");
_Static_assert(_Generic(kBinding.poll, uint32_t (*)(void*, uint64_t) : 1, default : 0),
               "poll must be uint32_t(void*, uint64_t)");
_Static_assert(_Generic(kBinding.send, bool (*)(void*, const uint8_t*, uint32_t) : 1,
                        default : 0),
               "send must be bool(void*, const uint8_t*, uint32_t)");
_Static_assert(_Generic(kBinding.close, void (*)(void*) : 1, default : 0),
               "close must be void(void*)");

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        g_checks++;                                                             \
        if (!(condition)) {                                                     \
            g_failures++;                                                       \
            printf("  FAIL line %d: %s\n", __LINE__, #condition);               \
        }                                                                       \
    } while (0)

static bool peerWrite(int fd, const void *bytes, size_t length) {
    const uint8_t *p = (const uint8_t*) bytes;
    size_t sent = 0u;
    while (sent < length) {
        ssize_t n = send(fd, p + sent, length - sent, 0);
        if (n <= 0)
            return false;
        sent += (size_t) n;
    }
    return true;
}

/** Server-side frame: never masked, which is what the RFC demands of a peer. */
static bool peerFrame(int fd, uint8_t opcode, const char *text) {
    size_t length = strlen(text);
    uint8_t header[4];
    header[0] = (uint8_t) (0x80u | opcode);
    header[1] = (uint8_t) length;
    return peerWrite(fd, header, 2u) && peerWrite(fd, text, length);
}

/** Read exactly length Bytes with the peer's bounded timeout. */
static bool peerReadExact(int peer, uint8_t *dest, size_t length) {
    size_t got = 0u;
    while (got < length) {
        ssize_t n = recv(peer, dest + got, length - got, 0);
        if (n <= 0)
            return false;
        got += (size_t) n;
    }
    return true;
}

/**
 * The scripted peer: accept, answer the handshake, send two text frames, then
 * require one masked TEXT frame back. Its exit status is the assertion.
 */
static int peerRun(int readyWriteFd) {
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0)
        return 90;
    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener, (struct sockaddr*) &address, sizeof(address)) != 0)
        return 91;
    socklen_t length = (socklen_t) sizeof(address);
    if (getsockname(listener, (struct sockaddr*) &address, &length) != 0)
        return 92;
    if (listen(listener, 1) != 0)
        return 93;
    int port = ntohs(address.sin_port);
    if (write(readyWriteFd, &port, sizeof(port)) != (ssize_t) sizeof(port))
        return 94;
    close(readyWriteFd);

    struct timeval timeout = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(listener, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    int peer = accept(listener, nullptr, nullptr);
    if (peer < 0)
        return 95;
    setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(peer, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    char request[4096];
    size_t used = 0u;
    while (used + 1u < sizeof(request)) {
        ssize_t n = recv(peer, request + used, 1u, 0);
        if (n <= 0)
            break;
        used += (size_t) n;
        request[used] = '\0';
        if (used >= 4u && strstr(request, "\r\n\r\n") != nullptr)
            break;
    }
    const char *key = strstr(request, "Sec-WebSocket-Key: ");
    if (key == nullptr)
        return 96;
    key += strlen("Sec-WebSocket-Key: ");
    const char *keyEnd = strstr(key, "\r\n");
    if (keyEnd == nullptr)
        return 97;
    char value[64];
    size_t keyLength = (size_t) (keyEnd - key);
    if (keyLength + 1u > sizeof(value))
        return 98;
    memcpy(value, key, keyLength);
    value[keyLength] = '\0';
    CryptoSha1 digest = CryptoSha1_0();
    Crypto_sha1Init(&digest);
    Crypto_sha1Update(&digest, value, strlen(value));
    Crypto_sha1Update(&digest, WS_HANDSHAKE_GUID, strlen(WS_HANDSHAKE_GUID));
    uint8_t hash[CRYPTO_SHA1_DIGEST_SIZE];
    Crypto_sha1Final(&digest, hash);
    char accept[64];
    if (Url_base64(hash, CRYPTO_SHA1_DIGEST_SIZE, accept, sizeof(accept)) <= 0)
        return 99;
    char response[256];
    int written = snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
    if (written <= 0 || (size_t) written >= sizeof(response) ||
        !peerWrite(peer, response, (size_t) written))
        return 100;

    // Two complete messages, sent separately so the seam delivers one per poll.
    if (!peerFrame(peer, WEBSOCKET_TEXT, "one"))
        return 101;
    if (!peerFrame(peer, WEBSOCKET_TEXT, "two"))
        return 102;

    // The host's send must arrive as one masked TEXT frame. Read it exactly: the
    // client writes its header and its masked payload separately, so a single
    // recv can see only the header and make this owner flap.
    uint8_t head[2];
    if (!peerReadExact(peer, head, sizeof(head)))
        return 103;
    if ((head[0] & 0x0Fu) != WEBSOCKET_TEXT)
        return 104;
    if ((head[1] & 0x80u) == 0u)
        return 105; // a client frame must be masked
    size_t payloadLength = (size_t) (head[1] & 0x7Fu);
    if (payloadLength > 16u)
        return 106;
    uint8_t mask[4];
    if (!peerReadExact(peer, mask, sizeof(mask)))
        return 107;
    uint8_t body[16];
    if (payloadLength > 0u && !peerReadExact(peer, body, payloadLength))
        return 108;
    char decoded[17];
    for (size_t i = 0u; i < payloadLength; i++)
        decoded[i] = (char) (body[i] ^ mask[i & 3u]);
    decoded[payloadLength] = '\0';
    if (payloadLength != 4u || strncmp(decoded, "pong", 4u) != 0)
        return 109;
    close(peer);
    close(listener);
    return 0;
}

int main(void) {
    printf("=== WebSocket WsSource binding ===\n");
    signal(SIGPIPE, SIG_IGN);

    int fds[2];
    if (pipe(fds) != 0) {
        printf("pipe failed\n");
        return 1;
    }
    pid_t child = fork();
    if (child == 0) {
        close(fds[0]);
        _exit(peerRun(fds[1]));
    }
    close(fds[1]);
    int port = 0;
    if (read(fds[0], &port, sizeof(port)) != (ssize_t) sizeof(port)) {
        printf("peer never reported a port\n");
        return 1;
    }
    close(fds[0]);

    char url[128];
    snprintf(url, sizeof(url), "ws://127.0.0.1:%d/live", port);

    // The host side: one handle, one seam table, one fan-out registry.
    VexWsSource handle = VexWsSource();
    HavenWsFanout *fanout = HavenWsFanout();
    CHECK(fanout != nullptr);
    WebSocketConfig config = { .timeoutMs = 2000u, .maxMessageBytes = 4096u };
    CHECK(VexWsSource_open(&handle, url, config));
    CHECK(VexWsSource_isOpen(&handle));
    CHECK(strcmp(VexWsSource_url(&handle), url) == 0);

    CHECK(HavenWsFanout_attach(fanout, &handle, &kBinding));
    CHECK(HavenWsFanout_getCount(fanout) == 1u);
    CHECK(HavenWsFanout_getHandle(fanout, 0u) == (void*) &handle);

    // One message per step, in order, with nothing dropped in between.
    CHECK(HavenWsFanout_pollStep(fanout, 500u) == 1u);
    const WebSocketMessage *first = VexWsSource_last(&handle);
    CHECK(first != nullptr && (*first).kind == WS_MESSAGE_TEXT);
    CHECK(first != nullptr && (*first).length == 3u);
    CHECK(first != nullptr && memcmp((*first).payload, "one", 3u) == 0);

    CHECK(HavenWsFanout_pollStep(fanout, 500u) == 1u);
    const WebSocketMessage *second = VexWsSource_last(&handle);
    CHECK(second != nullptr && (*second).kind == WS_MESSAGE_TEXT);
    CHECK(second != nullptr && memcmp((*second).payload, "two", 3u) == 0);

    // The seam's send reaches the peer as a masked TEXT frame.
    CHECK(kBinding.send(&handle, (const uint8_t*) "pong", 4u));
    CHECK(!kBinding.send(&handle, nullptr, 4u));      // null payload refused
    CHECK(!kBinding.send(nullptr, (const uint8_t*) "x", 1u));
    // A locally rejected payload must not kill a healthy transport.
    const uint8_t broken[2] = { 0xC3u, 0x28u };
    CHECK(!kBinding.send(&handle, broken, 2u));
    CHECK(VexWsSource_isOpen(&handle));

    // A step with nothing pending delivers nothing and reports nothing.
    CHECK(HavenWsFanout_pollStep(fanout, 0u) == 0u);

    WebSocket *socket = VexWsSource_socket(&handle);
    CHECK(socket != nullptr && WebSocket_framesIn(socket) == 2u);
    CHECK(socket != nullptr && WebSocket_framesOut(socket) == 1u);

    char text[512];
    bool truncated = false;
    VexWsSource_toString(&handle, text, sizeof(text), &truncated);
    CHECK(strstr(text, "VexWsSource {") != nullptr && strstr(text, url) != nullptr);
    VexWsSource_toStringStruct(&handle, text, sizeof(text), &truncated);
    CHECK(strstr(text, "configured=true") != nullptr);
    VexWsSource_toString(nullptr, text, sizeof(text), &truncated);
    CHECK(strcmp(text, "nullptr") == 0);
    VexWsSource_toStringStruct(nullptr, text, sizeof(text), &truncated);
    CHECK(strcmp(text, "nullptr") == 0);

    // Null/empty paths on the seam itself.
    CHECK(!VexWsSource_connect(nullptr));
    CHECK(VexWsSource_poll(nullptr, 10u) == 0u);
    CHECK(!VexWsSource_send(nullptr, (const uint8_t*) "x", 1u));
    VexWsSource_close(nullptr);
    CHECK(VexWsSource_last(nullptr) == nullptr);
    CHECK(VexWsSource_socket(nullptr) == nullptr);
    VexWsSource unconfigured = VexWsSource();
    CHECK(!VexWsSource_connect(&unconfigured));   // nothing configured yet
    CHECK(!VexWsSource_isOpen(&unconfigured));

    kBinding.close(&handle);
    CHECK(VexWsSource_isOpen(&handle) == false);
    CHECK(HavenWsFanout_detach(fanout, &handle));
    CHECK(HavenWsFanout_getCount(fanout) == 0u);
    HavenWsFanout_free(fanout);

    int status = 0;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    printf("%d checks, %d failed\n", g_checks, g_failures);
    if (g_failures == 0)
        printf("=== WebSocket WsSource binding: ALL PASS ===\n");
    return g_failures == 0 ? 0 : 1;
}
