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
    while (isspace((unsigned char)*str) || *str == '\r' || *str == '\n') str++;
    if (*str == 0) return str;
    end = str + strlen(str) - 1;
    while (end > str && (isspace((unsigned char)*end) || *end == '\r' || *end == '\n')) end--;
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

bool dvf_util_prompt_yes_no(const char *prompt) {
    printf("%s [y/N] ", prompt);
    fflush(stdout);
    char buf[16];
    if (!fgets(buf, sizeof(buf), stdin)) return false;
    char *p = dvf_util_trim_whitespace(buf);
    return (strcasecmp(p, "y") == 0 || strcasecmp(p, "yes") == 0);
}

void dvf_util_free_and_null(char **ptr) {
    if (ptr != NULL && *ptr != NULL) {
        free(*ptr);
        *ptr = NULL;
    }
}

static int version_part_order(char c) {
    if (isdigit(c)) return 0;
    if (isalpha(c)) return (unsigned char)c;
    if (c == '~') return -1;
    if (c) return (unsigned char)c + 256;
    return 0;
}

static int compare_version_part(const char *v1, const char *v2) {
    while (*v1 || *v2) {
        int first_diff = 0;
        while ((*v1 && !isdigit(*v1)) || (*v2 && !isdigit(*v2))) {
            int o1 = version_part_order(*v1);
            int o2 = version_part_order(*v2);
            if (o1 != o2) return o1 - o2;
            if (*v1) v1++;
            if (*v2) v2++;
        }
        while (*v1 == '0') v1++;
        while (*v2 == '0') v2++;
        while (isdigit(*v1) && isdigit(*v2)) {
            if (!first_diff) first_diff = *v1 - *v2;
            v1++; v2++;
        }
        if (isdigit(*v1)) return 1;
        if (isdigit(*v2)) return -1;
        if (first_diff) return first_diff;
    }
    return 0;
}

int dvf_util_compare_versions(const char *v1, const char *v2) {
    if (!v1 || !v2) return v1 ? 1 : (v2 ? -1 : 0);

    const char *e1 = strchr(v1, ':');
    long epoch1 = e1 ? strtol(v1, NULL, 10) : 0;
    const char *u1 = e1 ? e1 + 1 : v1;

    const char *e2 = strchr(v2, ':');
    long epoch2 = e2 ? strtol(v2, NULL, 10) : 0;
    const char *u2 = e2 ? e2 + 1 : v2;

    if (epoch1 != epoch2) return (epoch1 > epoch2) ? 1 : -1;

    const char *r1 = strrchr(u1, '-');
    const char *r2 = strrchr(u2, '-');

    if (r1 && r2) {
        size_t len1 = r1 - u1;
        size_t len2 = r2 - u2;
        char *up1 = strndup(u1, len1);
        char *up2 = strndup(u2, len2);
        int res = compare_version_part(up1, up2);
        free(up1); free(up2);
        if (res) return res;
        return compare_version_part(r1 + 1, r2 + 1);
    } else if (r1) {
        char *up1 = strndup(u1, r1 - u1);
        int res = compare_version_part(up1, u2);
        free(up1);
        if (res) return res;
        return compare_version_part(r1 + 1, "");
    } else if (r2) {
        char *up2 = strndup(u2, r2 - u2);
        int res = compare_version_part(u1, up2);
        free(up2);
        if (res) return res;
        return compare_version_part("", r2 + 1);
    }
    return compare_version_part(u1, u2);
}

uint32_t dvf_util_hash_string(const char *str) {
    if (!str) return 0;
    const uint32_t FNV_PRIME_32 = 16777619U;
    const uint32_t FNV_OFFSET_BASIS_32 = 2166136261U;
    uint32_t hash = FNV_OFFSET_BASIS_32;
    for (const char *p = str; *p != '\0'; p++) {
        hash ^= (uint8_t)*p;
        hash *= FNV_PRIME_32;
    }
    return hash;
}
