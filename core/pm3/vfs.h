// Virtual file system for the original Windows game data (case-insensitive, read-only game folder + writable save folder).
//  * Windows ignores case, iOS (APFS) does not -> every path component is looked up case-insensitively.
//  * The app bundle / game folder is read-only on iOS -> files the game creates (saves, settings) go to a separate save dir
//    (iOS: the app's Documents directory), and reads prefer that save dir so saves shadow shipped files.
//  * Guest (Windows) paths are accepted as-is: "C:\PM3\PM3SYS0.FPA", "D:\PM3W\ID_PM3JP.DAT", "MIDI\M001.MID", ...
// Pure POSIX (dirent/stat); no OS-specific headers.
#pragma once
#include <cstdio>
#include <string>
#include <vector>

namespace pm3 {

class Vfs {
public:
    void set_root(const std::string& dir) { root_ = dir; }            // read-only game data folder
    void set_save_dir(const std::string& dir) { save_ = dir; }        // writable folder (iOS Documents)
    // Directory names that may follow a drive letter and are mapped onto the root ("C:\PM3\x" -> "x").
    void add_root_alias(const std::string& name) { aliases_.push_back(name); }
    const std::string& root() const { return root_; }
    const std::string& save_dir() const { return save_; }

    // Normalises a guest path to a relative, '/'-separated path (drive letter and root alias removed). Returns false for ".." escapes.
    bool normalize(const std::string& guest, std::string* rel) const;
    // Host path of an existing file/dir (save dir first, then root); empty if missing.
    std::string find(const std::string& guest) const;
    // Host path to write to (creates missing directories in the save dir, keeps an existing case variant); empty on failure / no save dir.
    std::string make_writable(const std::string& guest) const;
    bool exists(const std::string& guest) const { return !find(guest).empty(); }
    // fopen with the mapping above: modes containing 'w', 'a' or '+' go to the save dir (copying nothing), others read.
    FILE* open(const std::string& guest, const char* mode) const;
    // Size of an existing file, -1 if missing.
    long long file_size(const std::string& guest) const;

private:
    std::string root_, save_;
    std::vector<std::string> aliases_{"PM3", "PM3W", "PM3DATA"};
};

// Case-insensitive lookup of a relative path under `base`; empty if not found. Exposed for tests.
std::string vfs_lookup_ci(const std::string& base, const std::string& rel);

}  // namespace pm3
