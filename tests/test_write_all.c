/* Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 * Copyright 2026 The ovh-ttyrec Authors. All rights reserved.
 */

#include <assert.h>
#include <unistd.h>

// Exercise the production helpers with controlled write results, including
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


static void reset(enum scenario selected)
{
    scenario     = selected;
    calls        = 0;
    received_len = 0;
    locked_since = 0;
    errno        = 0;
    memset(received, 0, sizeof(received));
}


static void check(enum scenario selected, size_t expected, int expected_errno, size_t expected_len, int expected_calls)
{
    int write_error = -1;

    reset(selected);
    assert(write_all(123, "abcdefg", 7, &write_error) == expected);
    assert(write_error == expected_errno);
    assert(calls == expected_calls);
    assert(received_len == expected_len);
    assert(memcmp(received, "abcdefg", expected_len) == 0);
}


// doinput() relays each chunk through relay_input(): activity is only refreshed when the whole
// chunk went through, and a lock (short count, no error) must be told apart from a failure.
static void check_input(enum scenario selected, int expected, int expected_errno, size_t expected_len)
{
    int write_error = -1;

    reset(selected);
    master        = 123;
    last_activity = 0;
    lock_warned   = 1;
    kill_warned   = 1;

    assert(relay_input("abcdefg", 7, &write_error) == expected);
    assert(write_error == expected_errno);
    assert(received_len == expected_len);
    assert(memcmp(received, "abcdefg", expected_len) == 0);
    assert((last_activity != 0) == expected);
    assert(lock_warned == !expected);
    assert(kill_warned == !expected);
}


// dooutput() relays each chunk through relay_output(): whatever reached the terminal must be
// recorded and counted, even when a lock or an error stopped the write midway.
static void check_output(enum scenario selected, int dont_write, int expected_errno, size_t expected_len)
{
    unsigned long long bytes = 1000;
    Header             h;
    char               recorded[16];

    reset(selected);
    fscript = tmpfile();
    assert(fscript != NULL);
    last_activity = 0;
    lock_warned   = 1;
    kill_warned   = 1;

    assert(relay_output(123, "abcdefg", 7, dont_write, &bytes) == expected_errno);
    assert(received_len == expected_len);
    assert(bytes == 1000 + expected_len);
    assert((last_activity != 0) == (expected_len > 0));
    assert(lock_warned == (expected_len == 0));
    assert(kill_warned == (expected_len == 0));

    rewind(fscript);
    if ((expected_len > 0) && !dont_write)
    {
        assert(read_header(fscript, &h) == 1);
        assert(h.len == (int)expected_len);
        assert(fread(recorded, 1, expected_len, fscript) == expected_len);
        assert(memcmp(recorded, "abcdefg", expected_len) == 0);
    }
    // nothing else may have been recorded
    assert(fgetc(fscript) == EOF);
    fclose(fscript);
    fscript = NULL;
}


int main(void)
{
    check(SHORT_WRITES, 7, 0, 7, 4);
    check(INTERRUPTED, 7, 0, 7, 6);
    check(LOCK_AFTER_PARTIAL, 2, 0, 2, 1);
    check(LOCK_AFTER_EINTR, 0, 0, 0, 1);
    check(ZERO_WRITE, 0, EIO, 0, 1);
    check(ZERO_AFTER_PARTIAL, 2, EIO, 2, 2);
    check(FATAL_AFTER_PARTIAL, 2, EPIPE, 2, 2);
    check(WOULD_BLOCK, 0, EAGAIN, 0, 1);

    int write_error = -1;

    calls        = 0;
    locked_since = 1;
    assert(write_all(123, "abcdefg", 7, &write_error) == 0);
    assert(write_error == 0);
    assert(calls == 0);

    locked_since = 0;
    write_error  = -1;
    assert(write_all(123, "", 0, &write_error) == 0);
    assert(write_error == 0);
    assert(calls == 0);

    check_input(SHORT_WRITES, 1, 0, 7);
    check_input(INTERRUPTED, 1, 0, 7);
    check_input(LOCK_AFTER_PARTIAL, 0, 0, 2);
    check_input(LOCK_AFTER_EINTR, 0, 0, 0);
    check_input(ZERO_WRITE, 0, EIO, 0);
    check_input(ZERO_AFTER_PARTIAL, 0, EIO, 2);
    check_input(FATAL_AFTER_PARTIAL, 0, EPIPE, 2);
    check_input(WOULD_BLOCK, 0, EAGAIN, 0);

    check_output(SHORT_WRITES, 0, 0, 7);
    check_output(SHORT_WRITES, 1, 0, 7);
    check_output(INTERRUPTED, 0, 0, 7);
    check_output(LOCK_AFTER_PARTIAL, 0, 0, 2);
    check_output(LOCK_AFTER_EINTR, 0, 0, 0);
    check_output(ZERO_WRITE, 0, EIO, 0);
    check_output(ZERO_AFTER_PARTIAL, 0, EIO, 2);
    check_output(FATAL_AFTER_PARTIAL, 0, EPIPE, 2);
    check_output(FATAL_AFTER_PARTIAL, 1, EPIPE, 2);
    check_output(WOULD_BLOCK, 0, EAGAIN, 0);

    puts("write_all tests passed");
    return 0;
}
