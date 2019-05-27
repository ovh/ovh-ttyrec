#ifndef __TTYREC_COMPRESS_ZSTD_H__
#define __TTYREC_COMPRESS_ZSTD_H__

#include <stdio.h>

#define ZSTD_MAX_FLUSH_SECONDS_DEFAULT    15

size_t fread_wrapper_zstd(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite_wrapper_zstd(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fclose_wrapper_zstd(FILE *fp);
void zstd_set_max_flush(long seconds);

#endif
