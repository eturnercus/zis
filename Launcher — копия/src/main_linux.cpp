#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <string>
#include <vector>
#include <iostream>
#include <codecvt>
#include <locale>
#include <unistd.h>
#include "engine.hpp"

// Colors
const SDL_Color COL_VOID = {7, 4, 12, 255};
const SDL_Color COL_PURPLE = {192, 132, 252, 255};
const SDL_Color COL_PURPLE_DARK = {170, 110, 230, 255};
const SDL_Color COL_FOAM = {243, 232, 255, 255};
const SDL_Color COL_INK = {10, 6, 18, 255};

static int gTab = 0;
static LaunchState gSt;
static LaunchConfig gCfg;
static bool gBusy = false;
static bool gGameLive = false;
static bool gDraggingSlider = false;
static long gTotalRamMb = 32768;
static std::string gStatusUtf8 = "";
static std::vector<NewsItem> gNews;
static CodexData gCodex;
static int gTickOff = 0;
static std::string kTickText = u8"ОТКРЫТИЕ   ✠   ТЕСТОВАЯ BETA   ✠   DYNASTY OF ROT   ✠   ДОБРО ПОЖАЛОВАТЬ   ✠   ";

// Helper to convert wstring to UTF-8 string
static std::string ToUtf8(const std::wstring& ws) {
    if (ws.empty()) return "";
    try {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
        return conv.to_bytes(ws);
    } catch (...) {
        return "";
    }
}

