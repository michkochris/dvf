#ifndef DVF_COMPLETION_H
#define DVF_COMPLETION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint32_t strings_size;
} AutocompleteHeader;

int is_completion_trigger(char *argv[]);
void handle_binary_completion(const char *partial, const char *prev);
int dvf_sync_autocomplete(void);

#ifdef __cplusplus
}
#endif

#endif // DVF_COMPLETION_H
