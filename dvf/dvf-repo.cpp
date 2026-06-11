#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <sys/utsname.h>
#include <dirent.h>
#include <cstring>
#include <unistd.h>
#include <curl/curl.h>
#include <map>
#include <set>
#include <deque>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "dvf-repo.h"
#include "dvf-util.h"
#include "dvf-config.h"
#include "dvf-sqlite.h"
#include "dvf-rpm.h"
#include "dvf-storage.h"
#include "dvf-hash.h"
#include "dvf-completion.h"
#include "dvf-metadata.h"

extern "C" int dvf_repo_ffi_available(void) {
    return 1;
}

struct Repo {
    std::string id;
    std::string name;
    std::string baseurl;
    std::string metalink;
    std::string mirrorlist;
    bool enabled = true;
};

class HttpFetcher {
public:
    HttpFetcher() {
        curl = curl_easy_init();
    }
    ~HttpFetcher() {
        if (curl) curl_easy_cleanup(curl);
    }

    static int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
        (void)clientp;
        (void)ultotal;
        (void)ulnow;
        if (dltotal <= 0) return 0;
        int percentage = (int)((dlnow * 100) / dltotal);
        printf("\rDownloading: [%-50s] %d%%", std::string(percentage / 2, '#').c_str(), percentage);
        fflush(stdout);
        return 0;
    }

    bool fetchToFile(const std::string& url, const std::string& path, bool show_progress = false) {
        if (!curl) return false;
        FILE* fp = fopen(path.c_str(), "wb");
        if (!fp) return false;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "dvf/1.0");

        if (show_progress) {
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
        } else {
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
        }

        CURLcode res = curl_easy_perform(curl);
        fclose(fp);

        if (show_progress) printf("\n");

        if (res != CURLE_OK) {
            dvf_log_debug("Fetch failed: %s (URL: %s)\n", curl_easy_strerror(res), url.c_str());
            return false;
        }
        return true;
    }

private:
    CURL* curl;
};

struct RepoPackage {
    std::string name;
    std::string version;
    std::string release;
    std::string epoch = "0";
    std::string arch;
    std::string summary;
    std::string description;
    std::string license;
    std::string url;
    std::string location;
    std::string repo_id;
    std::vector<std::string> provides;
    std::vector<std::string> requires_list;
};

template<typename F>
static void parse_primary(const std::string& path, const std::string& repo_id, F callback) {
    std::string decomp_cmd = "unzstd -c " + path + " 2>/dev/null || zcat -f " + path + " 2>/dev/null || xzcat " + path + " 2>/dev/null || cat " + path;
    FILE* fp = popen(decomp_cmd.c_str(), "r");
    if (!fp) return;

    std::string buffer;
    char chunk[262144];
    while (true) {
        size_t n = fread(chunk, 1, sizeof(chunk), fp);
        if (n <= 0) break;
        buffer.append(chunk, n);

        size_t pkg_start = 0;
        while (true) {
            pkg_start = buffer.find("<package", pkg_start);
            if (pkg_start == std::string::npos) {
                if (buffer.size() > 1024) buffer.erase(0, buffer.size() - 1024);
                break;
            }

            size_t pkg_end = buffer.find("</package>", pkg_start);
            if (pkg_end == std::string::npos) {
                buffer.erase(0, pkg_start);
                break;
            }

            std::string pkg_xml = buffer.substr(pkg_start, pkg_end - pkg_start + 10);

            auto get_tag = [&](const std::string& tag) {
                std::string open = "<" + tag + ">";
                std::string close = "</" + tag + ">";
                size_t s = pkg_xml.find(open);
                if (s == std::string::npos) return std::string("");
                size_t e = pkg_xml.find(close, s + open.size());
                if (e == std::string::npos) return std::string("");
                return pkg_xml.substr(s + open.size(), e - (s + open.size()));
            };

            RepoPackage pkg;
            pkg.repo_id = repo_id;
            pkg.name = get_tag("name");
            pkg.summary = get_tag("summary");
            pkg.description = get_tag("description");
            pkg.license = get_tag("license");
            pkg.url = get_tag("url");
            pkg.arch = get_tag("arch");

            size_t loc_pos = pkg_xml.find("<location href=\"");
            if (loc_pos != std::string::npos) {
                size_t s = loc_pos + 16;
                size_t e = pkg_xml.find("\"", s);
                if (e != std::string::npos) pkg.location = pkg_xml.substr(s, e - s);
            }

            size_t v_pos = pkg_xml.find("<version");
            if (v_pos != std::string::npos) {
                auto get_attr = [&](const std::string& attr, size_t start_from) {
                    std::string key = attr + "=\"";
                    size_t s = pkg_xml.find(key, start_from);
                    if (s == std::string::npos) return std::string("");
                    size_t e = pkg_xml.find("\"", s + key.size());
                    if (e == std::string::npos) return std::string("");
                    return pkg_xml.substr(s + key.size(), e - (s + key.size()));
                };
                pkg.version = get_attr("ver", v_pos);
                pkg.release = get_attr("rel", v_pos);
                std::string ep = get_attr("epoch", v_pos);
                if (!ep.empty()) pkg.epoch = ep;
            }

            // Extract provides for capability handling
            size_t prov_start = pkg_xml.find("<rpm:provides>");
            if (prov_start != std::string::npos) {
                size_t prov_end = pkg_xml.find("</rpm:provides>", prov_start);
                if (prov_end != std::string::npos) {
                    size_t entry_pos = prov_start;
                    while (true) {
                        entry_pos = pkg_xml.find("<rpm:entry name=\"", entry_pos);
                        if (entry_pos == std::string::npos || entry_pos > prov_end) break;
                        entry_pos += 17;
                        size_t name_end = pkg_xml.find("\"", entry_pos);
                        if (name_end != std::string::npos) {
                            pkg.provides.push_back(pkg_xml.substr(entry_pos, name_end - entry_pos));
                            entry_pos = name_end;
                        }
                    }
                }
            }

            // Extract file tags as they are also implicit provides (e.g., /usr/bin/sh)
            size_t file_pos = 0;
            while ((file_pos = pkg_xml.find("<file", file_pos)) != std::string::npos) {
                size_t tag_end = pkg_xml.find('>', file_pos);
                if (tag_end == std::string::npos) break;

                size_t start = tag_end + 1;
                size_t end = pkg_xml.find("</file>", start);
                if (end != std::string::npos) {
                    std::string fname = pkg_xml.substr(start, end - start);
                    // Filter out directories, we only want file-based provides
                    if (pkg_xml.substr(file_pos, tag_end - file_pos).find("type=\"dir\"") == std::string::npos) {
                        pkg.provides.push_back(fname);
                    }
                    file_pos = end + 7;
                } else {
                    file_pos = tag_end + 1;
                }
            }

            // Extract requires for dependency resolution
            size_t req_start = pkg_xml.find("<rpm:requires>");
            if (req_start != std::string::npos) {
                size_t req_end = pkg_xml.find("</rpm:requires>", req_start);
                if (req_end != std::string::npos) {
                    size_t entry_pos = req_start;
                    while (true) {
                        entry_pos = pkg_xml.find("<rpm:entry name=\"", entry_pos);
                        if (entry_pos == std::string::npos || entry_pos > req_end) break;
                        entry_pos += 17;
                        size_t name_end = pkg_xml.find("\"", entry_pos);
                        if (name_end != std::string::npos) {
                            std::string req_name = pkg_xml.substr(entry_pos, name_end - entry_pos);

                            // Handle config(file) syntax by extracting the file path
                            if (req_name.find("config(") == 0) {
                                size_t start = 7;
                                size_t end = req_name.find(')', start);
                                if (end != std::string::npos) {
                                    req_name = req_name.substr(start, end - start);
                                }
                            }

                            // Filter out internal RPM capabilities and self-requirements
                            if (req_name.find("rpmlib(") != 0 && req_name != pkg.name) {
                                // Handle rich dependencies: (pkg1 if pkg2) or (pkg1 with pkg2)
                                if (!req_name.empty() && req_name[0] == '(') {
                                    req_name = req_name.substr(1);
                                    size_t space = req_name.find(' ');
                                    if (space != std::string::npos) {
                                        req_name = req_name.substr(0, space);
                                    } else if (req_name.back() == ')') {
                                        req_name.pop_back();
                                    }
                                } else if (!req_name.empty() && req_name[0] != '/') {
                                    // Clean up requirement name (strip versions/operators/arch)
                                    // e.g., "bash >= 5.0" -> "bash", "font(:lang=en)" -> "font"
                                    size_t p = req_name.find_first_of(" (><=");
                                    if (p != std::string::npos) req_name = req_name.substr(0, p);
                                }
                                if (!req_name.empty()) {
                                    pkg.requires_list.push_back(req_name);
                                }
                            }
                            entry_pos = name_end;
                        }
                    }
                }
            }

            callback(pkg);
            pkg_start = pkg_end + 10;
        }
    }
    pclose(fp);
}