// Helper to render text (expects UTF-8)
void RenderText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color, bool center = false, int centerX = 0) {
    if (!font || text.empty()) return;

    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_Rect dst = {x, y, surface->w, surface->h};
        if (center) {
            dst.x = centerX - (surface->w / 2);
        }
        SDL_RenderCopy(renderer, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

// Helper to render text with automatic wrapping and manual newlines
void RenderTextWrapped(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color, int maxWidth, int& outHeight, int lineHeight = 20) {
    if (!font || text.empty()) {
        outHeight = 0;
        return;
    }

    int currentY = y;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        std::string line = (end == std::string::npos) ? text.substr(start) : text.substr(start, end - start);

        // Wrap this specific line
        std::vector<std::string> wrappedLines;
        std::string currentWord;
        std::string currentLine;

        size_t charPos = 0;
        while (charPos < line.size()) {
            // Handle UTF-8 characters (simplified)
            unsigned char c = (unsigned char)line[charPos];
            int len = 1;
            if (c >= 0xf0) len = 4;
            else if (c >= 0xe0) len = 3;
            else if (c >= 0xc0) len = 2;

            std::string word;
            size_t wordStart = charPos;
            while (charPos < line.size() && line[charPos] != ' ') {
                if ((unsigned char)line[charPos] >= 0x80) {
                    unsigned char c2 = (unsigned char)line[charPos];
                    int l2 = 1;
                    if (c2 >= 0xf0) l2 = 4; else if (c2 >= 0xe0) l2 = 3; else if (c2 >= 0xc0) l2 = 2;
                    for(int i=0; i<l2 && charPos < line.size(); ++i) word += line[charPos++];
                } else {
                    word += line[charPos++];
                }
            }

            // Check if currentLine + word fits
            std::string testLine = currentLine + (currentLine.empty() ? "" : " ") + word;
            int w, h_size;
            TTF_SizeUTF8(font, testLine.c_str(), &w, &h_size);
            if (w <= maxWidth) {
                currentLine = testLine;
            } else {
                if (!currentLine.empty()) {
                    wrappedLines.push_back(currentLine);
                    currentLine = word;
                } else {
                    // Word itself is too long, force break it
                    wrappedLines.push_back(word);
                    currentLine = "";
                }
            }

            if (charPos < line.size() && line[charPos] == ' ') charPos++;
        }
        if (!currentLine.empty()) wrappedLines.push_back(currentLine);

        for (const auto& wl : wrappedLines) {
            RenderText(renderer, font, wl, x, currentY, color);
            currentY += lineHeight;
        }

        if (end == std::string::npos) break;
        start = end + 1;
    }
    outHeight = currentY - y;
}

// Helper to render multiline text (expects UTF-8)
void RenderTextMultiline(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color, int lineHeight = 24) {
    if (!font || text.empty()) return;
    size_t start = 0;
    int lineY = y;
    while (true) {
        size_t end = text.find('\n', start);
        std::string line = (end == std::string::npos) ? text.substr(start) : text.substr(start, end - start);
        RenderText(renderer, font, line, x, lineY, color);
        if (end == std::string::npos) break;
        start = end + 1;
        lineY += lineHeight;
    }
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;
    if (TTF_Init() == -1) return 1;
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);

    SDL_Window* window = SDL_CreateWindow("Dynasty of Rot — Linux",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1100, 740, SDL_WINDOW_SHOWN);

    // Try accelerated renderer first, fallback to software if it fails
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "Accelerated renderer failed, trying software renderer..." << std::endl;
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
        std::cerr << "All renderers failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    TTF_Font* fontNav = TTF_OpenFont("/usr/share/fonts/TTF/DejaVuSans-Bold.ttf", 16);
    TTF_Font* fontBody = TTF_OpenFont("/usr/share/fonts/TTF/DejaVuSans.ttf", 18);
    TTF_Font* fontBig = TTF_OpenFont("/usr/share/fonts/TTF/DejaVuSans-Bold.ttf", 32);

    // Get total system RAM
    long pages = sysconf(_SC_PHYS_PAGES);
    long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && pageSize > 0) {
        gTotalRamMb = (pages * pageSize) / (1024 * 1024);
    }

    // Load Crest
    SDL_Texture* texCrest = nullptr;
    std::string crestPath = ToUtf8(JoinPath(LauncherDir(), L"assets/crest.png"));
    SDL_Surface* surfCrest = IMG_Load(crestPath.c_str());
    if (surfCrest) {
        // Convert surface to a compatible format for the software renderer
        SDL_Surface* optimizedSurf = SDL_ConvertSurfaceFormat(surfCrest, SDL_PIXELFORMAT_RGBA8888, 0);
        if (optimizedSurf) {
            texCrest = SDL_CreateTextureFromSurface(renderer, optimizedSurf);
            SDL_FreeSurface(optimizedSurf);
        }
        SDL_FreeSurface(surfCrest);
    }

    gCfg = LoadLiveConfig(JoinPath(LauncherDir(), L"config/live.json"));
    gSt = LoadState(JoinPath(ZisHome(), L"state.json"));
    gNews = LoadNews(gCfg);
    gCodex = LoadCodex(gCfg);

    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                StopGameClient();
                running = false;
            } else if (event.type == SDL_MOUSEBUTTONDOWN) {
                int x = event.button.x;
                int y = event.button.y;
                if (y > 44 && y < 84) {
                    if (x > 1100 - 444 && x < 1100 - 316) gTab = 0;
                    else if (x > 1100 - 304 && x < 1100 - 164) gTab = 1;
                    else if (x > 1100 - 152 && x < 1100 - 24) gTab = 2;
                }
                if (y > 740 - 108 + 26 && y < 740 - 108 + 78) {
                    if (x > 1100 - 208 && x < 1100 - 28) {
                        if (!gBusy && !gGameLive) {
                            gBusy = true;
                            RunLaunch(gSt, gCfg, [](const std::wstring& s) {
                                gStatusUtf8 = ToUtf8(s);
                            });
                        }
                    }
                }
                // RAM Chips and Slider (Settings Tab)
                if (gTab == 1) {
                    // Chips
                    if (y > 210 && y < 248) {
                        int maxAllowedRam = (int)std::min(32768L, gTotalRamMb - 1024);
                        if (maxAllowedRam < 2048) maxAllowedRam = 2048;

                        std::vector<int> chips = {8, 12, 16};
                        chips.push_back(maxAllowedRam / 1024);

                        int visibleCount = 0;
                        for (int i = 0; i < chips.size(); i++) {
                            if (i > 0 && chips[i] == chips[i-1]) continue;
                            if (i < chips.size() - 1 && chips[i] * 1024 >= maxAllowedRam) continue;

                            int cx = 40 + visibleCount * 86;
                            if (x > cx && x < cx + 76) {
                                if (i == chips.size() - 1) {
                                    gSt.ramMb = maxAllowedRam;
                                } else {
                                    gSt.ramMb = chips[i] * 1024;
                                }
                            }
                            visibleCount++;
                        }
                    }
                    // Slider handle
                    int sX = 40;
                    int sW = 334; // Aligned with 4 chips (4 * 86 - 10)
                    int sY = 270;
                    int maxRam = (int)std::min(32768L, gTotalRamMb - 1024);
                    if (maxRam < 2048) maxRam = 2048;
                    int handleX = sX + ( (gSt.ramMb - 2048) * sW / (maxRam - 2048) );
                    if (x > handleX - 10 && x < handleX + 10 && y > sY - 10 && y < sY + 10) {
                        gDraggingSlider = true;
                    }
                }
            } else if (event.type == SDL_MOUSEBUTTONUP) {
                gDraggingSlider = false;
            } else if (event.type == SDL_MOUSEMOTION) {
                if (gDraggingSlider && gTab == 1) {
                    int x = event.motion.x;
                    int sX = 40;
                    int sW = 334; // Aligned with 4 chips
                    int maxRam = (int)std::min(32768L, gTotalRamMb - 1024);
                    if (maxRam < 2048) maxRam = 2048;

                    int val = (x - sX) * (maxRam - 2048) / sW + 2048;
                    if (val < 2048) val = 2048;
                    if (val > maxRam) val = maxRam;
                    gSt.ramMb = val;
                }
            }
        }

        gTickOff += 2;

        SDL_SetRenderDrawColor(renderer, COL_PURPLE.r, COL_PURPLE.g, COL_PURPLE.b, 255);
        SDL_RenderClear(renderer);

        // Striped background
        SDL_SetRenderDrawColor(renderer, COL_PURPLE_DARK.r, COL_PURPLE_DARK.g, COL_PURPLE_DARK.b, 255);
        for (int x = -740; x < 1100; x += 40) {
            SDL_RenderDrawLine(renderer, x, 0, x + 740, 740);
        }

        SDL_SetRenderDrawColor(renderer, COL_VOID.r, COL_VOID.g, COL_VOID.b, 255);
        SDL_Rect topBar = {0, 0, 1100, 96};
        SDL_RenderFillRect(renderer, &topBar);
        SDL_Rect botBar = {0, 740 - 108, 1100, 108};
        SDL_RenderFillRect(renderer, &botBar);

        if (texCrest) {
            SDL_Rect crestRect = {16, 42, 40, 40};
            SDL_RenderCopy(renderer, texCrest, nullptr, &crestRect);
        }

        // "OF" background square
        SDL_SetRenderDrawColor(renderer, COL_PURPLE.r, COL_PURPLE.g, COL_PURPLE.b, 255);
        SDL_Rect ofBox = {220, 50, 30, 30}; // Centered around the "OF" text
        SDL_RenderFillRect(renderer, &ofBox);

        RenderText(renderer, fontBig, "DYNASTY", 64, 50, COL_FOAM);
        RenderText(renderer, fontNav, "OF", 220, 55, COL_INK);
        RenderText(renderer, fontBig, "ROT", 260, 50, COL_FOAM);

        SDL_Rect t1 = {1100 - 444, 44, 128, 40};
        SDL_Rect t2 = {1100 - 304, 44, 140, 40};
        SDL_Rect t3 = {1100 - 152, 44, 128, 40};

        if (gTab == 0) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDrawRect(renderer, &t1);
            RenderText(renderer, fontNav, u8"ИГРА", 0, t1.y + 10, COL_FOAM, true, t1.x + t1.w / 2);
        } else {
            RenderText(renderer, fontNav, u8"ИГРА", 0, t1.y + 10, COL_FOAM, true, t1.x + t1.w / 2);
        }

        if (gTab == 1) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDrawRect(renderer, &t2);
            RenderText(renderer, fontNav, u8"НАСТРОЙКИ", 0, t2.y + 10, COL_FOAM, true, t2.x + t2.w / 2);
        } else {
            RenderText(renderer, fontNav, u8"НАСТРОЙКИ", 0, t2.y + 10, COL_FOAM, true, t2.x + t2.w / 2);
        }

        if (gTab == 2) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDrawRect(renderer, &t3);
            RenderText(renderer, fontNav, u8"КОДЕКС", 0, t3.y + 10, COL_FOAM, true, t3.x + t3.w / 2);
        } else {
            RenderText(renderer, fontNav, u8"КОДЕКС", 0, t3.y + 10, COL_FOAM, true, t3.x + t3.w / 2);
        }

        if (gTab == 0) {
            // 1. Hero Eyebrow
            RenderText(renderer, fontNav, ToUtf8(gCodex.hero_eyebrow), 36, 112, COL_INK);

            // 2. Hero Title
            RenderTextMultiline(renderer, fontBig, ToUtf8(gCodex.hero_title), 36, 135, COL_INK, 36);

            // 3. Hero Media (Crest)
            if (texCrest) {
                SDL_Rect crestRect = {36, 210, 120, 120};
                SDL_RenderCopy(renderer, texCrest, nullptr, &crestRect);
            }
            RenderText(renderer, fontNav, ToUtf8(gCodex.hero_caption), 36, 340, COL_INK);

            // 4. Lede
            RenderTextMultiline(renderer, fontBody, ToUtf8(gCodex.lede), 36, 370, COL_INK, 24);

            // 5. Cards
            int cardX = 36;
            int cardY = 460;
            int cardW = 320;
            int maxCardH = 200;

            int tallestCard = 0;
            std::vector<int> heights;
            for (size_t i = 0; i < gCodex.cards.size(); i++) {
                int h = 0;
                RenderTextWrapped(renderer, fontBody, ToUtf8(gCodex.cards[i].text), 0, 0, {0,0,0,0}, cardW - 30, h);
                heights.push_back(h);
                if (h > tallestCard) tallestCard = h;
            }

            int finalCardH = std::max(100, tallestCard + 60); // Base height or dynamic

            for (size_t i = 0; i < gCodex.cards.size(); i++) {
                int x = cardX + (int)i * (cardW + 20);
                if (x > 1100 - 40) break;

                SDL_SetRenderDrawColor(renderer, COL_VOID.r, COL_VOID.g, COL_VOID.b, 255);
                SDL_Rect r = {x, cardY, cardW, finalCardH};
                SDL_RenderFillRect(renderer, &r);

                RenderText(renderer, fontNav, ToUtf8(gCodex.cards[i].title), x + 15, cardY + 15, COL_FOAM);

                int textH = 0;
                RenderTextWrapped(renderer, fontBody, ToUtf8(gCodex.cards[i].text), x + 15, cardY + 40, COL_FOAM, cardW - 30, textH);
            }
        } else if (gTab == 1) {
            RenderText(renderer, fontBig, u8"Настройки", 36, 112, COL_INK);
            RenderText(renderer, fontNav, u8"Память для клиента", 40, 172, COL_INK);

            int maxAllowedRam = (int)std::min(32768L, gTotalRamMb - 1024);
            if (maxAllowedRam < 2048) maxAllowedRam = 2048;

            std::vector<int> chips = {8, 12, 16};
            chips.push_back(maxAllowedRam / 1024);

            int chipCount = 0;
            for (int i = 0; i < chips.size(); i++) {
                // Avoid duplicate buttons if maxAllowed matches a preset
                if (i > 0 && chips[i] == chips[i-1]) continue;

                // Only show presets that are actually less than the max allowed
                // unless it's the last one (the max itself)
                if (i < chips.size() - 1 && chips[i] * 1024 >= maxAllowedRam) continue;

                SDL_Rect chip = {40 + chipCount * 86, 210, 76, 38};
                if (gSt.ramMb == chips[i] * 1024 || (i == chips.size() - 1 && gSt.ramMb == maxAllowedRam)) {
                    SDL_SetRenderDrawColor(renderer, COL_FOAM.r, COL_FOAM.g, COL_FOAM.b, 255);
                } else {
                    SDL_SetRenderDrawColor(renderer, COL_VOID.r, COL_VOID.g, COL_VOID.b, 255);
                }
                SDL_RenderFillRect(renderer, &chip);
                std::string lab = std::to_string(chips[i]) + u8" ГБ";
                SDL_Color txtCol = (gSt.ramMb == chips[i] * 1024 || (i == chips.size() - 1 && gSt.ramMb == maxAllowedRam)) ? COL_INK : COL_FOAM;
                RenderText(renderer, fontNav, lab, chip.x + 20, chip.y + 10, txtCol);
                chipCount++;
            }

            // Slider
            int sX = 40;
            int sW = 334; // Aligned with 4 chips (4 * 86 - 10)
            int sY = 270;

            SDL_SetRenderDrawColor(renderer, COL_VOID.r, COL_VOID.g, COL_VOID.b, 255);
            SDL_Rect track = {sX, sY - 2, sW, 4};
            SDL_RenderFillRect(renderer, &track);

            int handleX = sX + ( (gSt.ramMb - 2048) * sW / (maxAllowedRam - 2048) );
            SDL_SetRenderDrawColor(renderer, COL_FOAM.r, COL_FOAM.g, COL_FOAM.b, 255);
            SDL_Rect handle = {handleX - 6, sY - 8, 12, 16};
            SDL_RenderFillRect(renderer, &handle);

            std::string ramVal = std::to_string(gSt.ramMb / 1024) + u8" ГБ";
            RenderText(renderer, fontNav, ramVal, sX + sW + 20, sY - 10, COL_INK);
        } else if (gTab == 2) {
            RenderText(renderer, fontBig, u8"Кодекс", 36, 112, COL_INK);
            std::string codexText = ToUtf8(gCodex.lede) + "\n\n";
            for (size_t i = 0; i < gCodex.rules.size(); i++) {
                codexText += std::to_string(i+1) + u8". " + ToUtf8(gCodex.rules[i]) + "\n";
            }
            RenderText(renderer, fontBody, codexText, 40, 176, COL_INK);
        }

        SDL_Rect playBtn = {1100 - 208, 740 - 108 + 26, 180, 52};
        SDL_SetRenderDrawColor(renderer, COL_FOAM.r, COL_FOAM.g, COL_FOAM.b, 255);
        SDL_RenderFillRect(renderer, &playBtn);
        std::string playText = gBusy ? u8"ЗАГРУЗКА..." : (gGameLive ? u8"В ИГРЕ" : u8"ИГРАТЬ");
        RenderText(renderer, fontNav, playText, 0, playBtn.y + 15, COL_INK, true, playBtn.x + playBtn.w / 2);

        if (!gStatusUtf8.empty()) {
            RenderText(renderer, fontNav, gStatusUtf8, 1100 - 600, 740 - 60, COL_FOAM);
        }

        // Ticker (top marquee) - rendered last to ensure visibility
        std::string tickStr = "";
        if (!gCodex.ticker.empty()) {
            for (size_t i = 0; i < gCodex.ticker.size(); i++) {
                tickStr += ToUtf8(gCodex.ticker[i]) + u8"   ✠   ";
            }
        } else {
            tickStr = kTickText;
        }

        SDL_Surface* sTick = TTF_RenderUTF8_Blended(fontNav, tickStr.c_str(), COL_FOAM);
        if (sTick) {
            int tickW = sTick->w;
            int tickH = sTick->h;
            int offset = gTickOff % tickW;
            SDL_Texture* tTick = SDL_CreateTextureFromSurface(renderer, sTick);
            SDL_FreeSurface(sTick);

            if (tTick) {
                for (int x = -offset; x < 1100; x += tickW) {
                    SDL_Rect dst = { x, 8, tickW, tickH };
                    SDL_RenderCopy(renderer, tTick, nullptr, &dst);
                }
                SDL_DestroyTexture(tTick);
            }
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    if (fontNav) TTF_CloseFont(fontNav);
    if (fontBody) TTF_CloseFont(fontBody);
    if (fontBig) TTF_CloseFont(fontBig);
    if (texCrest) SDL_DestroyTexture(texCrest);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    return 0;
}
