/*
 * hash_table.h - Contains the interface for hash table data structure and functions
 * Author: Tristan Thomas
 * Date: 17-5-2023
 */

#ifndef HASH_TABLE_H
#define HASH_TABLE_H

#include <stdint.h>

typedef struct hash_table hash_table_t;
typedef uint32_t (*hash_func)(void *);
typedef int (*compare_func)(void *, void *);
typedef void (*free_func)(void *);

/**
 * Creates an empty hash table
 *
 * @return Newly created hash table
 */
hash_table_t *create_empty_table();

/**
 * Inserts data into a given hash-table
 *
 * @param table Table to be inserted into
 * @param key Key of new element
 * @param data Data of new element
 * @param hash Hash function
 * @param cmp Comparison function
 * @param free_key Free key function
 * @param free_data Free data function
 * @return 1 on success
 */
int insert_data(hash_table_t *table, void *key, void *data, hash_func hash, compare_func cmp, free_func free_key,
                free_func free_data);

/**
 * Gets data from a given hash-table
 *
 * @param table Table to be searched
 * @param key Key of desired element
 * @param hash Hash function used for able
 * @param cmp Comparison function used for table
 * @return Data stored in hash-table
 */
void *get_data(hash_table_t *table, void *key, hash_func hash, compare_func cmp);

/**
 * Frees a given hash-table
 *
 * @param table Hash-table to be freed
 * @param free_key Key freeing function
 * @param free_data Data freeing function
 */
void free_table(hash_table_t *table, free_func free_key, free_func free_data);

#endif
