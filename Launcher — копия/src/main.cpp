#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <initguid.h>
#include <wincodec.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <string>
#include <thread>
#include <vector>
#include <cstring>
#include "engine.hpp"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif

static const COLORREF COL_VOID = RGB(7, 4, 12);
static const COLORREF COL_PURPLE = RGB(192, 132, 252);
static const COLORREF COL_FOAM = RGB(243, 232, 255);
static const COLORREF COL_INK = RGB(10, 6, 18);
static const COLORREF COL_SCAN = RGB(168, 108, 228);
static const COLORREF COL_SLOT = RGB(18, 10, 28);
static const COLORREF COL_MUTED = RGB(168, 138, 196);
static const COLORREF COL_LINE = RGB(138, 78, 198);

static const int kRamPresets[] = { 5, 8, 12, 16, 24, 32, 48, 64 };
static const int kRamPresetCount = 8;
static const int kNickMin = 4;
static const int kNickMax = 19;
static const int ID_NICK = 101;
static const wchar_t kTick[] =
    L"ОТКРЫТИЕ   ✠   ТЕСТОВАЯ BETA   ✠   DYNASTY OF ROT   ✠   ДОБРО ПОЖАЛОВАТЬ   ✠   ";

static HWND gWnd, gNick;
static WNDPROC gNickPrev = nullptr;
static HFONT gFontTick, gFontNav, gFontBig, gFontBody, gFontNick;
static int gTab = 0;
static LaunchConfig gCfg;
static LaunchState gSt;
static bool gBusy = false;
static bool gGameLive = false;
static bool gCrashOpen = false;
static bool gLaunchFailed = false;
static bool gDragRam = false;
static bool gPlayHot = false;
static unsigned long gCrashCode = 0;
static ULONGLONG gGameStartedAt = 0;
static ULONGLONG gMinimizeAt = 0;
static int gDeadHits = 0;
static int gTickOff = 0;
static int gTickPeriod = 900;
static int gMaxGb = 16;
static int gGifPulse = 0;
static std::wstring gStatus;
static std::vector<NewsItem> gNews;
static CodexData gCodex;
static ULONGLONG gNewsAt = 0;
static ULONG_PTR gGdiToken = 0;
static Gdiplus::Image* gCrest = nullptr;
static HICON gAppIcon = nullptr;
struct NewsPic {
    Gdiplus::Image* img = nullptr;
    GUID dim{};
    UINT frames = 1;
    UINT frame = 0;
};
static std::vector<NewsPic> gNewsPic;

struct Hits {
    RECT tabPlay{}, tabSet{}, tabCodex{}, play{}, ramBar{}, nickLbl{}, nickBox{}, status{};
    RECT ramChip[8]{};
    int ramChipGb[8]{};
    int nChips = 0;
    RECT logsBtn{};
    RECT crashBox{}, crashCopy{}, crashOk{};
} gHit;

static std::wstring StatePath() { return JoinPath(ZisHome(), L"state.json"); }
static std::wstring LivePath() { return JoinPath(JoinPath(LauncherDir(), L"config"), L"live.json"); }

static bool IsNickChar(wchar_t c) {
    return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z')
        || (c >= L'0' && c <= L'9') || c == L'_' || c == L'-';
}

static std::wstring FilterNick(const std::wstring& s) {
    std::wstring o;
    o.reserve(s.size());
    for (wchar_t c : s) {
        if (IsNickChar(c)) o += c;
        if ((int)o.size() >= kNickMax) break;
    }
    return o;
}

static int TextPx(HFONT f, const wchar_t* s, int n) {
    if (!f || !s || n <= 0) return 0;
    HDC dc = GetDC(nullptr);
    if (!dc) return 0;
    HGDIOBJ old = SelectObject(dc, f);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s, n, &sz);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    return sz.cx;
}

static Gdiplus::Image* LoadImgWic(const std::wstring& path) {
    IWICImagingFactory* fac = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IWICImagingFactory, (void**)&fac)) || !fac)
        return nullptr;
    IWICBitmapDecoder* dec = nullptr;
    Gdiplus::Image* out = nullptr;
    if (SUCCEEDED(fac->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                 WICDecodeMetadataCacheOnLoad, &dec)) && dec) {
        IWICBitmapFrameDecode* frame = nullptr;
        if (SUCCEEDED(dec->GetFrame(0, &frame)) && frame) {
            IWICFormatConverter* conv = nullptr;
            if (SUCCEEDED(fac->CreateFormatConverter(&conv)) && conv) {
                if (SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                    UINT w = 0, h = 0;
                    conv->GetSize(&w, &h);
                    if (w && h && w < 8000 && h < 8000) {
                        UINT stride = w * 4;
                        std::vector<BYTE> bits((size_t)stride * h);
                        if (SUCCEEDED(conv->CopyPixels(nullptr, stride, (UINT)bits.size(), bits.data()))) {
                            auto* bmp = new Gdiplus::Bitmap((INT)w, (INT)h, PixelFormat32bppPARGB);
                            if (bmp->GetLastStatus() == Gdiplus::Ok) {
                                Gdiplus::BitmapData bd{};
                                Gdiplus::Rect rc(0, 0, (INT)w, (INT)h);
                                if (bmp->LockBits(&rc, Gdiplus::ImageLockModeWrite,
                                                  PixelFormat32bppPARGB, &bd) == Gdiplus::Ok && bd.Scan0) {
                                    BYTE* dst = (BYTE*)bd.Scan0;
                                    INT ds = bd.Stride;
                                    for (UINT y = 0; y < h; y++) {
                                        const BYTE* srcp = bits.data() + (size_t)y * stride;
                                        BYTE* row = ds >= 0 ? dst + (INT)y * ds
                                                            : dst + (INT)(h - 1 - y) * (-ds);
                                        memcpy(row, srcp, stride);
                                    }
                                    bmp->UnlockBits(&bd);
                                    out = bmp;
                                    bmp = nullptr;
                                }
                            }
                            delete bmp;
                        }
                    }
                }
                conv->Release();
            }
            frame->Release();
        }
        dec->Release();
    }
    fac->Release();
    if (out && out->GetLastStatus() != Gdiplus::Ok) { delete out; out = nullptr; }
    return out;
}

