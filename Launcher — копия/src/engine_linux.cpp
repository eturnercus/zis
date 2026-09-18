#include "engine.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <curl/curl.h>
#include <openssl/sha.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <pwd.h>
#include <codecvt>
#include <locale>
#include <vector>

namespace fs = std::filesystem;

// Utility to convert wstring to string (UTF-8)
static std::string WtoS(const std::wstring& ws) {
    if (ws.empty()) return "";
    try {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
        return conv.to_bytes(ws);
    } catch (...) { return ""; }
}

// Utility to convert string to wstring
static std::wstring StoW(const std::string& s) {
    if (s.empty()) return L"";
    try {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
        return conv.from_bytes(s);
    } catch (...) { return L""; }
}

std::wstring LauncherDir() {
    fs::path current = fs::current_path();
    if (current.filename() == "build") return StoW(current.parent_path().string());
    return StoW(current.string());
}

std::wstring UserHome() {
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (!pw) return L"";
        home = pw->pw_dir;
    }
    return StoW(home);
}

std::wstring ZisHome() {
    return JoinPath(UserHome(), L".zisLauncher");
}

std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    fs::path p1 = WtoS(a);
    fs::path p2 = WtoS(b);
    return StoW((p1 / p2).string());
}

bool FileExists(const std::wstring& p) {
    return fs::exists(WtoS(p)) && !fs::is_directory(WtoS(p));
}

bool DirExists(const std::wstring& p) {
    return fs::exists(WtoS(p)) && fs::is_directory(WtoS(p));
}

bool EnsureDir(const std::wstring& p) {
    try {
        return fs::create_directories(WtoS(p)) || DirExists(p);
    } catch (...) { return false; }
}

// --- Basic JSON Parser (Very primitive, for this specific format) ---
static std::string GetJsonValue(const std::string& json, const std::string& key) {
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    pos = json.find(":", pos);
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\"')) pos++;
    size_t end = json.find_first_of("\"}", pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

static std::string UnescapeJson(const std::string& s) {
    std::string res;
    res.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[i + 1]) {
                case 'n': res += '\n'; i++; break;
                case 'r': res += '\r'; i++; break;
                case 't': res += '\t'; i++; break;
                case '\"': res += '\"'; i++; break;
                case '\\': res += '\\'; i++; break;
                default: res += s[i]; break;
            }
        } else {
            res += s[i];
        }
    }
    return res;
}

static std::vector<std::string> GetJsonArray(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return result;
    pos = json.find("[", pos);
    if (pos == std::string::npos) return result;
    size_t end = json.find("]", pos);
    if (end == std::string::npos) return result;

    std::string arrStr = json.substr(pos + 1, end - pos - 1);
    size_t start = 0;
    while ((start = arrStr.find("\"", start)) != std::string::npos) {
        size_t endVal = arrStr.find("\"", start + 1);
        if (endVal == std::string::npos) break;
        result.push_back(arrStr.substr(start + 1, endVal - start - 1));
        start = endVal + 1;
    }
    return result;
}

static std::vector<std::string> GetJsonObjectArray(const std::string& json, const std::string& key, const std::string& valKey) {
    std::vector<std::string> result;
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return result;
    pos = json.find("[", pos);
    if (pos == std::string::npos) return result;
    size_t endArr = json.find("]", pos);
    if (endArr == std::string::npos) return result;

    std::string arrStr = json.substr(pos + 1, endArr - pos - 1);
    size_t objPos = 0;
    while ((objPos = arrStr.find("{", objPos)) != std::string::npos) {
        size_t objEnd = arrStr.find("}", objPos);
        if (objEnd == std::string::npos) break;
        std::string objStr = arrStr.substr(objPos, objEnd - objPos + 1);
        result.push_back(GetJsonValue(objStr, valKey));
        objPos = objEnd + 1;
    }
    return result;
}

LaunchConfig LoadLiveConfig(const std::wstring& path) {
    LaunchConfig cfg;
    std::ifstream f(WtoS(path));
    if (!f) return cfg;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    std::string host = GetJsonValue(content, "host");
    if (!host.empty()) cfg.host = StoW(host);
    // ... other fields ...
    return cfg;
}

