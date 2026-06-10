#ifndef DVF_STORAGE_H
#define DVF_STORAGE_H

#include "dvf-rpm.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Binary header for pkginfo.bin
typedef struct {
    uint32_t magic;      // 0x44564650 ("DVFP")
    uint32_t version;    // Format version
    char name[64];       // Fixed width for seeking
    char version_str[32];// Fixed width
    uint32_t info_hash;  // Hash of the serialized data
    uint64_t data_size;  // Size of serialized data following header
} DvfPkgHeader;

/**
 * @brief Writes package info to persistent storage in a pkgname-version subdirectory
 * @param info The RPM info to serialize and store
 * @return 0 on success, -1 on failure
 */
int dvf_storage_write_pkg_info(const rpm_info_t *info);

/**
 * @brief Gets the path to a package's storage directory
 * @param name Package name
 * @param version Package version
 * @param path_buffer Buffer to store the path (should be PATH_MAX)
 * @return 0 on success, -1 on failure
 */
int dvf_storage_get_pkg_path(const char *name, const char *version, char *path_buffer);

/**
 * @brief Reads package info from persistent storage
 * @param name Package name
 * @param version Package version
 * @param info Pointer to rpm_info_t to fill (should be pre-allocated)
 * @return 0 on success, -1 on failure
 */
int dvf_storage_read_pkg_info(const char *name, const char *version, rpm_info_t *info);

#ifdef __cplusplus
}
#endif

#endif // DVF_STORAGE_H
