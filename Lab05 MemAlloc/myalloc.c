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
    printf("...pagesize is %d bytes", pagesize);
    if(pagesize != size){
        printf("...adjusting size with page boundaries");
        size = ((size + pagesize - 1) / pagesize) * pagesize;
    }

    printf("...mapping arena with mmap()");
    _arena_head = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    arena_size = size;
    printf("...arena starts at 0x%lx", (unsigned long)_arena_head);
    printf("...arena ends at 0x%lx", (unsigned long)(_arena_head + size));

    printf("...initializing header for initial free chunk");
    printf("...header size is %zu bytes", sizeof(node_t));
    // Note: size represents the number of bytes available for allocation and does
    // not include the header bytes. 
    _arena_head->size = size - sizeof(node_t);
    _arena_head->is_free = 1;
    _arena_head->bwd = NULL;
    _arena_head->fwd = NULL;

    return size;
}

int mydestroy(){
    if (_arena_head == MAP_FAILED){
        _arena_head = NULL;
        arena_size = 0;
        printf("...error: cannot destroy unintialized arena. Setting error status");
        return ERR_UNINITIALIZED;
    }
    printf("...unmapping arena with munmap()");
    munmap(_arena_head, arena_size);
    return 0;
}

void* myalloc(size_t size){
    if (_arena_head == NULL || _arena_head == MAP_FAILED){
        printf("Error: Unitialized. Setting status code");
        statusno = ERR_UNINITIALIZED;
        return NULL;
    }
    printf("...looking for free chunk of >= %zu bytes\n", size);
    node_t *best = NULL;
    //First Fit approach
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
    printf("...found free chunk of %zu bytes with header at 0x%p\n", best->size, (void*)best);
    printf("...free chunk->fwd currently points to %p\n",(void*)best->fwd);
    printf("...free chunk->bwd currently points to %p\n",(void*)best->bwd);
    //calculate values
    char *base = (char*)best;
    char *new_header_addr = base + (sizeof(node_t) + size);
    size_t remainder = best->size - size;
    
    //Create new chunk to indicate remaining space
    // Also check to make sure remaining space is not used up
    printf("...checking if splitting is required\n");
    //TODO check splitting
    
    printf("...updating chunk header at %p\n",(void*)best);
    if(remainder >= (sizeof(node_t) + sizeof(void*))){
        printf("...splitting free chunk\n");
        node_t *new_header = (node_t*)new_header_addr;
        new_header->bwd = best;
        new_header->fwd = best->fwd;
        new_header->size = remainder - sizeof(node_t);
        new_header->is_free = 1;
        
        //Update previous header
        best->size = size;
        best->is_free = 0;
        best->fwd = new_header;
        if(new_header->fwd) new_header->fwd->bwd = new_header;
    }
    else {
        printf("...splitting not required\n");
        best->is_free = 0;
    }
    printf("...being careful with my pointer arthimetic and void pointer casting\n");
    printf("...allocation starts at %p\n", (void*)((char*)best + sizeof(node_t)) );
    return (void*)((char*)best + sizeof(node_t));
}

void myfree(void *ptr){
    //Null ptr
    if(!ptr){
        statusno = ERR_UNINITIALIZED;
        return;
    }
    node_t *hptr = (node_t*)(((void*)ptr) - sizeof(node_t));
    hptr->is_free = 1;
    return;
}