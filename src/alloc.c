/*
Guard functions over standard malloc.
*/

#include "matilda.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <omp.h>

#include "flog.h"
#include "types.h"

typedef struct __mem_link_ {
    struct __mem_link_ * next;
} mem_link;

static mem_link * queue = NULL;
static omp_lock_t queue_lock;
static bool queue_inited = false;
u32 alloced = 0;

/*
Initiate the safe allocation.
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
Allocate a small block of memory; intended for string formatting and the like.
*/
void * alloc()
{
    void * ret = NULL;

    omp_set_lock(&queue_lock);
    if(queue != NULL)
    {
        ret = queue;
        queue = queue->next;
    }else
        ++alloced;
    omp_unset_lock(&queue_lock);

    if(ret == NULL){
#if MATILDA_RELEASE_MODE
        ret = malloc(MAX_PAGE_SIZ);
        if(ret == NULL)
            flog_crit("alloc", "out of memory exception");
#else
        u8 * buf = malloc(MAX_PAGE_SIZ + 2);
        if(buf == NULL)
            flog_crit("alloc", "out of memory exception");

        /* canaries -- detection of out of bounds writes */
        buf[0] = buf[MAX_PAGE_SIZ + 1] = 255;

        ret = buf + 1;
#endif
    }

    /*
    Ensure that if used for string concatenation then gibberish is never
    returned.
    */
    ((char *)ret)[0] = 0;

    return ret;
}

/*
Releases a previously allocated block of memory.
*/
void release(void * ptr)
{
#if !MATILDA_RELEASE_MODE
    /* canaries -- detection of out of bounds writes */
    u8 * s = (u8 *)ptr;
    if(s[-1] != 255 || s[MAX_PAGE_SIZ] != 255){
        flog_crit("alloc", "canary violation detected");
    }
#endif

    omp_set_lock(&queue_lock);
    mem_link * l = (mem_link *)ptr;
    l->next = queue;
    queue = l;
    omp_unset_lock(&queue_lock);
}