class DVF_FSM {
public:
    dvf_state_t current_state;
    std::vector<Repo> repos;
    HttpFetcher fetcher;
    std::string releasever;
    std::string basearch;

    DVF_FSM() : current_state(DVF_STATE_INIT) {
        releasever = get_releasever();
        basearch = get_basearch();
    }

    void load_repo_dir(const std::string& path) {
        DIR* dir = opendir(path.c_str());
        if (!dir) return;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_name[0] == '.') continue;
            std::string filename = entry->d_name;
            if (filename.size() < 6 || filename.substr(filename.size() - 5) != ".repo") continue;

            parse_repo_file(path + "/" + filename);
        }
        closedir(dir);
    }

    void load_repos() {
        repos.clear();

        // Tier 1: Load repos from the main dvf-config file (Primary)
        parse_repo_file("/etc/yum.repos.d/dvf-config");
        if (!repos.empty()) {
            printf("Repositories: Loading from dvf-config (Primary)\n");
            return;
        }

        // Tier 2: Fallback to custom repo_dir from config
        if (g_dvf_repo_dir) {
            load_repo_dir(g_dvf_repo_dir);
            if (!repos.empty()) {
                printf("Repositories: Loading from %s (Custom)\n", g_dvf_repo_dir);
                return;
            }
        }

        // Tier 3: Final fallback to default system repos
        printf("Repositories: Loading from /etc/yum.repos.d (System Fallback)\n");
        load_repo_dir("/etc/yum.repos.d");
    }

    void run_sync() {
        load_repos();

        bool has_enabled = false;
        for (const auto& r : repos) if (r.enabled) { has_enabled = true; break; }

        if (!has_enabled) {
            printf("No enabled repositories found.\n");
            current_state = DVF_STATE_SUCCESS;
            return;
        }

        printf("\n%-45s %s\n", "Repository", "Status");
        printf("----------------------------------------------------------------\n");

        for (auto& repo : repos) {
            if (!repo.enabled) continue;

            // Format output similar to DNF: Name/ID on left, status on right
            printf("%-45s ", repo.id.c_str());
            fflush(stdout);

            if (sync_repo(repo)) {
                printf("\033[1;32m[DONE]\033[0m\n");
            } else {
                printf("\033[1;31m[FAILED]\033[0m\n");
            }
        }
        current_state = DVF_STATE_SUCCESS;
    }

    std::vector<std::string> get_mirrors(Repo& repo) {
        std::vector<std::string> mirrors;
        if (!repo.baseurl.empty()) {
            mirrors.push_back(repo.baseurl);
        } else if (!repo.metalink.empty() || !repo.mirrorlist.empty()) {
            std::string router_url = repo.metalink.empty() ? repo.mirrorlist : repo.metalink;
            std::string router_cache = std::string(g_dvf_cache_dir) + "/" + repo.id + "_router.txt";
            if (fetcher.fetchToFile(router_url, router_cache)) {
                if (!repo.metalink.empty()) mirrors = parse_metalink_for_mirrors(router_cache);
                else mirrors = parse_mirrorlist_for_mirrors(router_cache);
            }
        }
        return mirrors;
    }

    bool sync_repo(Repo& repo) {
        std::string repomd_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_repomd.xml";
        std::vector<std::string> base_urls = get_mirrors(repo);

        if (base_urls.empty()) return false;

        bool success = false;
        for (const auto& base_url : base_urls) {
            std::string repomd_url = base_url;
            if (repomd_url.back() != '/') repomd_url += "/";
            repomd_url += "repodata/repomd.xml";

            if (fetcher.fetchToFile(repomd_url, repomd_path)) {
                std::string primary_url = parse_repomd(repomd_path, base_url);
                if (!primary_url.empty()) {
                    std::string primary_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.xml.zst";
                    if (fetcher.fetchToFile(primary_url, primary_path, true)) {
                        // After successful download, we'll mark this repo for binary indexing
                        success = true;
                        break;
                    }
                }
            }
        }

        return success;
    }

    void build_binary_cache() {
        printf("Building high-speed metadata cache...\n");

        std::vector<RepoPackage> all_pkgs;
        for (const auto& repo : repos) {
            if (!repo.enabled) continue;
            std::string primary_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.xml.zst";
            if (!dvf_util_file_exists(primary_path.c_str()))
                 primary_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.xml.gz";
            if (!dvf_util_file_exists(primary_path.c_str()))
                 primary_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.raw";

            if (dvf_util_file_exists(primary_path.c_str())) {
                printf("  Processing %s... ", repo.id.c_str());
                fflush(stdout);
                size_t start_count = all_pkgs.size();
                parse_primary(primary_path, repo.id, [&](const RepoPackage& pkg) {
                    all_pkgs.push_back(pkg);
                });
                printf("%zu packages found.\n", all_pkgs.size() - start_count);
            }
        }

        if (all_pkgs.empty()) return;

        std::string cache_path = std::string(g_dvf_db_dir) + "/metadata.bin";
        FILE* fp = fopen(cache_path.c_str(), "wb");
        if (!fp) return;

        dvf_metadata_header_t hdr;
        memset(&hdr, 0, sizeof(hdr));
        hdr.magic = 0x4456464D;
        hdr.version = 1;
        hdr.package_count = (uint32_t)all_pkgs.size();

        // Initial pass to build string pool and calculate sizes
        std::map<std::string, uint32_t> string_pool;
        uint32_t current_string_offset = 0;

        auto add_to_pool = [&](const std::string& s) {
            if (string_pool.find(s) == string_pool.end()) {
                string_pool[s] = current_string_offset;
                current_string_offset += s.length() + 1;
            }
        };

        for (const auto& pkg : all_pkgs) {
            add_to_pool(pkg.name);
            add_to_pool(pkg.version);
            add_to_pool(pkg.release);
            add_to_pool(pkg.epoch);
            add_to_pool(pkg.arch);
            add_to_pool(pkg.location);
            add_to_pool(pkg.repo_id);
            for (const auto& p : pkg.provides) add_to_pool(p);
            for (const auto& r : pkg.requires_list) add_to_pool(r);
        }

        hdr.string_pool_size = 0; // Temporary
        fwrite(&hdr, sizeof(hdr), 1, fp);

        // Write package entries (placeholders)
        long pkg_entries_start = ftell(fp);
        std::vector<dvf_metadata_pkg_entry_t> entries(all_pkgs.size());
        fwrite(entries.data(), sizeof(dvf_metadata_pkg_entry_t), all_pkgs.size(), fp);

        // Calculate and write provide/require lists
        for (size_t i = 0; i < all_pkgs.size(); i++) {
            entries[i].name_offset = string_pool[all_pkgs[i].name];
            entries[i].version_offset = string_pool[all_pkgs[i].version];
            entries[i].release_offset = string_pool[all_pkgs[i].release];
            entries[i].epoch_offset = string_pool[all_pkgs[i].epoch];
            entries[i].arch_offset = string_pool[all_pkgs[i].arch];
            entries[i].location_offset = string_pool[all_pkgs[i].location];
            entries[i].repo_id_offset = string_pool[all_pkgs[i].repo_id];

            entries[i].provides_count = (uint32_t)all_pkgs[i].provides.size();
            entries[i].provides_offset = (uint32_t)ftell(fp);
            for (const auto& p : all_pkgs[i].provides) {
                uint32_t off = string_pool[p];
                fwrite(&off, sizeof(uint32_t), 1, fp);
            }

            entries[i].requires_count = (uint32_t)all_pkgs[i].requires_list.size();
            entries[i].requires_offset = (uint32_t)ftell(fp);
            for (const auto& r : all_pkgs[i].requires_list) {
                uint32_t off = string_pool[r];
                fwrite(&off, sizeof(uint32_t), 1, fp);
            }
        }

        // Write string pool
        uint32_t pool_start = (uint32_t)ftell(fp);
        std::vector<char> pool_data(current_string_offset);
        for (const auto& pair : string_pool) {
            memcpy(pool_data.data() + pair.second, pair.first.c_str(), pair.first.length() + 1);
        }
        fwrite(pool_data.data(), 1, current_string_offset, fp);

        // Fix up offsets and rewrite header/entries
        hdr.string_pool_size = pool_start; // Now use as offset to string pool
        fseek(fp, 0, SEEK_SET);
        fwrite(&hdr, sizeof(hdr), 1, fp);
        fseek(fp, pkg_entries_start, SEEK_SET);
        fwrite(entries.data(), sizeof(dvf_metadata_pkg_entry_t), entries.size(), fp);

        fclose(fp);
        printf("Binary cache built successfully.\n");
    }

