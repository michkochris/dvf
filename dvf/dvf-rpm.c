#include "dvf-rpm.h"
#include "dvf-util.h"
#include <stdlib.h>
#include <string.h>
#include <endian.h>

static uint32_t read_be32(uint32_t val) {
    return be32toh(val);
}

void rpm_free_info(rpm_info_t *info) {
    free(info->name);
    free(info->version);
    free(info->release);
    free(info->arch);
    free(info->payload_compressor);
    memset(info, 0, sizeof(rpm_info_t));
}

void rpm_print_info(const rpm_info_t *info) {
    printf("\033[1mPackage Info:\033[0m\n");
    printf("  Name       : %s\n", info->name ? info->name : "unknown");
    printf("  Version    : %s\n", info->version ? info->version : "unknown");
    printf("  Release    : %s\n", info->release ? info->release : "unknown");
    printf("  Arch       : %s\n", info->arch ? info->arch : "unknown");
    printf("  Compressor : %s\n", info->payload_compressor ? info->payload_compressor : "unknown");
    printf("  Payload At : %ld bytes\n", info->payload_offset);
}

static char *get_tag_string(const rpm_index_entry_t *entry, const uint8_t *data_store) {
    if (read_be32(entry->type) == RPM_STRING_TYPE || read_be32(entry->type) == RPM_I18NSTRING_TYPE) {
        return strdup((const char *)(data_store + read_be32(entry->offset)));
    }
    return NULL;
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
    if (fread(&main_header, sizeof(main_header), 1, f) != 1) goto err;
    if (memcmp(main_header.magic, "\x8e\xad\xe8", 3) != 0) {
        dvf_log_error("Invalid Main Header magic\n");
        goto err;
    }

    uint32_t index_count = read_be32(main_header.index_count);
    uint32_t data_size = read_be32(main_header.data_size);

    rpm_index_entry_t *indices = malloc(index_count * sizeof(rpm_index_entry_t));
    if (fread(indices, sizeof(rpm_index_entry_t), index_count, f) != index_count) {
        free(indices);
        goto err;
    }

    uint8_t *data_store = malloc(data_size);
    if (fread(data_store, 1, data_size, f) != data_size) {
        free(indices);
        free(data_store);
        goto err;
    }

    // 4. Parse Metadata
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
            case RPMTAG_ARCH:
                info->arch = get_tag_string(&indices[i], data_store);
                break;
            case RPMTAG_PAYLOADCOMPRESSOR:
                info->payload_compressor = get_tag_string(&indices[i], data_store);
                break;
        }
    }

    info->payload_offset = ftell(f);

    free(indices);
    free(data_store);
    fclose(f);
    return 0;

err:
    fclose(f);
    return -1;
}
