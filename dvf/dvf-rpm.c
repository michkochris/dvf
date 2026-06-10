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
    free(info->epoch);
    free(info->arch);
    free(info->summary);
    free(info->description);
    free(info->payload_compressor);
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
        }
    }
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
