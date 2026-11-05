/* Header-only provider vocabulary owner. Proves all result constants and both
 * callback signatures as a real C23 client. No network/backend is implemented. */
#include "storage/snapshot_io.h"
#include <assert.h>
#include <string.h>

/* Mocks provider upload outcomes, distinguishing cancellation from ordinary pending work. */
static int put(void *context, uint64_t key, const uint8_t *bytes, size_t length, int cancel) {
    assert(context == nullptr && key == 1 && length == 1 && bytes[0] == 42);
    return cancel ? HAVEN_SNAPSHOT_REJECT : HAVEN_SNAPSHOT_PENDING;
}

/* Mocks bounded snapshot retrieval and preserves outputs when key or capacity is invalid. */
static int get(void *context, uint64_t key, uint8_t *dest, size_t capacity, size_t *outLength) {
    assert(context == nullptr);
    if (key != 1 || capacity < 1)
        return HAVEN_SNAPSHOT_REJECT;
    dest[0] = 42;
    *outLength = 1;
    return HAVEN_SNAPSHOT_DONE;
}

/* Compiles and exercises the header-only callback vocabulary and its result/status contract. */
int main(void) {
    _Static_assert(HAVEN_SNAPSHOT_DONE != HAVEN_SNAPSHOT_PENDING);
    _Static_assert(HAVEN_SNAPSHOT_PENDING != HAVEN_SNAPSHOT_RETRY);
    _Static_assert(HAVEN_SNAPSHOT_RETRY != HAVEN_SNAPSHOT_REJECT);
    HavenSnapshotPutFn upload = put;
    HavenSnapshotGetFn download = get;
    uint8_t byte = 42;
    assert(upload(nullptr, 1, &byte, 1, 0) == HAVEN_SNAPSHOT_PENDING);
    assert(upload(nullptr, 1, &byte, 1, 1) == HAVEN_SNAPSHOT_REJECT);
    byte = 7;
    size_t length = 9;
    assert(download(nullptr, 2, &byte, 1, &length) == HAVEN_SNAPSHOT_REJECT);
    assert(byte == 7 && length == 9);
    assert(download(nullptr, 1, &byte, 0, &length) == HAVEN_SNAPSHOT_REJECT);
    assert(byte == 7 && length == 9);
    assert(download(nullptr, 1, &byte, 1, &length) == HAVEN_SNAPSHOT_DONE);
    assert(byte == 42 && length == 1);
    return 0;
}
