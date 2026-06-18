// vim: noai:ts=4:sw=4:expandtab:

/* Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 * Copyright 2001-2019 The ovh-ttyrec Authors. All rights reserved.
 *
 * This work is based on the original ttyrec, whose license text
 * can be found below unmodified.
 *
 * Copyright (c) 2000 Satoru Takabayashi <satoru@namazu.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by the University of
 *    California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <termios.h>
#include <sys/time.h>
#include <string.h>

#include "ttyrec.h"
#include "io.h"
#include "compress.h"
#include "configure.h"

// Upper sanity bound on a record length read from a (possibly corrupt) file.
#define MAX_RECORD_LEN    (16 * 1024 * 1024)

typedef double (*WaitFunc) (struct timeval prev,
                            struct timeval cur,
                            double         speed);
typedef int (*ReadFunc) (FILE *fp, Header *h, char **buf);
typedef void (*WriteFunc)    (char *buf, int len);
typedef void (*ProcessFunc)  (FILE *fp, double speed,
                              ReadFunc read_func, WaitFunc wait_func);

struct timeval timeval_diff(struct timeval tv1, struct timeval tv2);
struct timeval timeval_div(struct timeval tv1, double n);
double ttywait(struct timeval prev, struct timeval cur, double speed);
double ttynowait(struct timeval prev, struct timeval cur, double speed);
int ttyread(FILE *fp, Header *h, char **buf);
int ttypread(FILE *fp, Header *h, char **buf);
void ttywrite(char *buf, int len);
void ttynowrite(char *buf, int len);
void ttyplay(FILE *fp, double speed, ReadFunc read_func, WriteFunc write_func, WaitFunc wait_func);
void ttyskipall(FILE *fp);
void ttyplayback(FILE *fp, double speed, ReadFunc read_func, WaitFunc wait_func);
void ttypeek(FILE *fp, double speed, ReadFunc read_func, WaitFunc wait_func);
void usage(void);
FILE *input_from_stdin(void);


struct timeval timeval_diff(struct timeval tv1, struct timeval tv2)
{
    struct timeval diff;

    diff.tv_sec  = tv2.tv_sec - tv1.tv_sec;
    diff.tv_usec = tv2.tv_usec - tv1.tv_usec;
    if (diff.tv_usec < 0)
    {
        diff.tv_sec--;
        diff.tv_usec += 1000000;
    }

    return diff;
}


struct timeval timeval_div(struct timeval tv1, double n)
{
    double         x = ((double)tv1.tv_sec + (double)tv1.tv_usec / 1000000.0) / n;
    struct timeval div;

    div.tv_sec  = (int)x;
    div.tv_usec = (x - (int)x) * 1000000;

    return div;
}


double ttywait(struct timeval prev, struct timeval cur, double speed)
{
    static struct timeval drift = { 0, 0 };
    struct timeval        start;
    struct timeval        diff = timeval_diff(prev, cur);
    fd_set                readfs;

    gettimeofday(&start, NULL);

    assert(speed != 0);
    diff = timeval_diff(drift, timeval_div(diff, speed));
    if (diff.tv_sec < 0)
    {
        diff.tv_sec = diff.tv_usec = 0;
    }

    FD_ZERO(&readfs);
    FD_SET(STDIN_FILENO, &readfs);

    /*
     * We use select() for sleeping with subsecond precision.
     * select() is also used to wait user's input from a keyboard.
     *
     * Save "diff" since select(2) may overwrite it to {0, 0}.
     */
    struct timeval orig_diff = diff;

    int r = select(1, &readfs, NULL, NULL, &diff);
    diff = orig_diff;                    /* Restore the original diff value. */
    if ((r > 0) && FD_ISSET(0, &readfs)) /* a user hits a character? */
    {
        char c[32];

        /* If the read size is == 1, it's *probably* a human typing
         * to change ttyplay's behavior, and not the term answering
         * to a control code sent by the running program (e.g. vim)
         */
        if (read(STDIN_FILENO, c, 32) == 1)
        {
            /* drain the character */
            switch (c[0])
            {
            case '+':
            case 'f':
                speed *= 2;
                break;

            case '-':
            case 's':
                speed /= 2;
                break;

            case '1':
                speed = 1.0;
                break;
            }
        }
        drift.tv_sec = drift.tv_usec = 0;
    }
    else
    {
        struct timeval stop;
        gettimeofday(&stop, NULL);
        /* Hack to accumulate the drift */
        if ((diff.tv_sec == 0) && (diff.tv_usec == 0))
        {
            diff = timeval_diff(drift, diff);  // diff = 0 - drift.
        }
        drift = timeval_diff(diff, timeval_diff(start, stop));
    }
    return speed;
}


double ttynowait(struct timeval prev, struct timeval cur, double speed)
{
    /* do nothing */
    (void)prev;
    (void)cur;
    (void)speed;
    return 0; /* Speed isn't important. */
}


