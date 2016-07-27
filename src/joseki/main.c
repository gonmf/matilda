/*
Application for the transformation of joseki compilations in SGF format (as
variations). Tested with Kogo's Joseki Dictionary.

Upon running a DATA/output.joseki file should be written.
*/

#include "matilda.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "engine.h"
#include "file_io.h"
#include "timem.h"
#include "stringm.h"
#include "buffer.h"


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
            printf("End of file.");
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

            fprintf(fp, "%u", BOARD_SIZ);
            for(u16 i = 0; i < j->played.count; ++i)
                fprintf(fp, " %s", coord_to_alpha_num(j->played.coord[i]));
            fprintf(fp, " |");
            for(u16 i = 0; i < j->replies.count; ++i)
                fprintf(fp, " %s", coord_to_alpha_num(j->replies.coord[i]));
            fprintf(fp, "\n");
            fflush(fp);
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

int main(
    int argc,
    char * argv[]
){
    for(int i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], "-version") == 0)
        {
            printf("matilda %u.%u\n", VERSION_MAJOR, VERSION_MINOR);
            exit(EXIT_SUCCESS);
        }

        printf("Usage: %s [options]\n", argv[0]);
        printf("Options:\n");
        printf("-version - Print version information and exit.\n");
        exit(EXIT_SUCCESS);
    }

    timestamp();
    assert_data_folder_exists();

    char * buffer = calloc(MAX_FILE_SIZ, 1);

    char * filename = get_buffer();
    snprintf(filename, MAX_PAGE_SIZ, "%s%s", get_data_folder(), "kogo.sgf");

    s32 rd = read_ascii_file(filename, buffer, MAX_FILE_SIZ);
    if(rd <= 0)
    {
        fprintf(stderr, "Error reading %s\n", filename);
        exit(EXIT_FAILURE);
    }

    printf("Filesize: %u\n", (u32)rd);

    char * board_size_tag = get_buffer();
    snprintf(board_size_tag, MAX_PAGE_SIZ, "SZ[%u]", BOARD_SIZ);
    if(strstr(buffer, board_size_tag) == NULL)
    {
        printf("Error: wrong board size or SGF size property is missing.\n");
        exit(EXIT_FAILURE);
    }

    char * out_filename = get_buffer();
    snprintf(out_filename, MAX_PAGE_SIZ, "%s%s", get_data_folder(),
        "output.joseki");
    fp = fopen(out_filename, "w");
    if(fp == NULL)
    {
        fprintf(stderr, "Error: failed to open %s for writing\n", out_filename);
        exit(EXIT_FAILURE);
    }

    joseki j;
    j.played.count = 0;
    j.replies.count = 0;
    parse(buffer, &j);

    fclose(fp);

    printf("%s: Job done.\n", timestamp());
    return EXIT_SUCCESS;
}
