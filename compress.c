// vim: noai:ts=4:sw=4:expandtab:

/* Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 * Copyright 2019 The ovh-ttyrec Authors. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "compress.h"
#include "configure.h"

#ifdef HAVE_zstd
# include "compress_zstd.h"
#endif

size_t (*fread_wrapper)(void *ptr, size_t size, size_t nmemb, FILE *stream) = fread;
size_t (*fwrite_wrapper)(const void *ptr, size_t size, size_t nmemb, FILE *stream) = fwrite;
int    (*fclose_wrapper)(FILE *fp) = fclose;

static long compress_level = -1;

int set_compress_mode(compress_mode_t cm)
{
    switch (cm)
    {
    case COMPRESS_NONE:
        fread_wrapper  = fread;
        fwrite_wrapper = fwrite;
        fclose_wrapper = fclose;
        break;

#ifdef HAVE_zstd
    case COMPRESS_ZSTD:
        fread_wrapper  = fread_wrapper_zstd;
        fwrite_wrapper = fwrite_wrapper_zstd;
        fclose_wrapper = fclose_wrapper_zstd;
        break;
#endif

    default:
        fprintf(stderr, "ttyrec: unsupported compression mode\r\n");
        return 1;
    }
    return 0;
}


void set_compress_level(long level)
{
    compress_level = level;
}


long get_compress_level(void)
{
    return compress_level;
}
