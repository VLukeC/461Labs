#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "myalloc.h"

void *_arena_start = NULL;
size_t size;

int myinit(size_t size)
{
    printf("Initializing arena:\n");
    printf("...requested size %lu bytes\n", size);

    if (size > (size_t)MAX_ARENA_SIZE)
    {
        _arena_start = MAP_FAILED;
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
    _arena_start = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);


    printf("...arena starts at 0x%lx", (unsigned long)_arena_start);
    printf("...arena ends at 0x%lx", (unsigned long)(_arena_start + size));

    size_t header_size = 32;
    printf("...initializing header for initial free chunk");
    printf("...header size is %zu bytes", header_size);
    memset(_arena_start, 0, header_size);

    return size;
}

int mydestroy(){
    if (_arena_start == MAP_FAILED){
        _arena_start = NULL;
        size = 0;
        printf("...error: cannot destroy unintialized arena. Setting error status");
        return ERR_UNINITIALIZED;
    }
    printf("...unmapping arena with munmap()");
    munmap(_arena_start, size);
    return 0;
}