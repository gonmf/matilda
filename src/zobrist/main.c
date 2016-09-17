#include "matilda.h"

#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "alloc.h"
#include "constants.h"
#include "engine.h"
#include "randg.h"
#include "timem.h"
#include "types.h"
#include "version.h"


static u8 count_bits(
    u64 v
){
    u8 ret = 0;
    while(v){
        if(v & 1)
            ++ret;
        v /= 2;
    }
    return ret;
}

int main(
    int argc,
    char * argv[]
){
    if(argc == 2 && strcmp(argv[1], "-version") == 0)
    {
        fprintf(stderr, "matilda %s\n", MATILDA_VERSION);
        return 0;
    }
    else
        if(argc > 1)
        {
            fprintf(stderr, "usage: %s [-version]\n", argv[0]);
            return 0;
        }

    alloc_init();
    assert_data_folder_exists();

    fprintf(stderr, "This process aims to reduce the bit distribution variance \
of the data.\nWhen you are satisfied press ENTER\n\n");

    u64 iv[TOTAL_BOARD_SIZ][2];
    memset(iv, 0, TOTAL_BOARD_SIZ * sizeof(u64));

    u32 table_size = TOTAL_BOARD_SIZ * 2;
    u64 * table = (u64 *)malloc(table_size * sizeof(u64));
    u32 bits[64];
    double best_variance = 999999.0;
    fd_set readfs;
    memset(&readfs, 0, sizeof(fd_set));

    while(best_variance > 0.0)
    {
        rand_reinit();
        for(u32 attempts = 0; attempts < 100; ++attempts)
        {
            for(u32 i = 0; i < table_size; ++i)
            {
                do
                {
                    table[i] = 0;
                    for(u32 j = 0; j < 64; ++j)
                        table[i] = (table[i] << 1) | rand_u16(2);

                    bool found = false;
                    for(u32 j = 0; j < i; ++j)
                        if(table[i] == table[j])
                        {
                            found = true;
                            break;
                        }
                    if(found)
                        continue;
                }
                while(count_bits(table[i]) != 32);
            }

            memset(bits, 0, sizeof(u32) * 64);
            for(u32 i = 0; i < table_size; ++i)
                for(u32 b = 0; b < 64; ++b)
                    if((table[i] >> b) & 1)
                        ++bits[b];


            u32 total = 0;
            for(u32 b = 0; b < 64; ++b)
                total += bits[b];
            double average = ((double)total) / ((double)64);

            double variance = 0.0;
            for(u32 b = 0; b < 64; ++b)
                variance += (((double)bits[b]) - average) * (((double)bits[b]) -
                    average);
            variance = variance / ((double)64);

            if(variance < best_variance)
            {
                best_variance = variance;
                memcpy(iv, table, sizeof(u64) * table_size);
                fprintf(stderr, "\rBest variance=%5f ", best_variance);
                fflush(stderr);
            }
        }

        FD_ZERO(&readfs);
        FD_SET(STDIN_FILENO, &readfs);
        struct timeval tm;
        tm.tv_sec = 0;
        tm.tv_usec = 0;

        int ready = select(STDIN_FILENO + 1, &readfs, NULL, NULL, &tm);
        if(ready > 0)
            break;
    }

    free(table);

    fprintf(stderr, "\nSearch stopped.\n");

    char * filename = alloc();
    snprintf(filename, MAX_PAGE_SIZ, "%s%ux%u.zt.bak", get_data_folder(),
        BOARD_SIZ, BOARD_SIZ);


    FILE * h = fopen(filename, "wb");
    if(h == NULL)
    {
        fprintf(stderr, "Error: failed to open file %s for writing\n",
            filename);
        release(filename);
        exit(EXIT_FAILURE);
    }

    size_t w = fwrite(iv, sizeof(u64), TOTAL_BOARD_SIZ * 2, h);
    if(w != TOTAL_BOARD_SIZ * 2)
    {
        fprintf(stderr, "Error: unexpected number of bytes written\n");
        release(filename);
        exit(EXIT_FAILURE);
    }

    fclose(h);

    fprintf(stderr, "Zobrist table written to %s\n", filename);
    release(filename);
    return EXIT_SUCCESS;
}
