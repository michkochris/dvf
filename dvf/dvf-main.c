#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dvf-config.h"
#include "dvf-util.h"
#include "dvf-rpm.h"
#include "dvf-completion.h"
#include "dvf-sqlite.h"
#include "dvf-hash.h"
#include "dvf-storage.h"

#ifdef ENABLE_CPP_FFI
#include "dvf-repo.h"
#endif

void usage() {
    printf("DVF - Vamped Up DNF Dandified YUM (Successor to dnf)\n\n");
    printf("Usage: dvf [OPTIONS] COMMAND [ARGS]...\n\n");
    printf("Options:\n");
    printf("  -v, --verbose    Enable verbose output\n");
    printf("  -d, --debug      Enable debug output\n\n");
    printf("Commands:\n");
    printf("  update           Update repository metadata\n");
    printf("  install <pkg>    Install a package\n");
    printf("  upgrade          Upgrade all installed packages\n");
    printf("  remove <pkg>     Remove a package\n");
    printf("  search <term>    Search for packages\n");
    printf("  info <pkg>       Show package information\n");
    printf("  check-update     Check for available package updates\n");
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

    // Lazy sync: if the autocomplete index is missing, trigger a sync.
    char index_path[4096];
    snprintf(index_path, sizeof(index_path), "%s/autocomplete.bin", g_dvf_db_dir);
    if (!dvf_util_file_exists(index_path)) {
        dvf_log_verbose("Autocomplete index missing. Synchronizing...\n");
        dvf_sync_autocomplete();
    }

    int exit_code = 0;

    // Interleaved command handling
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') continue; // Skip options

        if (strcmp(argv[i], "update") == 0) {
#ifdef ENABLE_CPP_FFI
            if (dvf_repo_update() != 0) {
                dvf_log_error("Update failed.\n");
                exit_code = 1;
            }
#else
            dvf_log_verbose("Update command called (Core mode - FSM disabled)\n");
            printf("Notice: Full FSM update requires C++ FFI. Rebuild with 'make all'.\n");
#endif
        } else if (strcmp(argv[i], "install") == 0) {
            if (i + 1 < argc) {
                const char *pkg = argv[++i];
                if (dvf_util_file_exists(pkg) && strstr(pkg, ".rpm")) {
                    printf("Installing local RPM: %s\n", pkg);
                    if (dvf_util_prompt_yes_no("Proceed with installation?")) {
                        if (rpm_unpack(pkg, g_dvf_install_root) == 0) {
                            printf("\nSuccessfully installed %s to %s\n", pkg, g_dvf_install_root);
                            // Also update pkginfo.bin
                            rpm_info_t info;
                            memset(&info, 0, sizeof(info));
                            if (rpm_parse_file(pkg, &info) == 0) {
                                dvf_storage_write_pkg_info(&info);
                                rpm_free_info(&info);
                            }
                        } else {
                            dvf_log_error("Failed to install local RPM: %s\n", pkg);
                            exit_code = 1;
                        }
                    }
                } else {
                    printf("Resolving dependencies for %s...\n", pkg);
#ifdef ENABLE_CPP_FFI
                    if (dvf_repo_install(pkg) != 0) {
                        dvf_log_error("Failed to install %s\n", pkg);
                        exit_code = 1;
                    }
#else
                    printf("Installing %s (Core mode)...\n", pkg);
                    printf("Notice: Advanced mirror selection and repository install requires C++ FFI.\n");
#endif
                }
            } else {
                dvf_log_error("install requires a package name.\n");
                exit_code = 1;
            }
        } else if (strcmp(argv[i], "upgrade") == 0) {
            printf("Checking for available upgrades...\n");
#ifdef ENABLE_CPP_FFI
            if (dvf_repo_upgrade() != 0) {
                dvf_log_error("Failed to complete upgrade.\n");
                exit_code = 1;
            }
#else
            printf("Notice: Upgrading packages requires C++ FFI.\n");
#endif
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

                // Search installed packages
                const char *db_path = "/var/lib/rpm/rpmdb.sqlite";
                if (!dvf_util_file_exists(db_path)) db_path = "rpmdb.sqlite";
                dvf_blob_list_t *blobs = dvf_sqlite_get_package_blobs(db_path);
                if (blobs) {
                    for (size_t j = 0; j < blobs->count; j++) {
                        rpm_info_t info;
                        memset(&info, 0, sizeof(info));
                        if (rpm_parse_header(blobs->blobs[j].data, blobs->blobs[j].size, &info) == 0) {
                            if (info.name && (strcasestr(info.name, term) || (info.summary && strcasestr(info.summary, term)))) {
                                printf("%s.%s : %s (Installed)\n", info.name, info.arch ? info.arch : "noarch", info.summary ? info.summary : "");
                            }
                        }
                        rpm_free_info(&info);
                    }
                    dvf_sqlite_free_blob_list(blobs);
                }

#ifdef ENABLE_CPP_FFI
                dvf_repo_search(term);
#else
                printf("Notice: Remote repository search requires C++ FFI.\n");
#endif
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
                        dvf_hash_table_t *table = dvf_hash_create_table(blobs->count);
                        for (size_t j = 0; j < blobs->count; j++) {
                            rpm_info_t info;
                            memset(&info, 0, sizeof(info));
                            if (rpm_parse_header(blobs->blobs[j].data, blobs->blobs[j].size, &info) == 0) {
                                dvf_hash_add_package(table, &info);
                            }
                            rpm_free_info(&info);
                        }

                        rpm_info_t *match = dvf_hash_search(table, target);
                        if (match) {
                            rpm_print_info(match);
                            found = true;
                        }
                        dvf_hash_destroy_table(table);
                        dvf_sqlite_free_blob_list(blobs);
                    }

                    if (!found) {
                        printf("Package info for %s:\n", target);
#ifdef ENABLE_CPP_FFI
                        if (dvf_repo_info(target) != 0) {
                             printf(" (Not found in repositories)\n");
                        }
#else
                        printf(" (Not found in installed packages. Search in repositories not yet implemented in Core mode)\n");
#endif
                    }
                }
            } else {
                dvf_log_error("info requires a package name or .rpm file path.\n");
                exit_code = 1;
            }
        } else if (strcmp(argv[i], "check-update") == 0) {
            printf("Checking for available updates...\n");
#ifdef ENABLE_CPP_FFI
            if (dvf_repo_check_updates() != 0) {
                dvf_log_error("Failed to check for updates.\n");
                exit_code = 1;
            }
#else
            printf("Notice: Remote update checking requires C++ FFI.\n");
#endif
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