private:
    std::string get_releasever() {
        std::ifstream f("/etc/fedora-release");
        if (f.is_open()) {
            std::string line;
            if (std::getline(f, line)) {
                auto pos = line.find("release ");
                if (pos != std::string::npos) {
                    std::string ver = line.substr(pos + 8);
                    auto end = ver.find_first_of(" \t\n");
                    if (end != std::string::npos) ver = ver.substr(0, end);
                    return ver;
                }
            }
        }
        std::ifstream os("/etc/os-release");
        if (os.is_open()) {
            std::string line;
            while (std::getline(os, line)) {
                if (line.compare(0, 11, "VERSION_ID=") == 0) {
                    std::string v = line.substr(11);
                    if (!v.empty() && v[0] == '"') {
                        v = v.substr(1, v.size() - 2);
                    }
                    return v;
                }
            }
        }
        return "40";
    }

    std::string get_basearch() {
        struct utsname uts;
        uname(&uts);
        if (strcmp(uts.machine, "i686") == 0) return "i386";
        return uts.machine;
    }

    std::string replace_vars(std::string str) {
        size_t pos;
        while ((pos = str.find("$releasever")) != std::string::npos) {
            str.replace(pos, 11, releasever);
        }
        while ((pos = str.find("$basearch")) != std::string::npos) {
            str.replace(pos, 9, basearch);
        }
        return str;
    }

    void parse_repo_file(const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open()) return;

        std::string line;
        Repo* current_repo = nullptr;
        while (std::getline(f, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;

            if (line[0] == '[' && line.back() == ']') {
                std::string id = line.substr(1, line.size() - 2);

                // Check if repo already exists (to allow overrides)
                current_repo = nullptr;
                for (auto& r : repos) {
                    if (r.id == id) {
                        current_repo = &r;
                        break;
                    }
                }

                if (!current_repo) {
                    repos.emplace_back();
                    current_repo = &repos.back();
                    current_repo->id = id;
                }
            } else if (current_repo) {
                auto eq = line.find('=');
                if (eq != std::string::npos) {
                    std::string key = trim(line.substr(0, eq));
                    std::string val = trim(line.substr(eq + 1));
                    if (key == "name") current_repo->name = val;
                    else if (key == "baseurl") current_repo->baseurl = replace_vars(val);
                    else if (key == "metalink") current_repo->metalink = replace_vars(val);
                    else if (key == "mirrorlist") current_repo->mirrorlist = replace_vars(val);
                    else if (key == "enabled") current_repo->enabled = (val == "1" || val == "yes" || val == "true");
                }
            }
        }
    }

    std::string trim(std::string s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), s.end());
        return s;
    }

    std::vector<std::string> parse_metalink_for_mirrors(const std::string& path) {
        std::vector<std::string> mirrors;
        std::ifstream f(path);
        if (!f.is_open()) return mirrors;
        std::string line;
        while (std::getline(f, line)) {
            auto pos = line.find("<url ");
            if (pos != std::string::npos && line.find("http") != std::string::npos) {
                auto start = line.find('>', pos);
                if (start != std::string::npos) {
                    start++;
                    auto end = line.find('<', start);
                    if (end != std::string::npos) {
                        std::string url = line.substr(start, end - start);
                        auto rpos = url.find("/repodata/repomd.xml");
                        if (rpos != std::string::npos) url = url.substr(0, rpos);
                        if (!url.empty() && url.back() == '/') url.pop_back();
                        mirrors.push_back(url);
                        if (mirrors.size() >= 5) break;
                    }
                }
            }
        }
        return mirrors;
    }

    std::vector<std::string> parse_mirrorlist_for_mirrors(const std::string& path) {
        std::vector<std::string> mirrors;
        std::ifstream f(path);
        if (!f.is_open()) return mirrors;
        std::string line;
        while (std::getline(f, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            if (line.find("http") == 0) {
                if (line.back() == '/') line.pop_back();
                mirrors.push_back(line);
                if (mirrors.size() >= 5) break;
            }
        }
        return mirrors;
    }

    std::string parse_repomd(const std::string& path, const std::string& base_url) {
        std::ifstream f(path);
        if (!f.is_open()) return "";
        std::stringstream ss;
        ss << f.rdbuf();
        std::string content = ss.str();

        auto data_pos = content.find("type=\"primary\"");
        if (data_pos == std::string::npos) return "";

        // Find location after this specific data type
        auto loc_pos = content.find("<location href=\"", data_pos);
        if (loc_pos == std::string::npos) return "";

        auto start = loc_pos + 16;
        auto end = content.find("\"", start);
        if (end == std::string::npos) return "";

        std::string href = content.substr(start, end - start);
        std::string res = base_url;
        if (res.back() != '/') res += "/";
        return res + href;
    }
};

