#ifndef DVF_CONFIG_H
#define DVF_CONFIG_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

extern char *g_dvf_base_dir;
extern char *g_dvf_db_dir;
extern char *g_dvf_cache_dir;
extern char *g_dvf_install_root;
extern char *g_dvf_repo_dir;
extern bool g_dvf_md5_checks;
extern bool g_dvf_cleanup;

void dvf_config_init();
void dvf_config_cleanup();
int dvf_config_load();

#ifdef __cplusplus
}
#endif

#endif // DVF_CONFIG_H
