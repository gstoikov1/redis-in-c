#ifndef HAST_TABLE_H
#define HAST_TABLE_H

#include <stddef.h>

#define HASH_TABLE_DEFAULT_SIZE 1024

typedef struct Entry {
    char *key;
    int key_len;

    char *value;
    int value_len;

    struct Entry *next;
} Entry;

typedef struct Table {
    Entry **entries;
    int size;
} Table;

int init(Table *table, size_t initial_size);
int add(Table *table, const char *key, size_t key_len, const char *value, size_t value_len);
Entry *get(Table *table, const char *key, size_t key_len);
void free_entries(Entry **entries, size_t size);

#endif