static bool dvf_repo_has_metadata() {
    DVF_FSM fsm;
    fsm.load_repos();
    for (const auto& repo : fsm.repos) {
        if (!repo.enabled) continue;
        std::string p1 = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.xml.zst";
        std::string p2 = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.xml.gz";
        std::string p3 = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.raw";
        if (dvf_util_file_exists(p1.c_str()) || dvf_util_file_exists(p2.c_str()) || dvf_util_file_exists(p3.c_str()))
            return true;
    }
    return false;
}

extern "C" int dvf_repo_update(void) {
    dvf_log_verbose("Starting DVF repository update (Advanced C++ FFI mode)...\n");
    if (curl_global_init(CURL_GLOBAL_ALL) != 0) {
        dvf_log_error("Failed to initialize libcurl\n");
        return -1;
    }

    DVF_FSM fsm;
    fsm.load_repos();
    fsm.run_sync();

    curl_global_cleanup();
    if (fsm.current_state == DVF_STATE_SUCCESS) {
        printf("All repositories synchronized successfully.\n");
        fsm.build_binary_cache();
        printf("Updating autocomplete index...\n");
        dvf_sync_autocomplete();
        return 0;
    }
    return -1;
}

static bool contains_nocase(const std::string& str, const std::string& term) {
    if (term.empty()) return true;
    auto it = std::search(
        str.begin(), str.end(),
        term.begin(), term.end(),
        [](unsigned char ch1, unsigned char ch2) { return std::tolower(ch1) == std::tolower(ch2); }
    );
    return it != str.end();
}

