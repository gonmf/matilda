/*
Functions for file input/output.
*/

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <time.h> /* localtime */
#include <errno.h>

#include "alloc.h"
#include "engine.h"
#include "flog.h"
#include "types.h"

/*
Create a new file and open it for writing; creating new filenames if the file
already exists.
RETURNS file descriptor
*/
int create_and_open_file(
    char * restrict filename,
    u32 filename_size,
    const char * restrict prefix,
    const char * restrict extension
) {
    u32 attempt = 1;
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    while (1) {
        if (attempt == 1) {
            snprintf(filename, filename_size, "%s%s_%04u%02u%02u%02u%02u.%s", data_folder(), prefix,
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, extension);
        } else {
            snprintf(filename, filename_size, "%s%s_%04u%02u%02u%02u%02u_%u.%s", data_folder(), prefix,
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, attempt, extension);
        }

        int fd = open(filename, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);

        /* File created */
        if (fd != -1) {
            return fd;
        }

        /* We will never be able to create the file */
        if (errno != EEXIST) {
            return -1;
        }

        ++attempt;
    }
}

/*
RETURNS the number of bytes read or -1 if failed to open/be read
*/
d32 read_binary_file(
    void * dst_buf,
    u32 buf_len,
    const char * filename
) {
    FILE * h = fopen(filename, "rb");
    if (h == NULL) {
        return -1;
    }

    u32 total_read = 0;

    char * dst_buf2 = (char *)dst_buf;
    bool file_unfinished;

    do {
        size_t rd = fread(dst_buf2 + total_read, 1, buf_len - total_read, h);

        if (ferror(h) != 0) {
            fclose(h);
            return -1;
        }

        total_read += rd;
    } while (buf_len > total_read && (file_unfinished = (feof(h) == 0)));

    char tmp[2];
    fread(tmp, 1, 1, h);
    file_unfinished = (feof(h) == 0);

    fclose(h);

    if (file_unfinished) {
        char * s = alloc();
        snprintf(s, MAX_PAGE_SIZ, "file %s longer than buffer available for reading\n", filename);
        flog_crit("file", s);
        release(s);
    }

    return total_read;
}

/*
RETURNS the number of ASCII characters read or -1 if failed to open/be read
*/
d32 read_ascii_file(
    char * restrict dst_buf,
    u32 buf_len,
    const char * restrict filename
) {
    FILE * h = fopen(filename, "r"); /* text file, hopefully ASCII */
    if (h == NULL) {
        return -1;
    }

    u32 total_read = 0;
    bool file_unfinished;

    do {
        size_t rd = fread(dst_buf + total_read, 1, buf_len - total_read, h);

        if (ferror(h) != 0) {
            char * s = alloc();
            snprintf(s, MAX_PAGE_SIZ, "%s: %s", filename, strerror(errno));
            flog_warn("file", s);
            release(s);
            fclose(h);
            return -1;
        }

        total_read += rd;

    } while (buf_len > total_read && (file_unfinished = (feof(h) == 0)));

    char tmp[2];
    fread(tmp, 1, 1, h);
    file_unfinished = (feof(h) == 0);

    fclose(h);

    if (file_unfinished) {
        char * s = alloc();
        snprintf(s, MAX_PAGE_SIZ, "%s: larger than buffer space", filename);
        flog_crit("file", s);
        release(s);
        return -1;
    }

    dst_buf[total_read] = 0;
    return total_read;
}

static bool ends_in(
    const char * restrict a,
    const char * restrict b
) {
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);

    if (len_a <= len_b) {
        return false;
    }

    size_t offset = len_a - len_b;

    for (u16 i = 0; i < len_b; ++i) {
        if (a[offset + i] != b[i]) {
            return false;
        }
    }

    return true;
}


static u32 allocated;
static u32 filenames_found;
static u32 _max_files;

static void _recurse_find_files(
    const char * restrict root,
    const char * restrict extension,
    char ** filenames
) {
    DIR * dir;
    struct dirent * entry;

    if (!(dir = opendir(root))) {
        return;
    }

    while (filenames_found <= _max_files && (entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') { /* ignore special and hidden files */
            continue;
        }

        u32 strl = strlen(root) + strlen(entry->d_name) + 2;
        if (strl >= MAX_PATH_SIZ) {
            flog_crit("file", "path too long");
        }

        if (!ends_in(entry->d_name, extension)) { /* try following as if folder */
            char * path = malloc(strl);

            if (path == NULL) {
                flog_crit("file", "find files: system out of memory");
            }

            snprintf(path, strl, "%s%s/", root, entry->d_name);
            _recurse_find_files(path, extension, filenames);
            free(path);
        } else {
            allocated += strl;
            filenames[filenames_found] = malloc(strl);

            if (filenames[filenames_found] == NULL) {
                flog_crit("file", "find files: system out of memory");
            }

            snprintf(filenames[filenames_found], strl, "%s%s", root, entry->d_name);
            filenames_found++;

            if (filenames_found > _max_files) {
                char * s = alloc();
                snprintf(s, MAX_PAGE_SIZ, "maximum number of files (%u) reached", _max_files);
                flog_crit("file", s);
                release(s);
            }
        }
    }
    closedir(dir);
}

/*
Searches for and allocates the space needed for the relative path to the files
found ending with the text present in extension.
At most fills max_files file names.
RETURN number of file names saved
*/
u32 recurse_find_files(
    const char * restrict root,
    const char * restrict extension,
    char ** filenames,
    u32 max_files
) {
    filenames_found = 0;
    _max_files = max_files;
    _recurse_find_files(root, extension, filenames);
    return filenames_found;
}
