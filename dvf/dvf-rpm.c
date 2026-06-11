#include "dvf-rpm.h"
#include "dvf-util.h"
#include <stdlib.h>
#include <string.h>
#include <endian.h>

static uint32_t read_be32(uint32_t val) {
    return be32toh(val);
}

void rpm_free_info(rpm_info_t *info) {
    dvf_util_free_and_null(&info->name);
    dvf_util_free_and_null(&info->version);
    dvf_util_free_and_null(&info->release);
    dvf_util_free_and_null(&info->epoch);
    dvf_util_free_and_null(&info->arch);
    dvf_util_free_and_null(&info->summary);
    dvf_util_free_and_null(&info->description);
    dvf_util_free_and_null(&info->payload_compressor);
    if (info->file_list) {
        for (size_t i = 0; i < info->file_count; i++) {
            dvf_util_free_and_null(&info->file_list[i]);
        }
        free(info->file_list);
        info->file_list = NULL;
    }
    info->file_count = 0;
    if (info->provides_list) {
        for (size_t i = 0; i < info->provides_count; i++) {
            dvf_util_free_and_null(&info->provides_list[i]);
        }
        free(info->provides_list);
        info->provides_list = NULL;
    }
    info->provides_count = 0;
    memset(info, 0, sizeof(rpm_info_t));
}

void rpm_print_info(const rpm_info_t *info) {
    printf("\033[1mName          \033[0m: %s\n", info->name ? info->name : "unknown");
    if (info->epoch && strcmp(info->epoch, "0") != 0)
        printf("\033[1mEpoch         \033[0m: %s\n", info->epoch);
    printf("\033[1mVersion       \033[0m: %s\n", info->version ? info->version : "unknown");
    printf("\033[1mRelease       \033[0m: %s\n", info->release ? info->release : "unknown");
    printf("\033[1mArchitecture  \033[0m: %s\n", info->arch ? info->arch : "unknown");
    if (info->summary)     printf("\033[1mSummary       \033[0m: %s\n", info->summary);
    if (info->description) printf("\033[1mDescription   \033[0m: %s\n", info->description);
    printf("\033[1mCompressor    \033[0m: %s\n", info->payload_compressor ? info->payload_compressor : "unknown");
    if (info->payload_offset > 0)
        printf("\033[1mPayload Offset\033[0m: %ld bytes\n", info->payload_offset);
}

void rpm_print_transaction_summary(const rpm_info_t **pkgs, size_t count, const char *action) {
    printf("\nDependencies resolved.\n");
    printf("================================================================================\n");
    printf(" %-20s %-15s %-25s %-15s\n", "Package", "Architecture", "Version", "Repository");
    printf("================================================================================\n");
    printf("%s:\n", action);

    for (size_t i = 0; i < count; i++) {
        const rpm_info_t *info = pkgs[i];
        printf(" %-20s %-15s %-25s %-15s\n",
               info->name,
               info->arch ? info->arch : "noarch",
               info->version,
               "@commandline");
    }
    printf("\nTransaction Summary\n");
    printf("================================================================================\n");
    printf("%-10s %zu Package%s\n\n", action, count, count > 1 ? "s" : "");
}

static char *get_tag_string(const rpm_index_entry_t *entry, const uint8_t *data_store) {
    uint32_t type = read_be32(entry->type);
    if (type == RPM_STRING_TYPE || type == RPM_I18NSTRING_TYPE) {
        return strdup((const char *)(data_store + read_be32(entry->offset)));
    } else if (type == 3) { // INT16
        uint16_t v;
        memcpy(&v, data_store + read_be32(entry->offset), 2);
        uint32_t val = be16toh(v);
        char buf[16];
        snprintf(buf, sizeof(buf), "%u", val);
        return strdup(buf);
    } else if (type == 4) { // INT32
        uint32_t v;
        memcpy(&v, data_store + read_be32(entry->offset), 4);
        uint32_t val = be32toh(v);
        char buf[16];
        snprintf(buf, sizeof(buf), "%u", val);
        return strdup(buf);
    }
    return NULL;
}

static char **get_tag_string_array(const rpm_index_entry_t *entry, const uint8_t *data_store, uint32_t *count) {
    uint32_t type = read_be32(entry->type);
    if (type != RPM_STRING_ARRAY_TYPE) return NULL;
    *count = read_be32(entry->count);
    char **arr = malloc(sizeof(char *) * (*count));
    const char *p = (const char *)(data_store + read_be32(entry->offset));
    for (uint32_t i = 0; i < *count; i++) {
        arr[i] = strdup(p);
        p += strlen(p) + 1;
    }
    return arr;
}

