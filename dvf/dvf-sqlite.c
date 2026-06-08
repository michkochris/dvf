#include "dvf-sqlite.h"
#include "dvf-util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <endian.h>

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
    memcpy(list->blobs[list->count].data, data, size);
    list->blobs[list->count].size = size;
    list->count++;
}

static void parse_page(FILE *f, uint32_t page_num, uint32_t page_size, dvf_blob_list_t *list);

static void parse_table_btree_leaf(const uint8_t *page, uint32_t page_size, dvf_blob_list_t *list) {
    uint16_t cell_count = read_be16(page + 3);
    uint16_t cell_content_offset = read_be16(page + 5);
    if (cell_content_offset == 0) cell_content_offset = 65536;

    const uint8_t *cell_pointers = page + 8;
    for (int i = 0; i < cell_count; i++) {
        uint16_t cell_offset = read_be16(cell_pointers + i * 2);
        const uint8_t *cell = page + cell_offset;

        size_t len;
        uint64_t payload_size = read_varint(cell, &len);
        cell += len;
        uint64_t rowid = read_varint(cell, &len);
        cell += len;

        // Simplified: Assume record fits in page (no overflow for now)
        // Record format: header_size (varint), serial_types (varints), data
        size_t hdr_len;
        uint64_t header_size = read_varint(cell, &hdr_len);
        const uint8_t *hdr_end = cell + header_size;
        const uint8_t *data_ptr = cell + header_size;
        const uint8_t *hdr_ptr = cell + hdr_len;

        // RPM Packages table usually has (key, blob) or similar
        // We look for the BLOB serial type (usually > 12 and even)
        while (hdr_ptr < hdr_end) {
            uint64_t serial_type = read_varint(hdr_ptr, &len);
            hdr_ptr += len;

            uint64_t data_size = 0;
            if (serial_type >= 12 && (serial_type % 2) == 0) {
                data_size = (serial_type - 12) / 2;
                // If it looks like an RPM header (magic \x8e\xad\xe8)
                if (data_size > 16 && data_ptr[0] == 0x8e && data_ptr[1] == 0xad && data_ptr[2] == 0xe8) {
                    add_blob(list, data_ptr, data_size);
                }
            } else if (serial_type >= 13 && (serial_type % 2) == 1) {
                data_size = (serial_type - 13) / 2;
            } else {
                static const uint8_t static_sizes[] = {0, 1, 2, 3, 4, 6, 8, 8, 0, 0};
                if (serial_type < 10) data_size = static_sizes[serial_type];
            }
            data_ptr += data_size;
        }
    }
}

static void parse_table_btree_interior(FILE *f, const uint8_t *page, uint32_t page_size, dvf_blob_list_t *list) {
    uint16_t cell_count = read_be16(page + 3);
    const uint8_t *cell_pointers = page + 12;
    for (int i = 0; i < cell_count; i++) {
        uint16_t cell_offset = read_be16(cell_pointers + i * 2);
        uint32_t left_child = read_be32(page + cell_offset);
        parse_page(f, left_child, page_size, list);
    }
    uint32_t right_most = read_be32(page + 8);
    parse_page(f, right_most, page_size, list);
}

static void parse_page(FILE *f, uint32_t page_num, uint32_t page_size, dvf_blob_list_t *list) {
    uint8_t *page = malloc(page_size);
    fseek(f, (page_num - 1) * page_size, SEEK_SET);
    if (fread(page, 1, page_size, f) != page_size) {
        free(page);
        return;
    }

    uint8_t *p = (page_num == 1) ? page + SQLITE_HEADER_SIZE : page;
    if (p[0] == 0x0d) { // Table Leaf
        parse_table_btree_leaf(p, page_size, list);
    } else if (p[0] == 0x05) { // Table Interior
        parse_table_btree_interior(f, p, page_size, list);
    }

    free(page);
}

dvf_blob_list_t *dvf_sqlite_get_package_blobs(const char *db_path) {
    FILE *f = fopen(db_path, "rb");
    if (!f) return NULL;

    uint8_t header[SQLITE_HEADER_SIZE];
    if (fread(header, 1, SQLITE_HEADER_SIZE, f) != SQLITE_HEADER_SIZE) {
        fclose(f);
        return NULL;
    }

    if (memcmp(header, "SQLite format 3", 16) != 0) {
        fclose(f);
        return NULL;
    }

    uint32_t page_size = read_be16(header + 16);
    if (page_size == 1) page_size = 65536;

    dvf_blob_list_t *list = calloc(1, sizeof(dvf_blob_list_t));

    // We need to find the 'Packages' table root page.
    // For rpmdb.sqlite, it's typically page 2 or we can parse sqlite_master on page 1.
    // Simplification: try page 2 first as it's common for RPM, but let's be safer.
    // Actually, in many rpmdb.sqlite, 'Packages' is one of the first tables.

    // Scan page 1 (sqlite_master) to find "Packages" table
    parse_page(f, 1, page_size, list); // This will parse page 1's B-tree

    // Wait, the above will parse blobs from sqlite_master.
    // We need the rootpage of 'Packages'.
    // Let's implement a quick scan of page 1 to find the rootpage.

    fseek(f, 0, SEEK_SET);
    uint8_t *page1 = malloc(page_size);
    fread(page1, 1, page_size, f);

    // Minimalist scan for "Packages" in sqlite_master
    uint16_t cell_count = read_be16(page1 + SQLITE_HEADER_SIZE + 3);
    const uint8_t *cell_pointers = page1 + SQLITE_HEADER_SIZE + 8;
    uint32_t packages_root = 0;

    for (int i = 0; i < cell_count; i++) {
        uint16_t cell_offset = read_be16(cell_pointers + i * 2);
        const uint8_t *cell = page1 + cell_offset;
        size_t len;
        read_varint(cell, &len); cell += len; // payload
        read_varint(cell, &len); cell += len; // rowid

        uint64_t hdr_size = read_varint(cell, &len);
        const uint8_t *data = cell + hdr_size;
        const uint8_t *hdr = cell + len;

        // sqlite_master: type, name, tbl_name, rootpage, sql
        // name is 2nd column
        uint64_t type_st = read_varint(hdr, &len); hdr += len;
        uint64_t name_st = read_varint(hdr, &len); hdr += len;
        uint64_t tbl_st = read_varint(hdr, &len); hdr += len;
        uint64_t root_st = read_varint(hdr, &len);

        uint32_t type_sz = (type_st >= 13) ? (type_st - 13) / 2 : 0;
        uint32_t name_sz = (name_st >= 13) ? (name_st - 13) / 2 : 0;

        const uint8_t *name_ptr = data + type_sz;
        if (name_sz == 8 && memcmp(name_ptr, "Packages", 8) == 0) {
            // rootpage is 4th column. Serial type 1-4 are integers.
            const uint8_t *root_ptr = data + type_sz + name_sz + ((tbl_st >= 13) ? (tbl_st - 13) / 2 : 0);
            if (root_st == 1) packages_root = root_ptr[0];
            else if (root_st == 2) packages_root = read_be16(root_ptr);
            else if (root_st == 4) packages_root = read_be32(root_ptr);
            break;
        }
    }
    free(page1);

    if (packages_root > 0) {
        list->count = 0; // Reset count from sqlite_master parse
        parse_page(f, packages_root, page_size, list);
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
