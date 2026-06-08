#ifndef DVF_REPO_H
#define DVF_REPO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DVF_STATE_INIT,
    DVF_STATE_RANKING,
    DVF_STATE_DOWNLOADING,
    DVF_STATE_RECOVERY,
    DVF_STATE_SUCCESS,
    DVF_STATE_FATAL
} dvf_state_t;

int dvf_repo_ffi_available(void);
int dvf_repo_update(void);
int dvf_repo_install(const char *pkg_name);

#ifdef __cplusplus
}
#endif

#endif // DVF_REPO_H
