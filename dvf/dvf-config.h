#ifndef DVF_CONFIG_H
#define DVF_CONFIG_H

#include <stdbool.h>

extern char *g_dvf_base_dir;
extern char *g_dvf_db_dir;
extern char *g_dvf_cache_dir;

void dvf_config_init();
void dvf_config_cleanup();
int dvf_config_load();

#endif // DVF_CONFIG_H
