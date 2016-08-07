/*
Guard functions over standard malloc.
*/

#include "matilda.h"

#include <stdlib.h>
#include <omp.h>

#include "flog.h"
#include "types.h"

typedef struct __mem_link_ {
    struct __mem_link_ * next;
} mem_link;

static mem_link * queue = NULL;
static omp_lock_t queue_lock;
static bool queue_inited = false;

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
    }
    omp_unset_lock(&queue_lock);

    if(ret == NULL){
        ret = malloc(MAX_PAGE_SIZ);
        if(ret == NULL)
            flog_crit("alloc", "out of memory exception");
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
    mem_link * l = (mem_link *)ptr;
    l->next = queue;
    queue = l;
}
