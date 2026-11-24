#include "pm3/vfs.h"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <cstring>

namespace pm3 {

static std::vector<std::string> split_path(const std::string& s) {
    std::vector<std::string> out; std::string cur;
    for (char ch : s) { if (ch == '/') { if (!cur.empty()) out.push_back(cur); cur.clear(); } else cur += ch; }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string vfs_lookup_ci(const std::string& base, const std::string& rel) {
    std::string cur = base;
    for (const std::string& comp : split_path(rel)) {
        if (comp == ".") continue;
        std::string exact = cur + "/" + comp; struct stat st;
        if (stat(exact.c_str(), &st) == 0) { cur = exact; continue; }          // fast path: exact case
        DIR* d = opendir(cur.c_str()); if (!d) return std::string();
        std::string hit;
        while (dirent* e = readdir(d)) if (strcasecmp(e->d_name, comp.c_str()) == 0) { hit = e->d_name; break; }
        closedir(d);
        if (hit.empty()) return std::string();
        cur += "/" + hit;
    }
    struct stat st; return stat(cur.c_str(), &st) == 0 ? cur : std::string();
}

bool Vfs::normalize(const std::string& guest, std::string* rel) const {
    std::string p = guest; for (char& ch : p) if (ch == '\\') ch = '/';
    if (p.size() >= 2 && p[1] == ':') p.erase(0, 2);                              // drive letter
    while (!p.empty() && p[0] == '/') p.erase(0, 1);
    for (const std::string& a : aliases_) {                                       // "PM3/..." right after the drive
        if (p.size() > a.size() && p[a.size()] == '/' && strncasecmp(p.c_str(), a.c_str(), a.size()) == 0) { p.erase(0, a.size() + 1); break; }
        if (p.size() == a.size() && strcasecmp(p.c_str(), a.c_str()) == 0) { p.clear(); break; }
    }
    std::string out;
    for (const std::string& c : split_path(p)) { if (c == ".") continue; if (c == "..") return false; out += (out.empty() ? "" : "/") + c; }
    *rel = out; return true;
}

std::string Vfs::find(const std::string& guest) const {
    std::string rel; if (!normalize(guest, &rel)) return std::string();
    if (!save_.empty()) { std::string h = vfs_lookup_ci(save_, rel); if (!h.empty()) return h; }
    if (!root_.empty()) return vfs_lookup_ci(root_, rel);
    return std::string();
}

std::string Vfs::make_writable(const std::string& guest) const {
    std::string rel; if (save_.empty() || !normalize(guest, &rel) || rel.empty()) return std::string();
    mkdir(save_.c_str(), 0777);
    std::string cur = save_; std::vector<std::string> comps = split_path(rel);
    for (size_t i = 0; i < comps.size(); i++) {
        std::string hit = vfs_lookup_ci(cur, comps[i]);                           // reuse an existing case variant
        cur = hit.empty() ? cur + "/" + comps[i] : hit;
        if (i + 1 < comps.size()) mkdir(cur.c_str(), 0777);
    }
    return cur;
}

FILE* Vfs::open(const std::string& guest, const char* mode) const {
    bool write = std::strpbrk(mode, "wa+") != nullptr;
    std::string h = write ? make_writable(guest) : find(guest);
    if (h.empty()) return nullptr;
    if (write && mode[0] == 'r') {                                                // "r+b": start from the shipped file if the save copy does not exist yet
        struct stat st; if (stat(h.c_str(), &st) != 0) { std::string src = find(guest); if (!src.empty()) { if (FILE* in = fopen(src.c_str(), "rb")) { if (FILE* out = fopen(h.c_str(), "wb")) { char buf[65536]; size_t n; while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out); fclose(out); } fclose(in); } } }
    }
    return fopen(h.c_str(), mode);
}

long long Vfs::file_size(const std::string& guest) const {
    std::string h = find(guest); struct stat st;
    return (!h.empty() && stat(h.c_str(), &st) == 0) ? (long long)st.st_size : -1;
}

}  // namespace pm3
