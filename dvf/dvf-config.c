#include "dvf-config.h"
#include "dvf-util.h"
#include <stdlib.h>
#include <string.h>

char *g_dvf_base_dir = NULL;
char *g_dvf_db_dir = NULL;
char *g_dvf_cache_dir = NULL;

void dvf_config_cleanup() {
    free(g_dvf_base_dir);
    free(g_dvf_db_dir);
    free(g_dvf_cache_dir);
}

int dvf_config_load() {
    const char *config_path = "/etc/dvf/dvfconfig";
    if (!dvf_util_file_exists(config_path)) {
        char *home = getenv("HOME");
        if (!home) return -1;
        g_dvf_base_dir = dvf_util_concat_path(home, ".dvf");
    } else {
        g_dvf_base_dir = dvf_util_get_config_value(config_path, "dvf_dir", '=');
    }

    if (!g_dvf_base_dir) return -1;

    g_dvf_db_dir = dvf_util_concat_path(g_dvf_base_dir, "db");
    g_dvf_cache_dir = dvf_util_concat_path(g_dvf_base_dir, "cache");

    return 0;
}

void dvf_config_init() {
    if (dvf_config_load() != 0) {
        exit(1);
    }

    dvf_util_create_dir_recursive(g_dvf_base_dir, 0755);
    dvf_util_create_dir_recursive(g_dvf_db_dir, 0755);
    dvf_util_create_dir_recursive(g_dvf_cache_dir, 0755);
}
