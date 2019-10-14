/*
Implementation of a generic hash table
*/

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "flog.h"
#include "hash_table.h"
#include "primes.h"
#include "types.h"

/*
Creates a hash table for use with types of elem_size comparable by the functions
provided.
RETURNS generic hash table instance
*/
hash_table * hash_table_create(
    u32 nr_buckets,
    u32 elem_size,
    u32 (* hash_function)(void *),
    int (* compare_function)(const void *, const void *)
) {
    assert(nr_buckets > 0);
    assert(elem_size > 0);
    assert(hash_function != NULL);
    assert(compare_function != NULL);

    hash_table * ht = malloc(sizeof(hash_table));
    if (ht == NULL)
        flog_crit("ht", "could not allocate table memory");

    ht->number_of_buckets = get_prime_near(nr_buckets);
    ht->elem_size = elem_size;
    ht->elements = 0;
    ht->table = calloc(ht->number_of_buckets, sizeof(ht_node *));
    ht->hash_func = hash_function;
    ht->cmp_func = compare_function;
    return ht;
}



static u32 fast_bucket(u32 hash, u32 number_of_buckets) {
    return (u32)((((u64)hash) * ((u64)number_of_buckets)) >> 32);
}



/*
Inserts a value in the hash table.
Tests if it already exists and doesn't do anything if it does.
*/
void hash_table_insert_unique(
    hash_table * ht,
    void * elem
) {
    assert(ht != NULL);
    assert(elem != NULL);

    u32 hash = ht->hash_func(elem);
    u32 bucket = fast_bucket(hash, ht->number_of_buckets);

    ht_node * h = ht->table[bucket];
    while (h != NULL) {
        if (ht->cmp_func(h->data, elem) == 0)
            return;
        h = h->next;
    }

    ht_node * node = malloc(sizeof(ht_node));
    if (node == NULL)
        flog_crit("ht", "could not allocate node memory");

    node->data = elem;
    node->next = ht->table[bucket];
    ht->table[bucket] = node;
    ht->elements++;
}

/*
Inserts a value in the hash table.
Does not test if it already exists.
*/
void hash_table_insert(
    hash_table * ht,
    void * elem
) {
    assert(ht != NULL);
    assert(elem != NULL);

    u32 hash = ht->hash_func(elem);
    u32 bucket = fast_bucket(hash, ht->number_of_buckets);

    ht_node * node = malloc(sizeof(ht_node));
    node->data = elem;
    node->next = ht->table[bucket];
    ht->table[bucket] = node;
    ht->elements++;
}


/*
Find if a value exists by comparing it with another instance of the type.
RETURNS true if value found
*/
bool hash_table_exists(
    hash_table * ht,
    void * elem
) {
    assert(ht != NULL);
    assert(elem != NULL);

    u32 hash = ht->hash_func(elem);
    u32 bucket = fast_bucket(hash, ht->number_of_buckets);

    ht_node * h = ht->table[bucket];
    while (h != NULL) {
        if (ht->cmp_func(h->data, elem) == 0)
            return true;
        h = h->next;
    }
    return false;
}

/*
Find and returns a value by comparing it with another instance of the type.
RETURNS existing instance of structure or NULL
*/
void * hash_table_find(
    hash_table * ht,
    void * elem
) {
    assert(ht != NULL);
    assert(elem != NULL);

    u32 hash = ht->hash_func(elem);
    u32 bucket = fast_bucket(hash, ht->number_of_buckets);

    ht_node * h = ht->table[bucket];
    while (h != NULL) {
        if (ht->cmp_func(h->data, elem) == 0)
            return h->data;
        h = h->next;
    }
    return NULL;
}

static void recursive_free(
    ht_node * n,
    bool free_data_too
) {
    if (n == NULL)
        return;
    recursive_free(n->next, free_data_too);
    if (free_data_too)
        free(n->data);
    free(n);
}

/*
Free the structures created on-demand, plus ht itself.
Optionally also free the data stored in the table.
*/
void hash_table_destroy(
    hash_table * ht,
    bool free_data_too
) {
    assert(ht != NULL);

    for (u32 bucket = 0; bucket < ht->number_of_buckets; ++bucket)
        recursive_free(ht->table[bucket], free_data_too);

    free(ht->table);
    free(ht);
}

/*
Open or creates the file and exports the contents of the data to it.
*/
void hash_table_export_to_file(
    hash_table * ht,
    const char * filename
) {
    assert(ht != NULL);
    assert(filename != NULL);

    u32 written = 0;

    FILE * fp = fopen(filename, "wb");
    if (fp == NULL)
        flog_crit("ht", "couldn't open file for writing");

    for (u32 bucket = 0; bucket < ht->number_of_buckets; ++bucket) {
        ht_node * h = ht->table[bucket];
        while (h != NULL) {
            size_t w = fwrite(h->data, ht->elem_size, 1, fp);
            if (w != 1)
                flog_crit("ht", "write failed");

            ++written;
            h = h->next;
        }
    }

    fclose(fp);

    if (ht->elements != written)
        flog_crit("ht", "wrong number of hash table elements written");

    fprintf(stderr, "ht: wrote %u elements to file %s\n", written, filename);
}

/*
Opens a file and reads its contents filling the array.
RETURNS true if file found
*/
bool hash_table_import_from_file(
    hash_table * ht,
    const char * filename
) {
    assert(ht != NULL);
    assert(filename != NULL);

    FILE * fp = fopen(filename, "rb");
    if (fp == NULL)
        return false;

    void * data = malloc(ht->elem_size);
    assert(data != NULL);
    u32 elems_read = 0;

    while (1) {
        size_t r = fread(data, ht->elem_size, 1, fp);
        if (r < 1)
            break;
        ++elems_read;
        hash_table_insert(ht, data);
        data = malloc(ht->elem_size);
        assert(data != NULL);
    }

    free(data);

    fclose(fp);
    return true;
}

/*
Allocates the necessary memory and exports the data to an array of pointers,
returning it. The array is one position longer to fit in a NULL sentinel value.
RETURNS allocated array with data
*/
void ** hash_table_export_to_array(
    hash_table * ht
) {
    assert(ht != NULL);

    void ** ret = malloc(sizeof(void *) * (ht->elements + 1));
    assert(ret != NULL);

    u32 curr_elem = 0;
    for (u32 bucket = 0; bucket < ht->number_of_buckets; ++bucket) {
        ht_node * h = ht->table[bucket];
        while (h != NULL) {
            ret[curr_elem++] = h->data;
            h = h->next;
        }
    }
    ret[curr_elem] = NULL;

    if (curr_elem != ht->elements)
        flog_crit("ht", "unexpected number of elements exported");

    return ret;
}