extern "C" int dvf_repo_search(const char *term) {
    DVF_FSM fsm;
    fsm.load_repos();
    std::string sterm(term);
    bool found_any = false;
    int scanned_repos = 0;

    printf("====================== Name & Summary Matched: %s ======================\n", term);

    for (const auto& repo : fsm.repos) {
        if (!repo.enabled) continue;
        std::string primary_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.xml.zst";
        if (!dvf_util_file_exists(primary_path.c_str()))
             primary_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.xml.gz";
        if (!dvf_util_file_exists(primary_path.c_str()))
             primary_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.raw";

        if (dvf_util_file_exists(primary_path.c_str())) {
            scanned_repos++;
            parse_primary(primary_path, repo.id, [&](const RepoPackage& pkg) {
                bool match = contains_nocase(pkg.name, sterm) || contains_nocase(pkg.summary, sterm);
                if (!match) {
                    for (const auto& prov : pkg.provides) {
                        if (contains_nocase(prov, sterm)) {
                            match = true;
                            break;
                        }
                    }
                }
                if (match) {
                    printf("%s.%s : %s\n", pkg.name.c_str(), pkg.arch.c_str(), pkg.summary.c_str());
                    found_any = true;
                }
            });
        }
    }

    if (scanned_repos == 0) {
        printf("\n\033[1;33mNote:\033[0m No repository metadata found. Searching only local packages.\n");
        printf("Run 'dvf update' to include remote packages in search results.\n\n");
    }

    return found_any ? 0 : -1;
}

extern "C" int dvf_repo_info(const char *pkg_name) {
    DVF_FSM fsm;
    fsm.load_repos();
    std::string target(pkg_name);
    RepoPackage best_match;
    bool found = false;
    int scanned_repos = 0;

    for (const auto& repo : fsm.repos) {
        if (!repo.enabled) continue;
        std::string primary_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.xml.zst";
        if (!dvf_util_file_exists(primary_path.c_str()))
             primary_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.xml.gz";
        if (!dvf_util_file_exists(primary_path.c_str()))
             primary_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.raw";

        if (dvf_util_file_exists(primary_path.c_str())) {
            scanned_repos++;
            parse_primary(primary_path, repo.id, [&](const RepoPackage& pkg) {
                if (pkg.name == target) {
                    // For info, we just take the first one we find for now, or could compare versions
                    if (!found) {
                        best_match = pkg;
                        found = true;
                    }
                }
            });
        }
    }

    if (found) {
        printf("\033[1mName          \033[0m: %s\n", best_match.name.c_str());
        printf("\033[1mVersion       \033[0m: %s\n", best_match.version.c_str());
        printf("\033[1mRelease       \033[0m: %s\n", best_match.release.c_str());
        printf("\033[1mArchitecture  \033[0m: %s\n", best_match.arch.c_str());
        printf("\033[1mLicense       \033[0m: %s\n", best_match.license.c_str());
        printf("\033[1mURL           \033[0m: %s\n", best_match.url.c_str());
        printf("\033[1mSummary       \033[0m: %s\n", best_match.summary.c_str());
        printf("\033[1mDescription   \033[0m: %s\n", best_match.description.c_str());
        printf("\033[1mRepository    \033[0m: %s\n", best_match.repo_id.c_str());
        printf("\n");
        return 0;
    }

    if (scanned_repos == 0) {
        printf("\n\033[1;33mNote:\033[0m No repository metadata found. Run 'dvf update' to enable remote package info.\n");
    }

    return -1;
}

static void parse_primary_names_only(const std::string& path, std::vector<std::string>& names) {
    std::string decomp_cmd = "unzstd -c " + path + " 2>/dev/null || zcat -f " + path + " 2>/dev/null || xzcat " + path + " 2>/dev/null || cat " + path;
    FILE* fp = popen(decomp_cmd.c_str(), "r");
    if (!fp) return;

    std::string buffer;
    char chunk[32768];
    while (true) {
        size_t n = fread(chunk, 1, sizeof(chunk), fp);
        if (n <= 0) break;
        buffer.append(chunk, n);

        size_t pkg_start = 0;
        while (true) {
            pkg_start = buffer.find("<package", pkg_start);
            if (pkg_start == std::string::npos) {
                if (buffer.size() > 1024) buffer.erase(0, buffer.size() - 1024);
                break;
            }

            size_t pkg_end = buffer.find("</package>", pkg_start);
            if (pkg_end == std::string::npos) {
                buffer.erase(0, pkg_start);
                break;
            }

            size_t n_open = buffer.find("<name>", pkg_start);
            if (n_open != std::string::npos && n_open < pkg_end) {
                size_t n_close = buffer.find("</name>", n_open + 6);
                if (n_close != std::string::npos && n_close < pkg_end) {
                    names.push_back(buffer.substr(n_open + 6, n_close - (n_open + 6)));
                }
            }
            pkg_start = pkg_end + 10;
        }
    }
    pclose(fp);
}

extern "C" char** dvf_repo_get_all_names(size_t *count) {
    DVF_FSM fsm;
    fsm.load_repos();
    std::vector<std::string> all_names;

    for (const auto& repo : fsm.repos) {
        if (!repo.enabled) continue;

        std::string primary_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.xml.zst";
        if (!dvf_util_file_exists(primary_path.c_str()))
             primary_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.xml.gz";
        if (!dvf_util_file_exists(primary_path.c_str()))
             primary_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.raw";

        if (dvf_util_file_exists(primary_path.c_str())) {
            dvf_log_debug("Extracting names from %s...\n", repo.id.c_str());
            parse_primary_names_only(primary_path, all_names);
        }
    }

    if (all_names.empty()) {
        *count = 0;
        return nullptr;
    }

    std::sort(all_names.begin(), all_names.end());
    all_names.erase(std::unique(all_names.begin(), all_names.end()), all_names.end());

    char **res = (char**)malloc(sizeof(char*) * all_names.size());
    for (size_t i = 0; i < all_names.size(); i++) {
        res[i] = strdup(all_names[i].c_str());
    }
    *count = all_names.size();
    return res;
}

extern "C" void dvf_repo_free_names(char **names, size_t count) {
    if (!names) return;
    for (size_t i = 0; i < count; i++) free(names[i]);
    free(names);
}

struct UpdateInfo {
    RepoPackage repo_pkg;
    std::string installed_evr;
};

