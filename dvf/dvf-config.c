#include "dvf-config.h"
#include "dvf-util.h"
#include <stdlib.h>
#include <string.h>

char *g_dvf_base_dir = NULL;
char *g_dvf_db_dir = NULL;
char *g_dvf_cache_dir = NULL;
char *g_dvf_install_root = NULL;
char *g_dvf_repo_dir = NULL;
bool g_dvf_md5_checks = true;
bool g_dvf_cleanup = true;

void dvf_config_cleanup() {
    free(g_dvf_base_dir);
    free(g_dvf_db_dir);
    free(g_dvf_cache_dir);
    free(g_dvf_install_root);
    free(g_dvf_repo_dir);
}

int dvf_config_load() {
    // Strictly follow system-wide config only
    const char *config_path = "/etc/yum.repos.d/dvf-config";

    if (!dvf_util_file_exists(config_path)) {
        return -1;
    }

    g_dvf_base_dir = dvf_util_get_config_value(config_path, "dvf_dir", '=');

    if (!g_dvf_base_dir) {
        return -1;
    }

    g_dvf_db_dir = dvf_util_concat_path(g_dvf_base_dir, "db");
    g_dvf_cache_dir = dvf_util_concat_path(g_dvf_base_dir, "cache");

    g_dvf_install_root = dvf_util_get_config_value(config_path, "install_root", '=');
    if (!g_dvf_install_root) {
        g_dvf_install_root = strdup("/");
    }

    g_dvf_repo_dir = dvf_util_get_config_value(config_path, "repo_dir", '=');

    char *md5_val = dvf_util_get_config_value(config_path, "md5_checks", '=');
    g_dvf_md5_checks = dvf_util_parse_yes_no(md5_val, true);
    free(md5_val);

    char *cleanup_val = dvf_util_get_config_value(config_path, "cleanup", '=');
    g_dvf_cleanup = dvf_util_parse_yes_no(cleanup_val, true);
    free(cleanup_val);

    return 0;
}

void dvf_config_init() {
    if (dvf_config_load() != 0) {
        // If config is missing or invalid, we cannot proceed safely
        fprintf(stderr, "Error: Configuration file /etc/yum.repos.d/dvf-config not found or invalid.\n");
        exit(1);
    }

    // Ensure the directories specified in the config exist
    dvf_util_create_dir_recursive(g_dvf_base_dir, 0755);
    dvf_util_create_dir_recursive(g_dvf_db_dir, 0755);
    dvf_util_create_dir_recursive(g_dvf_cache_dir, 0755);
}
