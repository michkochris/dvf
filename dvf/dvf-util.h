#ifndef DVF_UTIL_H
#define DVF_UTIL_H

#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

extern bool g_verbose_mode;

void dvf_log_verbose(const char *fmt, ...);
void dvf_log_error(const char *fmt, ...);
void dvf_log_debug(const char *fmt, ...);

char *dvf_util_concat_path(const char *dir, const char *file);
bool dvf_util_file_exists(const char *path);
int dvf_util_create_dir_recursive(const char *path, int mode);
char *dvf_util_trim_whitespace(char *str);
char *dvf_util_get_config_value(const char *filepath, const char *key, char separator);
bool dvf_util_parse_yes_no(const char *val, bool default_val);

int dvf_util_compare_versions(const char *v1, const char *v2);

#ifdef __cplusplus
}
#endif

#endif // DVF_UTIL_H