extern "C" int dvf_repo_check_updates(void) {
    DVF_FSM fsm;
    fsm.load_repos();

    const char *db_path = "/var/lib/rpm/rpmdb.sqlite";
    if (!dvf_util_file_exists(db_path)) db_path = "rpmdb.sqlite";

    dvf_blob_list_t *blobs = dvf_sqlite_get_package_blobs(db_path);
    if (!blobs) return -1;

    // name -> {arch -> evr}
    std::map<std::string, std::map<std::string, std::string>> installed;
    // capability -> package_name
    std::map<std::string, std::string> capability_to_installed;

    for (size_t i = 0; i < blobs->count; i++) {
        rpm_info_t info;
        memset(&info, 0, sizeof(info));
        if (rpm_parse_header(blobs->blobs[i].data, blobs->blobs[i].size, &info) == 0) {
            if (info.name && info.version && info.release) {
                std::string evr = (info.epoch ? std::string(info.epoch) : "0") + ":" + info.version + "-" + info.release;
                installed[info.name][info.arch ? info.arch : "noarch"] = evr;
                capability_to_installed[info.name] = info.name;
            }
        }
        rpm_free_info(&info);
    }
    dvf_sqlite_free_blob_list(blobs);

    std::map<std::pair<std::string, std::string>, UpdateInfo> updates;
    int scanned_repos = 0;

    for (const auto& repo : fsm.repos) {
        if (!repo.enabled) continue;
        std::string primary_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.xml.zst";
        if (!dvf_util_file_exists(primary_path.c_str()))
             primary_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.xml.gz";
        if (!dvf_util_file_exists(primary_path.c_str()))
             primary_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.raw";

        if (dvf_util_file_exists(primary_path.c_str())) {
            scanned_repos++;
            parse_primary(primary_path, repo.id, [&](const RepoPackage& pkg) {
                std::string inst_evr;
                std::string matched_name;

                if (installed.count(pkg.name)) {
                    matched_name = pkg.name;
                } else {
                    // Check if this repo package provides an installed package (Capability handling)
                    for (const auto& prov : pkg.provides) {
                        if (installed.count(prov)) {
                            matched_name = prov;
                            break;
                        }
                    }
                }

                if (!matched_name.empty()) {
                    if (installed[matched_name].count(pkg.arch)) {
                        inst_evr = installed[matched_name][pkg.arch];
                    } else if (pkg.arch == "noarch" && !installed[matched_name].empty()) {
                        inst_evr = installed[matched_name].begin()->second;
                    } else if (installed[matched_name].count("noarch")) {
                        inst_evr = installed[matched_name]["noarch"];
                    }

                    // Only compare if the package names are identical.
                    // If matched_name != pkg.name, it's a capability/virtual provide, not a direct update.
                    if (!inst_evr.empty() && matched_name == pkg.name) {
                        std::string repo_evr = pkg.epoch + ":" + pkg.version + "-" + pkg.release;
                        if (dvf_util_compare_versions(repo_evr.c_str(), inst_evr.c_str()) > 0) {
                            auto key = std::make_pair(pkg.name, pkg.arch);
                            if (updates.find(key) == updates.end() ||
                                dvf_util_compare_versions(repo_evr.c_str(), (updates[key].repo_pkg.epoch + ":" + updates[key].repo_pkg.version + "-" + updates[key].repo_pkg.release).c_str()) > 0) {
                                updates[key] = {pkg, inst_evr};
                            }
                        }
                    }
                }
            });
        }
    }

    if (updates.empty()) {
        if (scanned_repos == 0) {
            if (dvf_util_prompt_yes_no("No repository metadata found. Run 'dvf update' now?")) {
                if (dvf_repo_update() == 0) {
                    return dvf_repo_check_updates();
                }
            }
        } else {
            printf("No updates available.\n");
        }
        return 0;
    }

    printf("\n%-45s %-35s %s\n", "Package", "Version", "Repository");
    printf("----------------------------------------------------------------------------------------------------\n");
    for (auto const& [key, up] : updates) {
        std::string evr = up.repo_pkg.epoch == "0" ?
                          up.repo_pkg.version + "-" + up.repo_pkg.release :
                          up.repo_pkg.epoch + ":" + up.repo_pkg.version + "-" + up.repo_pkg.release;
        std::string name_arch = up.repo_pkg.name + "." + up.repo_pkg.arch;
        printf("%-45s %-35s %s\n", name_arch.c_str(), evr.c_str(), up.repo_pkg.repo_id.c_str());
    }
    printf("\nTotal updates available: %zu\n", updates.size());

    return 0;
}

