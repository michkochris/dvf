#include "dvf-completion.h"
#include "dvf-sqlite.h"
#include "dvf-rpm.h"
#include "dvf-util.h"
#include "dvf-config.h"
#ifdef ENABLE_CPP_FFI
#include "dvf-repo.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>
#include <stdbool.h>
#include <ctype.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

int is_completion_trigger(char *argv[]) {
    (void)argv;
    return getenv("COMP_LINE") != NULL;
}

static int compare_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

int dvf_sync_autocomplete(void) {
    dvf_log_verbose("Starting autocomplete index synchronization...\n");
    printf("[1/4] Reading installed package database (rpmdb)...\n");

    const char *db_path = "/var/lib/rpm/rpmdb.sqlite";
    if (!dvf_util_file_exists(db_path)) {
        // Maybe try a relative path for testing or common alternate locations
        if (dvf_util_file_exists("rpmdb.sqlite")) {
             db_path = "rpmdb.sqlite";
        } else {
             dvf_log_verbose("  Primary rpmdb not found at %s\n", db_path);
        }
    }

    dvf_blob_list_t *blobs = dvf_sqlite_get_package_blobs(db_path);
    if (!blobs) {
        dvf_log_error("Failed to read rpmdb.sqlite at %s\n", db_path);
        return -1;
    }

    dvf_log_verbose("[2/4] Extracting package metadata from %zu records...\n", blobs->count);
    printf("  Found %zu installed packages.\n", blobs->count);

    char **names = NULL;
    size_t count = 0;

    for (size_t i = 0; i < blobs->count; i++) {
        rpm_info_t info;
        memset(&info, 0, sizeof(info));
        if (rpm_parse_header(blobs->blobs[i].data, blobs->blobs[i].size, &info) == 0) {
            if (info.name) {
                names = realloc(names, sizeof(char *) * (count + 1));
                names[count++] = info.name;
                info.name = NULL;
            } else {
                dvf_log_debug("  Blob %zu parsed but had no name tag.\n", i);
            }
        } else {
            dvf_log_debug("  Failed to parse RPM header for blob %zu (size %zu).\n", i, blobs->blobs[i].size);
        }
        rpm_free_info(&info);
    }
    dvf_sqlite_free_blob_list(blobs);

#ifdef ENABLE_CPP_FFI
    dvf_log_verbose("  Checking for repository metadata...\n");
    size_t repo_count = 0;
    char **repo_names = dvf_repo_get_all_names(&repo_count);
    if (repo_names) {
        printf("  Found %zu packages in remote repositories.\n", repo_count);
        names = (char**)realloc(names, sizeof(char *) * (count + repo_count));
        for (size_t i = 0; i < repo_count; i++) {
            names[count++] = repo_names[i];
        }
        free(repo_names); // Array only, strings are now in names[]
    } else {
        printf("  No repository metadata found. Run 'dvf update' first.\n");
    }
#endif

    if (count == 0) {
        dvf_log_verbose("No packages found to index.\n");
        return 0;
    }

    printf("[3/4] Processing and deduplicating %zu total package names...\n", count);
    qsort(names, count, sizeof(char *), compare_strings);

    // Deduplicate
    size_t unique_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (i > 0 && strcmp(names[i], names[i-1]) == 0) {
            free(names[i]);
            continue;
        }
        names[unique_count++] = names[i];
    }
    count = unique_count;

    printf("[4/4] Rebuilding autocomplete index with %zu unique entries...\n", count);

    size_t strings_size = 0;
    for (size_t i = 0; i < count; i++) {
        strings_size += strlen(names[i]) + 1;
    }

    char index_path[PATH_MAX];
    snprintf(index_path, sizeof(index_path), "%s/autocomplete.bin", g_dvf_db_dir);

    FILE *fp = fopen(index_path, "wb");
    if (!fp) {
        dvf_log_error("Failed to open %s for writing\n", index_path);
        for (size_t i = 0; i < count; i++) free(names[i]);
        free(names);
        return -1;
    }

    AutocompleteHeader hdr = {
        .magic = 0x44564643, // "DVFC"
        .version = 1,
        .entry_count = (uint32_t)count,
        .strings_size = (uint32_t)strings_size
    };
    fwrite(&hdr, sizeof(hdr), 1, fp);

    uint32_t offset = 0;
    for (size_t i = 0; i < count; i++) {
        fwrite(&offset, sizeof(uint32_t), 1, fp);
        offset += strlen(names[i]) + 1;
    }

    for (size_t i = 0; i < count; i++) {
        fwrite(names[i], strlen(names[i]) + 1, 1, fp);
        free(names[i]);
    }
    free(names);
    fclose(fp);

    chmod(index_path, 0644);
    dvf_log_verbose("Synchronization complete. Index saved to %s\n", index_path);
    return 0;
}

