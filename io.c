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

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "io.h"
#include "ttyrec.h"
#include "compress.h"

#define SWAP_ENDIAN(val)                                             \
        ((unsigned int)(                                                 \
             (((unsigned int)(val) & (unsigned int)0x000000ffU) << 24) | \
             (((unsigned int)(val) & (unsigned int)0x0000ff00U) << 8) |  \
             (((unsigned int)(val) & (unsigned int)0x00ff0000U) >> 8) |  \
             (((unsigned int)(val) & (unsigned int)0xff000000U) >> 24)))

static int is_little_endian(void)
{
    static int retval = -1;

    if (retval == -1)
    {
        int  n   = 1;
        char *p  = (char *)&n;
        char x[] = { 1, 0, 0, 0 };

        _Static_assert(sizeof(int) == 4, "Check relies on int size");

        if (memcmp(p, x, 4) == 0)
        {
            retval = 1;
        }
        else
        {
            retval = 0;
        }
    }

    return retval;
}


static uint32_t convert_to_little_endian(uint32_t x)
{
    if (is_little_endian())
    {
        return x;
    }
    else
    {
        return SWAP_ENDIAN(x);
    }
}


int read_header(FILE *fp, Header *h)
{
    uint32_t buf[3], raw_usec;

    if (fread_wrapper(buf, sizeof(uint32_t), 3, fp) != 3)
    {
        return 0;
    }

    raw_usec      = convert_to_little_endian(buf[1]);
    h->tv.tv_sec  = convert_to_little_endian(buf[0]) | ((raw_usec & 0xfff00000ull) << 12);
    h->tv.tv_usec = raw_usec & 0x000fffffU;
    h->len        = convert_to_little_endian(buf[2]);

    return 1;
}


int write_header(FILE *fp, Header *h)
{
    uint32_t buf[3];

    // The reasonable range of tv_usec is [0, 999999], which is [0, 0x00`0F`42`3F]
    // Thus, we can stuff 3 nibbles from tv_sec into the top bits, giving us a range of dates up to around year 559444
    buf[0] = convert_to_little_endian(h->tv.tv_sec & 0xffffffffU);
    buf[1] = convert_to_little_endian(h->tv.tv_usec | ((h->tv.tv_sec & 0x00000fff00000000ull) >> 12));
    buf[2] = convert_to_little_endian(h->len);

    if (fwrite_wrapper(buf, sizeof(uint32_t), 3, fp) == 0)
    {
        return 0;
    }

    return 1;
}


static const char *progname = "";
void set_progname(const char *name)
{
    progname = name;
}


FILE *efopen(const char *path, const char *mode)
{
    FILE *fp = fopen(path, mode);

    if (fp == NULL)
    {
        fprintf(stderr, "%s: %s: %s\n", progname, path, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return fp;
}


int edup(int oldfd)
{
    int fd = dup(oldfd);

    if (fd == -1)
    {
        fprintf(stderr, "%s: dup failed: %s\n", progname, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return fd;
}


int edup2(int oldfd, int newfd)
{
    int fd = dup2(oldfd, newfd);

    if (fd == -1)
    {
        fprintf(stderr, "%s: dup2 failed: %s\n", progname, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return fd;
}


FILE *efdopen(int fd, const char *mode)
{
    FILE *fp = fdopen(fd, mode);

    if (fp == NULL)
    {
        fprintf(stderr, "%s: fdopen failed: %s\n", progname, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return fp;
}