extern "C" int dvf_repo_install(const char *pkg_name) {
    DVF_FSM fsm;
    fsm.load_repos();

    std::string cache_path = std::string(g_dvf_db_dir) + "/metadata.bin";
    int fd = open(cache_path.c_str(), O_RDONLY);
    if (fd < 0) {
        if (dvf_util_prompt_yes_no("Metadata cache missing. Run 'dvf update' to build it?")) {
            if (dvf_repo_update() != 0) return -1;
            fd = open(cache_path.c_str(), O_RDONLY);
            if (fd < 0) return -1;
        } else {
            return -1;
        }
    }

    struct stat st;
    fstat(fd, &st);
    void* mapped = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) { close(fd); return -1; }

    dvf_metadata_header_t* hdr = (dvf_metadata_header_t*)mapped;
    dvf_metadata_pkg_entry_t* entries = (dvf_metadata_pkg_entry_t*)((char*)mapped + sizeof(dvf_metadata_header_t));
    char* pool = (char*)mapped + hdr->string_pool_size;

    std::multimap<std::string, dvf_metadata_pkg_entry_t*> provide_map;
    for (uint32_t i = 0; i < hdr->package_count; i++) {
        provide_map.insert({pool + entries[i].name_offset, &entries[i]});
        uint32_t* prov_offs = (uint32_t*)((char*)mapped + entries[i].provides_offset);
        for (uint32_t j = 0; j < entries[i].provides_count; j++) {
            const char* prov = pool + prov_offs[j];
            provide_map.insert({prov, &entries[i]});
            if (prov[0] != '/') {
                std::string s(prov);
                size_t p = s.find_first_of(" (");
                if (p != std::string::npos) provide_map.insert({s.substr(0, p), &entries[i]});
            }
        }
    }

    std::string effective_root = g_dvf_install_root ? g_dvf_install_root : "/";
    if (effective_root.length() > 1 && effective_root.back() == '/') effective_root.pop_back();

    char db_path[4096];
    snprintf(db_path, sizeof(db_path), "%s/var/lib/rpm/rpmdb.sqlite", effective_root.c_str());
    if (!dvf_util_file_exists(db_path)) snprintf(db_path, sizeof(db_path), "rpmdb.sqlite");

    std::set<std::string> installed_capabilities;
    dvf_blob_list_t *blobs = dvf_sqlite_get_package_blobs(db_path);
    if (blobs) {
        for (size_t i = 0; i < blobs->count; i++) {
            rpm_info_t info;
            memset(&info, 0, sizeof(info));
            if (rpm_parse_header(blobs->blobs[i].data, blobs->blobs[i].size, &info) == 0) {
                if (info.name) installed_capabilities.insert(info.name);
                for (size_t j = 0; j < info.provides_count; j++) installed_capabilities.insert(info.provides_list[j]);
                for (size_t j = 0; j < info.file_count; j++) installed_capabilities.insert(info.file_list[j]);
            }
            rpm_free_info(&info);
        }
        dvf_sqlite_free_blob_list(blobs);
    }

    std::vector<dvf_metadata_pkg_entry_t*> to_install;
    std::set<std::string> handled;
    std::deque<std::string> queue;
    queue.push_back(pkg_name);

    printf("Resolving dependencies...");
    fflush(stdout);

    while (!queue.empty()) {
        std::string current = queue.front();
        queue.pop_front();
        if (handled.count(current) || installed_capabilities.count(current)) continue;

        if (current[0] == '/') {
            std::string full_path = effective_root + current;
            if (dvf_util_file_exists(full_path.c_str())) {
                installed_capabilities.insert(current);
                continue;
            }
        }

        bool found = false;
        dvf_metadata_pkg_entry_t* best = nullptr;
        std::string best_evr;

        auto range = provide_map.equal_range(current);
        for (auto it = range.first; it != range.second; ++it) {
            dvf_metadata_pkg_entry_t* pkg = it->second;
            std::string pkg_evr = std::string(pool + pkg->epoch_offset) + ":" + (pool + pkg->version_offset) + "-" + (pool + pkg->release_offset);
            std::string pkg_arch = pool + pkg->arch_offset;

            bool is_preferred = (pkg_arch == fsm.basearch || pkg_arch == "noarch");
            bool best_is_preferred = (best && (std::string(pool + best->arch_offset) == fsm.basearch || std::string(pool + best->arch_offset) == "noarch"));

            if (!best || (is_preferred && !best_is_preferred) || (is_preferred == best_is_preferred && dvf_util_compare_versions(pkg_evr.c_str(), best_evr.c_str()) > 0)) {
                best = pkg;
                best_evr = pkg_evr;
                found = true;
            }
        }

        if (found && best) {
            std::string bname = pool + best->name_offset;
            if (handled.count(bname) || installed_capabilities.count(bname)) {
                // Capability resolved to an already handled or installed package
                installed_capabilities.insert(current);
                continue;
            }
            to_install.push_back(best);
            handled.insert(bname);
            uint32_t* prov_offs = (uint32_t*)((char*)mapped + best->provides_offset);
            for (uint32_t j = 0; j < best->provides_count; j++) {
                std::string p = pool + prov_offs[j];
                handled.insert(p);
                installed_capabilities.insert(p);
            }
            uint32_t* req_offs = (uint32_t*)((char*)mapped + best->requires_offset);
            for (uint32_t j = 0; j < best->requires_count; j++) {
                std::string r = pool + req_offs[j];
                if (!handled.count(r) && !installed_capabilities.count(r)) queue.push_back(r);
            }
        } else {
            printf("\033[1;31m [FAILED]\033[0m\n");
            dvf_log_error("Could not resolve dependency: %s\n", current.c_str());
            munmap(mapped, st.st_size); close(fd);
            return -1;
        }
    }

    if (to_install.empty()) {
        printf(" Done.\nNothing to do. Package already installed.\n");
        munmap(mapped, st.st_size); close(fd);
        return 0;
    }

    printf(" Done.\n\nDependencies resolved.\n");
    printf("================================================================================\n");
    printf(" %-20s %-15s %-25s %-15s\n", "Package", "Architecture", "Version", "Repository");
    printf("================================================================================\n");
    printf("Installing:\n");

    for (const auto& pkg : to_install) {
        printf(" %-20s %-15s %-25s %-15s\n", pool + pkg->name_offset, pool + pkg->arch_offset, pool + pkg->version_offset, pool + pkg->repo_id_offset);
    }

    printf("\nTransaction Summary\n");
    printf("================================================================================\n");
    printf("Install  %zu Package%s\n\n", to_install.size(), to_install.size() > 1 ? "s" : "");

    if (!dvf_util_prompt_yes_no("Is this ok")) {
        printf("Operation aborted.\n");
        munmap(mapped, st.st_size); close(fd);
        return 0;
    }

    if (curl_global_init(CURL_GLOBAL_ALL) != 0) { munmap(mapped, st.st_size); close(fd); return -1; }

    printf("Downloading Packages:\n");
    for (auto& pkg : to_install) {
        std::string name = pool + pkg->name_offset;
        std::string location = pool + pkg->location_offset;
        std::string repo_id = pool + pkg->repo_id_offset;
        std::string version = pool + pkg->version_offset;
        std::string arch = pool + pkg->arch_offset;

        Repo repo;
        for (const auto& r : fsm.repos) if (r.id == repo_id) { repo = r; break; }
        std::vector<std::string> mirrors = fsm.get_mirrors(repo);

        bool downloaded = false;
        std::string rpm_path = std::string(g_dvf_cache_dir) + "/" + name + ".rpm";

        for (const auto& m : mirrors) {
            std::string url = m;
            if (url.back() != '/') url += "/";
            url += location;
            printf(" %s:\n", name.c_str());
            if (fsm.fetcher.fetchToFile(url, rpm_path, true)) { downloaded = true; break; }
        }

        if (downloaded) {
            printf("  Installing       : %-45s ", (name + "-" + version + "." + arch).c_str());
            fflush(stdout);
            if (rpm_unpack(rpm_path.c_str(), g_dvf_install_root) == 0) {
                printf("\033[1;32m[DONE]\033[0m\n");
                rpm_info_t info;
                memset(&info, 0, sizeof(info));
                if (rpm_parse_file(rpm_path.c_str(), &info) == 0) {
                    dvf_storage_write_pkg_info(&info);
                    rpm_free_info(&info);
                }
            } else {
                printf("\033[1;31m[FAILED]\033[0m\n");
                dvf_log_error("Failed to extract %s\n", name.c_str());
            }
            if (g_dvf_cleanup) unlink(rpm_path.c_str());
        } else {
            dvf_log_error("Failed to download %s\n", name.c_str());
            curl_global_cleanup(); munmap(mapped, st.st_size); close(fd);
            return -1;
        }
    }

    curl_global_cleanup();
    munmap(mapped, st.st_size); close(fd);
    printf("\nComplete!\n");
    return 0;
}

