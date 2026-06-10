#include "dvf-sqlite.h"
#include "dvf-util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <endian.h>
#include <errno.h>

#define SQLITE_HEADER_SIZE 100

static uint64_t read_varint(const uint8_t *p, size_t *len) {
    uint64_t v = 0;
    size_t i;
    for (i = 0; i < 8; i++) {
        v = (v << 7) | (p[i] & 0x7f);
        if (!(p[i] & 0x80)) {
            *len = i + 1;
            return v;
        }
    }
    v = (v << 8) | p[8];
    *len = 9;
    return v;
}

static uint32_t read_be32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return be32toh(v);
}

static uint16_t read_be16(const uint8_t *p) {
    uint16_t v;
    memcpy(&v, p, 2);
    return be16toh(v);
}

static void add_blob(dvf_blob_list_t *list, const uint8_t *data, size_t size) {
    list->blobs = realloc(list->blobs, sizeof(dvf_blob_t) * (list->count + 1));
    list->blobs[list->count].data = malloc(size);
    if (list->blobs[list->count].data) {
        memcpy(list->blobs[list->count].data, data, size);
        list->blobs[list->count].size = size;
        list->count++;
    }
}

static uint32_t get_serial_size(uint64_t serial_type) {
    if (serial_type >= 13) {
        if (serial_type % 2 == 1) return (uint32_t)((serial_type - 13) / 2);
        else return (uint32_t)((serial_type - 12) / 2);
    }
    if (serial_type == 12) return 0;
    static const uint8_t static_sizes[] = {0, 1, 2, 3, 4, 6, 8, 8, 0, 0};
    if (serial_type < 10) return static_sizes[serial_type];
    return 0;
}

static uint8_t *read_full_payload(FILE *f, uint32_t page_size, uint64_t total_payload_size, const uint8_t *local_payload, uint32_t local_size, uint32_t first_overflow_page) {
    uint8_t *full = malloc(total_payload_size);
    if (!full) return NULL;

    memcpy(full, local_payload, local_size);
    uint32_t current_size = local_size;
    uint32_t next_page = first_overflow_page;

    while (next_page != 0 && current_size < total_payload_size) {
        if (fseek(f, (long)(next_page - 1) * page_size, SEEK_SET) != 0) break;
        uint8_t header[4];
        if (fread(header, 1, 4, f) != 4) break;
        uint32_t next = read_be32(header);

        uint32_t to_read = (uint32_t)total_payload_size - current_size;
        if (to_read > page_size - 4) to_read = page_size - 4;

        if (fread(full + current_size, 1, to_read, f) != to_read) break;

        current_size += to_read;
        next_page = next;
    }

    return full;
}

static void parse_page(FILE *f, uint32_t page_num, uint32_t page_size, dvf_blob_list_t *list);

