#include "engine.hpp"
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <bcrypt.h>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <set>
#include <vector>
#include <map>
#include <cctype>
#include <algorithm>
#include <cwchar>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "bcrypt.lib")

static std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}
static std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}

bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool DirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
bool EnsureDir(const std::wstring& p) {
    if (DirExists(p)) return true;
    return SHCreateDirectoryExW(nullptr, p.c_str(), nullptr) == ERROR_SUCCESS || DirExists(p);
}

std::wstring LauncherDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    auto sl = p.find_last_of(L"\\/");
    if (sl != std::wstring::npos) p.resize(sl);
    return p;
}

std::wstring UserHome() {
    wchar_t buf[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, SHGFP_TYPE_CURRENT, buf)))
        return buf;
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return buf;
    return L"";
}

std::wstring ZisHome() {
    return JoinPath(UserHome(), L".zisLauncher");
}

static std::wstring ParentOf(const std::wstring& p);
static void AppendLaunchLog(const std::wstring& path, const std::wstring& line) {
    EnsureDir(ParentOf(path));
    HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char head[40];
    sprintf(head, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    auto u = std::string(head) + ToUtf8(line) + "\r\n";
    DWORD wr = 0;
    WriteFile(f, u.data(), (DWORD)u.size(), &wr, nullptr);
    CloseHandle(f);
}

static std::wstring SlashToWin(std::wstring p) {
    for (auto& c : p) if (c == L'/') c = L'\\';
    return p;
}
static std::wstring FileNameOf(const std::wstring& p) {
    auto sl = p.find_last_of(L"/\\");
    return sl == std::wstring::npos ? p : p.substr(sl + 1);
}
static std::wstring ParentOf(const std::wstring& p) {
    auto sl = p.find_last_of(L"\\/");
    if (sl == std::wstring::npos) return L".";
    return p.substr(0, sl);
}
static std::wstring ToLower(std::wstring s) {
    for (auto& c : s) if (c >= L'A' && c <= L'Z') c = (wchar_t)(c + 32);
    return s;
}
static bool IsLoopbackHttp(const std::wstring& url) {
    auto u = ToLower(url);
    return u.find(L"://127.0.0.1") != std::wstring::npos ||
           u.find(L"://localhost") != std::wstring::npos ||
           u.find(L"://[::1]") != std::wstring::npos;
}

static std::wstring JoinUrl(std::wstring base, const std::wstring& rel) {
    if (base.empty()) return rel;
    if (base.back() != L'/') base.push_back(L'/');
    if (!rel.empty() && (rel[0] == L'/' || rel[0] == L'\\')) return base + rel.substr(1);
    return base + rel;
}

static std::string ReadAll(const std::wstring& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string JsonStr(const std::string& json, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    auto k = json.find(pat);
    if (k == std::string::npos) return {};
    auto c = json.find(':', k + pat.size());
    if (c == std::string::npos) return {};
    auto q1 = json.find('"', c + 1);
    if (q1 == std::string::npos) return {};
    auto q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return {};
    return json.substr(q1 + 1, q2 - q1 - 1);
}
static int JsonInt(const std::string& json, const std::string& key, int def) {
    std::string pat = "\"" + key + "\"";
    auto k = json.find(pat);
    if (k == std::string::npos) return def;
    auto c = json.find(':', k + pat.size());
    if (c == std::string::npos) return def;
    return atoi(json.c_str() + c + 1);
}
static long long JsonI64(const std::string& json, const std::string& key, long long def) {
    std::string pat = "\"" + key + "\"";
    auto k = json.find(pat);
    if (k == std::string::npos) return def;
    auto c = json.find(':', k + pat.size());
    if (c == std::string::npos) return def;
    return atoll(json.c_str() + c + 1);
}
static bool JsonBool(const std::string& json, const std::string& key, bool def) {
    std::string pat = "\"" + key + "\"";
    auto k = json.find(pat);
    if (k == std::string::npos) return def;
    auto c = json.find(':', k + pat.size());
    if (c == std::string::npos) return def;
    auto rest = json.substr(c + 1, 16);
    if (rest.find("true") != std::string::npos) return true;
    if (rest.find("false") != std::string::npos) return false;
    return def;
}
static std::string JsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char ch : s) {
        if (ch == '\\' || ch == '"') o.push_back('\\');
        o.push_back(ch);
    }
    return o;
}
static std::wstring ReplaceAll(std::wstring s, const std::wstring& from, const std::wstring& to) {
    size_t p = 0;
    while ((p = s.find(from, p)) != std::wstring::npos) {
        s.replace(p, from.size(), to);
        p += to.size();
    }
    return s;
}

static std::string ToHex(const unsigned char* d, ULONG n) {
    static const char* hex = "0123456789abcdef";
    std::string s(n * 2, '0');
    for (ULONG i = 0; i < n; i++) {
        s[i * 2] = hex[d[i] >> 4];
        s[i * 2 + 1] = hex[d[i] & 0xf];
    }
    return s;
}

static bool Sha256File(const std::wstring& path, std::string& out) {
    out.clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        DWORD objLen = 0, hashLen = 0, cb = 0;
        if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &cb, 0) == 0 &&
            BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLen, sizeof(hashLen), &cb, 0) == 0) {
            std::vector<UCHAR> obj(objLen), hash(hashLen);
            if (BCryptCreateHash(alg, &h, obj.data(), objLen, nullptr, 0, 0) == 0) {
                unsigned char buf[65536];
                DWORD rd = 0;
                ok = true;
                while (ReadFile(f, buf, sizeof(buf), &rd, nullptr) && rd) {
                    if (BCryptHashData(h, buf, rd, 0) != 0) { ok = false; break; }
                }
                if (ok && BCryptFinishHash(h, hash.data(), hashLen, 0) == 0)
                    out = ToHex(hash.data(), hashLen);
                else
                    ok = false;
                BCryptDestroyHash(h);
            }
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    CloseHandle(f);
    return ok && out.size() == 64;
}

static bool HashMatches(const std::wstring& path, const std::string& want) {
    if (want.empty() || !FileExists(path)) return false;
    std::string have;
    if (!Sha256File(path, have)) return false;
    return _stricmp(have.c_str(), want.c_str()) == 0;
}

