#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dvf-config.h"
#include "dvf-util.h"
#include "dvf-rpm.h"
#include "dvf-completion.h"
#include "dvf-sqlite.h"

#ifdef ENABLE_CPP_FFI
#include "dvf-repo.h"
#endif

void usage() {
    printf("DVF - Vamped DNF (Successor to dnf)\n\n");
    printf("Usage: dvf [OPTIONS] COMMAND [ARGS]...\n\n");
    printf("Options:\n");
    printf("  -v, --verbose    Enable verbose output\n");
    printf("  -d, --debug      Enable debug output\n\n");
    printf("Commands:\n");
    printf("  update           Update repository metadata\n");
    printf("  install <pkg>    Install a package\n");
    printf("  remove <pkg>     Remove a package\n");
    printf("  search <term>    Search for packages\n");
    printf("  info <pkg>       Show package information\n");
    printf("  sync             Sync autocomplete index from rpmdb\n");
}

int main(int argc, char **argv) {
    if (argc == 4 && is_completion_trigger(argv)) {
        dvf_config_init();
        handle_binary_completion(argv[2], argv[3]);
        dvf_config_cleanup();
        return 0;
    }

    if (argc < 2) {
        usage();
        return 1;
    }

    // Parse global options first
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_verbose_mode = true;
        }
    }

    dvf_config_init();

    int exit_code = 0;

    // Interleaved command handling
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') continue; // Skip options

        if (strcmp(argv[i], "update") == 0) {
            printf("Updating repository metadata...\n");
#ifdef ENABLE_CPP_FFI
            if (dvf_repo_update() != 0) {
                dvf_log_error("Update failed.\n");
                exit_code = 1;
            } else {
                printf("Update complete.\n");
            }
#else
            dvf_log_verbose("Update command called (Core mode - FSM disabled)\n");
            printf("Notice: Full FSM update requires C++ FFI. Rebuild with 'make all'.\n");
#endif
        } else if (strcmp(argv[i], "install") == 0) {
            if (i + 1 < argc) {
                const char *pkg = argv[++i];
                printf("Resolving dependencies for %s...\n", pkg);
#ifdef ENABLE_CPP_FFI
                if (dvf_repo_install(pkg) != 0) {
                    dvf_log_error("Failed to install %s\n", pkg);
                    exit_code = 1;
                }
#else
                printf("Installing %s (Core mode)...\n", pkg);
                printf("Notice: Advanced mirror selection requires C++ FFI.\n");
#endif
            } else {
                dvf_log_error("install requires a package name.\n");
                exit_code = 1;
            }
        } else if (strcmp(argv[i], "remove") == 0) {
             if (i + 1 < argc) {
                const char *pkg = argv[++i];
                printf("Removing %s...\n", pkg);
            } else {
                dvf_log_error("remove requires a package name.\n");
                exit_code = 1;
            }
        } else if (strcmp(argv[i], "search") == 0) {
             if (i + 1 < argc) {
                const char *term = argv[++i];
                printf("Searching for '%s'...\n", term);
            } else {
                dvf_log_error("search requires a search term.\n");
                exit_code = 1;
            }
        } else if (strcmp(argv[i], "info") == 0) {
            if (i + 1 < argc) {
                const char *target = argv[++i];
                printf("Fetching information for %s...\n", target);
                if (dvf_util_file_exists(target) && strstr(target, ".rpm")) {
                    rpm_info_t info;
                    if (rpm_parse_file(target, &info) == 0) {
                        rpm_print_info(&info);
                        rpm_free_info(&info);
                    } else {
                        dvf_log_error("Failed to parse RPM file: %s\n", target);
                    }
                } else {
                    dvf_log_verbose("Searching for installed package '%s'...\n", target);
                    const char *db_path = "/var/lib/rpm/rpmdb.sqlite";
                    if (!dvf_util_file_exists(db_path)) db_path = "rpmdb.sqlite";

                    dvf_blob_list_t *blobs = dvf_sqlite_get_package_blobs(db_path);
                    bool found = false;
                    if (blobs) {
                        for (size_t j = 0; j < blobs->count; j++) {
                            rpm_info_t info;
                            memset(&info, 0, sizeof(info));
                            if (rpm_parse_header(blobs->blobs[j].data, blobs->blobs[j].size, &info) == 0) {
                                if (info.name && strcmp(info.name, target) == 0) {
                                    rpm_print_info(&info);
                                    found = true;
                                    rpm_free_info(&info);
                                    break;
                                }
                            }
                            rpm_free_info(&info);
                        }
                        dvf_sqlite_free_blob_list(blobs);
                    }

                    if (!found) {
                        printf("Package info for %s:\n", target);
                        printf(" (Not found in installed packages. Search in repositories not yet implemented in Core mode)\n");
                    }
                }
            } else {
                dvf_log_error("info requires a package name or .rpm file path.\n");
                exit_code = 1;
            }
        } else if (strcmp(argv[i], "sync") == 0) {
            printf("Synchronizing autocomplete index...\n");
            if (dvf_sync_autocomplete() != 0) {
                dvf_log_error("Failed to sync autocomplete index.\n");
                exit_code = 1;
            } else {
                printf("Autocomplete index sync complete.\n");
            }
        } else {
            dvf_log_error("Unknown command: %s\n", argv[i]);
            exit_code = 1;
            break;
        }
    }

    dvf_config_cleanup();
    return exit_code;
}