/* returns 0 on error */
int ttyread(FILE *fp, Header *h, char **buf)
{
    fpos_t pos;
    int    can_seek = fgetpos(fp, &pos) == 0;

    clearerr(fp);

    if (read_header(fp, h) == 0)
    {
        goto err;
    }

    if ((h->len <= 0) || (h->len > MAX_RECORD_LEN))
    {
        /* corrupt/invalid record length: a valid record has 1 <= len <= MAX_RECORD_LEN.
         * reject it instead of feeding a negative (huge) or implausibly large size to malloc. */
        fprintf(stderr, "invalid record length %d\n", h->len);
        return 0;
    }

    *buf = malloc(h->len);
    if (*buf == NULL)
    {
        perror("malloc");
        return 0;
    }

    if (fread_wrapper(*buf, 1, h->len, fp) != (size_t)h->len)
    {
        /* short read (truncated/partial record): free the buffer we won't use and fall through
         * to the seek-back/retry path.
         */
        free(*buf);
        *buf = NULL;
        goto err;
    }
    return 1;

err:
    if (ferror(fp))
    {
        perror("fread");
    }
    else
    {
        /* Short read. Seek back to before header, to set up for retry. */
        if (can_seek)
        {
            fsetpos(fp, &pos);
        }
    }
    return 0;
}


int ttypread(FILE *fp, Header *h, char **buf)
{
    /*
     * Read persistently just like tail -f.
     */
    while (ttyread(fp, h, buf) == 0)
    {
        struct timeval w = { 0, 250000 };
        select(0, NULL, NULL, NULL, &w);
        clearerr(fp);
    }
    return 1;
}


void ttywrite(char *buf, int len)
{
    fwrite(buf, 1, len, stdout);
}


void ttynowrite(char *buf, int len)
{
    /* do nothing */
    (void)buf;
    (void)len;
}


void ttyplay(FILE *fp, double speed, ReadFunc read_func, WriteFunc write_func, WaitFunc wait_func)
{
    int            first_time = 1;
    struct timeval prev;

    setbuf(stdout, NULL);
    setbuf(fp, NULL);

    while (1)
    {
        char   *buf;
        Header h;

        if (read_func(fp, &h, &buf) == 0)
        {
            break;
        }

        if (!first_time)
        {
            speed = wait_func(prev, h.tv, speed);
        }
        first_time = 0;

        write_func(buf, h.len);
        prev = h.tv;
        free(buf);
    }
}


void ttyskipall(FILE *fp)
{
    /*
     * Skip all records.
     */
    ttyplay(fp, 0, ttyread, ttynowrite, ttynowait);
}


void ttyplayback(FILE *fp, double speed, ReadFunc read_func, WaitFunc wait_func)
{
    (void)read_func;
    ttyplay(fp, speed, ttyread, ttywrite, wait_func);
}


void ttypeek(FILE *fp, double speed, ReadFunc read_func, WaitFunc wait_func)
{
    (void)read_func;
    (void)wait_func;
    ttyskipall(fp);
    ttyplay(fp, speed, ttypread, ttywrite, ttynowait);
}


void usage(void)
{
    printf("Usage: ttyplay [OPTION] [FILE]\n");
    printf("  -s SPEED Set speed to SPEED [1.0]\n");
    printf("  -n       No wait mode\n");
    printf("  -p       Peek another person's ttyrecord\n");
#ifdef HAVE_zstd
    printf("  -Z       Enable on-the-fly zstd decompression\n");
    printf("\nThe -Z flag is implied if the file suffix is \".zst\"\n");
#endif
    exit(EXIT_FAILURE);
}


/*
 * We do some tricks so that select(2) properly works on
 * STDIN_FILENO in ttywait().
 */
FILE *input_from_stdin(void)
{
    int fd = edup(STDIN_FILENO);

    edup2(STDOUT_FILENO, STDIN_FILENO);
    return efdopen(fd, "r");
}


int main(int argc, char **argv)
{
    double         speed     = 1.0;
    ReadFunc       read_func = ttyread;
    WaitFunc       wait_func = ttywait;
    ProcessFunc    process   = ttyplayback;
    FILE           *input    = NULL;
    struct termios old, new;

    set_progname(argv[0]);
    while (1)
    {
#ifdef HAVE_zstd
        int ch = getopt(argc, argv, "hs:npZ");
#else
        int ch = getopt(argc, argv, "hs:np");
#endif
        if (ch == EOF)
        {
            break;
        }
        switch (ch)
        {
        case 's':
            if ((optarg == NULL) || (sscanf(optarg, "%lf", &speed) != 1) || (speed <= 0))
            {
                fprintf(stderr, "-s option requires a strictly positive number\n");
                exit(EXIT_FAILURE);
            }
            break;

        case 'n':
            wait_func = ttynowait;
            break;

        case 'p':
            process = ttypeek;
            break;

#ifdef HAVE_zstd
        case 'Z':
            set_compress_mode(COMPRESS_ZSTD);
            break;
#endif

        case 'h':
        default:
            usage();
        }
    }

    if (optind < argc)
    {
        input = efopen(argv[optind], "r");
#ifdef HAVE_zstd
        if (strstr(argv[optind], ".zst") == argv[optind] + strlen(argv[optind]) - 4)
        {
            set_compress_mode(COMPRESS_ZSTD);
        }
#endif
    }
    else
    {
        input = input_from_stdin();
    }
    assert(input != NULL);

    tcgetattr(0, &old);                       /* Get current terminal state */
    new          = old;                       /* Make a copy */
    new.c_lflag &= ~(ICANON | ECHO | ECHONL); /* unbuffered, no echo */
    tcsetattr(0, TCSANOW, &new);              /* Make it current */

    process(input, speed, read_func, wait_func);
    tcsetattr(0, TCSANOW, &old);              /* Return terminal state */

    return 0;
}
