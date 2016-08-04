#include "matilda.h"

#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "types.h"
#include "randg.h"
#include "engine.h"
#include "timem.h"
#include "buffer.h"
#include "flog.h"

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
        printf("matilda %u.%u\n", VERSION_MAJOR, VERSION_MINOR);
        return 0;
    }
    else
        if(argc > 1)
        {
            printf("usage: %s [-version]\n", argv[0]);
            return 0;
        }

    timestamp();
    config_logging(DEFAULT_LOG_MODES);
    assert_data_folder_exists();

    printf("This process aims to reduce the bit distribution variance of the da\
ta.\nWhen you are satisfied press ENTER\n\n");

    u64 iv[BOARD_SIZ * BOARD_SIZ][2];
    memset(iv, 0, BOARD_SIZ * BOARD_SIZ * sizeof(u64));

    u32 table_size = BOARD_SIZ * BOARD_SIZ * 2;
    u64 * table = (u64 *)malloc(table_size * sizeof(u64));
    u32 bits[64];
    double best_variance = 999999.0;

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
                printf("\rBest variance=%5f ", best_variance);
                fflush(stdout);
            }
        }

        fd_set readfs;
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

    printf("\nSearch stopped.\n");

    char * filename = get_buffer();
    snprintf(filename, MAX_PAGE_SIZ, "%s%ux%u.zt.bak", get_data_folder(),
        BOARD_SIZ, BOARD_SIZ);


    FILE * h = fopen(filename, "wb");
    if(h == NULL)
    {
        fprintf(stderr, "Error: failed to open file %s for writing\n",
            filename);
        exit(EXIT_FAILURE);
    }

    size_t w = fwrite(iv, sizeof(u64), BOARD_SIZ * BOARD_SIZ * 2, h);
    if(w != BOARD_SIZ * BOARD_SIZ * 2)
    {
        fprintf(stderr, "Error: unexpected number of bytes written\n");
        exit(EXIT_FAILURE);
    }

    fclose(h);

    printf("Zobrist table written to %s\n", filename);

    return EXIT_SUCCESS;
}
