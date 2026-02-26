#include "umalloc.h"
#include "csbrk.h"
#include <stdio.h>
#include <assert.h>
#include "ansicolors.h"



mem_block_header_t *free_heads[BIN_COUNT];
size_t bin_limits[BIN_COUNT - 1];

typedef struct chunk_header_struct
{
    size_t size;
    struct chunk_header_struct *next;
} chunk_header_t;

static chunk_header_t *chunk_list = NULL;
static int chunk_count = 0;

bool is_allocated(mem_block_header_t *block)
{
    assert(block != NULL);
    return block->block_metadata & 0x1;
}

void allocate(mem_block_header_t *block)
{
    assert(block != NULL);
    block->block_metadata |= 0x1;
}

void deallocate(mem_block_header_t *block)
{
    assert(block != NULL);
    block->block_metadata &= ~(size_t)0x1;
}

size_t get_size(mem_block_header_t *block)
{
    assert(block != NULL);
    return block->block_metadata & ~(size_t)(ALIGNMENT - 1);
}

mem_block_header_t *get_next(mem_block_header_t *block)
{
    assert(block != NULL);
    return block->next;
}

void *get_payload(mem_block_header_t *block)
{
    assert(block != NULL);
    return (void *)(block + 1);
}

mem_block_header_t *get_header(void *payload)
{
    assert(payload != NULL);
    return ((mem_block_header_t *)payload) - 1;
}

void set_block_metadata(mem_block_header_t *block, size_t size, bool alloc)
{
    assert(block != NULL);
    assert((size & (ALIGNMENT - 1)) == 0);
    block->block_metadata = size | (alloc ? 1 : 0);
}

size_t select_bin(size_t size)
{
    for (size_t i = 0; i < BIN_COUNT - 1; i++)
    {
        if (size <= bin_limits[i])
        {
            return i;
        }
    }
    return BIN_COUNT - 1;
}

void set_bin_limits()
{
    bin_limits[0] = 48;
    bin_limits[1] = 128;
    bin_limits[2] = 512;
    bin_limits[3] = 2048;
    bin_limits[4] = 4096;
}

static void insert_free_block(mem_block_header_t *block)
{
    assert(block != NULL);
    assert(!is_allocated(block));

    size_t bin = select_bin(get_size(block));
    size_t bsz = get_size(block);

    mem_block_header_t **cur = &free_heads[bin];
    while (*cur != NULL && get_size(*cur) <= bsz)
    {
        cur = &((*cur)->next);
    }
    block->next = *cur;
    *cur = block;
}

#define MIN_BLOCK_SIZE 32
#define CHUNK_HEADER_SIZE (ALIGN(sizeof(chunk_header_t)))

mem_block_header_t *split(mem_block_header_t *block, size_t new_block_size)
{
    assert(block != NULL);
    assert(!is_allocated(block));
    assert((new_block_size & (ALIGNMENT - 1)) == 0);

    size_t total = get_size(block);
    size_t remainder = total - new_block_size;

    if (remainder < MIN_BLOCK_SIZE)
    {
        return block;
    }

    set_block_metadata(block, new_block_size, false);

    mem_block_header_t *rem = (mem_block_header_t *)((char *)block + new_block_size);
    set_block_metadata(rem, remainder, false);
    rem->next = NULL;

    insert_free_block(rem);
    return block;
}