LaunchState LoadState(const std::wstring& path) {
    LaunchState st;
    std::ifstream f(WtoS(path));
    if (!f) return st;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    std::string nick = GetJsonValue(content, "nick");
    if (!nick.empty()) st.nick = StoW(nick);
    return st;
}

void SaveState(const std::wstring& path, const LaunchState& st) {
    std::ofstream f(WtoS(path));
    if (!f) return;
    f << "{\"nick\":\"" << WtoS(st.nick) << "\",\"ramMb\":" << st.ramMb << "}";
}

std::wstring RuntimeDir() {
    return JoinPath(ZisHome(), L"runtime");
}

std::wstring LauncherLogPath() {
    return JoinPath(RuntimeDir(), L"launcher.log");
}

std::wstring MinecraftLogPath() {
    return JoinPath(ZisHome(), L"game/logs/latest.log");
}

// --- Networking (CURL) ---
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static std::string FetchUrl(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "CURL error: " << curl_easy_strerror(res) << " for URL: " << url << std::endl;
    } else {
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        std::cout << "Fetched " << url << " - Status: " << response_code << " Size: " << response.size() << " bytes" << std::endl;
    }
    curl_easy_cleanup(curl);
    return (res == CURLE_OK) ? response : "";
}

bool DownloadFile(const std::wstring& url, const std::wstring& dest, LogFn log, bool allowLoopback) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    FILE* fp = fopen(WtoS(dest).c_str(), "wb");
    if (!fp) { curl_easy_cleanup(curl); return false; }
    curl_easy_setopt(curl, CURLOPT_URL, WtoS(url).c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    curl_easy_cleanup(curl);
    return res == CURLE_OK;
}

// --- Crypto (OpenSSL) ---
bool FileSha256Hex(const std::wstring& path, std::string& hex) {
    std::ifstream f(WtoS(path), std::ios::binary);
    if (!f) return false;
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    char buf[4096];
    while (f.read(buf, sizeof(buf))) SHA256_Update(&sha256, buf, f.gcount());
    SHA256_Update(&sha256, buf, f.gcount());
    SHA256_Final(hash, &sha256);
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << (int)hash[i];
    }
    hex = ss.str();
    return true;
}

// --- Process Management ---
static pid_t gGamePid = 0;

bool SpawnTrackedProcess(const std::wstring& exe, const std::wstring& args) {
    std::string exeS = WtoS(exe);
    std::string argsS = WtoS(args);
    std::vector<std::string> argList;
    argList.push_back(exeS);
    std::stringstream ss(argsS);
    std::string arg;
    while (ss >> arg) argList.push_back(arg);
    std::vector<char*> c_args;
    for (auto& s : argList) c_args.push_back(&s[0]);
    c_args.push_back(nullptr);

    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        execvp(c_args[0], c_args.data());
        exit(1);
    } else if (pid > 0) {
        gGamePid = pid;
        return true;
    }
    return false;
}

void StopGameClient() {
    if (gGamePid > 0) {
        kill(-gGamePid, SIGTERM);
        gGamePid = 0;
    }
}

bool GameClientRunning() {
    if (gGamePid <= 0) return false;
    int status;
    pid_t res = waitpid(gGamePid, &status, WNOHANG);
    if (res == 0) return true;
    gGamePid = 0;
    return false;
}

unsigned long GameClientExitCode() { return 0; }
void ReleaseGameHandles() { gGamePid = 0; }
void MarkGameSessionStart() {}

// --- Content Loading ---
std::vector<NewsItem> LoadNews(const LaunchConfig& cfg) {
    std::vector<NewsItem> news;
    std::string json = FetchUrl(WtoS(cfg.newsUrl));
    if (json.empty()) return news;

    // Very basic parser: looking for objects in "feed" array
    size_t pos = json.find("\"feed\"");
    if (pos == std::string::npos) return news;

    size_t startArr = json.find("[", pos);
    size_t endArr = json.rfind("]");
    if (startArr == std::string::npos || endArr == std::string::npos) return news;

    std::string feed = json.substr(startArr + 1, endArr - startArr - 1);
    size_t objPos = 0;
    while ((objPos = feed.find("{", objPos)) != std::string::npos) {
        size_t objEnd = feed.find("}", objPos);
        if (objEnd == std::string::npos) break;
        std::string itemJson = feed.substr(objPos, objEnd - objPos + 1);

        NewsItem item;
        item.date = StoW(GetJsonValue(itemJson, "date"));
        item.title = StoW(GetJsonValue(itemJson, "title"));
        item.text = StoW(GetJsonValue(itemJson, "text"));
        item.image = StoW(GetJsonValue(itemJson, "image"));
        news.push_back(item);
        objPos = objEnd + 1;
    }
    return news;
}

