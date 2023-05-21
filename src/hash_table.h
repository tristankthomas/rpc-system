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

hash_table_t *create_empty_table();
int insert_data(hash_table_t *table, void *key, void *data, hash_func hash, compare_func cmp);
void *get_data(hash_table_t *table, void *key, hash_func hash, compare_func cmp);
void free_table(hash_table_t *table);

#endif