extern "C" int dvf_repo_upgrade(void) {
    if (!dvf_repo_has_metadata()) {
        if (dvf_util_prompt_yes_no("No repository metadata found. Run 'dvf update' first?")) {
            if (dvf_repo_update() != 0) return -1;
        } else {
            return -1;
        }
    }

    DVF_FSM fsm;
    fsm.load_repos();

    const char *db_path = "/var/lib/rpm/rpmdb.sqlite";
    if (!dvf_util_file_exists(db_path)) db_path = "rpmdb.sqlite";

    dvf_blob_list_t *blobs = dvf_sqlite_get_package_blobs(db_path);
    if (!blobs) return -1;

    std::map<std::string, std::map<std::string, std::string>> installed;
    for (size_t i = 0; i < blobs->count; i++) {
        rpm_info_t info;
        memset(&info, 0, sizeof(info));
        if (rpm_parse_header(blobs->blobs[i].data, blobs->blobs[i].size, &info) == 0) {
            if (info.name && info.version && info.release) {
                std::string evr = (info.epoch ? std::string(info.epoch) : "0") + ":" + info.version + "-" + info.release;
                installed[info.name][info.arch ? info.arch : "noarch"] = evr;
            }
        }
        rpm_free_info(&info);
    }
    dvf_sqlite_free_blob_list(blobs);

    std::map<std::pair<std::string, std::string>, RepoPackage> updates;
    for (const auto& repo : fsm.repos) {
        if (!repo.enabled) continue;
        std::string primary_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.xml.zst";
        if (!dvf_util_file_exists(primary_path.c_str()))
             primary_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.xml.gz";
        if (!dvf_util_file_exists(primary_path.c_str()))
             primary_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.raw";

        if (dvf_util_file_exists(primary_path.c_str())) {
            parse_primary(primary_path, repo.id, [&](const RepoPackage& pkg) {
                if (installed.count(pkg.name) && installed[pkg.name].count(pkg.arch)) {
                    std::string inst_evr = installed[pkg.name][pkg.arch];
                    std::string repo_evr = pkg.epoch + ":" + pkg.version + "-" + pkg.release;
                    if (dvf_util_compare_versions(repo_evr.c_str(), inst_evr.c_str()) > 0) {
                        auto key = std::make_pair(pkg.name, pkg.arch);
                        if (updates.find(key) == updates.end() ||
                            dvf_util_compare_versions(repo_evr.c_str(), (updates[key].epoch + ":" + updates[key].version + "-" + updates[key].release).c_str()) > 0) {
                            updates[key] = pkg;
                        }
                    }
                }
            });
        }
    }

    if (updates.empty()) {
        printf("No packages to upgrade.\n");
        return 0;
    }

    printf("\nDependencies resolved.\n");
    printf("================================================================================\n");
    printf(" %-20s %-15s %-25s %-15s\n", "Package", "Architecture", "Version", "Repository");
    printf("================================================================================\n");
    printf("Upgrading:\n");

    for (const auto& pair : updates) {
        const auto& pkg = pair.second;
        printf(" %-20s %-15s %-25s %-15s\n",
               pkg.name.c_str(),
               pkg.arch.c_str(),
               pkg.version.c_str(),
               pkg.repo_id.c_str());
    }

    printf("\nTransaction Summary\n");
    printf("================================================================================\n");
    printf("Upgrade  %zu Package%s\n\n", updates.size(), updates.size() > 1 ? "s" : "");

    if (!dvf_util_prompt_yes_no("Is this ok")) {
        printf("Upgrade aborted.\n");
        return 0;
    }

    if (curl_global_init(CURL_GLOBAL_ALL) != 0) return -1;

    printf("Downloading Packages:\n");
    for (const auto& pair : updates) {
        auto pkg = pair.second;
        Repo repo;
        for (const auto& r : fsm.repos) if (r.id == pkg.repo_id) { repo = r; break; }
        std::vector<std::string> mirrors = fsm.get_mirrors(repo);

        bool downloaded = false;
        std::string rpm_path = std::string(g_dvf_cache_dir) + "/" + pkg.name + ".rpm";

        for (const auto& m : mirrors) {
            std::string url = m;
            if (url.back() != '/') url += "/";
            url += pkg.location;

            printf(" %s:\n", pkg.name.c_str());
            if (fsm.fetcher.fetchToFile(url, rpm_path, true)) {
                downloaded = true;
                break;
            }
        }

        if (downloaded) {
            printf("  Upgrading        : %-45s ", (pkg.name + "-" + pkg.version + "." + pkg.arch).c_str());
            fflush(stdout);
            if (rpm_unpack(rpm_path.c_str(), g_dvf_install_root) == 0) {
                printf("\033[1;32m[DONE]\033[0m\n");
                // Update pkginfo.bin
                rpm_info_t info;
                memset(&info, 0, sizeof(info));
                if (rpm_parse_file(rpm_path.c_str(), &info) == 0) {
                    dvf_storage_write_pkg_info(&info);
                    rpm_free_info(&info);
                }
            } else {
                printf("\033[1;31m [FAILED]\033[0m\n");
                dvf_log_error("Failed to upgrade %s\n", pkg.name.c_str());
            }
            if (g_dvf_cleanup) unlink(rpm_path.c_str());
        } else {
            dvf_log_error("Failed to download %s\n", pkg.name.c_str());
        }
    }

    curl_global_cleanup();
    printf("\nComplete!\n");
    return 0;
}
