#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "myalloc.h"

node_t *_arena_head = NULL;
static size_t arena_size;
int statusno = 0;

int myinit(size_t size)
{
    printf("Initializing arena:\n");
    printf("...requested size %lu bytes\n", size);

    if (size > (size_t)MAX_ARENA_SIZE)
    {
        _arena_head = MAP_FAILED;
        printf("...error: requested size larger than MAX_ARENA_SIZE (%d)\n", MAX_ARENA_SIZE);
        return ERR_BAD_ARGUMENTS;
    }

    int pagesize = getpagesize();
    printf("...pagesize is %d bytes\n", pagesize);
    if (size % pagesize != 0) {
        printf("...adjusting size with page boundaries\n");
        size = ((size + pagesize - 1) / pagesize) * pagesize;
    }

    printf("...mapping arena with mmap()\n");
    _arena_head = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    arena_size = size;
    printf("...arena starts at 0x%lx\n", (unsigned long)_arena_head);
    printf("...arena ends at 0x%lx\n", (unsigned long)((char*)_arena_head + size));

    printf("...initializing header for initial free chunk\n");
    printf("...header size is %zu bytes\n", sizeof(node_t));
    // Note: size represents the number of bytes available for allocation and does
    // not include the header bytes.
    _arena_head->size = size - sizeof(node_t);
    _arena_head->is_free = 1;
    _arena_head->bwd = NULL;
    _arena_head->fwd = NULL;

    return (int)size;
}

int mydestroy(){
    printf("Destroying Arena:\n");
    if (_arena_head == NULL || _arena_head == MAP_FAILED){
        _arena_head = NULL;
        arena_size = 0;
        printf("...error: cannot destroy unintialized arena. Setting error status\n");
        return ERR_UNINITIALIZED;
    }
    printf("...unmapping arena with munmap()\n");
    munmap(_arena_head, arena_size);
    _arena_head = NULL;
    arena_size = 0;
    return 0;
}

void* myalloc(size_t size){
    printf("Allocating memory:\n");
    if (_arena_head == NULL || _arena_head == MAP_FAILED){
        printf("Error: Unitialized. Setting status code\n");
        statusno = ERR_UNINITIALIZED;
        return NULL;
    }
    printf("...looking for free chunk of >= %zu bytes\n", size);

    node_t *best = NULL;
    // First Fit approach
    for (node_t *p = _arena_head; p; p = p->fwd) {
        if (p->is_free && p->size >= size){
            best = p;
            break;
        }
    }

    if (!best){
        statusno = ERR_OUT_OF_MEMORY;
        return NULL;
    }

    printf("...found free chunk of %zu bytes with header at %p\n", best->size, (void*)best);
    printf("...free chunk->fwd currently points to %p\n",(void*)best->fwd);
    printf("...free chunk->bwd currently points to %p\n",(void*)best->bwd);

    // calculate values
    char *base = (char*)best;
    char *new_header_addr = base + (sizeof(node_t) + size);
    size_t remainder = best->size - size;

    // Create new chunk to indicate remaining space
    // Also check to make sure remaining space is not used up
    printf("...checking if splitting is required\n");

    printf("...updating chunk header at %p\n",(void*)best);
    if (remainder >= (sizeof(node_t) + sizeof(void*))){
        printf("...splitting free chunk\n");
        node_t *new_header = (node_t*)new_header_addr;
        new_header->bwd = best;
        new_header->fwd = best->fwd;
        new_header->size = remainder - sizeof(node_t);
        new_header->is_free = 1;

        // Update previous header
        best->size = size;
        best->is_free = 0;
        best->fwd = new_header;
        if (new_header->fwd) new_header->fwd->bwd = new_header;
    } else {
        printf("...splitting not required\n");
        best->is_free = 0;
    }

    printf("...being careful with my pointer arthimetic and void pointer casting\n");
    printf("...allocation starts at %p\n", (void*)((char*)best + sizeof(node_t)) );
    return (void*)((char*)best + sizeof(node_t));
}

void myfree(void *ptr){
    printf("Freeing allocated memory:\n");
    // Null ptr
    if (!ptr){
        statusno = ERR_UNINITIALIZED;
        return;
    }
    printf("...supplied pointer %p:\n",(void*)ptr);
    printf("...being careful with my pointer arthimetic and void pointer casting\n");

    node_t *hptr = (node_t *)((char*)ptr - sizeof(node_t));
    printf("...accessing chunk header at %p\n", (void*)hptr);
    size_t chunk_size = hptr->size;
    printf("...chunk of size %ld\n",(long)chunk_size);
    printf("...checking if coalescing is needed\n");

    // mark current chunk free
    hptr->is_free = 1;

    int did_coalesce = 0;

    // coalesce with next if adjacent and free
    node_t *next = hptr->fwd;
    if (next && next->is_free &&
        (char*)next == (char*)hptr + sizeof(node_t) + hptr->size)
    {
        printf("...coalescing with next chunk\n");
        hptr->size += sizeof(node_t) + next->size;
        hptr->fwd = next->fwd;
        if (hptr->fwd) hptr->fwd->bwd = hptr;
        did_coalesce = 1;
    }

    // coalesce with previous if adjacent and free
    node_t *prev = hptr->bwd;
    if (prev && prev->is_free &&
        (char*)hptr == (char*)prev + sizeof(node_t) + prev->size)
    {
        printf("...coalescing with previous chunk\n");
        prev->size += sizeof(node_t) + hptr->size;
        prev->fwd = hptr->fwd;
        if (prev->fwd) prev->fwd->bwd = prev;
        hptr = prev;
        did_coalesce = 1;
    }

    if (!did_coalesce){
        printf("...coalescing not needed.\n");
    }

    return;
}
