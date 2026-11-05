#ifndef HAVEN_SNAPSHOT_IO_H
#define HAVEN_SNAPSHOT_IO_H

#include <stddef.h>
#include <stdint.h>

/* Provider-neutral vocabulary, not a Drive implementation. Host owns context,
 * credentials, transport and scheduling. Callbacks must return in bounded slices
 * (at most 100ms), never wait for an entire network transfer. Owner-thread only.
 * DONE means durable acknowledgement, not merely a successful socket write.
 * RETRY must be safe to repeat with the SAME key and bytes (idempotent).
 * PENDING continues that same operation; REJECT is terminal.
 * Keys are nonzero, owner-assigned unique identities, not file names.
 * Upload bytes/context remain live until completion or explicit cancellation.
 * Cancellation is delivered by calling put with cancel=1; the provider must
 * synchronously release its borrows, discard staged work and return REJECT.
 * Download is separate: provider must report full length, reject over-capacity
 * content without writing, and leave dest/length unchanged unless DONE.
 * Arbitrary pointers and hostile provider implementations are outside contract.
 */
#define HAVEN_SNAPSHOT_DONE 0
#define HAVEN_SNAPSHOT_PENDING 1
#define HAVEN_SNAPSHOT_RETRY 2
#define HAVEN_SNAPSHOT_REJECT 3

typedef int (*HavenSnapshotPutFn)(void *context, uint64_t key,
                                const uint8_t *bytes, size_t length, int cancel);
typedef int (*HavenSnapshotGetFn)(void *context, uint64_t key,
                                uint8_t *dest, size_t capacity, size_t *outLength);

#endif