static void scan_rpm_recursive(const char *base, const char *partial, int depth) {
    if (depth > 64) return;
    DIR *dir = opendir(base);
    if (!dir) return;

    struct dirent *entry;
    char path[PATH_MAX];
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        snprintf(path, sizeof(path), "%.*s/%s", (int)(sizeof(path)-258), base, entry->d_name);
        struct stat st;
        if (lstat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_rpm_recursive(path, partial, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            size_t len = strlen(entry->d_name);
            if (len > 4 && strcmp(entry->d_name + len - 4, ".rpm") == 0) {
                char rel[PATH_MAX];
                if (strncmp(path, "./", 2) == 0) snprintf(rel, sizeof(rel), "%s", path + 2);
                else snprintf(rel, sizeof(rel), "%s", path);
                if (strncmp(rel, partial, strlen(partial)) == 0) {
                    printf("%s\n", rel);
                }
            }
        }
    }
    closedir(dir);
}

void complete_rpm_files(const char *partial) {
    scan_rpm_recursive(".", partial, 0);
}

void complete_file_paths_ext(const char *partial, const char *extra_dir, const char *suffix_filter) {
    const char *prefix = partial ? partial : "";

    char dirbuf[PATH_MAX];
    char namebuf[PATH_MAX];
    const char *last_slash = strrchr(prefix, '/');
    const char *search_dir = ".";
    const char *match_prefix = prefix;
    if (last_slash) {
        size_t dirlen = last_slash - prefix;
        if (dirlen >= sizeof(dirbuf)) dirlen = sizeof(dirbuf)-1;
        memcpy(dirbuf, prefix, dirlen);
        dirbuf[dirlen] = '\0';

        if (dirlen == 0 && prefix[0] == '/') {
            search_dir = "/";
        } else {
            search_dir = dirbuf[0] ? dirbuf : ".";
        }
        match_prefix = last_slash + 1;
    }

    DIR *d = opendir(search_dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            if (strncmp(e->d_name, match_prefix, strlen(match_prefix)) != 0) continue;

            bool is_dir = (e->d_type == DT_DIR);
            bool is_reg = (e->d_type == DT_REG);
            if (e->d_type == DT_UNKNOWN) {
                char full[PATH_MAX];
                snprintf(full, sizeof(full), "%.*s/%s", (int)(sizeof(full)-258), search_dir, e->d_name);
                struct stat st;
                if (stat(full, &st) == 0) {
                    is_dir = S_ISDIR(st.st_mode);
                    is_reg = S_ISREG(st.st_mode);
                }
            }

            if (suffix_filter && is_reg) {
                size_t nlen = strlen(e->d_name);
                size_t slen = strlen(suffix_filter);
                if (nlen < slen || strcmp(e->d_name + nlen - slen, suffix_filter) != 0) continue;
            } else if (suffix_filter && !is_dir) {
                continue;
            }

            if (last_slash) {
                size_t sd_len = strlen(search_dir);
                const char *sep = (sd_len > 0 && search_dir[sd_len-1] == '/') ? "" : "/";
                snprintf(namebuf, sizeof(namebuf), "%.*s%s%.*s",
                         (int)(sizeof(namebuf)/2), search_dir,
                         sep,
                         (int)(sizeof(namebuf)/2 - 2), e->d_name);
            } else {
                snprintf(namebuf, sizeof(namebuf), "%s", e->d_name);
            }

            struct stat st;
            if (stat(namebuf, &st) == 0 && S_ISDIR(st.st_mode)) {
                if (namebuf[strlen(namebuf)-1] != '/') {
                    printf("%s/\n", namebuf);
                } else {
                    printf("%s\n", namebuf);
                }
            } else {
                printf("%s\n", namebuf);
            }
        }
        closedir(d);
    }

    if (extra_dir && !last_slash) {
        DIR *ed = opendir(extra_dir);
        if (ed) {
            struct dirent *e;
            while ((e = readdir(ed)) != NULL) {
                if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
                if (strncmp(e->d_name, match_prefix, strlen(match_prefix)) != 0) continue;

                bool is_dir = (e->d_type == DT_DIR);
                bool is_reg = (e->d_type == DT_REG);
                if (e->d_type == DT_UNKNOWN) {
                    char full[PATH_MAX];
                    snprintf(full, sizeof(full), "%.*s/%s", (int)(sizeof(full)-258), extra_dir, e->d_name);
                    struct stat st;
                    if (stat(full, &st) == 0) {
                        is_dir = S_ISDIR(st.st_mode);
                        is_reg = S_ISREG(st.st_mode);
                    }
                }

                if (suffix_filter && is_reg) {
                    size_t nlen = strlen(e->d_name);
                    size_t slen = strlen(suffix_filter);
                    if (nlen < slen || strcmp(e->d_name + nlen - slen, suffix_filter) != 0) continue;
                } else if (suffix_filter && !is_dir) {
                    continue;
                }

                snprintf(namebuf, sizeof(namebuf), "%.*s/%s", (int)(sizeof(namebuf)-258), extra_dir, e->d_name);
                struct stat st;
                if (stat(namebuf, &st) == 0 && S_ISDIR(st.st_mode)) {
                    if (namebuf[strlen(namebuf)-1] != '/') {
                        printf("%s/\n", namebuf);
                    } else {
                        printf("%s\n", namebuf);
                    }
                } else {
                    printf("%s\n", namebuf);
                }
            }
            closedir(ed);
        }
    }
}