CodexData LoadCodex(const LaunchConfig& cfg) {
    CodexData codex;
    std::string json = FetchUrl(WtoS(cfg.contentUrl));
    if (json.empty()) return codex;

    // Main Page
    std::vector<std::string> ticker = GetJsonArray(json, "ticker");
    for (const auto& s : ticker) codex.ticker.push_back(StoW(UnescapeJson(s)));

    codex.hero_eyebrow = StoW(UnescapeJson(GetJsonValue(json, "hero_eyebrow")));
    codex.hero_title = StoW(UnescapeJson(GetJsonValue(json, "hero_title")));
    codex.hero_media = StoW(UnescapeJson(GetJsonValue(json, "hero_media")));
    codex.hero_kind = StoW(UnescapeJson(GetJsonValue(json, "hero_kind")));
    codex.hero_caption = StoW(UnescapeJson(GetJsonValue(json, "hero_caption")));
    codex.lede = StoW(UnescapeJson(GetJsonValue(json, "lede")));

    std::vector<std::string> cardTitles = GetJsonObjectArray(json, "cards", "title");
    std::vector<std::string> cardTexts = GetJsonObjectArray(json, "cards", "text");
    for (size_t i = 0; i < cardTitles.size(); i++) {
        std::string txt = (i < cardTexts.size()) ? cardTexts[i] : "";
        codex.cards.push_back({StoW(UnescapeJson(cardTitles[i])), StoW(UnescapeJson(txt))});
    }

    // Codex Section
    size_t codexPos = json.find("\"codex\"");
    if (codexPos != std::string::npos) {
        size_t startObj = json.find("{", codexPos);
        size_t endObj = json.find("}", startObj); // This is too simple, but content.json usually has it simple
        // Better: find the matching closing brace. But for this format:
        size_t endArr = json.find("]", startObj); // Look for the end of the rules array inside codex
        size_t actualEnd = json.find("}", endArr);
        if (startObj != std::string::npos && actualEnd != std::string::npos) {
            std::string codexJson = json.substr(startObj, actualEnd - startObj + 1);
            codex.eyebrow = StoW(GetJsonValue(codexJson, "eyebrow"));
            codex.title = StoW(GetJsonValue(codexJson, "title"));
            codex.codex_lede = StoW(GetJsonValue(codexJson, "lede"));

            size_t rulesPos = codexJson.find("\"rules\"");
            if (rulesPos != std::string::npos) {
                size_t startArr = codexJson.find("[", rulesPos);
                size_t endArr_ = codexJson.find("]", startArr);
                if (startArr != std::string::npos && endArr_ != std::string::npos) {
                    std::string rulesStr = codexJson.substr(startArr + 1, endArr_ - startArr - 1);
                    size_t rulePos = 0;
                    while ((rulePos = rulesStr.find("\"", rulePos)) != std::string::npos) {
                        size_t nextQuote = rulesStr.find("\"", rulePos + 1);
                        if (nextQuote == std::string::npos) break;
                        codex.rules.push_back(StoW(rulesStr.substr(rulePos + 1, nextQuote - rulePos - 1)));
                        rulePos = nextQuote + 1;
                    }
                }
            }
        }
    }
    return codex;
}

std::vector<NewsItem> LoadNewsFile(const std::wstring& path) { return {}; }
std::wstring FindJava() { return L"java"; }
bool CopyTree(const std::wstring& src, const std::wstring& dst, LogFn log) { return true; }
bool RunLaunch(const LaunchState& st, const LaunchConfig& cfg, LogFn log) {
    return SpawnTrackedProcess(L"java", L"-jar bootstrap.jar");
}
bool InspectPackIndex(const std::wstring& path, int& nItems, int& nJava, std::wstring& javaPath, std::string& javaSha, bool& hasBracketMod, std::wstring& baseUrl) {
    return false;
}
void MaybeSelfUpdate() {}
