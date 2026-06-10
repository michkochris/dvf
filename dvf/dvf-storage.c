#include "dvf-storage.h"
#include "dvf-config.h"
#include "dvf-util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

int dvf_storage_get_pkg_path(const char *name, const char *version, char *path_buffer) {
    if (!name || !version || !path_buffer || !g_dvf_db_dir) return -1;
    snprintf(path_buffer, PATH_MAX, "%s/%s-%s", g_dvf_db_dir, name, version);
    return 0;
}

int dvf_storage_write_pkg_info(const rpm_info_t *info) {
    if (!info || !info->name || !info->version) return -1;

    char pkg_dir[PATH_MAX];
    if (dvf_storage_get_pkg_path(info->name, info->version, pkg_dir) != 0) return -1;

    if (dvf_util_create_dir_recursive(pkg_dir, 0755) != 0) {
        dvf_log_error("Failed to create package directory: %s\n", pkg_dir);
        return -1;
    }

    char bin_path[PATH_MAX + 64];
    snprintf(bin_path, sizeof(bin_path), "%s/pkginfo.bin", pkg_dir);

    // Calculate a hash of the package info content
    uint32_t combined_hash = 0;
    combined_hash ^= dvf_util_hash_string(info->name);
    combined_hash ^= dvf_util_hash_string(info->version);
    if (info->release) combined_hash ^= dvf_util_hash_string(info->release);
    if (info->epoch) combined_hash ^= dvf_util_hash_string(info->epoch);
    if (info->arch) combined_hash ^= dvf_util_hash_string(info->arch);
    if (info->summary) combined_hash ^= dvf_util_hash_string(info->summary);
    if (info->description) combined_hash ^= dvf_util_hash_string(info->description);
    if (info->payload_compressor) combined_hash ^= dvf_util_hash_string(info->payload_compressor);

    FILE *fp = fopen(bin_path, "wb");
    if (!fp) {
        dvf_log_error("Failed to open %s for writing\n", bin_path);
        return -1;
    }

    DvfPkgHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = 0x44564650; // "DVFP"
    hdr.version = 1;
    strncpy(hdr.name, info->name, sizeof(hdr.name) - 1);
    strncpy(hdr.version_str, info->version, sizeof(hdr.version_str) - 1);
    hdr.info_hash = combined_hash;

    // Seek past header to write data
    fseek(fp, sizeof(DvfPkgHeader), SEEK_SET);

    size_t total_data = 0;
    size_t len;

    #define WRITE_STR(s) \
        len = (s) ? strlen(s) + 1 : 0; \
        fwrite(&len, sizeof(size_t), 1, fp); \
        if (len > 0) fwrite(s, 1, len, fp); \
        total_data += sizeof(size_t) + (len > 0 ? len : 0);

    WRITE_STR(info->name);
    WRITE_STR(info->version);
    WRITE_STR(info->release);
    WRITE_STR(info->epoch);
    WRITE_STR(info->arch);
    WRITE_STR(info->summary);
    WRITE_STR(info->description);
    WRITE_STR(info->payload_compressor);

    fwrite(&info->payload_offset, sizeof(long), 1, fp);
    total_data += sizeof(long);

    // Write file list
    fwrite(&info->file_count, sizeof(size_t), 1, fp);
    total_data += sizeof(size_t);
    for (size_t i = 0; i < info->file_count; i++) {
        WRITE_STR(info->file_list[i]);
    }

    hdr.data_size = total_data;

    // Go back and write the header
    fseek(fp, 0, SEEK_SET);
    fwrite(&hdr, sizeof(DvfPkgHeader), 1, fp);

    fclose(fp);
    dvf_log_debug("Stored package info for %s-%s to %s\n", info->name, info->version, bin_path);
    return 0;
}

int dvf_storage_read_pkg_info(const char *name, const char *version, rpm_info_t *info) {
    if (!name || !version || !info) return -1;

    char pkg_dir[PATH_MAX];
    if (dvf_storage_get_pkg_path(name, version, pkg_dir) != 0) return -1;

    char bin_path[PATH_MAX + 64];
    snprintf(bin_path, sizeof(bin_path), "%s/pkginfo.bin", pkg_dir);

    FILE *fp = fopen(bin_path, "rb");
    if (!fp) return -1;

    DvfPkgHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1) {
        fclose(fp);
        return -1;
    }

    if (hdr.magic != 0x44564650) {
        fclose(fp);
        return -1;
    }

    memset(info, 0, sizeof(rpm_info_t));
    size_t len;

    #define READ_STR(s) \
        if (fread(&len, sizeof(size_t), 1, fp) != 1) { fclose(fp); return -1; } \
        if (len > 0) { \
            s = malloc(len); \
            if (fread(s, 1, len, fp) != len) { free(s); fclose(fp); return -1; } \
        } else { s = NULL; }

    READ_STR(info->name);
    READ_STR(info->version);
    READ_STR(info->release);
    READ_STR(info->epoch);
    READ_STR(info->arch);
    READ_STR(info->summary);
    READ_STR(info->description);
    READ_STR(info->payload_compressor);

    if (fread(&info->payload_offset, sizeof(long), 1, fp) != 1) {
        fclose(fp);
        return -1;
    }

    if (fread(&info->file_count, sizeof(size_t), 1, fp) != 1) {
        fclose(fp);
        return -1;
    }

    if (info->file_count > 0) {
        info->file_list = malloc(sizeof(char *) * info->file_count);
        for (size_t i = 0; i < info->file_count; i++) {
            READ_STR(info->file_list[i]);
        }
    }

    fclose(fp);
    return 0;
}