void complete_file_paths(const char *partial) {
    complete_file_paths_ext(partial, NULL, NULL);
}

static void prefix_search_and_print(const char *prefix) {
    char index_path[PATH_MAX];
    snprintf(index_path, sizeof(index_path), "%s/autocomplete.bin", g_dvf_db_dir);

    int fd = open(index_path, O_RDONLY);
    if (fd < 0) return;

    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return; }

    void *mapped = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) { close(fd); return; }

    AutocompleteHeader *hdr = (AutocompleteHeader *)mapped;
    if (hdr->magic != 0x44564643) { munmap(mapped, st.st_size); close(fd); return; }

    uint32_t *offsets = (uint32_t *)((char *)mapped + sizeof(AutocompleteHeader));
    char *names = (char *)mapped + sizeof(AutocompleteHeader) + hdr->entry_count * sizeof(uint32_t);

    int low = 0, high = hdr->entry_count - 1;
    int first_match = -1;
    size_t plen = strlen(prefix);

    while (low <= high) {
        int mid = low + (high - low) / 2;
        char *current_name = names + offsets[mid];
        int cmp = strncmp(prefix, current_name, plen);
        if (cmp == 0) {
            first_match = mid; high = mid - 1;
        } else if (cmp < 0) {
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }

    if (first_match != -1) {
        char last_printed[PATH_MAX] = {0};
        for (int i = first_match; (uint32_t)i < hdr->entry_count; i++) {
            char *name = names + offsets[i];
            if (strncmp(prefix, name, plen) != 0) break;

            bool prefix_has_slash = (strchr(prefix, '/') != NULL);
            bool name_has_slash = (strchr(name, '/') != NULL);

            if (plen > 0) {
                if (prefix_has_slash != name_has_slash) continue;
            }

            if (name_has_slash && prefix_has_slash) {
                const char *search_start = name + plen;
                const char *next_slash = strchr(search_start, '/');
                if (next_slash) {
                    char segment[PATH_MAX];
                    size_t seg_len = (next_slash - name) + 1;
                    if (seg_len < sizeof(segment)) {
                        strncpy(segment, name, seg_len);
                        segment[seg_len] = '\0';
                        if (strcmp(segment, last_printed) != 0) {
                            printf("%s\n", segment);
                            strncpy(last_printed, segment, sizeof(last_printed)-1);
                        }
                        /* Also print the full name as a "secondary" option to keep completion open and prevent jumping */
                        printf("%s\n", name);
                        continue;
                    }
                }
            }
            printf("%s\n", name);
        }
    }

    munmap(mapped, st.st_size);
    close(fd);
}