static bool FileIsWebp(const std::wstring& path) {
    auto low = path;
    for (auto& c : low) if (c >= L'A' && c <= L'Z') c = (wchar_t)(c + 32);
    if (low.size() >= 5 && low.rfind(L".webp") == low.size() - 5) return true;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    unsigned char b[12]{};
    DWORD rd = 0;
    ReadFile(f, b, 12, &rd, nullptr);
    CloseHandle(f);
    return rd >= 12 && b[0] == 'R' && b[1] == 'I' && b[2] == 'F' && b[3] == 'F'
        && b[8] == 'W' && b[9] == 'E' && b[10] == 'B' && b[11] == 'P';
}

static bool ImageUsable(Gdiplus::Image* img) {
    if (!img || img->GetLastStatus() != Gdiplus::Ok) return false;
    UINT w = img->GetWidth();
    UINT h = img->GetHeight();
    return w >= 8 && h >= 8 && w < 8000 && h < 8000;
}

static Gdiplus::Image* LoadImg(const std::wstring& path) {
    if (!FileExists(path)) return nullptr;
    if (FileIsWebp(path)) {
        auto* wic = LoadImgWic(path);
        if (ImageUsable(wic)) return wic;
        delete wic;
        return nullptr;
    }
    auto* img = new Gdiplus::Image(path.c_str());
    if (ImageUsable(img)) return img;
    delete img;
    auto* wic = LoadImgWic(path);
    if (ImageUsable(wic)) return wic;
    delete wic;
    return nullptr;
}

static void ClearNewsPics() {
    for (auto& p : gNewsPic) { delete p.img; p.img = nullptr; }
    gNewsPic.clear();
}

static void LoadNewsPics() {
    ClearNewsPics();
    gNewsPic.resize(gNews.size());
    for (size_t i = 0; i < gNews.size(); i++) {
        if (gNews[i].imagePath.empty()) continue;
        auto* img = LoadImg(gNews[i].imagePath);
        if (!img) continue;
        gNewsPic[i].img = img;
        gNewsPic[i].frames = 1;
        if (img->GetFrameDimensionsCount() > 0) {
            img->GetFrameDimensionsList(&gNewsPic[i].dim, 1);
            UINT n = img->GetFrameCount(&gNewsPic[i].dim);
            if (n > 0) gNewsPic[i].frames = n;
        }
    }
}

static void RefreshNews(bool force) {
    ULONGLONG now = GetTickCount64();
    if (!force && gNewsAt && now - gNewsAt < 4000) return;
    gNewsAt = now;
    gNews = LoadNews(gCfg);
    LoadNewsPics();
    gCodex = LoadCodex(gCfg);
    if (gWnd) InvalidateRect(gWnd, nullptr, FALSE);
}

static void LoadArt() {
    delete gCrest; gCrest = nullptr;
    auto assets = JoinPath(LauncherDir(), L"assets");
    gCrest = LoadImg(JoinPath(assets, L"crest.png"));
}

static void DarkCaption(HWND h) {
    BOOL on = TRUE;
    DwmSetWindowAttribute(h, DWMWA_USE_IMMERSIVE_DARK_MODE, &on, sizeof(on));
    DwmSetWindowAttribute(h, 19, &on, sizeof(on));
    COLORREF cap = COL_VOID;
    DwmSetWindowAttribute(h, DWMWA_CAPTION_COLOR, &cap, sizeof(cap));
    DwmSetWindowAttribute(h, DWMWA_BORDER_COLOR, &cap, sizeof(cap));
    COLORREF txt = COL_FOAM;
    DwmSetWindowAttribute(h, DWMWA_TEXT_COLOR, &txt, sizeof(txt));
}

static int DetectMaxGb() {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return 16;
    int cap = (int)(ms.ullTotalPhys / (1024ull * 1024ull * 1024ull));
    if (cap < 5) cap = 5;
    if (cap > 64) cap = 64;
    return cap;
}
static int ClampGb(int gb) {
    if (gb < 5) gb = 5;
    if (gb > gMaxGb) gb = gMaxGb;
    return gb;
}
static int RamGb() {
    int gb = gSt.ramMb / 1024;
    if (gSt.ramMb % 1024 >= 512) gb++;
    return ClampGb(gb < 1 ? 8 : gb);
}
static void ApplyRamGb(int gb) {
    if (gBusy || gGameLive) return;
    gSt.ramMb = ClampGb(gb) * 1024;
    SaveState(StatePath(), gSt);
    InvalidateRect(gWnd, nullptr, FALSE);
}

