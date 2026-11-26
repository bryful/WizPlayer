#define NOMINMAX
#include <windows.h>
#include "windows.h"
#include "core_bridge.h"
#include "android_turbo.h"
#include "wizardry_map.h"
#include "wizardry.h"
#include "../game.h"
#include "../console_inventory.h"
#include "../reward_support.h"
#include "../wizardry_item_text.h"
#ifdef WIZPLAYER_SFC
#include "../sfc/core.h"
#endif
#include <chrono>
#include <cwctype>
#include <iostream>
#include <string_view>
#include <optional>
#include <thread>

void write(const fs::path& path, const void* data, size_t size);

bool isNesRom(const std::vector<char>& bytes) {
    return bytes.size() >= 4 && std::memcmp(bytes.data(), "NES\x1a", 4) == 0;
}
bool isSfcRom(const fs::path& rom,const std::vector<char>& bytes) {
    if(isNesRom(bytes))return false;
    auto extension=rom.extension().wstring();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return extension==L".smc"||extension==L".sfc";
}

namespace {
constexpr int width = 256;
constexpr int height = 240;
struct AudioDevice {
    SDL_AudioDeviceID id = 0;
    ~AudioDevice() { if (id) SDL_CloseAudioDevice(id); }
};

enum MenuRow { Slot, Save, Load, SwapAB, SwapXY, Speed, Crt, Support, Characters, Reset, SelectRom, Resume, MenuRows };
enum class Overlay { Game, Map, Items };

class MenuScreen {
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = nullptr;
    HGDIOBJ previousBitmap = nullptr;
    HFONT font = nullptr;
    uint32_t* pixels = nullptr;
public:
    MenuScreen() {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* data = nullptr;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        pixels = static_cast<uint32_t*>(data);
        previousBitmap = SelectObject(dc, bitmap);
        font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           0, 0, ANTIALIASED_QUALITY, 0, L"Yu Gothic UI");
    }
    ~MenuScreen() {
        SelectObject(dc, previousBitmap);
        DeleteObject(bitmap);
        DeleteObject(font);
        DeleteDC(dc);
    }
    void drawInventory(SDL_Texture* screen,int frameHeight,const ConsoleInventory& inventory) {
        if (!pixels || !previousBitmap) return;
        RECT background{0,0,width,height};
        FillRect(dc,&background,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        const auto previousFont=SelectObject(dc,font);
        SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,RGB(240,240,240));
        for(const auto& line:inventory.lines()) {
            const int top=line.y*frameHeight/height;
            RECT bounds{line.x,top,line.x+line.width,(line.y+line.height)*frameHeight/height};
            SetDCBrushColor(dc,line.selected?RGB(48,69,107):RGB(0,0,0));
            FillRect(dc,&bounds,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            const std::string glyphs(line.label.begin(),line.label.end());
            if(WizardryItemText::raster(glyphs)) {
                GdiFlush();
                drawGlyphs(glyphs,line.x,top+3,line.width/8);
            } else {
                DrawTextW(dc,line.label.c_str(),static_cast<int>(line.label.size()),&bounds,
                          DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
            }
        }
        SelectObject(dc,previousFont);
        upload(screen,frameHeight);
    }
private:
    void drawGlyphs(const std::string& text,int left,int top,size_t columns) {
        if(!WizardryItemText::raster(text))return;
        columns=std::min(columns,(text.size()-5)/16);
        for(size_t glyph=0;glyph<columns;++glyph)for(unsigned row=0;row<8;++row) {
            const unsigned bits=WizardryItemText::row(text,glyph,row);
            for(unsigned x=0;x<8;++x)
                if(bits&(0x80u>>x))pixels[(top+row)*width+left+glyph*8+x]=0x00e0e6ee;
        }
    }
    void upload(SDL_Texture* screen,int frameHeight) {
        GdiFlush();
        for (int i = 0; i < width * frameHeight; ++i) {
            const uint32_t color = pixels[i];
            pixels[i]=0xff000000u|(frameHeight==height?
                ((color&0xffu)<<16)|(color&0xff00u)|((color>>16)&0xffu):color);
        }
        SDL_UpdateTexture(screen, nullptr, pixels, width * sizeof(uint32_t));
    }
};

unsigned padInput(unsigned buttons) {
    return (buttons & 15u) |
        ((buttons & 16u) ? PAD_RIGHT : 0) |
        ((buttons & 32u) ? PAD_LEFT : 0) |
        ((buttons & 64u) ? PAD_UP : 0) |
        ((buttons & 128u) ? PAD_DOWN : 0);
}
}

namespace {
using NesCore = std::unique_ptr<WiizCore, decltype(&wiiz_core_delete)>;

NesCore openCore(std::vector<char>& bytes) {
    NesCore core(wiiz_core_new(reinterpret_cast<uint8_t*>(bytes.data()), bytes.size()), wiiz_core_delete);
    if (!core) throw std::runtime_error("Unsupported FC ROM");
    return core;
}

std::vector<uint8_t> restoreRam(WiizCore* core, const fs::path& save) {
    std::vector<uint8_t> ram(wiiz_core_sram_size(core));
    if (!ram.empty() && fs::exists(save)) {
        const auto saved = read(save);
        if (saved.size() != ram.size()) throw std::runtime_error("Unexpected FC save size");
        std::memcpy(ram.data(), saved.data(), saved.size());
        if (!wiiz_core_sram_write(core, ram.data(), ram.size()))
            throw std::runtime_error("FC save load failed");
    }
    return ram;
}

void present(HWND window, SDL_Surface* surface, int aspectHeight, const std::vector<std::wstring>& menu,
             int row, const std::wstring& status,const wchar_t* heading,const std::string& glyphs,int crtMode) {
    RECT client{};
    GetClientRect(window, &client);
    if (client.right <= 0 || client.bottom <= 0) return;
    const RECT viewport = gameViewport(window, width, aspectHeight);
    const HDC dc = GetDC(window);
    const HDC buffer = CreateCompatibleDC(dc);
    const HBITMAP image = CreateCompatibleBitmap(dc, client.right, client.bottom);
    const auto previous = SelectObject(buffer, image);
    PatBlt(buffer, 0, 0, client.right, client.bottom, BLACKNESS);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = surface->pitch / 4;
    info.bmiHeader.biHeight = -surface->h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    StretchDIBits(buffer, viewport.left, viewport.top, viewport.right - viewport.left,
                  viewport.bottom - viewport.top, 0, 0, width, surface->h, surface->pixels,
                  &info, DIB_RGB_COLORS, SRCCOPY);
    drawScanlines(buffer,viewport,surface->h,crtMode);
    if (!menu.empty()) drawWindowMenu(buffer, client, heading, menu, row);
    if(WizardryItemText::raster(glyphs)) {
        for(size_t i=0;i<(glyphs.size()-5)/16;++i)
            for(unsigned y=0;y<8;++y) {
                const unsigned bits=WizardryItemText::row(glyphs,i,y);
                for(unsigned x=0;x<8;++x)
                    if(bits&(0x80u>>x))SetPixelV(buffer,140+static_cast<int>(i)*9+x,65+y,RGB(240,240,240));
            }
    }
    if (!status.empty()) {
        RECT bar{12, client.bottom - 40, client.right - 12, client.bottom - 4};
        SetBkMode(buffer, TRANSPARENT);
        SetTextColor(buffer, RGB(255, 230, 150));
        DrawTextW(buffer, status.c_str(), static_cast<int>(status.size()), &bar,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    BitBlt(dc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, previous);
    DeleteObject(image);
    DeleteDC(buffer);
    ReleaseDC(window, dc);
}

#ifdef WIZPLAYER_SFC
std::unique_ptr<SfcCore> openSfc(const std::vector<char>& bytes,const fs::path& save,bool skipLogo=false) {
    if(!save.empty()&&fs::exists(save)) {
        const auto ram=read(save);
        return std::make_unique<SfcCore>(bytes,&ram,skipLogo);
    }
    return std::make_unique<SfcCore>(bytes,nullptr,skipLogo);
}
#endif
}

void validateConsole(const fs::path& rom, std::vector<char>& bytes, const fs::path& saves) {
    if(isSfcRom(rom,bytes)) {
#ifdef WIZPLAYER_SFC
        openSfc(bytes,saves/L"SFC"/(rom.stem().wstring()+L".sav"));
        return;
#else
        throw std::runtime_error("This build does not include the SFC core");
#endif
    }
    auto core = openCore(bytes);
    restoreRam(core.get(), saves / L"FC" / (rom.stem().wstring() + L".sav"));
}

void smokeConsole(const fs::path& rom,std::vector<char>& bytes) {
    if(isSfcRom(rom,bytes)) {
#ifdef WIZPLAYER_SFC
        auto sfc=openSfc(bytes,{});
        bool video=false,audio=false;
        for(int frame=0;frame<600;++frame) {
            sfc->step(0);
            if(frame%30==0&&!video)
                video=std::any_of(sfc->pixels.begin(),sfc->pixels.end(),[&](uint32_t pixel){return pixel!=sfc->pixels.front();});
            audio=audio||!sfc->pcm.empty();
        }
        if(!video)throw std::runtime_error("No varied SFC frame produced");
        if(!audio)throw std::runtime_error("No SFC audio samples produced");
        if(!sfc->loadState(sfc->state()))throw std::runtime_error("SFC state round trip failed");
        std::cout<<"PASS: 600 frames, video, audio and state round trip; mode=SFC\n";
        return;
#else
        throw std::runtime_error("This build does not include the SFC core");
#endif
    }
    auto core = openCore(bytes);
    WizardryFeatures wizardry(bytes);
    if (wizardry.scenario) wiiz_core_wizardry(core.get(), wizardry.scenario);
    std::array<uint32_t, width * height> pixels{};
    std::vector<uint8_t> state(wiiz_core_state_size(core.get()));
    bool varied = false;
    for (int frame = 1; frame <= 600; ++frame) {
        if (!wiiz_core_frame(core.get())) throw std::runtime_error("FC frame failed");
        if (!varied && frame % 30 == 0) {
            wiiz_core_pixels(core.get(), pixels.data());
            varied = std::any_of(pixels.begin(), pixels.end(), [&](uint32_t p) { return p != pixels.front(); });
        }
    }
    if (!varied) throw std::runtime_error("No varied FC frame produced");
    if (!wiiz_core_state_write(core.get(), state.data(), state.size()) ||
        !wiiz_core_state_read(core.get(), state.data(), state.size()))
        throw std::runtime_error("FC state round trip failed");
    std::cout << "PASS: 600 frames, video output, state round trip; mode=FC\n";
    if (wizardry.scenario)
        std::cout << "Wizardry " << wizardry.scenario << ": " << wizardry.floors.size() << " floors\n";
}

std::optional<WindowsRom> runConsole(HWND window, const fs::path& rom, std::vector<char>& bytes, const fs::path& saves) {
    const bool sfcMode=isSfcRom(rom,bytes);
    const int frameHeight=sfcMode?224:height;
    const int aspectHeight=sfcMode?192:height;
    const auto directory=saves/(sfcMode?L"SFC":L"FC");
    fs::create_directories(directory);
    const auto& currentRom=rom;
    const auto settingsDirectory=saves/L"settings"/(sfcMode?L"SFC":L"FC");
    fs::create_directories(settingsDirectory);
    const auto settings=settingsDirectory/(rom.stem().wstring()+L".ini");
    const auto legacySettings=saves/L"settings.ini";
    auto setting=[&](const wchar_t* group,const wchar_t* key,int fallback) {
        return GetPrivateProfileIntW(group,key,
            GetPrivateProfileIntW(group,key,fallback,legacySettings.c_str()),settings.c_str());
    };
    auto setSetting=[&](const wchar_t* group,const wchar_t* key,int value) {
        if(!WritePrivateProfileStringW(group,key,std::to_wstring(value).c_str(),settings.c_str()))
            throw std::runtime_error("Settings write failed");
    };
    bool skipLogo=setting(L"support",L"skip_logo",1)!=0;
    auto save=directory/(currentRom.stem().wstring()+L".sav");
    NesCore core(nullptr,wiiz_core_delete);
    WizardryFeatures wizardry(sfcMode?std::vector<char>{}:bytes);
#ifdef WIZPLAYER_SFC
    std::unique_ptr<SfcCore> sfc;
    if(sfcMode)sfc=openSfc(bytes,save,skipLogo);
#else
    if(sfcMode)throw std::runtime_error("This build does not include the SFC core");
#endif
    if(!sfcMode) {
        core=openCore(bytes);
        if(wizardry.scenario)wiiz_core_wizardry(core.get(),wizardry.scenario);
    }
    std::array<uint32_t, width * height> pixels{};
    const auto turboFile=directory/(rom.stem().wstring()+L".turbofile");
    std::array<uint8_t,8192> savedCard{};
    if(wizardry.scenario) {
        wiiz_core_turbo_file_attach(core.get());
        if(fs::exists(turboFile)) {
            const auto saved=read(turboFile);
            if(!wiiz_core_turbo_file_write(core.get(),reinterpret_cast<const uint8_t*>(saved.data()),saved.size()))
                throw std::runtime_error("Unexpected Turbo File size");
            std::copy(saved.begin(),saved.end(),savedCard.begin());
        }
    }
    auto turbo=core?std::make_unique<FcTurbo>(core.get(),bytes,wizardry.scenario):nullptr;
    const auto exploration=directory/(rom.stem().wstring()+L".fc.map");
    if(wizardry.scenario&&fs::exists(exploration)) {
        const auto explored=read(exploration);
        if(explored.size()==WIIZ_MAP_BYTES)
            wiiz_core_map_set_explored(core.get(),reinterpret_cast<const uint8_t*>(explored.data()));
    }
    auto ram=core?restoreRam(core.get(),save):std::vector<uint8_t>{};
    auto flush = [&] {
        if(wizardry.scenario) {
            write(exploration,wiiz_core_map_explored(core.get()),WIIZ_MAP_BYTES);
            std::array<uint8_t,8192> card{};
            if(wiiz_core_turbo_file_read(core.get(),card.data(),card.size())&&card!=savedCard) {
                write(turboFile,card.data(),card.size());
                savedCard=card;
            }
        }
#ifdef WIZPLAYER_SFC
        if(sfc) {
            const auto battery=sfc->battery();
            if(!battery.empty())write(save,battery.data(),battery.size());
            return;
        }
#endif
        if (ram.empty()) return;
        if (!wiiz_core_sram_read(core.get(), ram.data(), ram.size()))
            throw std::runtime_error("FC battery RAM read failed");
        write(save, ram.data(), ram.size());
    };

    SdlRuntime runtime;
    const bool sfcWizardry=
#ifdef WIZPLAYER_SFC
        sfc&&sfc->characterEditorSupported();
#else
        false;
#endif
    const bool isWizardry=sfcWizardry||wizardry.scenario;
    setGameWindow(window, rom, isWizardry);
    std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)> surface(
        SDL_CreateRGBSurfaceWithFormat(0, width, frameHeight, 32, SDL_PIXELFORMAT_ARGB8888),
        SDL_FreeSurface);
    if (!surface) throw std::runtime_error(SDL_GetError());
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
        SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
    if (!renderer) throw std::runtime_error(SDL_GetError());
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> screen(
        SDL_CreateTexture(renderer.get(),sfcMode?SDL_PIXELFORMAT_ARGB8888:SDL_PIXELFORMAT_RGBA32,
                          SDL_TEXTUREACCESS_STREAMING,width,frameHeight), SDL_DestroyTexture);
    if (!screen) throw std::runtime_error(SDL_GetError());
    SDL_SetTextureScaleMode(screen.get(), SDL_ScaleModeNearest);

    AudioDevice audio;
    SDL_AudioSpec desired{};
    desired.freq = sfcMode?32768:48000;
    desired.format = sfcMode?AUDIO_S16SYS:AUDIO_F32SYS;
    desired.channels = sfcMode?2:1;
    desired.samples = 1024;
    audio.id = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
    if (audio.id) SDL_PauseAudioDevice(audio.id, 0);

    MenuScreen menuScreen;
    Controllers controllers;
    Edges edges;
    using Clock = std::chrono::steady_clock;
    auto deadline = Clock::now();
    auto lastFlush = deadline;
    int crtMode=std::clamp(static_cast<int>(setting(L"play",L"crt_mode",0)),0,2);
    bool swap=setting(L"controls",L"swap_ab",0)!=0;
    bool swapXY=setting(L"controls",L"swap_xy",0)!=0;
    constexpr std::array<int,7> speeds{2,3,4,6,8,10,20};
    auto setSpeed=[&](int speed) {
#ifdef WIZPLAYER_SFC
        if(sfc)return sfc->setSpeed(speed);
#endif
        return turbo->setSpeed(speed);
    };
    if(turbo&&skipLogo)turbo->skipBootLogo();
    int speedHalf=setting(L"play",L"speed_half",2);
    if(!setSpeed(speedHalf))speedHalf=2;
    auto framePeriod=std::chrono::nanoseconds(16639267);
#ifdef WIZPLAYER_SFC
    if(sfc)framePeriod=std::chrono::nanoseconds(sfc->period());
#endif
    struct SupportOption {
        const wchar_t* label;
        const wchar_t* key;
        int value;
        bool multiplier;
    };
    std::vector<SupportOption> options;
    auto addOption=[&](const wchar_t* label,const wchar_t* key,int fallback=0,bool multiplier=false) {
        int value=setting(L"support",key,fallback);
        if(multiplier&&!RewardSupport::valid(value))value=1;
        if(!multiplier)value=value!=0;
        options.push_back({label,key,value,multiplier});
    };
    const bool assists=core?wiiz_core_wizardry_assists_supported(core.get()):sfcWizardry;
    bool experience=core&&wiiz_core_reward_supported(core.get(),0);
    bool gold=core&&wiiz_core_reward_supported(core.get(),1);
    unsigned gameOptions=core?wiiz_core_ff_options(core.get()):0;
#ifdef WIZPLAYER_SFC
    if(sfc) {
        experience=gold=sfc->rewardsSupported();
        gameOptions=sfc->gameOptionsSupported();
    }
#endif
    if(experience)addOption(L"経験値倍率",L"experience_multiplier",1,true);
    if(gold)addOption(experience?L"お金倍率":L"ギル倍率",L"gold_multiplier",1,true);
    if(assists) {
        addOption(L"作成ボーナス最大",L"max_bonus");
        addOption(L"善悪混合",L"mixed_party");
        addOption(L"起動ロゴスキップ",L"skip_logo",1);
        addOption(L"MP消費なし",L"no_mp_cost");
        addOption(L"常時灯り",L"light");
        addOption(L"常時保護",L"protection");
        addOption(L"常時識別",L"identify");
        if(sfcMode)addOption(L"常時浮遊",L"levitate");
    }
    const wchar_t* const ffLabels[]={gameOptions==3?L"武器・魔法の熟練度最大":L"ジョブ熟練度最大",
                                    L"MP消費なし",L"キャパシティ消費なし"};
    const wchar_t* const dqLabels[]={L"仲間加入率アップ",L"ドロップ確率アップ",L"装備制限解除",
                                    L"アイテム非消費",L"仲間モンスターのレベル上限99"};
    const wchar_t* const gameKeys[]={L"game_option_1",L"game_option_2",L"game_option_3",
                                    L"game_option_4",L"game_option_5"};
    for(unsigned i=0;i<(sfcMode?5u:3u);++i)
        if(gameOptions&(1u<<i))addOption(sfcMode?dqLabels[i]:ffLabels[i],gameKeys[i]);
    auto applySupport=[&] {
        auto value=[&](const wchar_t* key,int fallback=0) {
            for(const auto& option:options)
                if(std::wstring_view(option.key)==key)return option.value;
            return fallback;
        };
        const unsigned exp=value(L"experience_multiplier",1),money=value(L"gold_multiplier",1);
        const unsigned effects=value(L"light")|(value(L"protection")<<1)|
                               (value(L"identify")<<2)|(value(L"levitate")<<3);
        unsigned flags=0;
        for(unsigned i=0;i<5;++i)flags|=value(gameKeys[i])<<i;
        skipLogo=value(L"skip_logo",skipLogo)!=0;
#ifdef WIZPLAYER_SFC
        if(sfc) {
            if(experience||gold)sfc->setRewards(exp,money);
            if(assists) {
                sfc->setMaxBonus(value(L"max_bonus")!=0);
                sfc->setMixedParty(value(L"mixed_party")!=0);
                sfc->setNoMpCost(value(L"no_mp_cost")!=0);
                sfc->setSkipLogo(skipLogo);
                sfc->setEffects(effects);
            }
            if(gameOptions)sfc->setGameOptions(flags);
            return;
        }
#endif
        if(experience||gold)wiiz_core_rewards(core.get(),exp,money);
        if(assists) {
            wiiz_core_set_wizardry_assist(core.get(),WIIZ_ASSIST_BONUS,value(L"max_bonus"));
            wiiz_core_set_wizardry_assist(core.get(),WIIZ_ASSIST_MIXED,value(L"mixed_party"));
            wiiz_core_set_wizardry_assist(core.get(),WIIZ_ASSIST_MP,value(L"no_mp_cost"));
            wiiz_core_set_wizardry_effects(core.get(),effects);
        }
        if(gameOptions) {
            wiiz_core_max_skills(core.get(),flags&1);
            wiiz_core_free_magic(core.get(),(flags&2)!=0);
            wiiz_core_free_jobs(core.get(),(flags&4)!=0);
        }
    };
    applySupport();
    enum class Page { Main, Support, Characters };
    Page page=Page::Main;
    int detailRow=0;
    std::vector<std::pair<unsigned,std::string>> roster;
    size_t character=0;
    auto openCharacters=[&] {
        std::array<std::string,20> names;
#ifdef WIZPLAYER_SFC
        if(sfc) {
            if(!sfc->canEditCharacters())return false;
            names=sfc->characterNames();
        } else
#endif
        {
            if(!wizardry.scenario||!wiiz_core_items_pause(core.get()))return false;
            names=wizardry.rosterNames(core.get(),bytes);
        }
        roster.clear();
        for(unsigned i=0;i<names.size();++i)
            if(!names[i].empty())roster.emplace_back(i,std::move(names[i]));
        character=0;
        return !roster.empty();
    };
    auto trait=[&](unsigned slot,int row) {
#ifdef WIZPLAYER_SFC
        if(sfc)return row==1?sfc->alignment(slot):sfc->characterTrait(slot,row==2);
#endif
        return row==1?wiiz_core_character_alignment(core.get(),slot):
                      wiiz_core_character_trait(core.get(),slot,row==2);
    };
    auto editCharacter=[&](unsigned slot,int row,int value) {
#ifdef WIZPLAYER_SFC
        if(sfc) {
            if(row==4)return sfc->learnSpells(slot);
            if(row==1)return sfc->setAlignment(slot,value);
            return sfc->setCharacterTrait(slot,row==2,value);
        }
#endif
        if(row==4)return wiiz_core_character_learn_spells(core.get(),
            reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size(),slot)!=0;
        if(row==1)return wiiz_core_set_character_alignment(core.get(),slot,value)!=0;
        return wiiz_core_set_character_trait(core.get(),slot,row==2,value)!=0;
    };
    const std::vector<std::wstring> traits[]={
        {L"善",L"中立",L"悪"},{L"人間",L"エルフ",L"ドワーフ",L"ノーム",L"ホビット"},
        {L"戦士",L"魔法使い",L"僧侶",L"盗賊",L"司教",L"侍",L"君主",L"忍者"}
    };
    int selectedSlot=std::clamp(static_cast<int>(setting(L"states",L"slot",1)),1,5);
    int row = Slot;
    bool menu = false;
    Overlay overlay = Overlay::Game;
    unsigned floor = 1, targetX = 0, targetY = 0;
    ConsoleInventory inventory;
    auto readItems=[&] {
#ifdef WIZPLAYER_SFC
        if(sfc)return sfc->items();
#endif
        return wizardry.items(core.get(),bytes);
    };
    auto transferItem=[&](unsigned from,unsigned item,unsigned to) {
#ifdef WIZPLAYER_SFC
        if(sfc)return sfc->transferItem(from,item,to);
#endif
        return wiiz_core_item_transfer(core.get(),reinterpret_cast<uint8_t*>(bytes.data()),
                                       bytes.size(),from,item,to);
    };
    auto editItem=[&](unsigned member,unsigned item,WiizItemAction action) {
#ifdef WIZPLAYER_SFC
        if(sfc)return sfc->editItem(member,item,action);
#endif
        return wiiz_core_item_edit(core.get(),reinterpret_cast<uint8_t*>(bytes.data()),
                                   bytes.size(),member,item,action);
    };
    bool releaseGame = false;
    std::wstring status;
    auto clearAudio = [&] { if (audio.id) SDL_ClearQueuedAudio(audio.id); };
    auto accessState = [&](int slot, bool load) {
        auto path = directory / currentRom.stem();
        path += L".slot" + std::to_wstring(slot) + L".state";
#ifdef WIZPLAYER_SFC
        if(sfc) {
            if(load) {
                if(!fs::exists(path)){status=L"保存がありません";return false;}
                if(!sfc->loadState(read(path)))throw std::runtime_error("SFC state load failed");
                setSpeed(speedHalf);
                applySupport();
                clearAudio();
            } else {
                const auto saved=sfc->state();
                write(path,saved.data(),saved.size());
                flush();
            }
            return true;
        }
#endif
        constexpr char mapTag[]="WZFCMAP1";
        constexpr size_t mapTail=WIIZ_MAP_BYTES+8;
        if (load) {
            if (!fs::exists(path)) { status = L"保存がありません"; return false; }
            auto saved=read(path);
            std::array<uint8_t,WIIZ_MAP_BYTES> explored{};
            const bool hasMap=wizardry.scenario&&saved.size()>=mapTail&&
                std::memcmp(saved.data()+saved.size()-8,mapTag,8)==0;
            if(hasMap) {
                std::copy_n(saved.end()-mapTail,WIIZ_MAP_BYTES,explored.begin());
                saved.resize(saved.size()-mapTail);
            }
            if(!turbo->loadState(saved))throw std::runtime_error("FC state load failed");
            if(hasMap)wiiz_core_map_set_explored(core.get(),explored.data());
            if(turbo->speed()!=static_cast<unsigned>(speedHalf))setSpeed(speedHalf);
            applySupport();
            clearAudio();
        } else {
            auto saved=turbo->saveState();
            if(wizardry.scenario) {
                const auto* explored=wiiz_core_map_explored(core.get());
                saved.insert(saved.end(),explored,explored+WIIZ_MAP_BYTES);
                saved.insert(saved.end(),mapTag,mapTag+8);
            }
            write(path,saved.data(),saved.size());
            flush();
        }
        return true;
    };
    auto chooseGame = [&]() -> std::optional<WindowsRom> {
        const auto next = selectRom(window, currentRom.parent_path());
        resetWindowInput();
        edges.previous.fill(true);
        releaseGame = true;
        deadline = Clock::now();
        if (!next) return std::nullopt;
        auto selected = readRomWithPatchChoice(window,*next);
        if(!selected)return std::nullopt;
        flush();
        if(isNesRom(*selected)||isSfcRom(*next,*selected)) {
            // Validate a new SFC ROM while retaining the current session on failure.
            validateConsole(*next,*selected,saves);
        } else validateGb(*next,*selected,saves);
        return WindowsRom{*next,std::move(*selected)};
    };
    const float* samples=nullptr;
    size_t sampleCount=0;
    while (pumpWindowMessages()) {
        Buttons input = keyboard();
        const bool focused = GetForegroundWindow() == window && !IsIconic(window);
        if(sfcMode&&focused) {
            if(GetAsyncKeyState('Q')&0x8000)input.game|=256;
            if(GetAsyncKeyState('W')&0x8000)input.game|=512;
            if(GetAsyncKeyState('S')&0x8000)input.game|=swapXY?2048:1024;
            if(GetAsyncKeyState('A')&0x8000)input.game|=swapXY?1024:2048;
        }
        controllers.poll(input,swapXY,true,sfcMode);
        if (!focused) input = {};
        const auto pressed = edges.update(input);
        try {
            if (pressed[RomSelect]) {
                if (auto next = chooseGame()) return next;
            } else if (overlay != Overlay::Game) {
                if ((pressed[Cancel]&&overlay==Overlay::Map) || pressed[Menu] ||
                    (overlay == Overlay::Map && pressed[Map]) ||
                    (overlay == Overlay::Items && pressed[Items])) {
#ifdef WIZPLAYER_SFC
                    if(sfc&&overlay==Overlay::Map)sfc->closeMap();
#endif
                    overlay = Overlay::Game;
                    releaseGame = true;
                } else if (overlay == Overlay::Map) {
#ifdef WIZPLAYER_SFC
                    if(sfc) {
                        const unsigned keys=(pressed[Confirm]?1:0)|(pressed[Right]?16:0)|
                            (pressed[Left]?32:0)|(pressed[Up]?64:0)|(pressed[Down]?128:0)|
                            (pressed[FloorUp]?256:0)|(pressed[FloorDown]?512:0);
                        if(sfc->mapKey(keys)&&
                           MessageBoxW(window,L"スロット1に保存して移動しますか？",L"WizPlayer",
                                       MB_YESNO|MB_DEFBUTTON2)==IDYES) {
                            if(accessState(1,false)&&sfc->moveMap()) {
                                overlay=Overlay::Game;
                                releaseGame=true;
                            } else status=L"現在は移動できません";
                        }
                    } else
#endif
                    {
                        if (pressed[FloorUp]) floor = floor == 1 ? wizardry.floors.size() : floor - 1;
                        if (pressed[FloorDown]) floor = floor == wizardry.floors.size() ? 1 : floor + 1;
                        if (pressed[Left]) targetX = (targetX + 19) % 20;
                        if (pressed[Right]) targetX = (targetX + 1) % 20;
                        if (pressed[Up]) targetY = (targetY + 1) % 20;
                        if (pressed[Down]) targetY = (targetY + 19) % 20;
                        if(pressed[Confirm]&&
                           MessageBoxW(window,L"スロット1に保存して移動しますか？",L"WizPlayer",
                                       MB_YESNO|MB_DEFBUTTON2)==IDYES) {
                            if (accessState(1,false)&&wiiz_core_map_warp(core.get(), floor, targetX, targetY)) {
                                overlay = Overlay::Game;
                                releaseGame = true;
                            } else status = L"現在は移動できません";
                        }
                    }
                } else {
                    const unsigned keys=(pressed[Confirm]?1:0)|(pressed[Cancel]?2:0)|
                        (pressed[Right]?16:0)|(pressed[Left]?32:0)|(pressed[Up]?64:0)|
                        (pressed[Down]?128:0)|(pressed[FloorUp]?256:0)|(pressed[FloorDown]?512:0);
                    if(keys&&!inventory.key(keys,-1,-1,readItems,transferItem,editItem)) {
                        overlay=Overlay::Game;
                        releaseGame=true;
                    }
                }
            } else if (!menu && isWizardry && (pressed[Map] || pressed[Items])) {
#ifdef WIZPLAYER_SFC
                if(sfc) {
                    status.clear();
                    if(pressed[Map]&&sfc->openMap())overlay=Overlay::Map;
                    if(pressed[Items]&&!pressed[Map]) {
                        if(inventory.show(readItems()))overlay=Overlay::Items;
                    }
                    if(overlay==Overlay::Game)status=L"現在は使用できません";
                    else clearAudio();
                } else
#endif
                if (wiiz_core_items_battle(core.get())) {
                    MessageBoxW(window, L"戦闘中は使用できません。", L"WizPlayer", MB_OK);
                } else {
                    clearAudio();
                    status.clear();
                    if (pressed[Map]) {
                        const unsigned position = wiiz_core_map_position(core.get());
                        const unsigned currentFloor = (position >> 16) & 255;
                        floor = currentFloor > 0 && currentFloor <= wizardry.floors.size() ? currentFloor : 1;
                        targetX = (position & 255) % 20;
                        targetY = ((position >> 8) & 255) % 20;
                        wiiz_core_map_record_position(core.get());
                        overlay = Overlay::Map;
                    } else {
                        if(inventory.show(readItems()))overlay=Overlay::Items;
                        else status=L"現在は使用できません";
                    }
                }
            } else if(menu&&page!=Page::Main) {
                const int direction=static_cast<int>(pressed[Right])-static_cast<int>(pressed[Left]);
                const int count=page==Page::Support?static_cast<int>(options.size()):5;
                if(pressed[Cancel]||pressed[Menu]) {
                    page=Page::Main;
                    status.clear();
                } else {
                    if(pressed[Up])detailRow=(detailRow+count-1)%count;
                    if(pressed[Down])detailRow=(detailRow+1)%count;
                    if(page==Page::Support&&(direction||pressed[Confirm])) {
                        auto& option=options[detailRow];
                        if(option.multiplier) {
                            constexpr std::array<int,7> factors{1,8,16,32,64,128,256};
                            const auto index=std::find(factors.begin(),factors.end(),option.value)-factors.begin();
                            option.value=factors[(index+(direction<0?6:1))%factors.size()];
                        } else option.value=!option.value;
                        applySupport();
                        setSetting(L"support",option.key,option.value);
                    }
                    if(page==Page::Characters) {
                        if(!detailRow&&direction)
                            character=(character+roster.size()+direction)%roster.size();
                        const auto slot=roster[character].first;
                        if(detailRow>0&&detailRow<4&&direction) {
                            const int current=trait(slot,detailRow);
                            const int values=static_cast<int>(traits[detailRow-1].size());
                            if(current>=0&&editCharacter(slot,detailRow,(current+values+direction)%values))flush();
                        }
                        if(detailRow==4&&pressed[Confirm]) {
                            if(editCharacter(slot,detailRow,0)) {
                                flush();
                                status=L"全呪文を習得しました";
                            } else status=L"現在は編集できません";
                        }
                    }
                }
            } else if (pressed[Menu] || (menu && pressed[Cancel])) {
                menu = !menu;
                row = Slot;
                status.clear();
                releaseGame = true;
                clearAudio();
            } else if (menu) {
                if (pressed[Up]) row = (row + MenuRows - 1) % MenuRows;
                if (pressed[Down]) row = (row + 1) % MenuRows;
                const int direction = static_cast<int>(pressed[Right]) - static_cast<int>(pressed[Left]);
                if(row==Slot&&direction) {
                    selectedSlot=(selectedSlot+direction+4)%5+1;
                    setSetting(L"states",L"slot",selectedSlot);
                }
                if(row==Crt&&(direction||pressed[Confirm])) {
                    crtMode=(crtMode+(direction<0?2:1))%3;
                    setSetting(L"play",L"crt_mode",crtMode);
                }
                if(row==Speed&&(direction||pressed[Confirm])) {
                    const auto index=std::find(speeds.begin(),speeds.end(),speedHalf)-speeds.begin();
                    const int next=speeds[(index+(direction<0?6:1))%speeds.size()];
                    if(setSpeed(next)) {
                        speedHalf=next;
                        setSetting(L"play",L"speed_half",speedHalf);
                        clearAudio();
                    } else status=L"このROMは倍速に対応していません";
                }
                if ((row == SwapAB || row == SwapXY) && (direction || pressed[Confirm])) {
                    bool& option = row == SwapAB ? swap : swapXY;
                    setSetting(L"controls",row==SwapAB?L"swap_ab":L"swap_xy",!option);
                    option = !option;
                } else if (pressed[Confirm]) {
                    if (row == Save || row == Load) menu = !accessState(selectedSlot, row == Load);
                    if(row==Support&&!options.empty()) {
                        page=Page::Support;
                        detailRow=0;
                    }
                    if(row==Characters&&isWizardry) {
                        if(openCharacters()) {
                            page=Page::Characters;
                            detailRow=0;
                        } else status=L"編集できるキャラクターがいません";
                    }
                    if (row == Reset) {
                        flush();
#ifdef WIZPLAYER_SFC
                        if(sfc)sfc->reset();else
#endif
                        turbo->reset(skipLogo);
                        applySupport();
                        clearAudio();
                        menu = false;
                    }
                    if (row == SelectRom) {
                        if (auto next = chooseGame()) return next;
                    }
                    if (row == Resume) menu = false;
                    releaseGame = true;
                }
            } else {
                if (pressed[Save1]) accessState(1, false);
                if (pressed[Load1]) {
                    if (!accessState(1, true)) MessageBoxW(window, status.c_str(), L"WizPlayer", MB_OK);
                    releaseGame = true;
                }
            }
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            status = L"操作に失敗しました";
            if (!menu) MessageBoxW(window, status.c_str(), L"WizPlayer", MB_OK | MB_ICONERROR);
        }
        if (!input.game) releaseGame = false;
        if (overlay == Overlay::Map) {
#ifdef WIZPLAYER_SFC
            if(sfc) {
                const auto map=sfc->mapFrame();
                SDL_UpdateTexture(screen.get(),nullptr,map.data(),width*sizeof(uint32_t));
            } else
#endif
            {
                renderNesMap(core.get(),wizardry,floor,targetX,targetY,pixels);
                for(auto& color:pixels)
                    color=(color&0xff00ff00u)|((color&0xffu)<<16)|((color>>16)&0xffu);
                SDL_UpdateTexture(screen.get(),nullptr,pixels.data(),width*sizeof(uint32_t));
            }
        } else if (overlay == Overlay::Items) {
            menuScreen.drawInventory(screen.get(),frameHeight,inventory);
        } else if (!menu) {
            const unsigned buttons=releaseGame?0:swapAB(input.game,swap);
#ifdef WIZPLAYER_SFC
            if(sfc) {
                sfc->step(buttons);
                SDL_UpdateTexture(screen.get(),nullptr,sfc->pixels.data(),width*sizeof(uint32_t));
            } else
#endif
            {
                samples=turbo->step(padInput(buttons),&sampleCount);
                wiiz_core_pixels(core.get(),pixels.data());
                SDL_UpdateTexture(screen.get(),nullptr,pixels.data(),width*sizeof(uint32_t));
            }
        }
        SDL_RenderClear(renderer.get());
        SDL_RenderCopy(renderer.get(), screen.get(), nullptr, nullptr);
        SDL_RenderPresent(renderer.get());
        if (!IsIconic(window)) {
            std::vector<std::wstring> lines = menu ? std::vector<std::wstring>{
                L"スロット : " + std::to_wstring(selectedSlot) + L" / 5  （左右で選択）",
                L"ステート保存", L"ステート読込", std::wstring(L"A / B 入れ替え ") + (swap ? L"ON" : L"OFF"),
                std::wstring(L"X / Y 入れ替え ") + (swapXY ? L"ON" : L"OFF"), L"速度 " + (speedHalf==3?std::wstring(L"1.5"):std::to_wstring(speedHalf/2)) + L"倍",
                std::wstring(L"ブラウン管  ")+(crtMode==0?L"OFF":crtMode==1?L"弱":L"標準"),L"サポート", L"キャラクター編集", L"リセット", L"ROM選択", L"ゲームに戻る"
            } : std::vector<std::wstring>{};
            if(menu&&options.empty())lines[Support]+=L"（対象外）";
            if(menu&&!isWizardry)lines[Characters]+=L"（対象外）";
            const wchar_t* heading=L"機能メニュー";
            int highlight=row;
            std::string characterGlyphs;
            if(menu&&page==Page::Support) {
                heading=L"サポート";
                highlight=detailRow;
                lines.clear();
                for(const auto& option:options)
                    lines.push_back(std::wstring(option.label)+L"  "+(option.multiplier?
                        L"×"+std::to_wstring(option.value):(option.value?L"ON":L"OFF")));
            }
            if(menu&&page==Page::Characters) {
                heading=L"キャラクター編集";
                highlight=detailRow;
                const auto& selected=roster[character];
                characterGlyphs=selected.second;
                lines={L"対象  "+std::to_wstring(selected.first+1),L"属性",L"種族",L"職業",L"全呪文習得"};
                for(int i=1;i<4;++i) {
                    const int value=trait(selected.first,i);
                    if(value>=0&&static_cast<size_t>(value)<traits[i-1].size())
                        lines[i]+=L"  "+traits[i-1][value];
                }
            }
            auto caption=status;
            if(overlay==Overlay::Map&&caption.empty()) {
                int level=floor,region=0;
#ifdef WIZPLAYER_SFC
                if(sfc) {level=sfc->selectedFloor();region=sfc->selectedRegion();}
#endif
                caption=L"B"+std::to_wstring(level);
                if(region)caption+=L"-"+std::to_wstring(region);
                caption+=L"   Q/W・L/R 階層   A 移動   B 戻る";
            }
            present(window,surface.get(),aspectHeight,lines,highlight,caption,heading,characterGlyphs,overlay==Overlay::Game?crtMode:0);
        }
        if (overlay == Overlay::Game && !menu && audio.id &&
            SDL_GetQueuedAudioSize(audio.id) < static_cast<Uint32>(desired.freq * 4 / 10)) {
#ifdef WIZPLAYER_SFC
            if(sfc) {
                const auto& pcm=sfc->pcm;
                if(!pcm.empty())SDL_QueueAudio(audio.id,pcm.data(),static_cast<Uint32>(pcm.size()*sizeof(int16_t)));
            } else
#endif
            {
            size_t count = 0;
            const auto* samples = wiiz_core_audio(core.get(), &count);
            if (count) SDL_QueueAudio(audio.id, samples, static_cast<Uint32>(count * sizeof(float)));
            }
        }
        const auto now = Clock::now();
        if (now - lastFlush >= std::chrono::seconds(5)) {
            flush();
            lastFlush = now;
        }
        deadline += framePeriod;
        if (deadline < now - std::chrono::milliseconds(50)) deadline = now;
        std::this_thread::sleep_until(deadline);
    }
    flush();
    return std::nullopt;
}