static uint32_t *get_tag_int32_array(const rpm_index_entry_t *entry, const uint8_t *data_store, uint32_t *count) {
    uint32_t type = read_be32(entry->type);
    if (type != RPM_INT32_TYPE) return NULL;
    *count = read_be32(entry->count);
    uint32_t *arr = malloc(sizeof(uint32_t) * (*count));
    const uint32_t *p = (const uint32_t *)(data_store + read_be32(entry->offset));
    for (uint32_t i = 0; i < *count; i++) {
        arr[i] = be32toh(p[i]);
    }
    return arr;
}

int rpm_parse_header(const uint8_t *data, size_t size, rpm_info_t *info) {
    if (size < sizeof(rpm_header_t)) return -1;

    const rpm_header_t *h = (const rpm_header_t *)data;
    if (memcmp(h->magic, "\x8e\xad\xe8", 3) != 0) return -1;

    uint32_t index_count = read_be32(h->index_count);
    uint32_t data_size = read_be32(h->data_size);

    if (size < sizeof(rpm_header_t) + index_count * sizeof(rpm_index_entry_t) + data_size) {
        return -1;
    }

    const rpm_index_entry_t *indices = (const rpm_index_entry_t *)(data + sizeof(rpm_header_t));
    const uint8_t *data_store = data + sizeof(rpm_header_t) + index_count * sizeof(rpm_index_entry_t);

    char **basenames = NULL;
    uint32_t base_count = 0;
    char **dirnames = NULL;
    uint32_t dir_count = 0;
    uint32_t *dirindexes = NULL;
    uint32_t index_count_files = 0;

    for (uint32_t i = 0; i < index_count; i++) {
        int32_t tag = read_be32(indices[i].tag);
        switch (tag) {
            case RPMTAG_NAME:
                info->name = get_tag_string(&indices[i], data_store);
                break;
            case RPMTAG_VERSION:
                info->version = get_tag_string(&indices[i], data_store);
                break;
            case RPMTAG_RELEASE:
                info->release = get_tag_string(&indices[i], data_store);
                break;
            case RPMTAG_EPOCH:
                info->epoch = get_tag_string(&indices[i], data_store);
                break;
            case RPMTAG_ARCH:
                info->arch = get_tag_string(&indices[i], data_store);
                break;
            case RPMTAG_SUMMARY:
                info->summary = get_tag_string(&indices[i], data_store);
                break;
            case RPMTAG_DESCRIPTION:
                info->description = get_tag_string(&indices[i], data_store);
                break;
            case RPMTAG_PAYLOADCOMPRESSOR:
                info->payload_compressor = get_tag_string(&indices[i], data_store);
                break;
            case RPMTAG_BASENAMES:
                basenames = get_tag_string_array(&indices[i], data_store, &base_count);
                break;
            case RPMTAG_DIRNAMES:
                dirnames = get_tag_string_array(&indices[i], data_store, &dir_count);
                break;
            case RPMTAG_DIRINDEXES:
                dirindexes = get_tag_int32_array(&indices[i], data_store, &index_count_files);
                break;
            case RPMTAG_PROVIDENAME:
                info->provides_list = get_tag_string_array(&indices[i], data_store, (uint32_t *)&info->provides_count);
                break;
        }
    }

    // Combine dirnames + basenames into full paths
    if (basenames && dirnames && dirindexes && base_count == index_count_files) {
        info->file_list = malloc(sizeof(char *) * base_count);
        info->file_count = base_count;
        for (uint32_t i = 0; i < base_count; i++) {
            uint32_t di = dirindexes[i];
            if (di < dir_count) {
                size_t len = strlen(dirnames[di]) + strlen(basenames[i]) + 1;
                info->file_list[i] = malloc(len);
                sprintf(info->file_list[i], "%s%s", dirnames[di], basenames[i]);
            } else {
                info->file_list[i] = strdup(basenames[i]);
            }
        }
    }

    // Cleanup temporary arrays
    if (basenames) {
        for (uint32_t i = 0; i < base_count; i++) free(basenames[i]);
        free(basenames);
    }
    if (dirnames) {
        for (uint32_t i = 0; i < dir_count; i++) free(dirnames[i]);
        free(dirnames);
    }
    if (dirindexes) free(dirindexes);

    return 0;
}

