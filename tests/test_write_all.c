/* Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 * Copyright 2026 The ovh-ttyrec Authors. All rights reserved.
 */

#include <assert.h>
#include <unistd.h>

// Exercise the production helper with controlled write results, including
// a lock arriving while write() is in progress, without requiring a real tty.
static ssize_t test_write(int fd, const void *buf, size_t len);
int ttyrec_main(int argc, char **argv);

#define write    test_write
#define main     ttyrec_main
#include "../ttyrec.c"
#undef main
#undef write

enum scenario
{
    SHORT_WRITES,
    INTERRUPTED,
    LOCK_AFTER_PARTIAL,
    LOCK_AFTER_EINTR,
    ZERO_WRITE,
    ZERO_AFTER_PARTIAL,
    FATAL_AFTER_PARTIAL,
    WOULD_BLOCK
};

static enum scenario scenario;
static int           calls;
static size_t        received_len;
static char          received[16];

static ssize_t test_write(int fd, const void *buf, size_t len)
{
    assert(fd == 123);
    assert(len > 0);
    // Fail boundedly if a regression introduces an endless retry loop.
    assert(++calls <= 16);

    if ((scenario == INTERRUPTED) && ((calls == 1) || (calls == 3)))
    {
        errno = EINTR;
        return -1;
    }
    if (scenario == LOCK_AFTER_EINTR)
    {
        locked_since = 1;
        errno        = EINTR;
        return -1;
    }
    if ((scenario == ZERO_WRITE) || ((scenario == ZERO_AFTER_PARTIAL) && (calls == 2)))
    {
        return 0;
    }
    if ((scenario == FATAL_AFTER_PARTIAL) && (calls == 2))
    {
        errno = EPIPE;
        return -1;
    }
    if (scenario == WOULD_BLOCK)
    {
        errno = EAGAIN;
        return -1;
    }

    size_t n = len > 2 ? 2 : len;
    assert(received_len + n <= sizeof(received));
    memcpy(received + received_len, buf, n);
    received_len += n;
    if (scenario == LOCK_AFTER_PARTIAL)
    {
        locked_since = 1;
    }
    return (ssize_t)n;
}


static void check(enum scenario selected, ssize_t expected, int expected_errno, size_t expected_len, int expected_calls)
{
    scenario     = selected;
    calls        = 0;
    received_len = 0;
    locked_since = 0;
    errno        = 0;
    memset(received, 0, sizeof(received));

    assert(write_all(123, "abcdefg", 7) == expected);
    if (expected == -1)
    {
        assert(errno == expected_errno);
    }
    assert(calls == expected_calls);
    assert(received_len == expected_len);
    assert(memcmp(received, "abcdefg", expected_len) == 0);
}


int main(void)
{
    check(SHORT_WRITES, 7, 0, 7, 4);
    check(INTERRUPTED, 7, 0, 7, 6);
    check(LOCK_AFTER_PARTIAL, 2, 0, 2, 1);
    check(LOCK_AFTER_EINTR, 0, 0, 0, 1);
    check(ZERO_WRITE, -1, EIO, 0, 1);
    check(ZERO_AFTER_PARTIAL, -1, EIO, 2, 2);
    check(FATAL_AFTER_PARTIAL, -1, EPIPE, 2, 2);
    check(WOULD_BLOCK, -1, EAGAIN, 0, 1);

    calls        = 0;
    locked_since = 1;
    assert(write_all(123, "abcdefg", 7) == 0);
    assert(calls == 0);

    locked_since = 0;
    assert(write_all(123, "", 0) == 0);
    assert(calls == 0);

    puts("write_all tests passed");
    return 0;
}
