#include "dvf-hash.h"
#include "dvf-util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// --- Internal Utility Functions ---

static bool is_prime(size_t num) {
    if (num <= 1) return false;
    if (num <= 3) return true;
    if (num % 2 == 0 || num % 3 == 0) return false;
    for (size_t i = 5; i * i <= num; i += 6) {
        if (num % i == 0 || num % (i + 2) == 0) return false;
    }
    return true;
}

static size_t find_next_prime(size_t num) {
    if (num <= 2) return 2;
    if (num % 2 == 0) num++;
    while (!is_prime(num)) num += 2;
    return num;
}

static uint32_t get_hash(const char *name, size_t table_size) {
    if (table_size == 0) return 0;
    return dvf_util_hash_string(name) % table_size;
}

static void copy_rpm_info(rpm_info_t *dest, const rpm_info_t *src) {
    memset(dest, 0, sizeof(rpm_info_t));
    if (src->name) dest->name = strdup(src->name);
    if (src->version) dest->version = strdup(src->version);
    if (src->release) dest->release = strdup(src->release);
    if (src->epoch) dest->epoch = strdup(src->epoch);
    if (src->arch) dest->arch = strdup(src->arch);
    if (src->summary) dest->summary = strdup(src->summary);
    if (src->description) dest->description = strdup(src->description);
    if (src->payload_compressor) dest->payload_compressor = strdup(src->payload_compressor);
    dest->payload_offset = src->payload_offset;
}

// --- Public API ---

dvf_hash_table_t* dvf_hash_create_table(size_t initial_size) {
    dvf_hash_table_t *table = malloc(sizeof(dvf_hash_table_t));
    if (!table) return NULL;

    if (initial_size < MIN_HASH_TABLE_SIZE) initial_size = MIN_HASH_TABLE_SIZE;
    initial_size = find_next_prime(initial_size);

    table->buckets = calloc(initial_size, sizeof(dvf_hash_node_t*));
    if (!table->buckets) {
        free(table);
        return NULL;
    }

    table->size = initial_size;
    table->count = 0;
    return table;
}

rpm_info_t* dvf_hash_search(dvf_hash_table_t *table, const char *name) {
    if (!table || !name) return NULL;
    uint32_t index = get_hash(name, table->size);
    dvf_hash_node_t *current = table->buckets[index];
    while (current) {
        if (current->data.name && strcmp(current->data.name, name) == 0) {
            return &current->data;
        }
        current = current->next;
    }
    return NULL;
}

static int resize_hash_table(dvf_hash_table_t *table, size_t new_size) {
    if (new_size < MIN_HASH_TABLE_SIZE) new_size = MIN_HASH_TABLE_SIZE;
    new_size = find_next_prime(new_size);
    if (new_size == table->size) return 0;

    dvf_hash_node_t **new_buckets = calloc(new_size, sizeof(dvf_hash_node_t*));
    if (!new_buckets) return -1;

    dvf_log_debug("Resizing hash table: %zu -> %zu\n", table->size, new_size);

    for (size_t i = 0; i < table->size; i++) {
        dvf_hash_node_t *current = table->buckets[i];
        while (current) {
            dvf_hash_node_t *next = current->next;
            uint32_t index = get_hash(current->data.name, new_size);
            current->next = new_buckets[index];
            new_buckets[index] = current;
            current = next;
        }
    }

    free(table->buckets);
    table->buckets = new_buckets;
    table->size = new_size;
    return 0;
}

int dvf_hash_add_package(dvf_hash_table_t *table, const rpm_info_t *info) {
    if (!table || !info || !info->name) return -1;

    rpm_info_t *existing = dvf_hash_search(table, info->name);
    if (existing) {
        rpm_free_info(existing);
        copy_rpm_info(existing, info);
        return 0;
    }

    if ((double)(table->count + 1) / table->size > GROW_LOAD_FACTOR_THRESHOLD) {
        resize_hash_table(table, table->size * 2);
    }

    dvf_hash_node_t *node = malloc(sizeof(dvf_hash_node_t));
    if (!node) return -1;

    copy_rpm_info(&node->data, info);
    uint32_t index = get_hash(info->name, table->size);
    node->next = table->buckets[index];
    table->buckets[index] = node;
    table->count++;
    return 0;
}

void dvf_hash_remove_package(dvf_hash_table_t *table, const char *name) {
    if (!table || !name) return;
    uint32_t index = get_hash(name, table->size);
    dvf_hash_node_t *current = table->buckets[index];
    dvf_hash_node_t *prev = NULL;

    while (current && strcmp(current->data.name, name) != 0) {
        prev = current;
        current = current->next;
    }

    if (current) {
        if (prev) prev->next = current->next;
        else table->buckets[index] = current->next;
        rpm_free_info(&current->data);
        free(current);
        table->count--;

        if (table->size > MIN_HASH_TABLE_SIZE && (double)table->count / table->size < SHRINK_LOAD_FACTOR_THRESHOLD) {
            resize_hash_table(table, table->size / 2);
        }
    }
}

void dvf_hash_destroy_table(dvf_hash_table_t *table) {
    if (!table) return;
    for (size_t i = 0; i < table->size; i++) {
        dvf_hash_node_t *current = table->buckets[i];
        while (current) {
            dvf_hash_node_t *next = current->next;
            rpm_free_info(&current->data);
            free(current);
            current = next;
        }
    }
    free(table->buckets);
    free(table);
}

void dvf_hash_list_packages(dvf_hash_table_t *table) {
    if (!table) return;
    for (size_t i = 0; i < table->size; i++) {
        dvf_hash_node_t *current = table->buckets[i];
        while (current) {
            printf("%s\n", current->data.name);
            current = current->next;
        }
    }
}