static void parse_table_btree_leaf(FILE *f, const uint8_t *page_start, uint32_t page_num, uint32_t page_size, dvf_blob_list_t *list) {
    const uint8_t *header = (page_num == 1) ? page_start + SQLITE_HEADER_SIZE : page_start;
    uint16_t cell_count = read_be16(header + 3);
    const uint8_t *cell_pointers = header + 8;

    dvf_log_debug("    Parsing B-tree leaf page %u with %d cells\n", page_num, cell_count);

    for (int i = 0; i < cell_count; i++) {
        uint16_t cell_offset = read_be16(cell_pointers + i * 2);
        if (cell_offset >= page_size) continue;
        const uint8_t *cell_ptr = page_start + cell_offset;

        size_t len;
        uint64_t payload_size = read_varint(cell_ptr, &len);
        const uint8_t *p = cell_ptr + len;
        uint64_t rowid = read_varint(p, &len);
        p += len;
        (void)rowid;

        uint32_t U = page_size;
        uint32_t X = U - 35;
        uint32_t M = ((U - 12) * 32 / 255) - 23;
        uint32_t local_size;
        uint32_t first_overflow = 0;

        if (payload_size <= X) {
            local_size = (uint32_t)payload_size;
        } else {
            uint32_t K = M + (uint32_t)((payload_size - M) % (U - 4));
            if (K <= X) local_size = K;
            else local_size = M;
            first_overflow = read_be32(p + local_size);
        }

        uint8_t *full_payload = read_full_payload(f, page_size, payload_size, p, local_size, first_overflow);
        if (!full_payload) continue;

        size_t hdr_len_len;
        uint64_t record_header_size = read_varint(full_payload, &hdr_len_len);
        const uint8_t *hdr_ptr = full_payload + hdr_len_len;
        const uint8_t *hdr_end = full_payload + record_header_size;
        const uint8_t *data_ptr = full_payload + record_header_size;

        while (hdr_ptr < hdr_end && data_ptr < full_payload + payload_size) {
            uint64_t serial_type = read_varint(hdr_ptr, &len);
            hdr_ptr += len;
            uint32_t data_size = get_serial_size(serial_type);

            if (serial_type >= 12 && (serial_type % 2) == 0) {
                // Detection logic for RPM headers in rpmdb.sqlite
                if (data_size > 16 && data_ptr[0] == 0x8e && data_ptr[1] == 0xad && data_ptr[2] == 0xe8) {
                    // Standard header with magic
                    add_blob(list, data_ptr, data_size);
                } else if (data_size > 8) {
                    // Headless header (Index Count + Data Size followed by indices and data)
                    uint32_t count = read_be32(data_ptr);
                    uint32_t d_size = read_be32(data_ptr + 4);
                    if (count > 0 && count < 10000 && (uint64_t)count * 16 + d_size + 8 == data_size) {
                        // Reconstruct full header for the parser
                        uint8_t *full_hdr = malloc(data_size + 8);
                        if (full_hdr) {
                            memcpy(full_hdr, "\x8e\xad\xe8\x01\x00\x00\x00\x00", 8);
                            memcpy(full_hdr + 8, data_ptr, data_size);
                            add_blob(list, full_hdr, data_size + 8);
                            free(full_hdr);
                        }
                    }
                }
            }
            data_ptr += data_size;
        }
        free(full_payload);
    }
}

static void parse_table_btree_interior(FILE *f, const uint8_t *page_start, uint32_t page_num, uint32_t page_size, dvf_blob_list_t *list) {
    const uint8_t *header = (page_num == 1) ? page_start + SQLITE_HEADER_SIZE : page_start;
    uint16_t cell_count = read_be16(header + 3);
    const uint8_t *cell_pointers = header + 12;
    for (int i = 0; i < cell_count; i++) {
        uint16_t cell_offset = read_be16(cell_pointers + i * 2);
        if (cell_offset >= page_size) continue;
        uint32_t left_child = read_be32(header + cell_offset - (page_num == 1 ? SQLITE_HEADER_SIZE : 0));
        parse_page(f, left_child, page_size, list);
    }
    uint32_t right_most = read_be32(header + 8);
    parse_page(f, right_most, page_size, list);
}

static void parse_page(FILE *f, uint32_t page_num, uint32_t page_size, dvf_blob_list_t *list) {
    if (page_num == 0) return;
    uint8_t *page = malloc(page_size);
    if (!page) return;
    if (fseek(f, (long)(page_num - 1) * page_size, SEEK_SET) != 0) {
        free(page);
        return;
    }
    if (fread(page, 1, page_size, f) != page_size) {
        free(page);
        return;
    }

    const uint8_t *header = (page_num == 1) ? page + SQLITE_HEADER_SIZE : page;
    if (header[0] == 0x0d) { // Table Leaf
        parse_table_btree_leaf(f, page, page_num, page_size, list);
    } else if (header[0] == 0x05) { // Table Interior
        parse_table_btree_interior(f, page, page_num, page_size, list);
    }

    free(page);
}

