/*
Implementation of a generic hash table
*/

#ifndef MATILDA_HTABLE_H
#define MATILDA_HTABLE_H

#include "matilda.h"

#include "types.h"

typedef struct __ht_node_ {
	void * data;
	struct __ht_node_ * next;
} ht_node;

typedef struct __hash_table_ {
	u32 number_of_buckets;
	u32 elem_size;
	u32 elements;
	ht_node ** table;
	u32 (* hash_func)(void *);
	int (* cmp_func)(const void *, const void *);
} hash_table;



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
);

/*
Inserts a value in the hash table.
Tests if it already exists and doesn't do anything if it does.
*/
void hash_table_insert_unique(
    hash_table * ht,
    void * elem
);

/*
Inserts a value in the hash table.
Does not test if it already exists.
*/
void hash_table_insert(
    hash_table * ht,
    void * elem
);

/*
Find if a value exists by comparing it with another instance of the type.
RETURNS true if value found
*/
bool hash_table_exists(
    hash_table * ht,
    void * elem
);

/*
Find and returns a value by comparing it with another instance of the type.
RETURNS existing instance of structure or NULL
*/
void * hash_table_find(
    hash_table * ht,
    void * elem
);

/*
Free the structures created on-demand, plus ht itself.
Optionally also free the data stored in the table.
*/
void hash_table_destroy(
    hash_table * ht,
    bool free_data_too
);

/*
Open or creates the file and exports the contents of the data to it.
*/
void hash_table_export_to_file(
    hash_table * ht,
    const char * filename
);

/*
Opens a file and reads its contents filling the array.
RETURNS true if file found
*/
bool hash_table_import_from_file(
    hash_table * ht,
    const char * filename
);

/*
Allocates the necessary memory and exports the data to an array of pointers,
returning it. The array is one position longer to fit in a NULL sentinel value.
RETURNS allocated array with data
*/
void ** hash_table_export_to_array(
    hash_table * ht
);

#endif

