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

compress_mode_t compress_mode  = 0;
long            compress_level = -1;

#ifdef HAVE_zstd
# include "compress_zstd.h"
#endif

void set_compress_mode(compress_mode_t cm)
{
    compress_mode = cm;
}


void set_compress_level(long level)
{
    compress_level = level;
}


long get_compress_level(void)
{
    return compress_level;
}


size_t fread_wrapper(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    if (compress_mode == COMPRESS_NONE)
    {
        return fread(ptr, size, nmemb, stream);
    }
#ifdef HAVE_zstd
    else if (compress_mode == COMPRESS_ZSTD)
    {
        return fread_wrapper_zstd(ptr, size, nmemb, stream);
    }
#endif
    else
    {
        fprintf(stderr, "ttyrec: unsupported compression mode (%d)\r\n", compress_mode);
        exit(1);
    }
}


size_t fwrite_wrapper(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    if (compress_mode == COMPRESS_NONE)
    {
        return fwrite(ptr, size, nmemb, stream);
    }
#ifdef HAVE_zstd
    else if (compress_mode == COMPRESS_ZSTD)
    {
        return fwrite_wrapper_zstd(ptr, size, nmemb, stream);
    }
#endif
    else
    {
        fprintf(stderr, "ttyrec: unsupported compression mode (%d)\r\n", compress_mode);
        exit(1);
    }
}


int fclose_wrapper(FILE *fp)
{
    if (compress_mode == COMPRESS_NONE)
    {
        return fclose(fp);
    }
#ifdef HAVE_zstd
    else if (compress_mode == COMPRESS_ZSTD)
    {
        return fclose_wrapper_zstd(fp);
    }
#endif
    else
    {
        fprintf(stderr, "ttyrec: unsupported compression mode (%d)\r\n", compress_mode);
        fclose(fp);
        exit(1);
    }
}
