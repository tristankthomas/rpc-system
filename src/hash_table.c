/*
 * hash_table.c - Contains definitions for a hash table using the djb2 hashing algorithm
 * Author: Tristan Thomas
 * Date: 17-5-2023
 */

#include "hash_table.h"
#include <stdlib.h>
#include <assert.h>

#define TABLE_SIZE 100



typedef struct node {
    void *key;
    void *data;
    struct node *next;
} node_t;

struct hash_table {
    node_t* buckets[TABLE_SIZE];
    int num_items;
};

static node_t *create_node(void *key, void *data);


hash_table_t *create_empty_table() {
    hash_table_t *table = malloc(sizeof(*table));
    assert(table);
    table->num_items = 0;

    for (int i = 0; i < TABLE_SIZE; i++) {
        table->buckets[i] = NULL;
    }

    return table;
}

static node_t *create_node(void *key, void *data) {
    node_t *new_node = malloc(sizeof(*new_node));
    new_node->key = key;
    new_node->data = data;
    new_node->next = NULL;
    return new_node;
}

int insert_data(hash_table_t *table, void *key, void *data, hash_func hash, compare_func cmp, free_func free_key,
                free_func free_data) {
    uint32_t index = hash(key) % TABLE_SIZE;

    node_t *new_node = create_node(key, data);
    if (new_node == NULL) {
        return -1;
    }
    if (table->buckets[index] == NULL) {
        table->buckets[index] = new_node;
    } else {
        node_t *current = table->buckets[index];

        while (current->next != NULL) {
            // replace the data
            if (cmp(current->key, key) == 0) {
                // free replaced data (keeps current key)
                if (free_data != NULL) {
                    free_data(current->data);
                    current->data = NULL;
                }
                if (free_key != NULL) {
                    free_key(key);
                    key = NULL;
                }
                current->data = data;
                table->num_items++;
                break;
            }
            current = current->next;
        }
        current->next = new_node;
    }

    table->num_items++;
    return 1;

}

void *get_data(hash_table_t *table, void *key, hash_func hash, compare_func cmp) {
    uint32_t index = hash(key) % TABLE_SIZE;

    node_t *current = table->buckets[index];
    while (current) {
        if (cmp(current->key, key) == 0) {
            return current->data;
        }
        current = current->next;
    }
    return NULL; // Key not found
}

void free_table(hash_table_t *table, free_func free_key, free_func free_data) {
    // Iterate through the hash table
    for (int i = 0; i < TABLE_SIZE; i++) {
        node_t *current = table->buckets[i];

        // Free each node in the linked list at the current index (only if not NULL)
        while (current) {
            node_t *next = current->next;
            if (free_data != NULL) {
                free_data(current->data);
            }
            if (free_key != NULL) {
                free_key(current->key);
            }

            free(current);
            current = NULL;
            current = next;
        }
    }

    free(table);
    table = NULL;
}

