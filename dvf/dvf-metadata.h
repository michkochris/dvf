#ifndef DVF_METADATA_H
#define DVF_METADATA_H

#include <stdint.h>
#include <vector>
#include <string>

#ifdef __cplusplus
extern "C" {
#endif

// Binary Metadata Cache Header
typedef struct {
    uint32_t magic;         // "DVFM" (0x4456464D)
    uint32_t version;       // Cache format version
    uint32_t package_count;
    uint32_t string_pool_size;
} dvf_metadata_header_t;

// Compact package representation in binary format
typedef struct {
    uint32_t name_offset;
    uint32_t version_offset;
    uint32_t release_offset;
    uint32_t epoch_offset;
    uint32_t arch_offset;
    uint32_t location_offset;
    uint32_t repo_id_offset;
    uint32_t provides_count;
    uint32_t provides_offset; // Offset to list of string pool offsets
    uint32_t requires_count;
    uint32_t requires_offset; // Offset to list of string pool offsets
} dvf_metadata_pkg_entry_t;

#ifdef __cplusplus
}
#endif

#endif // DVF_METADATA_H
