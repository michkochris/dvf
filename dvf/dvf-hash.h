#ifndef DVF_HASH_H
#define DVF_HASH_H

#include "dvf-rpm.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Hash Table Configuration ---
#define INITIAL_HASH_TABLE_SIZE 17
#define GROW_LOAD_FACTOR_THRESHOLD 0.75
#define SHRINK_LOAD_FACTOR_THRESHOLD 0.25
#define MIN_HASH_TABLE_SIZE 17

// --- Hash Table Node Structure ---
typedef struct dvf_hash_node {
    rpm_info_t data;
    struct dvf_hash_node *next;
} dvf_hash_node_t;

// --- Hash Table Structure ---
typedef struct dvf_hash_table {
    dvf_hash_node_t **buckets;
    size_t size;
    size_t count;
} dvf_hash_table_t;

/**
 * @brief Creates and initializes a new hash table.
 * @param initial_size The desired initial size of the hash table.
 * @return A pointer to the new hash table, or NULL on failure.
 */
dvf_hash_table_t* dvf_hash_create_table(size_t initial_size);

/**
 * @brief Searches the hash table for a package.
 * @param table A pointer to the hash table.
 * @param name The name of the package to search for.
 * @return A pointer to the package info, or NULL if not found.
 */
rpm_info_t* dvf_hash_search(dvf_hash_table_t *table, const char *name);

/**
 * @brief Adds a package to the hash table.
 * @param table A pointer to the hash table.
 * @param info The package info to add (deep copy).
 * @return 0 on success, -1 on failure.
 */
int dvf_hash_add_package(dvf_hash_table_t *table, const rpm_info_t *info);

/**
 * @brief Removes a package from the hash table.
 * @param table A pointer to the hash table.
 * @param name The name of the package to remove.
 */
void dvf_hash_remove_package(dvf_hash_table_t *table, const char *name);

/**
 * @brief Destroys the hash table and frees all memory.
 * @param table A pointer to the hash table to destroy.
 */
void dvf_hash_destroy_table(dvf_hash_table_t *table);

/**
 * @brief Lists all package names in the hash table.
 * @param table A pointer to the hash table.
 */
void dvf_hash_list_packages(dvf_hash_table_t *table);

#ifdef __cplusplus
}
#endif

#endif // DVF_HASH_H
