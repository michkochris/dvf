#ifndef DVF_SQLITE_H
#define DVF_SQLITE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *data;
    size_t size;
} dvf_blob_t;

typedef struct {
    dvf_blob_t *blobs;
    size_t count;
} dvf_blob_list_t;

dvf_blob_list_t *dvf_sqlite_get_package_blobs(const char *db_path);
void dvf_sqlite_free_blob_list(dvf_blob_list_t *list);

#ifdef __cplusplus
}
#endif

#endif // DVF_SQLITE_H