static HFONT MakeFont(int px, int weight, const wchar_t* face) {
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_ROMAN, face);
}

static void Fill(HDC dc, RECT r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}
static void Frame(HDC dc, RECT r, COLORREF c) {
    HPEN p = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ op = SelectObject(dc, p);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, r.left, r.top, r.right, r.bottom);
    SelectObject(dc, op);
    DeleteObject(p);
}
static void TextAt(HDC dc, RECT r, const wchar_t* s, COLORREF c, HFONT f, UINT fmt) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    SelectObject(dc, f);
    DrawTextW(dc, s, -1, &r, fmt);
}

static void Layout(int W, int H) {
    gHit = {};
    gHit.tabPlay = { W - 444, 44, W - 316, 84 };
    gHit.tabSet = { W - 304, 44, W - 164, 84 };
    gHit.tabCodex = { W - 152, 44, W - 24, 84 };
    const int boxH = 52;
    const int boxY = H - 108 + (108 - boxH) / 2;
    gHit.play = { W - 208, boxY, W - 28, boxY + boxH };
    int lblW = TextPx(gFontNick, L"ник", 3) + 14;
    if (lblW < 48) lblW = 48;
    gHit.nickLbl = { 28, boxY, 28 + lblW, boxY + boxH };
    // Typical nick glyphs (digits / lowercase), not 'M' — that made the line ~2× too wide.
    wchar_t sample[kNickMax + 1];
    for (int i = 0; i < kNickMax; i++) sample[i] = L'x';
    sample[kNickMax] = 0;
    int fieldW = TextPx(gFontNick, sample, kNickMax) + 10;
    int nickL = gHit.nickLbl.right + 10;
    int nickR = nickL + fieldW;
    int cap = gHit.play.left - 20;
    if (nickR > cap) nickR = cap;
    gHit.nickBox = { nickL, boxY, nickR, boxY + boxH };
    gHit.status = { gHit.nickBox.right + 16, boxY, gHit.play.left - 16, boxY + boxH };

    int ramY = 210;
    int x = 40;
    gHit.nChips = 0;
    for (int i = 0; i < kRamPresetCount; i++) {
        if (kRamPresets[i] > gMaxGb) continue;
        if (x + 76 > W - 40) {
            x = 40;
            ramY += 48;
        }
        gHit.ramChipGb[gHit.nChips] = kRamPresets[i];
        gHit.ramChip[gHit.nChips++] = { x, ramY, x + 76, ramY + 38 };
        x += 86;
    }
    int barR = W - 40;
    if (barR > 720) barR = 720;
    gHit.ramBar = { 40, ramY + 52, barR, ramY + 76 };
    int logsY = gHit.ramBar.bottom + 128;
    gHit.logsBtn = { 40, logsY, 220, logsY + 44 };

    int cx = W / 2, cy = H / 2;
    gHit.crashBox = { cx - 250, cy - 118, cx + 250, cy + 118 };
    gHit.crashCopy = { gHit.crashBox.left + 24, gHit.crashBox.bottom - 58, cx - 10, gHit.crashBox.bottom - 18 };
    gHit.crashOk = { cx + 10, gHit.crashBox.bottom - 58, gHit.crashBox.right - 24, gHit.crashBox.bottom - 18 };
}

static void PlaceNick() {
    if (!gNick) return;
    RECT r = gHit.nickBox;
    MoveWindow(gNick, r.left + 2, r.top + 12, (r.right - r.left) - 4, (r.bottom - r.top) - 22, TRUE);
}

static bool SessionLocked() { return gBusy || gGameLive; }

static void SetNickLocked(bool on) {
    if (!gNick) return;
    // Never ES_READONLY / EnableWindow: Windows then paints a gray-white box.
    if (on && GetFocus() == gNick) {
        HideCaret(gNick);
        SetFocus(gWnd);
    }
    InvalidateRect(gNick, nullptr, TRUE);
}

static HBRUSH NickBrush() {
    static HBRUSH br = CreateSolidBrush(COL_VOID);
    return br;
}

static LRESULT ColorNick(HDC dc) {
    SetTextColor(dc, SessionLocked() ? COL_MUTED : COL_FOAM);
    SetBkColor(dc, COL_VOID);
    SetBkMode(dc, OPAQUE);
    return (LRESULT)NickBrush();
}

static std::wstring ReadTextTail(const std::wstring& path, size_t maxTail) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    size_t n = sz.QuadPart > 0 ? (size_t)sz.QuadPart : 0;
    if (maxTail && n > maxTail) {
        LARGE_INTEGER off;
        off.QuadPart = (LONGLONG)(n - maxTail);
        SetFilePointerEx(f, off, nullptr, FILE_BEGIN);
        n = maxTail;
    }
    std::string raw(n, 0);
    DWORD rd = 0;
    ReadFile(f, raw.data(), (DWORD)n, &rd, nullptr);
    CloseHandle(f);
    raw.resize(rd);
    if (raw.empty()) return {};
    int wn = MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), nullptr, 0);
    std::wstring w(wn, 0);
    MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), w.data(), wn);
    return w;
}

