#pragma once
#include <string>
#include <vector>
#include <functional>

struct NewsItem {
    std::wstring date;
    std::wstring title;
    std::wstring text;
    std::wstring image;
    std::wstring imagePath;
};

struct CodexData {
    std::wstring eyebrow;
    std::wstring title;
    std::wstring lede;
    std::vector<std::wstring> rules;
};

struct LaunchConfig {
    std::wstring host = L"zis.inflexus.world";
    int port = 25666;
    std::wstring javaArgs = L"-Xmx{RAM}m -Xms256m -Dfile.encoding=UTF-8";
    bool lockToServer = true;
    std::wstring newsUrl = L"http://zis.inflexus.world/news.json";
    std::wstring contentUrl = L"http://zis.inflexus.world/content.json";
    std::wstring indexUrl = L"http://zis.inflexus.world/download/cloud/index.json";
    std::wstring packBase = L"http://zis.inflexus.world/download/cloud/";
    std::wstring launcherUrl = L"http://zis.inflexus.world/download/DynastyLauncher.exe";
};

struct LaunchState {
    std::wstring nick;
    int ramMb = 8192;
    std::wstring gameDir;
};

using LogFn = std::function<void(const std::wstring&)>;

std::wstring LauncherDir();
std::wstring UserHome();
std::wstring ZisHome();
std::wstring JoinPath(const std::wstring& a, const std::wstring& b);
bool FileExists(const std::wstring& p);
bool DirExists(const std::wstring& p);
bool EnsureDir(const std::wstring& p);
LaunchConfig LoadLiveConfig(const std::wstring& path);
LaunchState LoadState(const std::wstring& path);
std::vector<NewsItem> LoadNewsFile(const std::wstring& path);
std::vector<NewsItem> LoadNews(const LaunchConfig& cfg);
CodexData LoadCodex(const LaunchConfig& cfg);
void SaveState(const std::wstring& path, const LaunchState& st);
std::wstring RuntimeDir();
std::wstring LauncherLogPath();
std::wstring MinecraftLogPath();
std::wstring FindJava();
bool CopyTree(const std::wstring& src, const std::wstring& dst, LogFn log);
bool DownloadFile(const std::wstring& url, const std::wstring& dest, LogFn log, bool allowLoopback = false);
bool RunLaunch(const LaunchState& st, const LaunchConfig& cfg, LogFn log);
void StopGameClient();
bool GameClientRunning();
unsigned long GameClientExitCode();
void ReleaseGameHandles();
void MarkGameSessionStart();
bool HasFreshCrashReport(const std::wstring& gameDir);
bool SpawnTrackedProcess(const std::wstring& exe, const std::wstring& args);
bool InspectPackIndex(const std::wstring& path, int& nItems, int& nJava,
                      std::wstring& javaPath, std::string& javaSha, bool& hasBracketMod,
                      std::wstring& baseUrl);
bool FileSha256Hex(const std::wstring& path, std::string& hex);
void MaybeSelfUpdate();
