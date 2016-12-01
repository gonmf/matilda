/*
UNFINISHED AND INCOMPLETE RIGHT NOW


Application for the transformation of joseki compilations in SGF format (as
variations). Tested with Kogo's Joseki Dictionary.

Upon running a data/output.joseki file should be written.
*/

#include "matilda.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "alloc.h"
#include "board.h"
#include "constants.h"
#include "engine.h"
#include "file_io.h"
#include "flog.h"
#include "stringm.h"
#include "timem.h"


typedef struct __joseki_ {
    move_seq played;
    move_seq replies;
} joseki;

static FILE * fp = NULL;

static void update(char ** tok, u8 * mark, char * buf, u8 m)
{
    if(buf == NULL)
        return;

    if(*tok == NULL || *tok > buf)
    {
        *tok = buf;
        *mark = m;
    }
}

static char * parse(
    char * buffer,
    joseki * jout
){
    joseki * j = malloc(sizeof(joseki));
    memcpy(j, jout, sizeof(joseki));
    j->replies.count = 0;

    while(1){

        char * tok = NULL;
        u8 mark = 0;
        update(&tok, &mark, strstr(buffer, ";B["), 1);
        update(&tok, &mark, strstr(buffer, ";W["), 2);
        update(&tok, &mark, strstr(buffer, "LB["), 3);
        update(&tok, &mark, strstr(buffer, "(;"), 4);
        update(&tok, &mark, strstr(buffer, ")"), 5);
        update(&tok, &mark, strstr(buffer, "C["), 6);

        if(mark == 0)
        {
            fprintf(stderr, "End of file.");
            free(j);
            return buffer;
        }

        if(mark == 1 || mark == 2)
        {
            buffer = tok + 3;

            buffer[2] = 0;
            move m = coord_parse_alpha_alpha(buffer);
            buffer += 3;
            if(is_board_move(m))
                j->played.coord[j->played.count++] = m;
            continue;
        }

        if(mark == 3)
        {
            buffer = tok + 2;

            u32 largest_v = 0;
            while(1)
            {
                if(buffer[0] == '[' && buffer[3] == ':' && buffer[5] == ']')
                {
                    buffer[3] = 0;
                    move m = coord_parse_alpha_alpha(buffer + 1);
                    if(!is_board_move(m))
                        break;

                    char c = buffer[4];

                    u32 v;
                    if(c >= 'A' && c <= 'Z')
                        v = c - 'A';
                    else
                        if(c >= 'a' && c <= 'z')
                            v = c - 'a';
                        else
                        {
                            buffer += 6;
                            continue;
                        }
                    if(v > largest_v)
                        largest_v = v;
                    j->replies.coord[v] = m;
                    j->replies.count++;
                    buffer += 6;
                }
                else
                    break;
            }
            if(j->replies.count > 1)
            {
                j->replies.count--;
            }

            char * s = alloc();

            fprintf(fp, "%u", BOARD_SIZ);
            for(u16 i = 0; i < j->played.count; ++i)
            {
                coord_to_alpha_num(s, j->played.coord[i]);
                fprintf(fp, " %s", s);
            }
            fprintf(fp, " |");
            for(u16 i = 0; i < j->replies.count; ++i)
            {
                coord_to_alpha_num(s, j->played.coord[i]);
                fprintf(fp, " %s", s);
            }
            fprintf(fp, "\n");
            fflush(fp);

            release(s);
            continue;
        }

        if(mark == 4)
        {
            buffer = tok + 1;
            buffer = parse(buffer, j);
            continue;
        }

        if(mark == 5)
        {
            buffer = tok + 1;
            free(j);
            return buffer;
        }

        if(mark == 6)
        {
            buffer = tok + 2;
            tok = strstr(buffer, "]");
            if(tok == NULL)
            {
                fprintf(stderr, "Parse error 2\n");
                exit(EXIT_SUCCESS);
            }
            buffer = tok + 1;
            continue;
        }
    }
}

int main()
{
    alloc_init();

    flog_config_modes(LOG_MODE_ERROR | LOG_MODE_WARN);
    flog_config_destinations(LOG_DEST_STDF);

    assert_data_folder_exists();

    char * buffer = calloc(MAX_FILE_SIZ, 1);

    char * s = alloc();
    snprintf(s, MAX_PAGE_SIZ, "%s%s", data_folder(), "kogo.sgf");

    d32 rd = read_ascii_file(buffer, MAX_FILE_SIZ, s);
    if(rd <= 0)
    {
        fprintf(stderr, "Error reading %s\n", s);
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "Filesize: %u\n", (u32)rd);

    snprintf(s, MAX_PAGE_SIZ, "SZ[%u]", BOARD_SIZ);
    if(strstr(buffer, s) == NULL)
    {
        fprintf(stderr, "Error: wrong board size or SGF size property is missing.\n");
        exit(EXIT_FAILURE);
    }

    snprintf(s, MAX_PAGE_SIZ, "%s%s", data_folder(),
        "output.joseki");
    fp = fopen(s, "w");
    if(fp == NULL)
    {
        fprintf(stderr, "Error: failed to open %s for writing\n", s);
        exit(EXIT_FAILURE);
    }

    release(s);

    joseki j;
    j.played.count = 0;
    j.replies.count = 0;
    parse(buffer, &j);

    fclose(fp);

    fprintf(stderr, "Job done.\n");
    return EXIT_SUCCESS;
}