void handle_binary_completion(const char *partial, const char *prev) {
    const char *comp_line = getenv("COMP_LINE");
    const char *comp_point_s = getenv("COMP_POINT");
    int comp_point = 0;
    if (comp_point_s) comp_point = atoi(comp_point_s);

    char inferred_cmd[64] = {0};
    if (comp_line) {
        size_t len = strlen(comp_line);
        size_t use_len = (comp_point > 0 && (size_t)comp_point < len) ? (size_t)comp_point : len;
        char *buf = strndup(comp_line, use_len);
        if (buf) {
            char *saveptr = NULL;
            char *tok = strtok_r(buf, " \t", &saveptr);
            if (tok) tok = strtok_r(NULL, " \t", &saveptr);
            while (tok) {
                if (strcmp(tok, "install") == 0) strncpy(inferred_cmd, "install", sizeof(inferred_cmd)-1);
                else if (strcmp(tok, "remove") == 0) strncpy(inferred_cmd, "remove", sizeof(inferred_cmd)-1);
                else if (strcmp(tok, "update") == 0) strncpy(inferred_cmd, "update", sizeof(inferred_cmd)-1);
                else if (strcmp(tok, "search") == 0) strncpy(inferred_cmd, "search", sizeof(inferred_cmd)-1);
                else if (strcmp(tok, "info") == 0) strncpy(inferred_cmd, "info", sizeof(inferred_cmd)-1);
                else if (strcmp(tok, "check-update") == 0) strncpy(inferred_cmd, "check-update", sizeof(inferred_cmd)-1);
                else if (strcmp(tok, "sync") == 0) strncpy(inferred_cmd, "sync", sizeof(inferred_cmd)-1);
                tok = strtok_r(NULL, " \t", &saveptr);
            }
            free(buf);
        }
    }

    if (strcmp(prev, "dvf") == 0) {
        const char *subcmds[] = {"update", "install", "remove", "search", "info", "check-update", "sync"};
        for (size_t i = 0; i < sizeof(subcmds)/sizeof(subcmds[0]); i++) {
            if (strncmp(subcmds[i], partial, strlen(partial)) == 0) printf("%s\n", subcmds[i]);
        }
        return;
    }

    const char *cmd = inferred_cmd[0] ? inferred_cmd : prev;

    if (strcmp(cmd, "install") == 0 || strcmp(cmd, "info") == 0) {
        if (partial[0] == '-') {
            printf("--verbose\n--debug\n");
        } else {
            prefix_search_and_print(partial);
            complete_rpm_files(partial);
            complete_file_paths_ext(partial, g_dvf_cache_dir, ".rpm");
        }
    } else if (strcmp(cmd, "remove") == 0 || strcmp(cmd, "search") == 0) {
        prefix_search_and_print(partial);
    } else if (strcmp(cmd, "update") == 0 || strcmp(cmd, "sync") == 0 || strcmp(cmd, "check-update") == 0) {
        if (partial[0] == '-') printf("--verbose\n--debug\n");
    }
}