static bool CopyTextClip(const std::wstring& s) {
    if (!OpenClipboard(gWnd)) return false;
    EmptyClipboard();
    size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) { CloseClipboard(); return false; }
    void* p = GlobalLock(mem);
    memcpy(p, s.c_str(), bytes);
    GlobalUnlock(mem);
    SetClipboardData(CF_UNICODETEXT, mem);
    CloseClipboard();
    return true;
}

static void CopyCrashLogs() {
    std::wstring all = L"=== launcher.log ===\r\n";
    all += ReadTextTail(LauncherLogPath(), 0);
    all += L"\r\n=== minecraft.log (хвост) ===\r\n";
    all += ReadTextTail(MinecraftLogPath(), 48000);
    if (CopyTextClip(all))
        gStatus = L"Логи скопированы. Отправьте их разработчику.";
    else
        gStatus = L"Не удалось скопировать логи.";
}

static void OpenLogsFolder() {
    auto dir = RuntimeDir();
    EnsureDir(dir);
    ShellExecuteW(gWnd, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static const wchar_t* PlayCaption() {
    if (gBusy) {
        static const wchar_t* dots[] = { L"ЗАГРУЗКА", L"ЗАГРУЗКА.", L"ЗАГРУЗКА..", L"ЗАГРУЗКА..." };
        return dots[(gGifPulse / 8) % 4];
    }
    if (gGameLive) return L"В ИГРЕ";
    return L"ИГРАТЬ";
}

static int ChipForPoint(POINT p) {
    for (int i = 0; i < gHit.nChips; i++) {
        if (PtInRect(&gHit.ramChip[i], p)) return gHit.ramChipGb[i];
    }
    return 0;
}

static bool HitRamBar(POINT p) {
    RECT r = gHit.ramBar;
    r.top -= 10;
    r.bottom += 10;
    return PtInRect(&r, p) ? true : false;
}

static void RamFromBar(int x) {
    int w = gHit.ramBar.right - gHit.ramBar.left;
    if (w < 1) return;
    int t = x - gHit.ramBar.left;
    if (t < 0) t = 0;
    if (t > w) t = w;
    int gb = 5 + (int)((gMaxGb - 5) * (t / (double)w) + 0.5);
    ApplyRamGb(gb);
}

static void OnGameStopped(HWND h) {
    gCrashCode = GameClientExitCode();
    ReleaseGameHandles();
    gGameLive = false;
    gDeadHits = 0;
    gMinimizeAt = 0;
    SetNickLocked(false);
    ShowWindow(h, SW_RESTORE);
    SetForegroundWindow(h);
    gLaunchFailed = false;
    gCrashOpen = gCrashCode != 0;
    gStatus.clear();
    InvalidateRect(h, nullptr, FALSE);
}

static void DoPlay() {
    if (SessionLocked() || gCrashOpen) return;
    wchar_t nick[32]{};
    GetWindowTextW(gNick, nick, 32);
    std::wstring n = FilterNick(nick);
    if (n.size() < (size_t)kNickMin || n.size() > (size_t)kNickMax) {
        gStatus = L"Ник: 4–19 символов, латиница, цифры, _ и -.";
        SetWindowTextW(gNick, n.c_str());
        InvalidateRect(gWnd, nullptr, FALSE);
        return;
    }
    gSt.nick = n;
    SetWindowTextW(gNick, n.c_str());
    gSt.ramMb = RamGb() * 1024;
    SaveState(StatePath(), gSt);
    gBusy = true;
    gGameLive = false;
    gLaunchFailed = false;
    gMinimizeAt = 0;
    gDeadHits = 0;
    SetNickLocked(true);
    gStatus = L"Качаем сборку…";
    InvalidateRect(gWnd, nullptr, FALSE);
    auto st = gSt;
    auto cfg = gCfg;
    std::thread([st, cfg]() {
        bool ok = RunLaunch(st, cfg, [](const std::wstring& s) {
            auto* heap = new std::wstring(s);
            PostMessageW(gWnd, WM_APP + 1, 0, (LPARAM)heap);
        });
        PostMessageW(gWnd, WM_APP + 2, ok ? 1 : 0, 0);
    }).detach();
}

static LRESULT CALLBACK NickProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (SessionLocked()) {
        if (m == WM_CHAR || m == WM_IME_CHAR || m == WM_PASTE) return 0;
        if (m == WM_LBUTTONDOWN || m == WM_LBUTTONDBLCLK || m == WM_RBUTTONDOWN) {
            SetFocus(gWnd);
            return 0;
        }
        if (m == WM_SETFOCUS) {
            HideCaret(h);
            SetFocus(gWnd);
            return 0;
        }
        if (m == WM_KEYDOWN && w != VK_LEFT && w != VK_RIGHT && w != VK_HOME && w != VK_END)
            return 0;
    }
    if (m == WM_CHAR) {
        if (w == 8 || w == 127 || w == 1 || w == 3 || w == 24) {
            return CallWindowProcW(gNickPrev, h, m, w, l);
        }
        if (w == 22) {
            SendMessageW(h, WM_PASTE, 0, 0);
            return 0;
        }
        if (!IsNickChar((wchar_t)w)) return 0;
    }
    if (m == WM_IME_CHAR) {
        if (!IsNickChar((wchar_t)w)) return 0;
    }
    if (m == WM_PASTE) {
        if (!OpenClipboard(h)) return 0;
        HANDLE data = GetClipboardData(CF_UNICODETEXT);
        std::wstring filtered;
        if (data) {
            auto* t = static_cast<const wchar_t*>(GlobalLock(data));
            if (t) {
                filtered = FilterNick(t);
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
        if (!filtered.empty()) {
            SendMessageW(h, EM_REPLACESEL, TRUE, (LPARAM)filtered.c_str());
        }
        return 0;
    }
    return CallWindowProcW(gNickPrev, h, m, w, l);
}

static void DrawTicker(HDC mem, int width) {
    SelectObject(mem, gFontTick);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, COL_FOAM);
    SIZE sz{};
    GetTextExtentPoint32W(mem, kTick, lstrlenW(kTick), &sz);
    gTickPeriod = sz.cx > 80 ? sz.cx : 900;
    HRGN clip = CreateRectRgn(0, 0, width, 34);
    SelectClipRgn(mem, clip);
    for (int x = 8 - (gTickOff % gTickPeriod); x < width + gTickPeriod; x += gTickPeriod) {
        TextOutW(mem, x, 8, kTick, lstrlenW(kTick));
    }
    SelectClipRgn(mem, nullptr);
    DeleteObject(clip);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        gFontTick = MakeFont(15, FW_SEMIBOLD, L"Palatino Linotype");
        gFontNav = MakeFont(16, FW_SEMIBOLD, L"Palatino Linotype");
        gFontBig = MakeFont(32, FW_BOLD, L"Palatino Linotype");
        gFontBody = MakeFont(18, FW_NORMAL, L"Georgia");
        gFontNick = MakeFont(22, FW_NORMAL, L"Georgia");
        gMaxGb = DetectMaxGb();
        gCfg = LoadLiveConfig(LivePath());
        gSt = LoadState(StatePath());
        gSt.nick = FilterNick(gSt.nick);
        if (gSt.gameDir.empty()) gSt.gameDir = JoinPath(ZisHome(), L"game");
        gSt.ramMb = RamGb() * 1024;
        LoadArt();
        RefreshNews(true);
        DarkCaption(h);
        if (gAppIcon) {
            SendMessageW(h, WM_SETICON, ICON_BIG, (LPARAM)gAppIcon);
            SendMessageW(h, WM_SETICON, ICON_SMALL, (LPARAM)gAppIcon);
        }

        gNick = CreateWindowExW(0, L"EDIT", gSt.nick.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
            0, 0, 10, 10, h, (HMENU)ID_NICK, nullptr, nullptr);
        SendMessageW(gNick, EM_SETLIMITTEXT, kNickMax, 0);
        SendMessageW(gNick, WM_SETFONT, (WPARAM)gFontNick, TRUE);
        SendMessageW(gNick, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 2));
        SetWindowTheme(gNick, L"", L"");
        SendMessageW(gNick, EM_SETCUEBANNER, TRUE, (LPARAM)L"ваш ник");
        gNickPrev = (WNDPROC)SetWindowLongPtrW(gNick, GWLP_WNDPROC, (LONG_PTR)NickProc);
        SetTimer(h, 1, 40, nullptr);
        return 0;
    }
    case WM_SIZE: {
        if (w == SIZE_MINIMIZED) return 0;
        int W = LOWORD(l), H = HIWORD(l);
        if (W < 80 || H < 80) return 0;
        Layout(W, H);
        PlaceNick();
        return 0;
    }
    case WM_TIMER: {
        if (IsIconic(h)) {
            gMinimizeAt = 0;
            if (gGameLive && !GameClientRunning()) {
                if (++gDeadHits >= 30) OnGameStopped(h);
            } else {
                gDeadHits = 0;
            }
            return 0;
        }
        gTickOff += 2;
        if (gTickPeriod > 0) gTickOff %= gTickPeriod;
        gGifPulse++;
        if ((gGifPulse % 1500) == 0 && gTab == 0) RefreshNews(true);
        if ((gGifPulse % 12) == 0) {
            for (auto& p : gNewsPic) {
                if (!p.img || p.frames < 2) continue;
                p.frame = (p.frame + 1) % p.frames;
                p.img->SelectActiveFrame(&p.dim, p.frame);
            }
        }
        if (gMinimizeAt && GetTickCount64() >= gMinimizeAt) {
            gMinimizeAt = 0;
            if (gGameLive && GameClientRunning()) {
                ShowWindow(h, SW_MINIMIZE);
                return 0;
            }
        }
        if (gGameLive && !GameClientRunning()) {
            if (++gDeadHits >= 30) OnGameStopped(h);
        } else {
            gDeadHits = 0;
        }
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        if (IsIconic(h) || rc.right < 80 || rc.bottom < 80) {
            EndPaint(h, &ps);
            return 0;
        }
        Layout(rc.right, rc.bottom);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);

        Fill(mem, rc, COL_PURPLE);
        RECT body{ 0, 96, rc.right, rc.bottom - 108 };
        Fill(mem, body, COL_PURPLE);
        HBRUSH scan = CreateSolidBrush(COL_SCAN);
        for (int y = body.top; y < body.bottom; y += 4) {
            RECT sl{ body.left, y, body.right, y + 1 };
            FillRect(mem, &sl, scan);
        }
        DeleteObject(scan);

        RECT top{ 0, 0, rc.right, 96 };
        Fill(mem, top, COL_VOID);
        RECT dock{ 0, rc.bottom - 108, rc.right, rc.bottom };
        Fill(mem, dock, COL_VOID);

        DrawTicker(mem, rc.right);

        {
            Gdiplus::Graphics gfx(mem);
            gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            gfx.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            int lx = 20;
            if (gCrest) {
                gfx.DrawImage(gCrest, 16, 42, 40, 40);
                gfx.Flush();
                lx = 64;
            }
            SelectObject(mem, gFontNav);
            SetTextColor(mem, COL_FOAM);
            SetBkMode(mem, TRANSPARENT);
            int ly = 50;
            SIZE sz{};
            GetTextExtentPoint32W(mem, L"DYNASTY", 7, &sz);
            TextOutW(mem, lx, ly, L"DYNASTY", 7);
            lx += sz.cx + 10;
            RECT of{ lx, 48, lx + 44, 80 };
            Fill(mem, of, COL_PURPLE);
            TextAt(mem, of, L"OF", COL_INK, gFontTick, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            lx = of.right + 10;
            SelectObject(mem, gFontNav);
            SetTextColor(mem, COL_FOAM);
            GetTextExtentPoint32W(mem, L"ROT", 3, &sz);
            TextOutW(mem, lx, ly, L"ROT", 3);
            lx += sz.cx + 12;
            RECT beta{ lx, 50, lx + 64, 78 };
            Frame(mem, beta, COL_FOAM);
            TextAt(mem, beta, L"BETA", COL_FOAM, gFontTick, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            auto tab = [&](RECT r, const wchar_t* s, bool on) {
                if (on) Frame(mem, r, COL_FOAM);
                TextAt(mem, r, s, COL_FOAM, gFontNav, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            };
            tab(gHit.tabPlay, L"ИГРА", gTab == 0);
            tab(gHit.tabSet, L"НАСТРОЙКИ", gTab == 1);
            tab(gHit.tabCodex, L"КОДЕКС", gTab == 2);

            if (gTab == 0) {
                int newsL = 36;
                int artBot = rc.bottom - 124;
                TextAt(mem, RECT{ newsL, 112, rc.right - 28, 164 }, L"Новости", COL_INK, gFontBig, DT_LEFT | DT_SINGLELINE);
                int y = 176;
                int n = (int)gNews.size();
                if (n == 0) {
                    TextAt(mem, RECT{ newsL, y, rc.right - 36, y + 80 },
                           L"Новостей пока нет.", COL_INK, gFontBody, DT_LEFT | DT_WORDBREAK);
                }
                if (n > 3) n = 3;
                int slot = n > 0 ? (artBot - 176) / n : 90;
                if (slot < 88) slot = 88;
                if (slot > 160) slot = 160;
                for (int i = 0; i < n; i++) {
                    int textL = newsL;
                    if (i < (int)gNewsPic.size() && gNewsPic[i].img) {
                        int iw = 150;
                        int ih = slot - 14;
                        gfx.DrawImage(gNewsPic[i].img, newsL, y + 4, iw, ih);
                        gfx.Flush();
                        RECT frame{ newsL, y + 4, newsL + iw, y + 4 + ih };
                        Frame(mem, frame, COL_INK);
                        textL = newsL + iw + 18;
                    }
                    RECT head{ textL, y, rc.right - 32, y + 28 };
                    std::wstring hline = gNews[i].date + L"  ·  " + gNews[i].title;
                    TextAt(mem, head, hline.c_str(), COL_INK, gFontBody, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                    RECT bodyr{ textL, y + 28, rc.right - 36, y + slot - 10 };
                    TextAt(mem, bodyr, gNews[i].text.c_str(), COL_INK, gFontBody, DT_LEFT | DT_WORDBREAK);
                    y += slot;
                }
            } else if (gTab == 1) {
                TextAt(mem, RECT{ 36, 112, rc.right - 36, 164 }, L"Настройки", COL_INK, gFontBig, DT_LEFT | DT_SINGLELINE);
                TextAt(mem, RECT{ 40, 172, rc.right - 40, 204 }, L"Память для клиента", COL_INK, gFontNav, DT_LEFT | DT_SINGLELINE);
                for (int i = 0; i < gHit.nChips; i++) {
                    bool on = RamGb() == gHit.ramChipGb[i];
                    Fill(mem, gHit.ramChip[i], on ? COL_VOID : COL_PURPLE);
                    Frame(mem, gHit.ramChip[i], COL_INK);
                    std::wstring lab = std::to_wstring(gHit.ramChipGb[i]) + L" ГБ";
                    TextAt(mem, gHit.ramChip[i], lab.c_str(), on ? COL_FOAM : COL_INK, gFontNav, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                RECT track = gHit.ramBar;
                int midY = (track.top + track.bottom) / 2;
                RECT groove{ track.left, midY - 4, track.right, midY + 4 };
                Fill(mem, groove, COL_SLOT);
                Frame(mem, groove, COL_INK);
                int span = gMaxGb - 5;
                int fillw = span < 1 ? 0 : (int)((track.right - track.left) * ((RamGb() - 5) / (double)span));
                RECT fillr = groove;
                fillr.right = fillr.left + fillw;
                Fill(mem, fillr, COL_INK);
                int cx = track.left + fillw;
                if (cx < track.left) cx = track.left;
                if (cx > track.right) cx = track.right;
                RECT thumb{ cx - 9, midY - 11, cx + 9, midY + 11 };
                Fill(mem, thumb, COL_FOAM);
                Frame(mem, thumb, COL_INK);
                std::wstring ram = std::to_wstring(RamGb()) + L" ГБ   ·   от 5 до " + std::to_wstring(gMaxGb) + L" ГБ";
                TextAt(mem, RECT{ 40, gHit.ramBar.bottom + 10, rc.right - 40, gHit.ramBar.bottom + 40 }, ram.c_str(), COL_INK, gFontBody, DT_LEFT);
                TextAt(mem, RECT{ 40, gHit.ramBar.bottom + 42, rc.right - 48, gHit.ramBar.bottom + 118 },
                       L"Потяните квадрат на полоске или нажмите клетку. Ниже 8 ГБ сборка часто не влезает — лаги и вылеты.",
                       COL_INK, gFontBody, DT_LEFT | DT_WORDBREAK);
                if (SessionLocked()) {
                    TextAt(mem, RECT{ 40, gHit.logsBtn.top - 28, rc.right - 40, gHit.logsBtn.top - 4 },
                           L"Пока игра загружается или запущена, память менять нельзя.",
                           COL_INK, gFontTick, DT_LEFT | DT_SINGLELINE);
                }
                Fill(mem, gHit.logsBtn, COL_VOID);
                Frame(mem, gHit.logsBtn, COL_INK);
                TextAt(mem, gHit.logsBtn, L"ЛОГИ", COL_FOAM, gFontNav, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else {
                TextAt(mem, RECT{ 36, 112, rc.right - 36, 168 }, L"Кодекс", COL_INK, gFontBig, DT_LEFT | DT_SINGLELINE);
                std::wstring codex;
                if (!gCodex.lede.empty()) {
                    codex += gCodex.lede;
                    codex += L"\n\n";
                }
                for (size_t i = 0; i < gCodex.rules.size(); i++) {
                    codex += std::to_wstring((int)i + 1);
                    codex += L". ";
                    codex += gCodex.rules[i];
                    codex += L"\n";
                }
                if (codex.empty()) {
                    codex = L"Пожалуйста, входите только через этот лаунчер.\n"
                            L"Гриферство и дюп наказываются.\n"
                            L"О найденных ошибках пишите администрации.\n"
                            L"Чанки колоний принадлежат поселению.\n"
                            L"Сейчас идёт тестовая BETA: мир может быть сброшен.";
                }
                TextAt(mem, RECT{ 40, 176, rc.right - 48, rc.bottom - 130 },
                       codex.c_str(), COL_INK, gFontBody, DT_LEFT | DT_WORDBREAK);
            }
        }

        TextAt(mem, gHit.nickLbl, L"ник", COL_MUTED, gFontNick,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        {
            HPEN p = CreatePen(PS_SOLID, 1, SessionLocked() ? COL_MUTED : COL_LINE);
            HGDIOBJ op = SelectObject(mem, p);
            MoveToEx(mem, gHit.nickBox.left, gHit.nickBox.bottom - 6, nullptr);
            LineTo(mem, gHit.nickBox.right, gHit.nickBox.bottom - 6);
            SelectObject(mem, op);
            DeleteObject(p);
        }
        if (!gCrashOpen && !gStatus.empty()) {
            TextAt(mem, gHit.status, gStatus.c_str(), COL_FOAM, gFontNav,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }

        Fill(mem, gHit.play, gPlayHot && !SessionLocked() ? RGB(255, 255, 255) : COL_FOAM);
        TextAt(mem, gHit.play, PlayCaption(), COL_INK, gFontNav, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (gBusy) {
            RECT pulse = gHit.play;
            int w = pulse.right - pulse.left;
            int x = pulse.left + ((gGifPulse * 6) % (w + 40)) - 20;
            RECT bar{ x, pulse.bottom - 6, x + 48, pulse.bottom - 2 };
            if (bar.left < pulse.left) bar.left = pulse.left;
            if (bar.right > pulse.right) bar.right = pulse.right;
            if (bar.right > bar.left) Fill(mem, bar, COL_VOID);
        }

        if (gCrashOpen) {
            Fill(mem, gHit.crashBox, COL_VOID);
            Frame(mem, gHit.crashBox, COL_FOAM);
            std::wstring head = L"Игра закрылась";
            if (gLaunchFailed) head = L"Игра не запустилась";
            else if (gCrashCode)
                head = L"Игра вылетела  ·  код " + std::to_wstring((long)gCrashCode);
            TextAt(mem, RECT{ gHit.crashBox.left + 20, gHit.crashBox.top + 16, gHit.crashBox.right - 20, gHit.crashBox.top + 52 },
                   head.c_str(), COL_FOAM, gFontNav, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            TextAt(mem, RECT{ gHit.crashBox.left + 20, gHit.crashBox.top + 52, gHit.crashBox.right - 20, gHit.crashBox.bottom - 70 },
                   L"Скопируйте логи и отправьте разработчику — так проще понять, что случилось.",
                   COL_FOAM, gFontBody, DT_CENTER | DT_WORDBREAK);
            Fill(mem, gHit.crashCopy, COL_FOAM);
            TextAt(mem, gHit.crashCopy, L"СКОПИРОВАТЬ ЛОГИ", COL_INK, gFontTick, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            Frame(mem, gHit.crashOk, COL_FOAM);
            TextAt(mem, gHit.crashOk, L"ЗАКРЫТЬ", COL_FOAM, gFontTick, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        if ((HWND)l == gNick)
            return ColorNick((HDC)w);
        break;
    case WM_LBUTTONDOWN: {
        POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (gCrashOpen) {
            if (PtInRect(&gHit.crashCopy, p)) {
                CopyCrashLogs();
                InvalidateRect(h, nullptr, FALSE);
            } else if (PtInRect(&gHit.crashOk, p)) {
                gCrashOpen = false;
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        }
        if (PtInRect(&gHit.tabPlay, p)) { gTab = 0; RefreshNews(false); InvalidateRect(h, nullptr, FALSE); }
        else if (PtInRect(&gHit.tabSet, p)) { gTab = 1; InvalidateRect(h, nullptr, FALSE); }
        else if (PtInRect(&gHit.tabCodex, p)) { gTab = 2; RefreshNews(false); InvalidateRect(h, nullptr, FALSE); }
        else if (PtInRect(&gHit.play, p)) DoPlay();
        else if (gTab == 1 && PtInRect(&gHit.logsBtn, p)) OpenLogsFolder();
        else if (gTab == 1 && !SessionLocked()) {
            int chip = ChipForPoint(p);
            if (chip) ApplyRamGb(chip);
            else if (HitRamBar(p)) {
                gDragRam = true;
                SetCapture(h);
                RamFromBar(p.x);
            }
        }
        return 0;
    }
    case WM_LBUTTONUP:
        gDragRam = false;
        ReleaseCapture();
        return 0;
    case WM_MOUSEMOVE: {
        POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        if (gDragRam) RamFromBar(p.x);
        bool hot = !SessionLocked() && !gCrashOpen && PtInRect(&gHit.play, p);
        if (hot != gPlayHot) { gPlayHot = hot; InvalidateRect(h, &gHit.play, FALSE); }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == ID_NICK && (HIWORD(w) == EN_UPDATE || HIWORD(w) == EN_CHANGE)) {
            wchar_t nick[32]{};
            GetWindowTextW(gNick, nick, 32);
            std::wstring f = FilterNick(nick);
            if (f != nick) {
                SetWindowTextW(gNick, f.c_str());
                SendMessageW(gNick, EM_SETSEL, (WPARAM)f.size(), (LPARAM)f.size());
            }
        }
        if (LOWORD(w) == ID_NICK && HIWORD(w) == EN_KILLFOCUS) {
            wchar_t nick[32]{};
            GetWindowTextW(gNick, nick, 32);
            gSt.nick = FilterNick(nick);
            SetWindowTextW(gNick, gSt.nick.c_str());
            SaveState(StatePath(), gSt);
        }
        return 0;
    case WM_APP + 1: {
        auto* s = (std::wstring*)l;
        gStatus = *s;
        delete s;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_APP + 2:
        gBusy = false;
        if (w == 0) {
            SetNickLocked(false);
            gGameLive = false;
            gLaunchFailed = true;
            gMinimizeAt = 0;
            gDeadHits = 0;
            if (gStatus.find(L"Java сразу") == std::wstring::npos &&
                gStatus.find(L"CreateProcess") == std::wstring::npos &&
                gStatus.find(L"Нет ") == std::wstring::npos)
                gStatus = L"Игра не запустилась. Смотрите строку статуса.";
            gCrashOpen = true;
            gCrashCode = GameClientExitCode();
        } else {
            gGameLive = true;
            gGameStartedAt = GetTickCount64();
            MarkGameSessionStart();
            gDeadHits = 0;
            gMinimizeAt = GetTickCount64() + 4000;
            gStatus = L"Игра запущена. Сворачиваю через пару секунд…";
        }
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_CLOSE:
        StopGameClient();
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        StopGameClient();
        KillTimer(h, 1);
        delete gCrest; gCrest = nullptr;
        ClearNewsPics();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MaybeSelfUpdate();
    SetProcessDPIAware();
    Gdiplus::GdiplusStartupInput gdiIn;
    Gdiplus::GdiplusStartup(&gGdiToken, &gdiIn, nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"DynastyOfRotLauncher";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(COL_PURPLE);
    gAppIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    if (!gAppIcon) {
        auto icoPath = JoinPath(JoinPath(LauncherDir(), L"assets"), L"app.ico");
        gAppIcon = (HICON)LoadImageW(nullptr, icoPath.c_str(), IMAGE_ICON, 0, 0,
                                     LR_LOADFROMFILE | LR_DEFAULTSIZE);
    }
    wc.hIcon = gAppIcon;
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    if (!wc.hIconSm) wc.hIconSm = gAppIcon;
    RegisterClassExW(&wc);
    gWnd = CreateWindowExW(0, wc.lpszClassName, L"Dynasty of Rot — BETA",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           CW_USEDEFAULT, CW_USEDEFAULT, 1100, 740,
                           nullptr, nullptr, inst, nullptr);
    if (gWnd) DarkCaption(gWnd);
    ShowWindow(gWnd, show);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    Gdiplus::GdiplusShutdown(gGdiToken);
    CoUninitialize();
    return 0;
}