static uint32_t scan_master_for_packages(FILE *f, uint32_t page_num, uint32_t page_size) {
    if (page_num == 0) return 0;
    uint8_t *page = malloc(page_size);
    if (!page) return 0;
    if (fseek(f, (long)(page_num - 1) * page_size, SEEK_SET) != 0) {
        free(page);
        return 0;
    }
    if (fread(page, 1, page_size, f) != page_size) {
        free(page);
        return 0;
    }

    const uint8_t *header = (page_num == 1) ? page + SQLITE_HEADER_SIZE : page;
    uint8_t page_type = header[0];
    uint16_t cell_count = read_be16(header + 3);

    dvf_log_debug("  Scanning sqlite_master page %u (type 0x%02x, cells %u)...\n", page_num, page_type, cell_count);

    uint32_t found_root = 0;

    if (page_type == 0x0d) { // Table Leaf
        const uint8_t *cell_pointers = header + 8;
        for (int i = 0; i < cell_count; i++) {
            uint16_t cell_offset = read_be16(cell_pointers + i * 2);
            if (cell_offset >= page_size) continue;
            const uint8_t *cell_ptr = page + cell_offset;

            size_t len;
            (void)read_varint(cell_ptr, &len);
            const uint8_t *rec_ptr = cell_ptr + len;
            uint64_t rowid = read_varint(rec_ptr, &len);
            rec_ptr += len;
            (void)rowid;

            size_t hdr_len_len;
            uint64_t record_header_size = read_varint(rec_ptr, &hdr_len_len);
            const uint8_t *hdr_ptr = rec_ptr + hdr_len_len;
            const uint8_t *data_ptr = rec_ptr + record_header_size;

            uint64_t serial_types[5] = {0};
            for (int j = 0; j < 5; j++) {
                serial_types[j] = read_varint(hdr_ptr, &len);
                hdr_ptr += len;
            }

            uint32_t type_sz = get_serial_size(serial_types[0]);
            uint32_t name_sz = get_serial_size(serial_types[1]);
            uint32_t tbl_name_sz = get_serial_size(serial_types[2]);

            const uint8_t *name_data = data_ptr + type_sz;
            if (name_sz == 8 && memcmp(name_data, "Packages", 8) == 0) {
                const uint8_t *root_ptr = data_ptr + type_sz + name_sz + tbl_name_sz;
                uint64_t root_st = serial_types[3];
                if (root_st == 1) found_root = root_ptr[0];
                else if (root_st == 2) found_root = read_be16(root_ptr);
                else if (root_st == 4) found_root = read_be32(root_ptr);
    dvf_log_debug("    -> Root page for 'Packages' is %u\n", found_root);
                break;
            }
        }
    } else if (page_type == 0x05) { // Table Interior
        const uint8_t *cell_pointers = header + 12;
        for (int i = 0; i < cell_count; i++) {
            uint16_t cell_offset = read_be16(cell_pointers + i * 2);
            if (cell_offset >= page_size) continue;
            uint32_t left_child = read_be32(header + cell_offset - (page_num == 1 ? SQLITE_HEADER_SIZE : 0));
            found_root = scan_master_for_packages(f, left_child, page_size);
            if (found_root) break;
        }
        if (!found_root) {
            uint32_t right_most = read_be32(header + 8);
            found_root = scan_master_for_packages(f, right_most, page_size);
        }
    }

    free(page);
    return found_root;
}

dvf_blob_list_t *dvf_sqlite_get_package_blobs(const char *db_path) {
    dvf_log_debug("Opening rpmdb at %s\n", db_path);
    FILE *f = fopen(db_path, "rb");
    if (!f) {
        dvf_log_verbose("Could not open %s: %s\n", db_path, strerror(errno));
        return NULL;
    }

    uint8_t header[SQLITE_HEADER_SIZE];
    if (fread(header, 1, SQLITE_HEADER_SIZE, f) != SQLITE_HEADER_SIZE) {
        dvf_log_verbose("Failed to read SQLite header from %s\n", db_path);
        fclose(f);
        return NULL;
    }

    if (memcmp(header, "SQLite format 3", 16) != 0) {
        dvf_log_verbose("File %s is not a SQLite3 database\n", db_path);
        fclose(f);
        return NULL;
    }

    uint32_t page_size = read_be16(header + 16);
    if (page_size == 1) page_size = 65536;
    dvf_log_debug("SQLite page size: %u\n", page_size);

    dvf_blob_list_t *list = calloc(1, sizeof(dvf_blob_list_t));

    dvf_log_debug("  Scanning system package database (rpmdb)...\n");
    uint32_t packages_root = scan_master_for_packages(f, 1, page_size);

    if (packages_root > 0) {
        dvf_log_debug("  Found 'Packages' table, extracting records...\n");
        parse_page(f, packages_root, page_size, list);
        dvf_log_debug("  Extracted %zu raw package records.\n", list->count);
    } else {
        dvf_log_verbose("  Error: Could not find 'Packages' table in rpmdb.\n");
    }

    fclose(f);
    return list;
}

void dvf_sqlite_free_blob_list(dvf_blob_list_t *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) {
        free(list->blobs[i].data);
    }
    free(list->blobs);
    free(list);
}
