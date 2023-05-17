/*
 * hash_table.c - Contains definitions for a hash table using the djb2 hashing algorithm
 * Author: Tristan Thomas
 * Date: 17-5-2023
 */

#include "hash_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#define TABLE_SIZE 100

static uint32_t hash_djb2(char* str);
static node_t *create_node(char *key, void *data);

struct node {
    char *key;
    void *data;
    node_t *next;
};

struct hash_table {
    node_t* buckets[TABLE_SIZE];
};

static uint32_t hash_djb2(char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

static node_t *create_node(char *key, void *data) {
    node_t *new_node = malloc(sizeof(new_node));
    assert(new_node);
    new_node->key = strdup(key);
    assert(new_node->key);

    new_node->data = data;
    new_node->next = NULL;
    return new_node;
}

void insert_data(hash_table_t *table, char *key, void *data) {
    uint32_t index = hash_djb2(key) % TABLE_SIZE;

    node_t *new_node = create_node(key, data);

    if (table->buckets[index] == NULL) {
        table->buckets[index] = new_node;
    } else {
        node_t* current = table->buckets[index];
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = new_node;
    }
}

void *get_data(hash_table_t *table, char *key) {
    uint32_t index = hash_djb2(key) % TABLE_SIZE;

    node_t *current = table->buckets[index];
    while (current) {
        if (strcmp(current->key, key) == 0) {
            return current->data;
        }
        current = current->next;
    }
    return NULL; // Key not found
}

void free_table(hash_table_t *table) {
    // Iterate through the hash table
    for (int i = 0; i < TABLE_SIZE; i++) {
        node_t *current = table->buckets[i];

        // Free each node in the linked list at the current index
        while (current) {
            node_t *next = current->next;

            free(current);
            current = next;
        }
    }

    free(table);
}

