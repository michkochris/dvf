#include "dvf-util.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

bool g_verbose_mode = false;

void dvf_log_verbose(const char *fmt, ...) {
    if (!g_verbose_mode) return;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

void dvf_log_error(const char *fmt, ...) {
    fprintf(stderr, "\033[1;31mError:\033[0m ");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

void dvf_log_debug(const char *fmt, ...) {
#ifdef DEBUG
    printf("\033[1;34mDebug:\033[0m ");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
#else
    (void)fmt;
#endif
}

char *dvf_util_concat_path(const char *dir, const char *file) {
    size_t len = strlen(dir) + strlen(file) + 2;
    char *res = malloc(len);
    if (!res) return NULL;
    sprintf(res, "%s/%s", dir, file);
    return res;
}

bool dvf_util_file_exists(const char *path) {
    return access(path, F_OK) == 0;
}

int dvf_util_create_dir_recursive(const char *path, int mode) {
    char tmp[4096];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') tmp[len - 1] = 0;
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) return -1;
    return 0;
}

char *dvf_util_trim_whitespace(char *str) {
    char *end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

char *dvf_util_get_config_value(const char *filepath, const char *key, char separator) {
    FILE *file = fopen(filepath, "r");
    if (!file) return NULL;

    char line[1024];
    char *value = NULL;
    while (fgets(line, sizeof(line), file)) {
        char *trimmed = dvf_util_trim_whitespace(line);
        if (trimmed[0] == '#' || trimmed[0] == '\0') continue;

        char *sep_pos = strchr(trimmed, separator);
        if (sep_pos) {
            *sep_pos = '\0';
            char *k = dvf_util_trim_whitespace(trimmed);
            if (strcmp(k, key) == 0) {
                value = strdup(dvf_util_trim_whitespace(sep_pos + 1));
                break;
            }
        }
    }
    fclose(file);
    return value;
}

bool dvf_util_parse_yes_no(const char *val, bool default_val) {
    if (!val) return default_val;
    if (strcasecmp(val, "yes") == 0 || strcasecmp(val, "y") == 0 || strcmp(val, "1") == 0 || strcasecmp(val, "true") == 0) return true;
    if (strcasecmp(val, "no") == 0 || strcasecmp(val, "n") == 0 || strcmp(val, "0") == 0 || strcasecmp(val, "false") == 0) return false;
    return default_val;
}
