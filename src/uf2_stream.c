/*
 * uf2_stream.c - see uf2_stream.h.
 */
#include "uf2_stream.h"

#include "storage.h"

/* 8 blocks = 4 KB: one CMD18 of 8 sectors per refill. Static rather than on
 * the stack (the main stack is 3 KB) and rather than f_malloc (by the time a
 * launch reads a file, a board without PSRAM has given most of its heap to the
 * GUI buffers). */
#define UF2_STREAM_BLOCKS 8

static uf2_block_t s_buf[UF2_STREAM_BLOCKS] __attribute__((aligned(4)));
static uint32_t    s_have;   /* blocks in s_buf      */
static uint32_t    s_next;   /* next block to hand out */
static bool        s_short;  /* last refill ended mid-block */
static bool        s_eof;

static void reset(void)
{
    s_have = s_next = 0;
    s_short = s_eof = false;
}

bool uf2_stream_open(const char *path)
{
    reset();
    return storage_open(path);
}

int uf2_stream_next(const uf2_block_t **out)
{
    if (s_next == s_have) {
        if (s_short) return -1;
        if (s_eof)   return 0;
        uint32_t got = 0;
        if (!storage_read(s_buf, sizeof(s_buf), &got)) return -1;
        s_have  = got / UF2_BLOCK_SIZE;
        s_next  = 0;
        s_short = (got % UF2_BLOCK_SIZE) != 0;
        s_eof   = got < sizeof(s_buf);
        if (s_have == 0) return s_short ? -1 : 0;
    }
    *out = &s_buf[s_next++];
    return 1;
}

bool uf2_stream_rewind(void)
{
    reset();
    return storage_rewind();
}

void uf2_stream_close(void)
{
    reset();
    storage_close();
}
