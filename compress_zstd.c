// vim: noai:ts=4:sw=4:expandtab:

/* Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 * Copyright 2019 The ovh-ttyrec Authors. All rights reserved.
 */

#include "compress.h"
#include "compress_zstd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zstd.h>

static ZSTD_CStream *cstream    = NULL;
static size_t       buffOutSize = 0;
static void         *buffOut;
static long         zstd_max_flush_seconds = ZSTD_MAX_FLUSH_SECONDS_DEFAULT;

void zstd_set_max_flush(long seconds)
{
    zstd_max_flush_seconds = seconds;
}


size_t fwrite_wrapper_zstd(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    static time_t last_sync      = 0;
    long          compress_level = get_compress_level();

    if (cstream == NULL)
    {
        cstream = ZSTD_createCStream();
        if (cstream == NULL)
        {
            fprintf(stderr, "ZSTD_createCStream() error\r\n");
            exit(10);
        }

        if (compress_level < 0)
        {
            compress_level = 3;
        }
        size_t const initResult = ZSTD_initCStream(cstream, compress_level);
        if (ZSTD_isError(initResult))
        {
            fprintf(stderr, "ZSTD_initCStream() error: %s\r\n", ZSTD_getErrorName(initResult));
            exit(11);
        }

        if (buffOutSize == 0)
        {
            buffOutSize = ZSTD_CStreamOutSize();
            buffOut     = malloc(buffOutSize);
            if (buffOut == NULL)
            {
                fprintf(stderr, "couldn't malloc() zstd out buffer\r\n");
                exit(12);
            }
        }

        last_sync = time(NULL);
    }

    size_t        written = 0;
    ZSTD_inBuffer input   = { ptr, size * nmemb, 0 };
    while (input.pos < input.size)
    {
        ZSTD_outBuffer output = { buffOut, buffOutSize, 0 };
        size_t         toRead = ZSTD_compressStream(cstream, &output, &input); /* toRead is guaranteed to be <= ZSTD_CStreamInSize() */
        if (ZSTD_isError(toRead))
        {
            fprintf(stderr, "ZSTD_compressStream() error: %s\r\n", ZSTD_getErrorName(toRead));
            exit(13);
        }
        size_t thisWritten = fwrite(buffOut, 1, output.pos, stream);
        if (thisWritten != output.pos)
        {
            return thisWritten;     // error or eof, pass to caller
        }
        written += thisWritten;
    }
    //fprintf(stderr, "[zstd:nbwr=%lu]", written);
    // if we actually did write data to disk (instead of just compressing in memory),
    // then we can reset last_sync
    if (written > 0)
    {
        last_sync = time(NULL);
        //fprintf(stderr, "[zstd:rst]");
    }
    // otherwise, check for last sync time. if it's > X seconds, force zstd to flush its buffers
    // and write to disk. we don't want to loose data from almost-idle sessions in case of server crash
    else if (last_sync + zstd_max_flush_seconds < time(NULL))
    {
        ZSTD_outBuffer output = { buffOut, buffOutSize, 0 };
        written = ZSTD_flushStream(cstream, &output);
        if (ZSTD_isError(written))
        {
            fprintf(stderr, "ZSTD_flushStream() error: %s\r\n", ZSTD_getErrorName(written));
            exit(14);
        }
        //fprintf(stderr, "[zstd:tmoutflushed=%lu]", output.pos);
        written = fwrite(buffOut, 1, output.pos, stream);
        //fprintf(stderr, "[zstd:tmoutnbwr=%lu]", written);
        last_sync = time(NULL);
    }
    return written;
}


int fclose_wrapper_zstd(FILE *fp)
{
    if (cstream != NULL)
    {
        ZSTD_outBuffer output           = { buffOut, buffOutSize, 0 };
        size_t const   remainingToFlush = ZSTD_endStream(cstream, &output);                     /* close frame */
        if (remainingToFlush)
        {
            fprintf(stderr, "error: zstd not fully flushed\r\n");
        }
        fwrite(buffOut, 1, output.pos, fp);
        //fprintf(stderr, "[closezstd:written=%lu]", output.pos);
        ZSTD_freeCStream(cstream);
        cstream = NULL;
    }
    return fclose(fp);
}


size_t fread_wrapper_zstd(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    // input: compressed data read from file
    // output: decompressed data from (a part of) buffin
    // buffoutptr: pointing to decompressed not-yet-returned to caller data (remaining bytes is buffoutptrlen)

    static ZSTD_inBuffer input = { NULL, 0, 0 };

    static size_t         buffOutSize;
    static ZSTD_outBuffer output = { NULL, 0, 0 };

    static char   *buffOutPtr   = NULL;
    static size_t buffOutPtrLen = 0;       // number of valid not-yet-returned bytes after ptr

    static ZSTD_DStream *dstream = NULL;
    // static because ZSTD_initDStream return the first recommended input size, we'll use ot for first fread()
    static size_t toRead;

    size_t remainingBytesToReturn = size * nmemb;
    char   *returnData            = (char *)ptr;

    // init dstream if needed (first call only)
    if (dstream == NULL)
    {
        dstream = ZSTD_createDStream();
        toRead  = ZSTD_initDStream(dstream);

        input.src = malloc(ZSTD_DStreamInSize());

        buffOutSize = ZSTD_DStreamOutSize();
        output.dst  = malloc(buffOutSize);
    }

    // do we have remaining decompressed data from a previous call, ready to be returned?
GOTDATA:
    if (buffOutPtrLen > 0)
    {
        if (buffOutPtrLen >= remainingBytesToReturn)
        {
            // easy: we already have all the wanted data in the previous runs buffer
            // so we'll just consume data from it and return
            memcpy(returnData, buffOutPtr, remainingBytesToReturn);
            buffOutPtrLen -= remainingBytesToReturn;
            buffOutPtr    += remainingBytesToReturn;
            return nmemb;
        }
        else
        {
            // we have SOME data in the previous runs buffer, use it
            memcpy(returnData, buffOutPtr, buffOutPtrLen);
            returnData             += buffOutPtrLen;
            remainingBytesToReturn -= buffOutPtrLen;
            buffOutPtrLen           = 0;
            buffOutPtr              = NULL;
        }
    }

    // if we're here, we don't have any data left in buffOutPtr, and the caller wants more data
    // but maybe we still have not-yet-decompressed data from a previously read compressed chunk?
DECOMPRESS:
    if (input.pos < input.size)
    {
        output.pos  = 0;
        output.size = buffOutSize;
        toRead      = ZSTD_decompressStream(dstream, &output, &input);      /* toRead : size of next compressed block */
        if (ZSTD_isError(toRead))
        {
            fprintf(stderr, "ZSTD_decompressStream() error: %s\r\n", ZSTD_getErrorName(toRead));
            exit(16);
        }
        buffOutPtr    = output.dst;      // aka buffOut
        buffOutPtrLen = output.pos;
        if (buffOutPtrLen == 0)
        {
            // ok this is an empty frame (or beggining of zst stream), read again
            goto DECOMPRESS;
        }
        goto GOTDATA;
    }
    // nope we don't, alright, decompress a new chunk then
    else
    {
        size_t read = fread((void *)input.src, 1, toRead, stream);
        if (read == 0)
        {
            // eof or error, return it
            return 0;
        }
        input.size = read;
        input.pos  = 0;
        goto DECOMPRESS;
    }
}
