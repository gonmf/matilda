/*
Support for rotating buffers, to avoid dynamic allocating.

Use these buffers to pass data around and writing logs, places where it doesn't
have to persist for long.
*/

#include "matilda.h"

#include <stdlib.h>
#include <stdio.h>
#include <omp.h>

#include "types.h"
#include "buffer.h"

static void * buffers[NR_OF_BUFFERS];
static u8 buffers_index;
static bool buffers_inited = false;
static omp_lock_t buffers_index_lock;

static void buffers_init(){
    if(!buffers_inited)
    {
        omp_init_lock(&buffers_index_lock);
        buffers_index = 0;
        for(u8 i = 0; i < NR_OF_BUFFERS; ++i)
        {
            buffers[i] = malloc(MAX_PAGE_SIZ);
            if(buffers[i] == NULL)
            {
                fprintf(stderr, "error: buffers: system out of memory\n");
                exit(EXIT_FAILURE);
            }
        }
        buffers_inited = true;
    }
}

/*
Get a rotating buffer of MAX_PAGE_SIZ size.
*/
void * get_buffer(){
    buffers_init();

    omp_set_lock(&buffers_index_lock);
    void * ret = (void *)buffers[buffers_index];
    buffers_index = (buffers_index + 1) % NR_OF_BUFFERS;
    omp_unset_lock(&buffers_index_lock);

    return ret;
}