int rpm_parse_file(const char *filename, rpm_info_t *info) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        dvf_log_error("Could not open RPM file: %s\n", filename);
        return -1;
    }

    memset(info, 0, sizeof(rpm_info_t));

    // 1. Read Lead
    rpm_lead_t lead;
    if (fread(&lead, sizeof(lead), 1, f) != 1) goto err;
    if (memcmp(lead.magic, "\xed\xab\xee\xdb", 4) != 0) {
        dvf_log_error("Not a valid RPM file (Lead magic mismatch)\n");
        goto err;
    }

    // 2. Read Signature Header
    rpm_header_t sig_header;
    if (fread(&sig_header, sizeof(sig_header), 1, f) != 1) goto err;
    if (memcmp(sig_header.magic, "\x8e\xad\xe8", 3) != 0) {
        dvf_log_error("Invalid Signature Header magic\n");
        goto err;
    }

    uint32_t sig_index_count = read_be32(sig_header.index_count);
    uint32_t sig_data_size = read_be32(sig_header.data_size);

    // Skip signature index and data
    fseek(f, sig_index_count * 16 + sig_data_size, SEEK_CUR);

    // Padding: Signature is padded to 8-byte boundary
    long current_pos = ftell(f);
    if (current_pos % 8 != 0) {
        fseek(f, 8 - (current_pos % 8), SEEK_CUR);
    }

    // 3. Read Main Header
    rpm_header_t main_header;
    long main_header_pos = ftell(f);
    if (fread(&main_header, sizeof(main_header), 1, f) != 1) goto err;
    if (memcmp(main_header.magic, "\x8e\xad\xe8", 3) != 0) {
        dvf_log_error("Invalid Main Header magic\n");
        goto err;
    }

    uint32_t index_count = read_be32(main_header.index_count);
    uint32_t data_size = read_be32(main_header.data_size);
    size_t header_full_size = sizeof(rpm_header_t) + index_count * sizeof(rpm_index_entry_t) + data_size;

    uint8_t *header_buf = malloc(header_full_size);
    fseek(f, main_header_pos, SEEK_SET);
    if (fread(header_buf, 1, header_full_size, f) != header_full_size) {
        free(header_buf);
        goto err;
    }

    if (rpm_parse_header(header_buf, header_full_size, info) != 0) {
        free(header_buf);
        goto err;
    }

    info->payload_offset = ftell(f);

    free(header_buf);
    fclose(f);
    return 0;

err:
    fclose(f);
    return -1;
}

int rpm_unpack(const char *filename, const char *dest_dir) {
    rpm_info_t info;
    if (rpm_parse_file(filename, &info) != 0) return -1;

    char cmd[8192];
    const char *decompressor = "cat";
    if (info.payload_compressor) {
        if (strcmp(info.payload_compressor, "zstd") == 0) decompressor = "unzstd";
        else if (strcmp(info.payload_compressor, "xz") == 0) decompressor = "xzcat";
        else if (strcmp(info.payload_compressor, "gzip") == 0) decompressor = "zcat";
        else if (strcmp(info.payload_compressor, "lzma") == 0) decompressor = "lzcat";
    }

    char abs_path[4096];
    if (realpath(filename, abs_path) == NULL) {
        strncpy(abs_path, filename, sizeof(abs_path) - 1);
    }

    const char *target = (dest_dir && strlen(dest_dir) > 0) ? dest_dir : "/";

    // We use a subshell to change directory safely without affecting the main process.
    // tail -c +N is 1-indexed, payload_offset is 0-indexed.
    // Use --no-absolute-filenames to ensure everything stays inside target even if RPM has absolute paths.
    snprintf(cmd, sizeof(cmd),
             "sh -c \"mkdir -p '%s' && cd '%s' && tail -c +%ld '%s' | %s | cpio -idmu%s --quiet --no-absolute-filenames\"",
             target, target, info.payload_offset + 1, abs_path, decompressor, g_verbose_mode ? "v" : "");

    dvf_log_verbose("Extracting %s payload to %s using %s...\n", info.name, target, decompressor);

    int res = system(cmd);

    rpm_free_info(&info);
    return (res == 0) ? 0 : -1;
}
