/*
 * uf2_stream.h - Sequential reader for the 512-byte blocks of a .uf2 file,
 *                a chunk of blocks per storage read.
 *
 * Reading one block per storage_read() makes FatFs fetch one sector per SD
 * command (CMD17). Reading several blocks at once lets it use a single
 * multi-sector read (CMD18) for each chunk, which is what the whole-file walks
 * (fingerprint, validate, program) spend most of their time on.
 *
 * One stream at a time: the chunk buffer is static, and the stream sits on
 * storage.h's single open file. The walks that use it never overlap.
 */
#ifndef UF2_STREAM_H
#define UF2_STREAM_H

#include <stdbool.h>
#include <stdint.h>

#include "uf2_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Open `path` via storage_open() and position at block 0. */
bool uf2_stream_open(const char *path);

/* Point *out at the next block, valid until the next call. Returns 1 for a
 * block, 0 at a clean end of file, -1 on a read error or a short (truncated)
 * trailing block. No magic checks: callers judge the contents. */
int uf2_stream_next(const uf2_block_t **out);

/* Back to block 0 of the same file. */
bool uf2_stream_rewind(void);

void uf2_stream_close(void);

#ifdef __cplusplus
}
#endif

#endif /* UF2_STREAM_H */