static std::wstring ToFwd(std::wstring p) {
    for (auto& c : p) if (c == L'\\') c = L'/';
    return p;
}
static void SkipWs(const std::string& s, size_t& i) {
    while (i < s.size() && (unsigned char)s[i] <= ' ') i++;
}
static bool SkipJsonString(const std::string& s, size_t& i) {
    if (i >= s.size() || s[i] != '"') return false;
    i++;
    while (i < s.size()) {
        if (s[i] == '\\') { i += (i + 1 < s.size()) ? 2 : 1; continue; }
        if (s[i] == '"') { i++; return true; }
        i++;
    }
    return false;
}
static bool SkipJsonValue(const std::string& s, size_t& i) {
    SkipWs(s, i);
    if (i >= s.size()) return false;
    if (s[i] == '"') return SkipJsonString(s, i);
    if (s[i] == '{' || s[i] == '[') {
        char open = s[i], close = (open == '{') ? '}' : ']';
        int d = 0;
        while (i < s.size()) {
            if (s[i] == '"') { SkipJsonString(s, i); continue; }
            if (s[i] == open) { d++; i++; continue; }
            if (s[i] == close) { d--; i++; if (d == 0) return true; continue; }
            i++;
        }
        return false;
    }
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']') i++;
    return true;
}
static std::string DecodeJsonStringAt(const std::string& s, size_t q1) {
    if (q1 >= s.size() || s[q1] != '"') return {};
    std::string o;
    o.reserve(64);
    size_t i = q1 + 1;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i++];
        if (c == '"') break;
        if (c == '\\' && i < s.size()) {
            char e = s[i++];
            if (e == 'n') o += '\n';
            else if (e == 't') o += '\t';
            else if (e == 'r') o += '\r';
            else if (e == '"') o += '"';
            else if (e == '\\') o += '\\';
            else if (e == 'u' && i + 4 <= s.size()) i += 4;
            else o += e;
        } else {
            o += (char)c;
        }
    }
    return o;
}
static std::string JsonDecoded(const std::string& json, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    size_t k = 0;
    while ((k = json.find(pat, k)) != std::string::npos) {
        size_t c = json.find(':', k + pat.size());
        if (c == std::string::npos) return {};
        size_t i = c + 1;
        SkipWs(json, i);
        if (i < json.size() && json[i] == '"') return DecodeJsonStringAt(json, i);
        k += pat.size();
    }
    return {};
}
static std::string JsonChild(const std::string& json, const std::string& key, char open) {
    std::string pat = "\"" + key + "\"";
    size_t k = 0;
    while ((k = json.find(pat, k)) != std::string::npos) {
        size_t c = json.find(':', k + pat.size());
        if (c == std::string::npos) break;
        size_t i = c + 1;
        SkipWs(json, i);
        if (i < json.size() && json[i] == open) {
            size_t start = i;
            if (SkipJsonValue(json, i)) return json.substr(start, i - start);
        }
        k += pat.size();
    }
    return {};
}
static std::vector<std::string> JsonObjectList(const std::string& arr) {
    std::vector<std::string> out;
    size_t i = 0;
    SkipWs(arr, i);
    if (i >= arr.size() || arr[i] != '[') return out;
    i++;
    while (i < arr.size()) {
        SkipWs(arr, i);
        if (i < arr.size() && arr[i] == ']') break;
        if (i < arr.size() && arr[i] == '{') {
            size_t start = i;
            if (!SkipJsonValue(arr, i)) break;
            out.push_back(arr.substr(start, i - start));
        } else if (!SkipJsonValue(arr, i)) {
            break;
        }
        SkipWs(arr, i);
        if (i < arr.size() && arr[i] == ',') i++;
    }
    return out;
}
static std::vector<std::wstring> JsonStringList(const std::string& arr) {
    std::vector<std::wstring> out;
    size_t i = 0;
    SkipWs(arr, i);
    if (i >= arr.size() || arr[i] != '[') return out;
    i++;
    while (i < arr.size()) {
        SkipWs(arr, i);
        if (i < arr.size() && arr[i] == ']') break;
        if (i < arr.size() && arr[i] == '"') {
            auto s = DecodeJsonStringAt(arr, i);
            if (!SkipJsonString(arr, i)) break;
            if (!s.empty()) out.push_back(ToWide(s));
        } else if (!SkipJsonValue(arr, i)) {
            break;
        }
        SkipWs(arr, i);
        if (i < arr.size() && arr[i] == ',') i++;
    }
    return out;
}
static std::string SliceJsonValue(const std::string& s, size_t i) {
    size_t start = i, j = i;
    if (!SkipJsonValue(s, j)) return {};
    return s.substr(start, j - start);
}
static bool Md5Bytes(const std::string& in, unsigned char out[16]) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, nullptr, 0) != 0) return false;
    DWORD objLen = 0, hashLen = 0, cb = 0;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &cb, 0) == 0 &&
        BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLen, sizeof(hashLen), &cb, 0) == 0 &&
        hashLen == 16) {
        std::vector<UCHAR> obj(objLen);
        if (BCryptCreateHash(alg, &h, obj.data(), objLen, nullptr, 0, 0) == 0) {
            if (BCryptHashData(h, (PUCHAR)in.data(), (ULONG)in.size(), 0) == 0 &&
                BCryptFinishHash(h, out, 16, 0) == 0)
                ok = true;
            BCryptDestroyHash(h);
        }
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}
static std::wstring OfflineUuid(const std::wstring& nick) {
    unsigned char d[16]{};
    if (!Md5Bytes("OfflinePlayer:" + ToUtf8(nick), d)) return L"00000000-0000-0000-0000-000000000000";
    d[6] = (unsigned char)((d[6] & 0x0f) | 0x30);
    d[8] = (unsigned char)((d[8] & 0x3f) | 0x80);
    wchar_t buf[48];
    _snwprintf(buf, 48, L"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7],
             d[8], d[9], d[10], d[11], d[12], d[13], d[14], d[15]);
    return buf;
}
static bool OsRuleMatchesWindows(const std::string& rule) {
    if (rule.find("\"os\"") == std::string::npos) return true;
    if (rule.find("\"windows\"") == std::string::npos) return false;
    if (rule.find("\"arch\"") != std::string::npos && rule.find("\"x86\"") != std::string::npos)
        return false;
    return true;
}
static bool LibraryAllowed(const std::string& obj) {
    auto r = obj.find("\"rules\"");
    if (r == std::string::npos) return true;
    auto lb = obj.find('[', r);
    if (lb == std::string::npos) return true;
    bool allow = false;
    size_t i = lb + 1;
    while (i < obj.size()) {
        SkipWs(obj, i);
        if (i >= obj.size() || obj[i] == ']') break;
        if (obj[i] == ',') { i++; continue; }
        auto rule = SliceJsonValue(obj, i);
        SkipJsonValue(obj, i);
        if (!OsRuleMatchesWindows(rule)) continue;
        if (rule.find("\"disallow\"") != std::string::npos) allow = false;
        else if (rule.find("\"allow\"") != std::string::npos) allow = true;
    }
    return allow;
}
static bool SkipNativeJar(const std::string& path) {
    if (path.find("natives-linux") != std::string::npos) return true;
    if (path.find("natives-macos") != std::string::npos) return true;
    if (path.find("natives-windows-arm64") != std::string::npos) return true;
    if (path.find("natives-windows-x86") != std::string::npos) return true;
    if (path.find("linux-aarch") != std::string::npos) return true;
    if (path.find("linux-x86_64") != std::string::npos) return true;
    return false;
}
static std::string LibMapKey(const std::string& name) {
    std::vector<std::string> p;
    size_t b = 0;
    for (size_t i = 0; i <= name.size(); i++) {
        if (i == name.size() || name[i] == ':') {
            p.push_back(name.substr(b, i - b));
            b = i + 1;
        }
    }
    if (p.size() < 2) return name;
    std::string k = p[0] + ":" + p[1];
    if (p.size() >= 4) k += ":" + p[3];
    return k;
}
static void CollectLibraries(const std::string& json, std::vector<std::string>& paths, std::map<std::string, size_t>& idx) {
    auto k = json.find("\"libraries\"");
    if (k == std::string::npos) return;
    auto lb = json.find('[', k);
    if (lb == std::string::npos) return;
    size_t i = lb + 1;
    while (i < json.size()) {
        SkipWs(json, i);
        if (i >= json.size() || json[i] == ']') break;
        if (json[i] == ',') { i++; continue; }
        auto obj = SliceJsonValue(json, i);
        SkipJsonValue(json, i);
        if (!LibraryAllowed(obj)) continue;
        auto path = JsonStr(obj, "path");
        auto name = JsonStr(obj, "name");
        if (path.empty() || SkipNativeJar(path)) continue;
        auto key = LibMapKey(name.empty() ? path : name);
        auto it = idx.find(key);
        if (it != idx.end()) paths[it->second] = path;
        else { idx[key] = paths.size(); paths.push_back(path); }
    }
}
static bool ArgObjectWanted(const std::string& obj, bool lockServer) {
    if (obj.find("is_demo_user") != std::string::npos) return false;
    if (obj.find("has_custom_resolution") != std::string::npos) return false;
    if (obj.find("has_quick_plays_support") != std::string::npos) return false;
    if (obj.find("is_quick_play_singleplayer") != std::string::npos) return false;
    if (obj.find("is_quick_play_realms") != std::string::npos) return false;
    if (obj.find("is_quick_play_multiplayer") != std::string::npos) return lockServer;
    if (obj.find("\"os\"") != std::string::npos) return OsRuleMatchesWindows(obj);
    return true;
}
static void PushJsonStringValue(const std::string& raw, std::vector<std::wstring>& out) {
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
        out.push_back(ToWide(raw.substr(1, raw.size() - 2)));
}
static void ExtractArgValues(const std::string& obj, std::vector<std::wstring>& out) {
    auto v = obj.find("\"value\"");
    if (v == std::string::npos) return;
    size_t i = obj.find(':', v);
    if (i == std::string::npos) return;
    i++;
    SkipWs(obj, i);
    if (i >= obj.size()) return;
    if (obj[i] == '"') {
        out.push_back(ToWide(JsonStr(obj, "value")));
        return;
    }
    if (obj[i] != '[') return;
    i++;
    while (i < obj.size()) {
        SkipWs(obj, i);
        if (i >= obj.size() || obj[i] == ']') break;
        if (obj[i] == ',') { i++; continue; }
        if (obj[i] == '"') {
            auto raw = SliceJsonValue(obj, i);
            SkipJsonValue(obj, i);
            PushJsonStringValue(raw, out);
        } else {
            SkipJsonValue(obj, i);
        }
    }
}
static std::vector<std::wstring> ProfileArgList(const std::string& json, const char* which, bool lockServer) {
    std::vector<std::wstring> out;
    auto argsKey = json.find("\"arguments\"");
    if (argsKey == std::string::npos) return out;
    auto whichKey = json.find(std::string("\"") + which + "\"", argsKey);
    if (whichKey == std::string::npos) return out;
    auto lb = json.find('[', whichKey);
    if (lb == std::string::npos) return out;
    size_t i = lb + 1;
    while (i < json.size()) {
        SkipWs(json, i);
        if (i >= json.size() || json[i] == ']') break;
        if (json[i] == ',') { i++; continue; }
        if (json[i] == '"') {
            auto raw = SliceJsonValue(json, i);
            SkipJsonValue(json, i);
            PushJsonStringValue(raw, out);
            continue;
        }
        if (json[i] == '{') {
            auto obj = SliceJsonValue(json, i);
            SkipJsonValue(json, i);
            if (ArgObjectWanted(obj, lockServer)) ExtractArgValues(obj, out);
            continue;
        }
        SkipJsonValue(json, i);
    }
    return out;
}
static std::vector<std::wstring> SplitWs(const std::wstring& s) {
    std::vector<std::wstring> o;
    std::wstring cur;
    for (wchar_t c : s) {
        if (c == L' ' || c == L'\t') {
            if (!cur.empty()) { o.push_back(cur); cur.clear(); }
        } else cur.push_back(c);
    }
    if (!cur.empty()) o.push_back(cur);
    return o;
}
static std::wstring FindVersionJson(const std::wstring& versions) {
    auto pref = JoinPath(JoinPath(versions, L"neoforge-21.1.248"), L"neoforge-21.1.248.json");
    if (FileExists(pref)) return pref;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((versions + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::wstring found;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == L'.') continue;
        auto p = JoinPath(JoinPath(versions, fd.cFileName), std::wstring(fd.cFileName) + L".json");
        if (FileExists(p) && ToLower(fd.cFileName).find(L"neoforge") != std::wstring::npos) {
            found = p;
            break;
        }
        if (found.empty() && FileExists(p)) found = p;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}
static bool WriteArgFile(const std::wstring& path, const std::vector<std::wstring>& args) {
    EnsureDir(ParentOf(path));
    std::ofstream f(path.c_str(), std::ios::binary);
    if (!f) return false;
    for (auto& a : args) {
        auto u = ToUtf8(a);
        bool q = u.empty() || u.find_first_of(" \t#\"") != std::string::npos;
        if (q) f << '"' << u << "\"\n";
        else f << u << '\n';
    }
    return true;
}
static DWORD SpawnWait(const std::wstring& exe, const std::wstring& args, const std::wstring& cwd, LogFn log, bool wait);
static bool ExtractZip(const std::wstring& zip, const std::wstring& dest, LogFn log);
static void ClearReadonlyTree(const std::wstring& dir);
static void CollectNativeJars(const std::wstring& dir, std::vector<std::wstring>& out) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.') continue;
        auto p = JoinPath(dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CollectNativeJars(p, out);
            continue;
        }
        auto low = ToLower(fd.cFileName);
        if (low.size() > 4 && low.compare(low.size() - 4, 4, L".jar") == 0 &&
            low.find(L"natives-windows") != std::wstring::npos)
            out.push_back(p);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static void WipeDirFiles(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.') continue;
        auto p = JoinPath(dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            WipeDirFiles(p);
            RemoveDirectoryW(p.c_str());
        } else {
            DeleteFileW(p.c_str());
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static bool PrepareNatives(const std::wstring& libDir, const std::wstring& natives, LogFn log) {
    WipeDirFiles(natives);
    EnsureDir(natives);
    std::vector<std::wstring> jars;
    CollectNativeJars(libDir, jars);
    std::sort(jars.begin(), jars.end());
    if (jars.empty()) {
        if (log) log(L"Нет natives-windows.jar в libraries.");
        return false;
    }
    if (log) log(L"Нативы: " + std::to_wstring(jars.size()) + L" jar");
    wchar_t sys[MAX_PATH]{};
    GetSystemDirectoryW(sys, MAX_PATH);
    auto tar = JoinPath(sys, L"tar.exe");
    if (!FileExists(tar)) tar = L"tar.exe";
    auto bat = JoinPath(natives, L"_extract.cmd");
    {
        std::ofstream f(bat.c_str(), std::ios::binary);
        if (!f) return false;
        f << "@echo off\r\n";
        auto tarU = ToUtf8(tar);
        auto destU = ToUtf8(natives);
        for (auto& jar : jars) {
            f << "\"" << tarU << "\" -xf \"" << ToUtf8(jar) << "\" -C \"" << destU << "\"\r\n";
            f << "if errorlevel 1 exit /b 1\r\n";
        }
    }
    auto cmd = JoinPath(sys, L"cmd.exe");
    DWORD code = SpawnWait(cmd, L"/c \"" + bat + L"\"", natives, log, true);
    DeleteFileW(bat.c_str());
    if (code != 0) {
        if (log) log(L"Не распаковались natives.");
        return false;
    }
    ClearReadonlyTree(natives);
    return true;
}
static std::wstring QuoteArg(const std::wstring& a) {
    std::wstring o = L"\"";
    for (wchar_t c : a) {
        if (c == L'"') o += L'\\';
        o += c;
    }
    o += L'"';
    return o;
}
static HANDLE gGameJob = nullptr;
static HANDLE gGameProc = nullptr;
static DWORD gGameExitCode = 0;

void StopGameClient() {
    if (gGameJob) {
        TerminateJobObject(gGameJob, 1);
        CloseHandle(gGameJob);
        gGameJob = nullptr;
    }
    if (gGameProc) {
        DWORD code = 0;
        if (GetExitCodeProcess(gGameProc, &code) && code == STILL_ACTIVE)
            TerminateProcess(gGameProc, 1);
        CloseHandle(gGameProc);
        gGameProc = nullptr;
    }
}

bool GameClientRunning() {
    if (!gGameProc) return false;
    DWORD code = 0;
    if (!GetExitCodeProcess(gGameProc, &code)) return false;
    if (code != STILL_ACTIVE) {
        gGameExitCode = code;
        return false;
    }
    return true;
}

unsigned long GameClientExitCode() { return gGameExitCode; }

static FILETIME gSessionStartFt{};

void MarkGameSessionStart() {
    GetSystemTimeAsFileTime(&gSessionStartFt);
}

bool HasFreshCrashReport(const std::wstring& gameDir) {
    std::wstring root = gameDir.empty() ? JoinPath(ZisHome(), L"game") : gameDir;
    auto scan = [&](const std::wstring& pattern) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return false;
        bool hit = false;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (CompareFileTime(&fd.ftLastWriteTime, &gSessionStartFt) >= 0) {
                hit = true;
                break;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        return hit;
    };
    if (scan(JoinPath(root, L"crash-reports") + L"\\*.txt")) return true;
    if (scan(root + L"\\hs_err_pid*.log")) return true;
    if (scan(JoinPath(ZisHome(), L"runtime") + L"\\hs_err_pid*.log")) return true;
    return false;
}

void ReleaseGameHandles() {
    if (gGameProc) {
        DWORD code = 0;
        if (GetExitCodeProcess(gGameProc, &code)) gGameExitCode = code;
        CloseHandle(gGameProc);
        gGameProc = nullptr;
    }
    if (gGameJob) {
        CloseHandle(gGameJob);
        gGameJob = nullptr;
    }
}

std::wstring RuntimeDir() { return JoinPath(ZisHome(), L"runtime"); }
std::wstring LauncherLogPath() { return JoinPath(RuntimeDir(), L"launcher.log"); }
std::wstring MinecraftLogPath() { return JoinPath(RuntimeDir(), L"minecraft.log"); }

static bool BindGameJob(HANDLE proc) {
    if (gGameJob) {
        CloseHandle(gGameJob);
        gGameJob = nullptr;
    }
    gGameJob = CreateJobObjectW(nullptr, nullptr);
    if (!gGameJob) return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(gGameJob, JobObjectExtendedLimitInformation, &li, sizeof(li)))
        return false;
    return AssignProcessToJobObject(gGameJob, proc) != 0;
}

static bool SpawnJava(const std::wstring& java, const std::vector<std::wstring>& argv,
                      const std::wstring& cwd, const std::wstring& natives,
                      const std::wstring& argFile, const std::wstring& logFile, LogFn log) {
    StopGameClient();
    std::vector<std::wstring> fileArgs;
    fileArgs.reserve(argv.size());
    for (auto& a : argv) fileArgs.push_back(ToFwd(a));
    if (!WriteArgFile(argFile, fileArgs)) {
        if (log) log(L"Не записался client.args");
        return false;
    }
    std::wstring cmd = QuoteArg(java) + L" @" + QuoteArg(argFile);
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hLog = CreateFileW(logFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (hLog != INVALID_HANDLE_VALUE) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hLog;
        si.hStdError = hLog;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    wchar_t oldPath[32768]{};
    DWORD n = GetEnvironmentVariableW(L"PATH", oldPath, 32768);
    std::wstring newPath = natives + L";";
    if (n) newPath += oldPath;
    SetEnvironmentVariableW(L"PATH", newPath.c_str());
    std::vector<wchar_t> mut(cmd.begin(), cmd.end());
    mut.push_back(0);
    PROCESS_INFORMATION pi{};
    DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT;
    BOOL ok = CreateProcessW(java.c_str(), mut.data(), nullptr, nullptr,
                             hLog != INVALID_HANDLE_VALUE, flags, nullptr,
                             cwd.c_str(), &si, &pi);
    DWORD err = GetLastError();
    if (n) SetEnvironmentVariableW(L"PATH", oldPath);
    else SetEnvironmentVariableW(L"PATH", nullptr);
    if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);
    if (!ok) {
        if (log) log(L"CreateProcess ошибка " + std::to_wstring(err));
        return false;
    }
    if (!BindGameJob(pi.hProcess) && log)
        log(L"Клиент не в job — закрою по PID.");
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    gGameProc = pi.hProcess;
    WaitForSingleObject(gGameProc, 8000);
    DWORD code = 0;
    GetExitCodeProcess(gGameProc, &code);
    if (code == STILL_ACTIVE) {
        if (log) log(L"Minecraft запущен.");
        return true;
    }
    StopGameClient();
    if (log) log(L"Java сразу вышла, код " + std::to_wstring(code) + L". Лог: minecraft.log");
    return false;
}

bool SpawnTrackedProcess(const std::wstring& exe, const std::wstring& args) {
    StopGameClient();
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    std::vector<wchar_t> mut(cmd.begin(), cmd.end());
    mut.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, mut.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                             nullptr, nullptr, &si, &pi);
    if (!ok) return false;
    BindGameJob(pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    gGameProc = pi.hProcess;
    return true;
}
static bool LaunchClient(const LaunchState& st, const LaunchConfig& cfg,
                         const std::wstring& game, const std::wstring& java,
                         const std::wstring& runtime, LogFn log) {
    auto versions = JoinPath(game, L"versions");
    auto vjsonPath = FindVersionJson(versions);
    if (vjsonPath.empty()) {
        if (log) log(L"Нет versions/*.json — сборка не распаковалась.");
        return false;
    }
    auto child = ReadAll(vjsonPath);
    auto inherit = JsonStr(child, "inheritsFrom");
    if (inherit.empty()) inherit = "1.21.1";
    auto parentPath = JoinPath(JoinPath(versions, ToWide(inherit)), ToWide(inherit) + L".json");
    auto parent = ReadAll(parentPath);
    if (parent.empty()) {
        if (log) log(L"Нет ванильного профиля: " + ToWide(inherit));
        return false;
    }
    auto clientJar = JoinPath(JoinPath(versions, ToWide(inherit)), ToWide(inherit) + L".jar");
    if (!FileExists(clientJar)) {
        if (log) log(L"Нет client jar: " + clientJar);
        return false;
    }
    auto mainClass = JsonStr(child, "mainClass");
    if (mainClass.empty()) mainClass = "cpw.mods.bootstraplauncher.BootstrapLauncher";

    std::string assets = "17";
    {
        auto k = parent.find("\"assetIndex\"");
        if (k != std::string::npos) {
            auto aid = JsonStr(parent.substr(k, 500), "id");
            if (!aid.empty()) assets = aid;
        }
    }

    auto libDir = JoinPath(game, L"libraries");
    auto natives = JoinPath(game, L"natives");
    if (!PrepareNatives(libDir, natives, log)) return false;

    std::vector<std::wstring> cp;
    std::set<std::wstring> seen;
    auto addCp = [&](const std::wstring& jar) {
        auto key = ToLower(jar);
        if (!FileExists(jar) || !seen.insert(key).second) return;
        cp.push_back(jar);
    };
    addCp(clientJar);
    std::vector<std::string> libPaths;
    std::map<std::string, size_t> libIdx;
    CollectLibraries(child, libPaths, libIdx);
    CollectLibraries(parent, libPaths, libIdx);
    for (auto& rel : libPaths) addCp(JoinPath(libDir, SlashToWin(ToWide(rel))));
    if (cp.size() < 2) {
        if (log) log(L"Classpath слишком короткий.");
        return false;
    }
    std::wstring classpath;
    for (size_t i = 0; i < cp.size(); i++) {
        if (i) classpath += L";";
        classpath += cp[i];
    }

    auto nfJvm = ProfileArgList(child, "jvm", false);
    std::wstring modulePath;
    std::vector<std::wstring> nfJvmRest;
    for (size_t i = 0; i < nfJvm.size(); i++) {
        if (nfJvm[i] == L"-p" || nfJvm[i] == L"--module-path") {
            if (i + 1 < nfJvm.size()) { modulePath = nfJvm[i + 1]; i++; }
            continue;
        }
        nfJvmRest.push_back(nfJvm[i]);
    }
    if (modulePath.empty()) {
        if (log) log(L"В NeoForge манифесте нет -p.");
        return false;
    }

    std::wstring server = L"zis.inflexus.world:25666";
    std::map<std::wstring, std::wstring> sub;
    sub[L"${library_directory}"] = libDir;
    sub[L"${classpath_separator}"] = L";";
    sub[L"${version_name}"] = ToWide(inherit);
    sub[L"${natives_directory}"] = natives;
    sub[L"${game_directory}"] = game;
    sub[L"${assets_root}"] = JoinPath(game, L"assets");
    sub[L"${assets_index_name}"] = ToWide(assets);
    sub[L"${auth_player_name}"] = st.nick;
    sub[L"${auth_uuid}"] = OfflineUuid(st.nick);
    sub[L"${auth_access_token}"] = L"0";
    sub[L"${clientid}"] = L"";
    sub[L"${auth_xuid}"] = L"";
    sub[L"${user_type}"] = L"mojang";
    sub[L"${version_type}"] = L"release";
    sub[L"${launcher_name}"] = L"DynastyLauncher";
    sub[L"${launcher_version}"] = L"25";
    sub[L"${classpath}"] = classpath;
    sub[L"${quickPlayMultiplayer}"] = server;
    auto apply = [&](std::wstring s) {
        for (auto& kv : sub) s = ReplaceAll(s, kv.first, kv.second);
        return s;
    };

    std::vector<std::wstring> argv;
    argv.push_back(L"-Xmx" + std::to_wstring(st.ramMb) + L"M");
    argv.push_back(L"-Xms512M");
    argv.push_back(L"-XX:+UseG1GC");
    argv.push_back(L"-XX:+UnlockExperimentalVMOptions");
    argv.push_back(L"-XX:+DisableExplicitGC");
    argv.push_back(L"-XX:MaxGCPauseMillis=50");
    argv.push_back(L"-Dfile.encoding=UTF-8");
    argv.push_back(L"-Djava.rmi.server.useCodebaseOnly=true");
    argv.push_back(L"-Dcom.sun.jndi.rmi.object.trustURLCodebase=false");
    argv.push_back(L"-Dcom.sun.jndi.cosnaming.object.trustURLCodebase=false");
    argv.push_back(L"-Dlog4j2.formatMsgNoLookups=true");
    argv.push_back(L"-Djava.awt.headless=false");
    argv.push_back(L"-Djava.net.preferIPv4Stack=true");
    argv.push_back(L"-Dminecraft.client.jar=" + clientJar);
    argv.push_back(L"-Dfml.ignoreInvalidMinecraftCertificates=true");
    argv.push_back(L"-Dfml.ignorePatchDiscrepancies=true");
    argv.push_back(L"-DlegacyClassPath=" + classpath);
    argv.push_back(L"-Djava.library.path=" + natives);
    argv.push_back(L"-Dorg.lwjgl.librarypath=" + natives);
    argv.push_back(L"-p");
    argv.push_back(apply(modulePath));
    for (auto& a : nfJvmRest) argv.push_back(apply(a));
    for (auto& a : ProfileArgList(parent, "jvm", false)) argv.push_back(apply(a));

    auto logCfg = JoinPath(JoinPath(JoinPath(game, L"assets"), L"log_configs"), L"client-1.12.xml");
    if (FileExists(logCfg))
        argv.push_back(L"-Dlog4j.configurationFile=file:///" + ToFwd(logCfg));

    argv.push_back(ToWide(mainClass));
    for (auto& a : ProfileArgList(child, "game", true)) argv.push_back(apply(a));
    for (auto& a : ProfileArgList(parent, "game", true)) argv.push_back(apply(a));
    bool hasQuick = false;
    for (auto& a : argv) {
        if (a == L"--quickPlayMultiplayer") { hasQuick = true; break; }
    }
    if (!hasQuick) {
        argv.push_back(L"--quickPlayMultiplayer");
        argv.push_back(server);
    }

    if (log) log(L"Запуск NeoForge  " + st.nick + L"  →  " + server);
    return SpawnJava(java, argv, game, natives, JoinPath(runtime, L"client.args"),
                     JoinPath(runtime, L"minecraft.log"), log);
}

LaunchConfig LoadLiveConfig(const std::wstring& path) {
    LaunchConfig c;
    auto j = ReadAll(path);
    if (j.empty()) return c;
    auto h = JsonStr(j, "server_host");
    if (!h.empty()) c.host = ToWide(h);
    int p = JsonInt(j, "server_port", 0);
    if (p > 0) c.port = p;
    auto a = JsonStr(j, "java_args");
    if (!a.empty()) c.javaArgs = ToWide(a);
    c.lockToServer = JsonBool(j, "lock_to_server", true);
    auto nu = JsonStr(j, "news_url");
    if (!nu.empty()) c.newsUrl = ToWide(nu);
    auto iu = JsonStr(j, "index_url");
    if (!iu.empty()) c.indexUrl = ToWide(iu);
    auto pb = JsonStr(j, "pack_base");
    if (!pb.empty()) c.packBase = ToWide(pb);
    auto lu = JsonStr(j, "launcher_url");
    if (!lu.empty()) c.launcherUrl = ToWide(lu);
    auto cu = JsonStr(j, "content_url");
    if (!cu.empty()) c.contentUrl = ToWide(cu);
    return c;
}

std::vector<NewsItem> LoadNewsFile(const std::wstring& path) {
    std::vector<NewsItem> items;
    auto j = ReadAll(path);
    if (j.empty()) return items;
    auto arr = JsonChild(j, "items", '[');
    if (arr.empty()) {
        if (j.find("\"feed\"") == std::string::npos) return items;
        size_t pos = 0;
        while (items.size() < 8) {
            auto k = j.find("\"date\"", pos);
            if (k == std::string::npos) break;
            auto start = j.rfind('{', k);
            auto end = j.find('}', k);
            if (start == std::string::npos || end == std::string::npos) break;
            auto obj = j.substr(start, end - start + 1);
            NewsItem it;
            it.date = ToWide(JsonDecoded(obj, "date"));
            it.title = ToWide(JsonDecoded(obj, "title"));
            it.text = ToWide(JsonDecoded(obj, "text"));
            it.image = ToWide(JsonDecoded(obj, "image"));
            if (!it.title.empty() || !it.text.empty() || !it.image.empty()) items.push_back(it);
            pos = end + 1;
        }
        return items;
    }
    for (const auto& obj : JsonObjectList(arr)) {
        if (items.size() >= 8) break;
        NewsItem it;
        it.date = ToWide(JsonDecoded(obj, "date"));
        it.title = ToWide(JsonDecoded(obj, "title"));
        it.text = ToWide(JsonDecoded(obj, "text"));
        it.image = ToWide(JsonDecoded(obj, "image"));
        if (!it.title.empty() || !it.text.empty() || !it.image.empty()) items.push_back(it);
    }
    return items;
}

static std::wstring HttpOrigin(const std::wstring& url) {
    auto p = url.find(L"://");
    if (p == std::wstring::npos) return {};
    auto slash = url.find(L'/', p + 3);
    if (slash == std::wstring::npos) return url;
    return url.substr(0, slash);
}

static std::wstring NewsImageUrl(const LaunchConfig& cfg, const std::wstring& image) {
    if (image.empty()) return {};
    auto low = ToLower(image);
    bool ok = low.find(L"news-media/") != std::wstring::npos
           || low.find(L"/img/") != std::wstring::npos
           || low.find(L".png") != std::wstring::npos
           || low.find(L".jpg") != std::wstring::npos
           || low.find(L".jpeg") != std::wstring::npos
           || low.find(L".gif") != std::wstring::npos
           || low.find(L".webp") != std::wstring::npos;
    if (!ok) return {};
    if (low.find(L"://") != std::wstring::npos) return image;
    auto origin = HttpOrigin(cfg.newsUrl);
    if (origin.empty()) origin = L"http://zis.inflexus.world";
    if (!image.empty() && image[0] == L'/') return origin + image;
    return origin + L"/" + image;
}

static std::wstring GuessExt(const std::wstring& url) {
    auto q = url.find(L'?');
    auto p = q == std::wstring::npos ? url : url.substr(0, q);
    auto d = p.find_last_of(L'.');
    if (d == std::wstring::npos) return L".img";
    auto ext = ToLower(p.substr(d));
    if (ext == L".gif" || ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".webp")
        return ext;
    return L".img";
}

static int SniffImageKind(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return 0;
    unsigned char b[12]{};
    DWORD rd = 0;
    ReadFile(f, b, 12, &rd, nullptr);
    CloseHandle(f);
    if (rd >= 8 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') return 1;
    if (rd >= 3 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF) return 2;
    if (rd >= 6 && b[0] == 'G' && b[1] == 'I' && b[2] == 'F') return 3;
    if (rd >= 12 && b[0] == 'R' && b[1] == 'I' && b[2] == 'F' && b[3] == 'F'
        && b[8] == 'W' && b[9] == 'E' && b[10] == 'B' && b[11] == 'P') return 4;
    return 0;
}

static std::wstring SwapUrlExt(const std::wstring& url, const std::wstring& ext) {
    auto q = url.find(L'?');
    auto p = q == std::wstring::npos ? url : url.substr(0, q);
    auto d = p.find_last_of(L'.');
    if (d == std::wstring::npos || d + 1 >= p.size()) return {};
    auto out = p.substr(0, d) + ext;
    if (q != std::wstring::npos) out += url.substr(q);
    return out;
}

std::vector<NewsItem> LoadNews(const LaunchConfig& cfg) {
    auto cache = JoinPath(JoinPath(ZisHome(), L"runtime"), L"news.json");
    std::vector<NewsItem> items;
    if (!cfg.newsUrl.empty()) {
        if (DownloadFile(cfg.newsUrl, cache, nullptr, true))
            items = LoadNewsFile(cache);
    }
    if (items.empty() && FileExists(cache)) items = LoadNewsFile(cache);
    if (items.empty()) return items;
    auto media = JoinPath(JoinPath(ZisHome(), L"runtime"), L"news-media");
    EnsureDir(media);
    for (size_t i = 0; i < items.size(); i++) {
        auto url = NewsImageUrl(cfg, items[i].image);
        if (url.empty()) continue;
        auto prefix = JoinPath(media, L"n" + std::to_wstring((int)i));
        auto dest = prefix + GuessExt(url);
        if (!DownloadFile(url, dest, nullptr, true)) continue;
        int kind = SniffImageKind(dest);
        if (kind == 4) {
            const wchar_t* alts[] = { L".jpg", L".jpeg", L".png" };
            bool swapped = false;
            for (auto ext : alts) {
                auto altUrl = SwapUrlExt(url, ext);
                if (altUrl.empty()) continue;
                auto altDest = prefix + ext;
                if (!DownloadFile(altUrl, altDest, nullptr, true)) continue;
                int ak = SniffImageKind(altDest);
                if (ak == 1 || ak == 2 || ak == 3) {
                    dest = altDest;
                    swapped = true;
                    break;
                }
            }
            (void)swapped;
        }
        if (SniffImageKind(dest) != 0)
            items[i].imagePath = dest;
    }
    return items;
}

CodexData LoadCodex(const LaunchConfig& cfg) {
    CodexData d;
    auto cache = JoinPath(JoinPath(ZisHome(), L"runtime"), L"content.json");
    std::wstring url = cfg.contentUrl;
    if (url.empty()) {
        auto origin = HttpOrigin(cfg.newsUrl);
        if (!origin.empty()) url = origin + L"/content.json";
    }
    if (!url.empty()) DownloadFile(url, cache, nullptr, true);
    auto j = ReadAll(cache);
    if (j.empty()) return d;
    auto obj = JsonChild(j, "codex", '{');
    if (obj.empty()) return d;
    d.eyebrow = ToWide(JsonDecoded(obj, "eyebrow"));
    d.title = ToWide(JsonDecoded(obj, "title"));
    d.lede = ToWide(JsonDecoded(obj, "lede"));
    d.rules = JsonStringList(JsonChild(obj, "rules", '['));
    return d;
}

LaunchState LoadState(const std::wstring& path) {
    LaunchState s;
    auto j = ReadAll(path);
    if (j.empty()) return s;
    auto n = JsonStr(j, "nick");
    if (!n.empty()) s.nick = ToWide(n);
    int r = JsonInt(j, "ram", 0);
    if (r > 0) s.ramMb = r;
    auto d = JsonStr(j, "gameDir");
    if (!d.empty()) s.gameDir = ToWide(d);
    return s;
}

void SaveState(const std::wstring& path, const LaunchState& st) {
    auto dir = path.substr(0, path.find_last_of(L"\\/"));
    EnsureDir(dir);
    std::ofstream f(path.c_str(), std::ios::binary);
    f << "{\n  \"nick\": \"" << ToUtf8(st.nick) << "\",\n  \"ram\": " << st.ramMb
      << ",\n  \"gameDir\": \"" << ToUtf8(st.gameDir) << "\"\n}\n";
}

static std::wstring FindJavaExe(const std::wstring& dir, int depth) {
    auto p = JoinPath(JoinPath(dir, L"bin"), L"java.exe");
    if (FileExists(p)) return p;
    if (depth <= 0 || !DirExists(dir)) return {};
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::wstring found;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == L'.') continue;
        found = FindJavaExe(JoinPath(dir, fd.cFileName), depth - 1);
        if (!found.empty()) break;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

std::wstring FindJava() {
    auto bundled = FindJavaExe(JoinPath(ZisHome(), L"java"), 4);
    if (!bundled.empty()) return bundled;
    wchar_t env[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"JAVA_HOME", env, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        auto p = JoinPath(JoinPath(env, L"bin"), L"java.exe");
        if (FileExists(p)) return p;
    }
    const wchar_t* roots[] = {
        L"C:\\Program Files\\Eclipse Adoptium",
        L"C:\\Program Files\\Java",
        L"C:\\Program Files\\Microsoft",
        L"C:\\Program Files\\Amazon Corretto",
        L"C:\\Program Files\\Zulu"
    };
    WIN32_FIND_DATAW fd;
    for (auto root : roots) {
        auto glob = std::wstring(root) + L"\\*";
        HANDLE h = FindFirstFileW(glob.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.cFileName[0] == L'.') continue;
            auto p = JoinPath(JoinPath(JoinPath(root, fd.cFileName), L"bin"), L"java.exe");
            if (FileExists(p)) { FindClose(h); return p; }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return L"java";
}

bool CopyTree(const std::wstring& src, const std::wstring& dst, LogFn log) {
    if (!DirExists(src)) {
        if (log) log(L"Нет папки модов: " + src);
        return false;
    }
    EnsureDir(dst);
    auto glob = JoinPath(src, L"*.jar");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(glob.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;
    int n = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        auto from = JoinPath(src, fd.cFileName);
        auto to = JoinPath(dst, fd.cFileName);
        if (CopyFileW(from.c_str(), to.c_str(), FALSE)) n++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (log) log(L"Синхронизировано банок: " + std::to_wstring(n));
    return true;
}

static bool DownloadFileOnce(const std::wstring& url, const std::wstring& dest, LogFn log, bool allowLoopback) {
    if (!allowLoopback && IsLoopbackHttp(url)) {
        if (log) log(L"Локальный URL запрещён: " + url);
        return false;
    }
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{}, path[2048]{};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
        if (log) log(L"Плохой URL: " + url);
        return false;
    }
    HINTERNET s = WinHttpOpen(L"DynastyOfRot/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) {
        if (log) log(L"WinHttpOpen failed");
        return false;
    }
    WinHttpSetTimeouts(s, 10000, 8000, 30000, 600000);
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    if (!c) { WinHttpCloseHandle(s); return false; }
    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET r = WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    BOOL ok = r && WinHttpSendRequest(r, L"Cache-Control: no-cache\r\nPragma: no-cache\r\n",
                                     (DWORD)-1, nullptr, 0, 0, 0)
              && WinHttpReceiveResponse(r, nullptr);
    if (!ok) {
        if (log) log(L"Сеть: не открылся " + url);
        if (r) WinHttpCloseHandle(r);
        WinHttpCloseHandle(c); WinHttpCloseHandle(s);
        return false;
    }
    DWORD status = 0, slen = sizeof(status);
    WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &slen, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        if (log) log(L"HTTP " + std::to_wstring(status) + L"  " + url);
        WinHttpCloseHandle(r); WinHttpCloseHandle(c); WinHttpCloseHandle(s);
        return false;
    }
    EnsureDir(ParentOf(dest));
    HANDLE f = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        WinHttpCloseHandle(r); WinHttpCloseHandle(c); WinHttpCloseHandle(s);
        return false;
    }
    DWORD avail = 0, rd = 0, wr = 0;
    char buf[262144];
    ULONGLONG total = 0;
    ULONGLONG lastLog = 0;
    bool wrote = true;
    while (WinHttpQueryDataAvailable(r, &avail) && avail) {
        DWORD chunk = avail > sizeof(buf) ? sizeof(buf) : avail;
        if (!WinHttpReadData(r, buf, chunk, &rd) || !rd) { wrote = false; break; }
        DWORD wtot = 0;
        while (wtot < rd) {
            if (!WriteFile(f, buf + wtot, rd - wtot, &wr, nullptr) || !wr) { wrote = false; break; }
            wtot += wr;
        }
        if (!wrote) break;
        total += rd;
        if (log && total - lastLog >= 32ull * 1024ull * 1024ull) {
            lastLog = total;
            log(L"Скачивание… " + std::to_wstring(total / (1024 * 1024)) + L" МБ  " + FileNameOf(dest));
        }
    }
    CloseHandle(f);
    WinHttpCloseHandle(r); WinHttpCloseHandle(c); WinHttpCloseHandle(s);
    if (!wrote || !FileExists(dest)) {
        DeleteFileW(dest.c_str());
        return false;
    }
    if (log) log(L"Скачано: " + FileNameOf(dest) + L"  " + std::to_wstring(total / 1024) + L" КБ");
    return true;
}

bool DownloadFile(const std::wstring& url, const std::wstring& dest, LogFn log, bool allowLoopback) {
    if (DownloadFileOnce(url, dest, log, allowLoopback)) return true;
    if (url.size() >= 7 && _wcsnicmp(url.c_str(), L"http://", 7) == 0) {
        std::wstring https = L"https://";
        https += url.c_str() + 7;
        return DownloadFileOnce(https, dest, log, allowLoopback);
    }
    return false;
}

static bool EnsureHashed(const std::wstring& url, const std::wstring& dest, const std::string& sha, LogFn log) {
    if (HashMatches(dest, sha)) return true;
    if (FileExists(dest)) DeleteFileW(dest.c_str());
    if (log) log(L"Скачивание… " + FileNameOf(dest));
    if (!DownloadFile(url, dest, log)) return false;
    if (!sha.empty() && !HashMatches(dest, sha)) {
        if (log) log(L"Хеш не совпал: " + FileNameOf(dest));
        DeleteFileW(dest.c_str());
        return false;
    }
    return true;
}

static DWORD SpawnWait(const std::wstring& exe, const std::wstring& args, const std::wstring& cwd, LogFn log, bool wait) {
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    std::vector<wchar_t> mut(cmd.begin(), cmd.end());
    mut.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, mut.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr,
                             cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    if (!ok) {
        if (log) log(L"Не удалось запустить процесс.");
        return 1;
    }
    DWORD code = 0;
    if (wait) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (!wait && log) log(L"Процесс запущен.");
    return code;
}

static void ClearReadonlyTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.' && (!fd.cFileName[1] || (fd.cFileName[1] == L'.' && !fd.cFileName[2])))
            continue;
        auto p = JoinPath(dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ClearReadonlyTree(p);
        } else {
            DWORD a = GetFileAttributesW(p.c_str());
            if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY))
                SetFileAttributesW(p.c_str(), a & ~FILE_ATTRIBUTE_READONLY);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static void WriteRoyalVariationsToml(const std::wstring& configDir) {
    EnsureDir(configDir);
    auto rv = JoinPath(configDir, L"royalvariations.toml");
    SetFileAttributesW(rv.c_str(), FILE_ATTRIBUTE_NORMAL);
    const char* body =
        "\n[Spawning]\n"
        "\tRoyalZombieSpawnWeight = 9\n"
        "\tRoyalZombieSpawnMinCount = 1\n"
        "\tRoyalZombieSpawnMaxCount = 1\n"
        "\tRoyalSkeletonSpawnWeight = 9\n"
        "\troyalSkeletonSpawnMinCount = 1\n"
        "\troyalSkeletonSpawnMaxCount = 1\n"
        "\troyalCreeperSpawnWeight = 9\n"
        "\troyalCreeperSpawnMinCount = 1\n"
        "\troyalCreeperSpawnMaxCount = 1\n"
        "\tRoyalEndermanSpawnWeight = 5\n"
        "\tRoyalEndermanSpawnMinCount = 1\n"
        "\troyalEndermanSpawnMaxCount = 1\n"
        "\tRoyalEndermanEndSpawnChance = 5\n";
    std::ofstream f(rv.c_str(), std::ios::binary | std::ios::trunc);
    f << body;
}

static void UnlockConfigDir(const std::wstring& configDir) {
    EnsureDir(configDir);
    ClearReadonlyTree(configDir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((configDir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            auto low = ToLower(fd.cFileName);
            if (low.find(L".tmp.toml") != std::wstring::npos ||
                low.find(L".new.tmp") != std::wstring::npos ||
                (low.size() > 4 && low.compare(low.size() - 4, 4, L".tmp") == 0)) {
                auto p = JoinPath(configDir, fd.cFileName);
                SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(p.c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    WriteRoyalVariationsToml(configDir);
}

static bool ExtractZip(const std::wstring& zip, const std::wstring& dest, LogFn log) {
    EnsureDir(dest);
    wchar_t sys[MAX_PATH]{};
    GetSystemDirectoryW(sys, MAX_PATH);
    auto tar = JoinPath(sys, L"tar.exe");
    if (!FileExists(tar)) tar = L"tar.exe";
    if (log) log(L"Распаковка… " + FileNameOf(zip));
    DWORD code = SpawnWait(tar, L"-xf " + QuoteArg(zip) + L" -C " + QuoteArg(dest), dest, log, true);
    if (code != 0) {
        if (log) log(L"tar завершился с кодом " + std::to_wstring(code));
        return false;
    }
    ClearReadonlyTree(dest);
    return true;
}

struct PackItem {
    std::string kind;
    std::wstring path;
    std::string sha256;
    long long size = 0;
};

static void PushItemObj(const std::string& obj, std::vector<PackItem>& out) {
    PackItem it;
    it.kind = JsonStr(obj, "kind");
    it.path = ToWide(JsonStr(obj, "path"));
    if (it.path.empty()) it.path = ToWide(JsonStr(obj, "file"));
    it.sha256 = JsonStr(obj, "sha256");
    it.size = JsonI64(obj, "size", 0);
    if (!it.path.empty()) out.push_back(it);
}

static std::vector<PackItem> ParseItems(const std::string& json) {
    std::vector<PackItem> out;
    auto arr = json.find("\"items\"");
    if (arr != std::string::npos) {
        auto lb = json.find('[', arr);
        if (lb != std::string::npos) {
            size_t arrEnd = lb;
            if (SkipJsonValue(json, arrEnd)) {
                size_t pos = lb + 1;
                while (pos < arrEnd) {
                    SkipWs(json, pos);
                    if (pos >= arrEnd || json[pos] == ']') break;
                    if (json[pos] == ',') { pos++; continue; }
                    if (json[pos] != '{') { pos++; continue; }
                    size_t objEnd = pos;
                    if (!SkipJsonValue(json, objEnd)) break;
                    PushItemObj(json.substr(pos, objEnd - pos), out);
                    pos = objEnd;
                }
            }
        }
    }
    bool hasJava = false;
    for (const auto& it : out) {
        if (it.kind == "java") { hasJava = true; break; }
    }
    if (!hasJava) {
        auto jk = json.find("\"java\"");
        while (jk != std::string::npos) {
            size_t c = jk + 6;
            SkipWs(json, c);
            if (c < json.size() && json[c] == ':') {
                auto w = json.find("\"windows\"", c);
                if (w != std::string::npos && w < c + 400) {
                    auto brace = json.find('{', w);
                    if (brace != std::string::npos) {
                        size_t objEnd = brace;
                        if (SkipJsonValue(json, objEnd)) {
                            PackItem it;
                            auto obj = json.substr(brace, objEnd - brace);
                            it.kind = "java";
                            it.path = ToWide(JsonStr(obj, "file"));
                            if (it.path.empty()) it.path = ToWide(JsonStr(obj, "path"));
                            it.sha256 = JsonStr(obj, "sha256");
                            it.size = JsonI64(obj, "size", 0);
                            if (!it.path.empty()) out.push_back(it);
                        }
                    }
                }
                break;
            }
            jk = json.find("\"java\"", jk + 1);
        }
    }
    return out;
}

static void PurgeExtraJars(const std::wstring& dir, const std::set<std::wstring>& keep, LogFn log) {
    if (!DirExists(dir)) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.jar").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    int n = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        auto low = ToLower(fd.cFileName);
        if (!keep.count(low)) {
            if (DeleteFileW(JoinPath(dir, fd.cFileName).c_str())) n++;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (n && log) log(L"Убрано лишних модов: " + std::to_wstring(n));
}

bool RunLaunch(const LaunchState& st, const LaunchConfig& cfg, LogFn log) {
    auto home = ZisHome();
    auto cache = JoinPath(home, L"cache");
    auto game = st.gameDir.empty() ? JoinPath(home, L"game") : st.gameDir;
    auto javaHome = JoinPath(JoinPath(game, L"runtime"), L"java");
    auto runtime = JoinPath(home, L"runtime");
    auto modsDst = JoinPath(game, L"mods");
    auto launchLog = JoinPath(runtime, L"launcher.log");
    EnsureDir(home); EnsureDir(cache); EnsureDir(game); EnsureDir(javaHome);
    EnsureDir(runtime); EnsureDir(modsDst);
    {
        HANDLE wipe = CreateFileW(launchLog.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (wipe != INVALID_HANDLE_VALUE) CloseHandle(wipe);
    }
    LogFn ui = log;
    log = [ui, launchLog](const std::wstring& s) {
        AppendLaunchLog(launchLog, s);
        if (ui) ui(s);
    };

    log(L"DynastyLauncher 25  лог: " + launchLog);
    log(L"Ник: " + st.nick + L"  RAM: " + std::to_wstring(st.ramMb) + L"M");

    if (IsLoopbackHttp(cfg.indexUrl) || cfg.indexUrl.empty()) {
        log(L"index_url должен быть zis.inflexus.world, не localhost.");
        return false;
    }

    auto indexPath = JoinPath(cache, L"index.json");
    bool gotIndex = false;
    log(L"Индекс… " + cfg.indexUrl);
    gotIndex = DownloadFile(cfg.indexUrl, indexPath, log);
    if (!gotIndex) {
        log(L"Не удалось скачать index.json с zis.inflexus.world.");
        return false;
    }

    auto idx = ReadAll(indexPath);
    auto items = ParseItems(idx);
    if (items.empty()) {
        if (log) log(L"Пустой индекс сборки.");
        return false;
    }
    {
        int nj = 0;
        for (const auto& it : items) if (it.kind == "java") nj++;
        log(L"Индекс: " + std::to_wstring((int)items.size()) + L" пунктов, Java: " +
            (nj ? L"есть" : L"нет"));
    }

    std::wstring base = ToWide(JsonStr(idx, "base_url"));
    if (base.empty()) base = cfg.packBase;
    if (IsLoopbackHttp(base)) {
        log(L"base_url локальный, беру сайт: " + cfg.packBase);
        base = cfg.packBase;
    }
    if (base.empty() || IsLoopbackHttp(base)) {
        if (log) log(L"Нет site base_url в индексе.");
        return false;
    }
    log(L"Качаю с " + base);

    std::set<std::wstring> keepMods;
    auto syncOne = [&](const PackItem& it) -> bool {
        auto dest = JoinPath(cache, SlashToWin(it.path));
        auto url = JoinUrl(base, it.path);
        if (IsLoopbackHttp(url)) {
            log(L"Пропуск локального URL: " + url);
            return false;
        }
        if (it.kind == "java") dest = JoinPath(game, L"java.zip");
        if (!EnsureHashed(url, dest, it.sha256, log)) {
            log(L"Сбой синка: " + it.path);
            return false;
        }
        if (it.kind == "archive") {
            auto marker = JoinPath(cache, L"unpacked-" + FileNameOf(it.path) + L"-" + ToWide(it.sha256.substr(0, 16)));
            if (!FileExists(marker)) {
                if (!ExtractZip(dest, game, log)) return false;
                HANDLE m = CreateFileW(marker.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (m != INVALID_HANDLE_VALUE) CloseHandle(m);
            }
        } else if (it.kind == "java") {
            auto marker = JoinPath(javaHome, L".version_hash");
            auto have = ReadAll(marker);
            while (!have.empty() && (have.back() == '\n' || have.back() == '\r' || have.back() == ' '))
                have.pop_back();
            auto bundled = FindJavaExe(javaHome, 4);
            bool ready = !bundled.empty() && !it.sha256.empty() &&
                         _stricmp(have.c_str(), it.sha256.c_str()) == 0;
            if (!ready) {
                log(L"Распаковка Java в game\\runtime\\java…");
                WipeDirFiles(javaHome);
                EnsureDir(javaHome);
                if (!ExtractZip(dest, javaHome, log)) {
                    log(L"Не распаковался JRE.");
                    return false;
                }
                std::ofstream mf(marker.c_str(), std::ios::binary);
                mf << it.sha256;
                bundled = FindJavaExe(javaHome, 4);
                if (bundled.empty()) {
                    log(L"После распаковки нет java.exe.");
                    return false;
                }
                log(L"Java готова: " + bundled);
            } else {
                log(L"Java уже на месте: " + bundled);
            }
        } else if (it.kind == "file") {
            auto name = FileNameOf(it.path);
            auto low = ToLower(it.path);
            if (low.find(L"mods/") == 0 || low.find(L"mods\\") == 0) {
                keepMods.insert(ToLower(name));
                auto gdest = JoinPath(modsDst, name);
                if (!HashMatches(gdest, it.sha256)) {
                    if (!CopyFileW(dest.c_str(), gdest.c_str(), FALSE)) {
                        log(L"Не скопировался мод: " + name);
                        return false;
                    }
                }
            } else if (low.find(L"config/") == 0 || low.find(L"config\\") == 0) {
                auto gdest = JoinPath(game, SlashToWin(it.path));
                EnsureDir(ParentOf(gdest));
                if (!HashMatches(gdest, it.sha256)) {
                    if (!CopyFileW(dest.c_str(), gdest.c_str(), FALSE)) {
                        log(L"Не скопировался конфиг: " + it.path);
                        return false;
                    }
                    log(L"Конфиг: " + it.path);
                }
            } else if (name == L"servers.dat") {
            } else if (name == L"options.txt") {
                auto gdest = JoinPath(game, name);
                if (!FileExists(gdest)) CopyFileW(dest.c_str(), gdest.c_str(), FALSE);
            }
        }
        return true;
    };
    int nJava = 0;
    for (auto& it : items) {
        if (it.kind != "java") continue;
        nJava++;
        if (!syncOne(it)) return false;
    }
    if (nJava < 1) {
        log(L"В index.json нет Windows JRE. Пересоберите индекс.");
        return false;
    }
    for (auto& it : items) {
        if (it.kind == "java") continue;
        if (!syncOne(it)) return false;
    }
    PurgeExtraJars(modsDst, keepMods, log);

    auto java = FindJavaExe(javaHome, 4);
    if (java.empty() || !FileExists(java)) {
        log(L"Нет своей Java. Нужен пункт java в index.json с сайта.");
        return false;
    }
    log(L"Java: " + java);
    UnlockConfigDir(JoinPath(game, L"config"));
    ClearReadonlyTree(JoinPath(game, L"config"));
    {
        auto p = JoinPath(game, L"servers.dat");
        SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
        const unsigned char nbt[] = {
            0x0A, 0x00, 0x00,
            0x09, 0x00, 0x07, 's','e','r','v','e','r','s',
            0x0A, 0x00, 0x00, 0x00, 0x00,
            0x00
        };
        std::ofstream f(p.c_str(), std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(nbt), sizeof(nbt));
    }
    LaunchState resolved = st;
    resolved.gameDir = game;
    return LaunchClient(resolved, cfg, game, java, runtime, log);
}

bool FileSha256Hex(const std::wstring& path, std::string& hex) {
    return Sha256File(path, hex);
}

static bool CmdHas(const wchar_t* flag) {
    const wchar_t* cl = GetCommandLineW();
    return cl && wcsstr(cl, flag);
}

static std::string ParseSha256Text(const std::string& raw) {
    size_t i = 0;
    while (i < raw.size() && !isxdigit((unsigned char)raw[i])) i++;
    std::string tok;
    while (i < raw.size() && tok.size() < 64 && isxdigit((unsigned char)raw[i])) {
        tok.push_back((char)tolower(raw[i++]));
    }
    if (tok.size() != 64) return {};
    return tok;
}

static bool RelaunchSelf(const std::wstring& self, const std::wstring& dir) {
    std::wstring cmd = L"\"" + self + L"\" --updated";
    std::vector<wchar_t> mut(cmd.begin(), cmd.end());
    mut.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(self.c_str(), mut.data(), nullptr, nullptr, FALSE,
                             CREATE_UNICODE_ENVIRONMENT, nullptr,
                             dir.empty() ? nullptr : dir.c_str(), &si, &pi);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

void MaybeSelfUpdate() {
    wchar_t buf[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, buf, MAX_PATH)) return;
    std::wstring self(buf);
    std::wstring dir = LauncherDir();
    std::wstring oldp = self + L".old";
    DeleteFileW(oldp.c_str());
    if (CmdHas(L"--skip-update") || CmdHas(L"--updated")) return;
    if (FindWindowW(L"DynastyOfRotLauncher", nullptr)) return;
    if (FileExists(JoinPath(JoinPath(dir, L"src"), L"engine.cpp"))) return;

    auto cfg = LoadLiveConfig(JoinPath(JoinPath(dir, L"config"), L"live.json"));
    if (cfg.launcherUrl.empty()) return;
    if (IsLoopbackHttp(cfg.launcherUrl)) return;

    auto shaUrl = cfg.launcherUrl + L".sha256";
    auto shaPath = JoinPath(JoinPath(ZisHome(), L"runtime"), L"launcher.sha256");
    if (!DownloadFile(shaUrl, shaPath, nullptr, false)) return;
    auto want = ParseSha256Text(ReadAll(shaPath));
    if (want.empty()) return;

    std::string have;
    if (!Sha256File(self, have)) return;
    if (_stricmp(have.c_str(), want.c_str()) == 0) return;

    auto neu = self + L".new";
    DeleteFileW(neu.c_str());
    if (!DownloadFile(cfg.launcherUrl, neu, nullptr, false)) return;
    WIN32_FILE_ATTRIBUTE_DATA ad{};
    if (!GetFileAttributesExW(neu.c_str(), GetFileExInfoStandard, &ad)) {
        DeleteFileW(neu.c_str());
        return;
    }
    ULARGE_INTEGER sz;
    sz.HighPart = ad.nFileSizeHigh;
    sz.LowPart = ad.nFileSizeLow;
    if (sz.QuadPart < 400000ull || sz.QuadPart > 40ull * 1024ull * 1024ull) {
        DeleteFileW(neu.c_str());
        return;
    }
    if (!HashMatches(neu, want)) {
        DeleteFileW(neu.c_str());
        return;
    }
    if (!MoveFileExW(self.c_str(), oldp.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(neu.c_str());
        return;
    }
    if (!MoveFileExW(neu.c_str(), self.c_str(), 0)) {
        MoveFileExW(oldp.c_str(), self.c_str(), 0);
        DeleteFileW(neu.c_str());
        return;
    }
    if (!RelaunchSelf(self, dir)) {
        DeleteFileW(self.c_str());
        MoveFileExW(oldp.c_str(), self.c_str(), 0);
        return;
    }
    ExitProcess(0);
}

bool InspectPackIndex(const std::wstring& path, int& nItems, int& nJava,
                      std::wstring& javaPath, std::string& javaSha, bool& hasBracketMod,
                      std::wstring& baseUrl) {
    nItems = 0;
    nJava = 0;
    javaPath.clear();
    javaSha.clear();
    hasBracketMod = false;
    baseUrl.clear();
    auto json = ReadAll(path);
    if (json.empty()) return false;
    baseUrl = ToWide(JsonStr(json, "base_url"));
    auto items = ParseItems(json);
    nItems = (int)items.size();
    for (const auto& it : items) {
        if (it.path.find(L"[NeoForge]") != std::wstring::npos) hasBracketMod = true;
        if (it.kind == "java") {
            nJava++;
            javaPath = it.path;
            javaSha = it.sha256;
        }
    }
    return nItems > 0;
}
