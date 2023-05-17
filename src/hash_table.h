/*
 * hash_table.h - Contains the interface for hash table data structure and functions
 * Author: Tristan Thomas
 * Date: 17-5-2023
 */

#ifndef HASH_TABLE_H
#define HASH_TABLE_H

typedef struct node node_t;
typedef struct hash_table hash_table_t;

hash_table_t *create_empty_table();
void insert_data(hash_table_t *table, char *key, void *data);
void *get_data(hash_table_t *table, char *key);
void free_table(hash_table_t *table);

#endif