mem_block_header_t *extend(size_t size)
{
    size_t needed = CHUNK_HEADER_SIZE + size;
    size_t request;

    if (needed < (size_t)PAGESIZE)
    {
        request = PAGESIZE;
    }
    else if (needed >= 1024 * 1024)
    {
        request = ALIGN(needed);
    }
    else
    {
        request = ALIGN(needed + PAGESIZE);
    }

    void *raw = csbrk((intptr_t)request);
    if (raw == NULL)
    {
        request = ALIGN(needed);
        raw = csbrk((intptr_t)request);
        if (raw == NULL)
            return NULL;
    }

    if (chunk_count >= 1024)
    {
        return NULL;
    }

    chunk_header_t *ch = (chunk_header_t *)raw;
    ch->size = request;
    ch->next = chunk_list;
    chunk_list = ch;
    chunk_count++;

    mem_block_header_t *block =
        (mem_block_header_t *)((char *)raw + CHUNK_HEADER_SIZE);
    size_t block_size = request - CHUNK_HEADER_SIZE;
    block_size = block_size & ~(size_t)(ALIGNMENT - 1);

    if (block_size < size)
    {
        chunk_count--;
        chunk_list = ch->next;
        return NULL;
    }

    set_block_metadata(block, block_size, false);
    block->next = NULL;

    return block;
}

mem_block_header_t *find(size_t total_size)
{
    size_t start_bin = select_bin(total_size);

    for (size_t bin = start_bin; bin < (size_t)BIN_COUNT; bin++)
    {
        mem_block_header_t **cur = &free_heads[bin];

        while (*cur != NULL)
        {
            size_t cur_size = get_size(*cur);
            if (cur_size >= total_size)
            {
                mem_block_header_t *found = *cur;
                *cur = found->next;
                found->next = NULL;
                return found;
            }
            cur = &((*cur)->next);
        }
    }
    return NULL;
}

bool coalesce()
{
    if (chunk_list == NULL)
        return false;

    for (int i = 0; i < BIN_COUNT; i++)
    {
        free_heads[i] = NULL;
    }

    bool merged_any = false;

    chunk_header_t *ch = chunk_list;
    while (ch != NULL)
    {
        char *chunk_start = (char *)ch + CHUNK_HEADER_SIZE;
        char *chunk_end = (char *)ch + ch->size;
        mem_block_header_t *cur = (mem_block_header_t *)chunk_start;

        while ((char *)cur < chunk_end)
        {
            size_t cur_size = get_size(cur);
            if (cur_size == 0)
                break;

            if (!is_allocated(cur))
            {
                mem_block_header_t *next_blk =
                    (mem_block_header_t *)((char *)cur + cur_size);

                while ((char *)next_blk < chunk_end && !is_allocated(next_blk))
                {
                    size_t next_size = get_size(next_blk);
                    if (next_size == 0)
                        break;
                    cur_size += next_size;
                    set_block_metadata(cur, cur_size, false);
                    cur->next = NULL;
                    merged_any = true;
                    next_blk = (mem_block_header_t *)((char *)cur + cur_size);
                }

                insert_free_block(cur);
            }

            cur = (mem_block_header_t *)((char *)cur + cur_size);
        }

        ch = ch->next;
    }

    return merged_any;
}

int uinit()
{
    set_bin_limits();

    for (int i = 0; i < BIN_COUNT; i++)
    {
        free_heads[i] = NULL;
    }

    chunk_list = NULL;
    chunk_count = 0;

    mem_block_header_t *initial = extend(PAGESIZE);
    if (initial == NULL)
        return -1;

    insert_free_block(initial);
    return 0;
}

void *umalloc(size_t size)
{
    size_t total = ALIGN(sizeof(mem_block_header_t) + size);
    if (total < MIN_BLOCK_SIZE)
        total = MIN_BLOCK_SIZE;

    mem_block_header_t *block = find(total);

    if (block == NULL)
    {
        coalesce();
        block = find(total);
    }

    if (block == NULL)
    {
        mem_block_header_t *new_mem = extend(total);
        if (new_mem == NULL)
            return NULL;
        block = new_mem;
    }

    block = split(block, total);
    allocate(block);
    block->next = NULL;
    return get_payload(block);
}

void ufree(void *ptr)
{
    if (ptr == NULL)
        return;

    mem_block_header_t *block = get_header(ptr);
    assert(is_allocated(block));

    deallocate(block);
    block->next = NULL;
    insert_free_block(block);
}
