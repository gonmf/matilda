/*
Fast memory allocation layer above standard malloc.

These are meant to inexpensively allocate buffers for string operations.

They are thread-safe, fast, with canary values used (in debug mode) to ensure
memory is correctly freed and written to.

If you are need to perform recursive operations then use malloc/free. Releasing
these buffers does not actually free the underlying memory to be used by other
programs.
*/

#include "matilda.h"

#include <stdio.h>
#include <stdlib.h>
#include <omp.h>

#include "flog.h"
#include "types.h"

typedef struct __mem_link_ {
    struct __mem_link_ * next;
} mem_link;

#define HEAD_USED 251
#define HEAD_FREE 252
#define TAIL_USED 253
#define TAIL_FREE 254

static mem_link * queue = NULL;
static omp_lock_t queue_lock;
static bool queue_inited = false;

#if !MATILDA_RELEASE_MODE
static u16 concurrent_allocs = 0;
#define WARN_CONCURRENT_ALLOCS 16
#endif


/*
Initiate the safe allocation functions.
*/
void alloc_init()
{
    if(!queue_inited)
    {
        omp_init_lock(&queue_lock);
        queue_inited = true;
    }
}

/*
Allocate a block of exactly MAX_PAGE_SIZ.
Thread-safe.
*/
void * alloc()
{
    void * ret = NULL;

    omp_set_lock(&queue_lock);

#if !MATILDA_RELEASE_MODE
    concurrent_allocs++;
    if(concurrent_allocs >= WARN_CONCURRENT_ALLOCS)
    {
        fprintf(stderr, "alloc: suspicious memory allocations number (%u)\n",
            concurrent_allocs);
    }
#endif

    if(queue != NULL)
    {
        ret = queue;
        queue = queue->next;
    }
    omp_unset_lock(&queue_lock);

    if(ret == NULL){
#if MATILDA_RELEASE_MODE
        ret = malloc(MAX_PAGE_SIZ);
        if(ret == NULL)
        {
            fprintf(stderr, "alloc: out of memory exception\n");
            exit(EXIT_FAILURE);
        }
#else
        u8 * buf = malloc(MAX_PAGE_SIZ + 2);
        if(buf == NULL)
        {
            fprintf(stderr, "alloc: out of memory exception\n");
            exit(EXIT_FAILURE);
        }

        /* canaries -- detection of out of bounds writes */
        buf[0] = HEAD_FREE;
        buf[MAX_PAGE_SIZ + 1] = TAIL_FREE;

        ret = buf + 1;
#endif
    }

    /*
    Ensure that if used for string concatenation then gibberish is never
    returned.
    */
#if !MATILDA_RELEASE_MODE
    if(((u8 *)ret)[-1] != HEAD_FREE || ((u8 *)ret)[MAX_PAGE_SIZ] != TAIL_FREE)
    {
        fprintf(stderr, "memory corruption detected; check for repeated release\
s, rolling block releasing or writes past bounds (1)\n");
        exit(EXIT_FAILURE);
    }
    /* change the canary */
    ((u8 *)ret)[-1] = HEAD_USED;
    ((u8 *)ret)[MAX_PAGE_SIZ] = TAIL_USED;
#endif
    ((char *)ret)[0] = 0;
    return ret;
}

/*
Releases a previously allocated block of memory, to be used again in later
calls. Does not free the memory.
Thread-safe.
*/
void release(void * ptr)
{
#if !MATILDA_RELEASE_MODE
    /* canaries -- detection of out of bounds writes */
    u8 * s = (u8 *)ptr;
    if(s[-1] != HEAD_USED || s[MAX_PAGE_SIZ] != TAIL_USED)
    {
        fprintf(stderr, "memory corruption detected; check for repeated release\
s, rolling block releasing or writes past bounds (2)\n");
        exit(EXIT_FAILURE);
    }
    s[-1] = HEAD_FREE;
    s[MAX_PAGE_SIZ] = TAIL_FREE;
#endif

    omp_set_lock(&queue_lock);
    mem_link * l = (mem_link *)ptr;
    l->next = queue;
    queue = l;
#if !MATILDA_RELEASE_MODE
    --concurrent_allocs;
#endif
    omp_unset_lock(&queue_lock);
}
