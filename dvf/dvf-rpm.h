#ifndef DVF_RPM_H
#define DVF_RPM_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// RPM Lead (Legacy, 96 bytes)
typedef struct {
    uint8_t magic[4];   // ed ab ee db
    uint8_t major;
    uint8_t minor;
    uint16_t type;
    uint16_t archnum;
    char name[66];
    uint16_t osnum;
    uint16_t signature_type;
    uint8_t reserved[16];
} __attribute__((packed)) rpm_lead_t;

// RPM Header Structure (16 bytes)
typedef struct {
    uint8_t magic[3];   // 8e ad e8
    uint8_t version;     // 01
    uint32_t reserved;
    uint32_t index_count;
    uint32_t data_size;
} __attribute__((packed)) rpm_header_t;

// RPM Index Entry (16 bytes)
typedef struct {
    int32_t tag;
    int32_t type;
    int32_t offset;
    int32_t count;
} __attribute__((packed)) rpm_index_entry_t;

// Important Tags
#define RPMTAG_NAME 1000
#define RPMTAG_VERSION 1001
#define RPMTAG_RELEASE 1002
#define RPMTAG_EPOCH 1003
#define RPMTAG_SUMMARY 1004
#define RPMTAG_DESCRIPTION 1005
#define RPMTAG_ARCH 1022
#define RPMTAG_BASENAMES 1028
#define RPMTAG_DIRNAMES 1030
#define RPMTAG_DIRINDEXES 1031
#define RPMTAG_PROVIDENAME 1047
#define RPMTAG_PAYLOADCOMPRESSOR 1125

// Important Types
#define RPM_STRING_TYPE 6
#define RPM_STRING_ARRAY_TYPE 8
#define RPM_I18NSTRING_TYPE 9
#define RPM_INT32_TYPE 4

typedef struct {
    char *name;
    char *version;
    char *release;
    char *epoch;
    char *arch;
    char *summary;
    char *description;
    char *payload_compressor;
    long payload_offset;

    char **file_list;
    size_t file_count;

    char **provides_list;
    size_t provides_count;
} rpm_info_t;

int rpm_parse_file(const char *filename, rpm_info_t *info);
int rpm_parse_header(const uint8_t *data, size_t size, rpm_info_t *info);
void rpm_free_info(rpm_info_t *info);
void rpm_print_info(const rpm_info_t *info);
void rpm_print_transaction_summary(const rpm_info_t **pkgs, size_t count, const char *action);
int rpm_unpack(const char *filename, const char *dest_dir);

#ifdef __cplusplus
}
#endif

#endif // DVF_RPM_H
