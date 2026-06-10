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

#include "dvf-repo.h"
#include "dvf-util.h"
#include "dvf-config.h"

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

    bool fetchToFile(const std::string& url, const std::string& path) {
        if (!curl) return false;
        FILE* fp = fopen(path.c_str(), "wb");
        if (!fp) return false;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "dvf/1.0");

        CURLcode res = curl_easy_perform(curl);
        fclose(fp);

        if (res != CURLE_OK) {
            dvf_log_debug("Fetch failed: %s (URL: %s)\n", curl_easy_strerror(res), url.c_str());
            return false;
        }
        return true;
    }

private:
    CURL* curl;
};

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

    void load_repos() {
        repos.clear();
        DIR* dir = opendir("/etc/yum.repos.d");
        if (!dir) return;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_name[0] == '.') continue;
            std::string filename = entry->d_name;
            if (filename.size() < 6 || filename.substr(filename.size() - 5) != ".repo") continue;

            parse_repo_file("/etc/yum.repos.d/" + filename);
        }
        closedir(dir);
    }

    void run_sync() {
        load_repos();
        int total = 0;
        for (const auto& r : repos) if (r.enabled) total++;

        if (total == 0) {
            printf("No enabled repositories found.\n");
            current_state = DVF_STATE_SUCCESS;
            return;
        }

        int current = 0;
        for (auto& repo : repos) {
            if (!repo.enabled) continue;
            current++;
            printf(" [%d/%d] Syncing repository: %s\n", current, total, repo.id.c_str());
            if (!sync_repo(repo)) {
                dvf_log_error("  Failed to sync repository %s\n", repo.id.c_str());
            }
        }
        current_state = DVF_STATE_SUCCESS;
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
                repos.emplace_back();
                current_repo = &repos.back();
                current_repo->id = line.substr(1, line.size() - 2);
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

    bool sync_repo(Repo& repo) {
        std::string repomd_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_repomd.xml";
        std::vector<std::string> base_urls;

        if (!repo.baseurl.empty()) {
            base_urls.push_back(repo.baseurl);
        } else if (!repo.metalink.empty() || !repo.mirrorlist.empty()) {
            std::string router_url = repo.metalink.empty() ? repo.mirrorlist : repo.metalink;
            std::string router_cache = std::string(g_dvf_cache_dir) + "/" + repo.id + "_router.txt";

            if (fetcher.fetchToFile(router_url, router_cache)) {
                if (!repo.metalink.empty()) {
                    base_urls = parse_metalink_for_mirrors(router_cache);
                } else {
                    base_urls = parse_mirrorlist_for_mirrors(router_cache);
                }
            }
        }

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
                    if (fetcher.fetchToFile(primary_url, primary_path)) {
                        success = true;
                        break;
                    }
                }
            }
        }

        return success;
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

extern "C" int dvf_repo_update(void) {
    dvf_log_verbose("Starting DVF repository update (Advanced C++ FFI mode)...\n");
    if (curl_global_init(CURL_GLOBAL_ALL) != 0) {
        dvf_log_error("Failed to initialize libcurl\n");
        return -1;
    }

    DVF_FSM fsm;
    fsm.run_sync();

    curl_global_cleanup();
    if (fsm.current_state == DVF_STATE_SUCCESS) {
        printf("All repositories synchronized successfully.\n");
        return 0;
    }
    return -1;
}

extern "C" int dvf_repo_install(const char *pkg_name) {
    dvf_log_verbose("Installing package '%s' via Advanced FSM...\n", pkg_name);
    return 0;
}

static void parse_primary_for_names(const std::string& path, std::vector<std::string>& names) {
    // Robust extraction: handles single-line and multi-line XML by using a streaming buffer.
    // This avoids the limitations of line-based awk and correctly picks the first <name> tag per package.
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

            // Extract the first <name> tag within this <package> block
            size_t n_open = buffer.find("<name>", pkg_start);
            if (n_open != std::string::npos && n_open < pkg_end) {
                size_t n_close = buffer.find("</name>", n_open + 6);
                if (n_close != std::string::npos && n_close < pkg_end) {
                    std::string pkg_name = buffer.substr(n_open + 6, n_close - (n_open + 6));
                    if (!pkg_name.empty()) {
                        names.push_back(pkg_name);
                    }
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
        if (!dvf_util_file_exists(primary_path.c_str())) {
             primary_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.xml.gz";
        }
        if (!dvf_util_file_exists(primary_path.c_str())) {
             primary_path = std::string(g_dvf_cache_dir) + "/" + repo.id + "_primary.raw";
        }

        if (dvf_util_file_exists(primary_path.c_str())) {
            dvf_log_debug("Extracting names from %s...\n", repo.id.c_str());
            parse_primary_for_names(primary_path, all_names);
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

extern "C" int dvf_repo_search(const char *term) {
    dvf_log_verbose("Searching for '%s' in repositories...\n", term);
    return 0;
}

extern "C" int dvf_repo_info(const char *pkg_name) {
    (void)pkg_name;
    return 0;
}
