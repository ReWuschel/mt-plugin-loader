// The loader's ini files.
//
// The loader owns them, so two plugins that set different keys of the same
// file cannot overwrite each other: every write goes through here, under one
// lock, and the Win32 profile API rewrites the file from what is already in
// it -- so the other plugin's keys, the user's own values and the comments in
// the file all survive.
//
// The rules, in order of precedence:
//
//   - a value already in the file wins over a plugin's default. The user
//     edits these files by hand and keeps what they typed;
//   - two plugins declaring the same key with different values: the plugin
//     that loaded first wins, and the other one is told (the loader logs it);
//   - a forced value is written through at once and always wins (live state).
//
// Everything here is wide, because that is what the profile API is: no
// conversion happens between a plugin and the file it writes.
//
// A file is a path relative to the game folder (Binaries\Win64), so a plugin's
// settings keep living where they always have.

#pragma once
#include <windows.h>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

namespace ini {

// GetPrivateProfileSectionW hands back one string: key=value pairs with NULs
// between them, not one line per pair. Walking it as if it were an array of
// lines turns every key into a dozen broken ones.
inline std::vector<std::wstring> sectionLines(const std::wstring& ini, const wchar_t* section) {
    std::vector<std::wstring> out;
    std::vector<wchar_t> buf(64 * 1024);
    int n = GetPrivateProfileSectionW(section, buf.data(), (int)buf.size(), ini.c_str());
    if (n <= 0) return out;
    const wchar_t* p = buf.data();
    const wchar_t* end = buf.data() + n;
    while (p < end) {
        size_t len = wcslen(p);
        if (len) out.emplace_back(p, len);
        p += len + 1;
    }
    return out;
}

struct Entry {
    std::wstring value;   // what the key resolves to
    std::wstring owner;   // who declared it
    int order = 0;        // the owner's load order: lower wins
    bool fromDisk = false;// the value came from the file, not from a plugin
};

class Store {
public:
    void init(const std::wstring& dir, void (*log)(const wchar_t*)) { m_dir = dir; m_log = log; }

    enum Result {
        APPLIED,   // your value is the one that applies
        ON_DISK,   // the file already had a value: it wins over your default
        CONFLICT,  // another plugin declared that key first: its value wins
        REJECTED,  // the file name is not one this store will touch
    };

    // Declare a value for a key.
    //   force = 0: a default. The file's own value wins if it has one.
    //   force = 1: write it through at once and always win (live state).
    // `conflict`, if given, then says whose value it was.
    Result declare(const std::wstring& file, const std::wstring& section, const std::wstring& key,
                   const std::wstring& value, bool force, const std::wstring& owner, int order,
                   std::wstring* conflict) {
        std::lock_guard<std::mutex> g(m_lock);
        if (!validPath(file)) return REJECTED;
        auto id = std::make_tuple(file, section, key);
        if (force) {
            // Live state: on disk at once, always wins, and not tracked -- so
            // the store does not grow with every value a plugin ever writes.
            m_keys.erase(id);
            write(file, section, key, value);
            return APPLIED;
        }
        auto it = m_keys.find(id);
        if (it != m_keys.end() && !it->second.fromDisk) {
            if (it->second.value == value) return APPLIED;   // both want the same thing
            if (order < it->second.order) {                  // this plugin loaded first: it wins
                if (conflict)
                    *conflict = owner + L" loaded before " + it->second.owner + L" and set " +
                                key + L"=" + value + L", so it wins over " + it->second.value;
                it->second.value = value; it->second.owner = owner; it->second.order = order;
                write(file, section, key, value);
                return APPLIED;
            }
            if (conflict)
                *conflict = owner + L" wants " + key + L"=" + value + L", " + it->second.owner +
                            L" loaded first and set " + key + L"=" + it->second.value + L" (the first one wins)";
            return CONFLICT;
        }
        std::wstring onDisk;
        if (read(file, section, key, onDisk)) {  // the file already has one: it wins
            m_keys[id] = {onDisk, owner, order, true};
            return ON_DISK;
        }
        m_keys[id] = {value, owner, order, false};
        write(file, section, key, value);
        return APPLIED;
    }

    // What the key resolves to: what was declared for it, or what the file
    // holds. Reads go through here so a plugin sees the merged view.
    bool get(const std::wstring& file, const std::wstring& section, const std::wstring& key, std::wstring& out) {
        std::lock_guard<std::mutex> g(m_lock);
        auto it = m_keys.find(std::make_tuple(file, section, key));
        if (it != m_keys.end()) { out = it->second.value; return true; }
        return read(file, section, key, out);
    }

    // A file is a path relative to the game folder: no "..", no drive letter,
    // no leading separator. Subfolders are allowed.
    static bool validPath(const std::wstring& f) {
        if (f.empty() || f.size() > 200) return false;
        if (f.find(L"..") != std::wstring::npos) return false;
        if (f[0] == L'/' || f[0] == L'\\' || f.find(L':') != std::wstring::npos) return false;
        for (wchar_t c : f)
            if (c == L'<' || c == L'>' || c == L'"' || c == L'|' || c == L'?' || c == L'*') return false;
        return true;
    }

private:
    std::wstring path(const std::wstring& file) const {
        std::wstring p = m_dir;
        if (!p.empty() && p.back() != L'\\') p += L'\\';
        std::wstring rel = file;
        for (wchar_t& c : rel) if (c == L'/') c = L'\\';
        return p + rel;
    }

    // The value in the file, or false if the file has no such key. An empty
    // value counts as no key: the plugins have always treated it that way.
    bool read(const std::wstring& file, const std::wstring& section, const std::wstring& key, std::wstring& out) {
        wchar_t buf[1024];
        // The default tells "not there" from "there but empty".
        static const wchar_t kAbsent[] = L"\x01absent";
        GetPrivateProfileStringW(section.c_str(), key.c_str(), kAbsent, buf, 1024, path(file).c_str());
        if (!wcscmp(buf, kAbsent) || !buf[0]) return false;
        out = buf;
        return true;
    }

    // Write one key. The profile API rewrites the file, keeping every other
    // line in it -- comments, other sections, other keys.
    void write(const std::wstring& file, const std::wstring& section, const std::wstring& key, const std::wstring& value) {
        std::wstring p = path(file);
        size_t slash = p.find_last_of(L'\\');
        if (slash != std::wstring::npos) {          // the folder has to exist
            CreateDirectoryW(p.substr(0, slash).c_str(), nullptr);
        }
        if (!WritePrivateProfileStringW(section.c_str(), key.c_str(), value.c_str(), p.c_str()) && m_log) {
            std::wstring m = L"ini: could not write " + file + L" [" + section + L"] " + key;
            m_log(m.c_str());
        }
    }

    std::mutex m_lock;
    std::wstring m_dir;
    void (*m_log)(const wchar_t*) = nullptr;
    std::map<std::tuple<std::wstring, std::wstring, std::wstring>, Entry> m_keys;
};

}  // namespace ini
