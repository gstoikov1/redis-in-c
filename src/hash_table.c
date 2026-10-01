#define _POSIX_C_SOURCE 200809L

#include "hash_table.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <time.h>

uint64_t hash_bytes(const char *data, size_t length);
int64_t current_time_ms();

int init(Table *table, size_t initial_size) {
    if (table == NULL) {
        return -1;
    }

    size_t size = initial_size == 0 ? HASH_TABLE_DEFAULT_SIZE : initial_size;

    table->entries = calloc(size, sizeof(*table->entries));

    if (table->entries == NULL) {
        return -1;
    }

    table->size = size;

    return 0;
}

void free_entries(Entry **entries, size_t size) {
    for (size_t i = 0; i < size; i++) {
        Entry *current = entries[i];

        while (current != NULL) {
            Entry *next = current->next;

            free(current->key);
            free(current->value);
            free(current);

            current = next;
        }
    }

    free(entries);
}

int add(Table *table, const char *key, size_t key_len, const char *value, size_t value_len,
        int exp_time) {
    if (table == NULL || key == NULL || value == NULL) {
        return -1;
    }

    uint64_t hash = hash_bytes(key, key_len);
    size_t index = hash % table->size;

    Entry *current = table->entries[index];

    // Update an existing key
    while (current != NULL) {
        if (current->key_len == key_len && memcmp(current->key, key, key_len) == 0) {

            char *new_value = malloc(value_len);

            if (new_value == NULL && value_len > 0) {
                return -1;
            }

            memcpy(new_value, value, value_len);

            free(current->value);

            current->value = new_value;
            current->value_len = value_len;
            if (exp_time > 0) {
                time_t currentTime;
                time(&currentTime);
                exp_time = currentTime + exp_time;
            }
            return 0;
        }

        current = current->next;
    }

    // Create a new entry
    Entry *new_entry = calloc(1, sizeof(*new_entry));

    if (new_entry == NULL) {
        return -1;
    }

    new_entry->key = malloc(key_len);
    new_entry->value = malloc(value_len);

    if ((new_entry->key == NULL && key_len > 0) || (new_entry->value == NULL && value_len > 0)) {
        free(new_entry->key);
        free(new_entry->value);
        free(new_entry);
        return -1;
    }

    memcpy(new_entry->key, key, key_len);
    memcpy(new_entry->value, value, value_len);

    new_entry->key_len = key_len;
    new_entry->value_len = value_len;

    if (exp_time > 0) {
        exp_time = current_time_ms() + exp_time;
    }
    new_entry->exp_time = exp_time;
    // Insert at the beginning of the collision chain
    new_entry->next = table->entries[index];
    table->entries[index] = new_entry;

    return 0;
}

uint64_t hash_bytes(const char *data, size_t length) {
    uint64_t hash = 5381;

    for (size_t i = 0; i < length; i++) {
        hash = ((hash << 5) + hash) + (unsigned char)data[i];
    }

    return hash;
}

Entry *get(Table *table, const char *key, size_t key_len) {
    uint64_t hash = hash_bytes(key, key_len);
    int index = hash % table->size;
    Entry *e = table->entries[index];
    while (e) {
        if (strncmp(e->key, key, MIN(e->key_len, key_len)) == 0) {
            int64_t currentTime = current_time_ms();

            if (e->exp_time <= 0 || currentTime < e->exp_time) {
                return e;
            } else {
                break;
            }
        }
        e = e->next;
    }

    return NULL;
}

int64_t current_time_ms() {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}