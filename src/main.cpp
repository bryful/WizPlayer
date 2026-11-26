#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <gambatte.h>
#include <filesystem>
#include <fstream>
#include "built_in_bugfix.h"
#include <iostream>
#include <vector>
#include <array>
#include <chrono>
#include <thread>
#include <cstring>
#include <algorithm>
#include "windows_shell.h"
#include "windows_resources.h"
#include "game.h"
#include "game_support.h"
#include "turbo_runner.h"
#include "dungeon_map.h"
#include "item_menu.h"
#include "nes/windows.h"
#include <optional>
namespace fs = std::filesystem;
std::array<bool,256> keyPulses{};
std::array<bool,ActionCount> menuPulses{};
enum WindowCommand { ChooseRom = 4100, OpenMenu, OpenMap, OpenItems };
std::optional<POINT> itemClick;
// The core's optional cartridge rumble hook. UI rumble is not enabled yet.
void cartridge_set_rumble(unsigned) {}
struct Input : gambatte::InputGetter {
    unsigned value=0;
    unsigned operator()() override { return value; }
};
Buttons keyboard() {
    Buttons result;
    auto held=[](int key) { return (GetAsyncKeyState(key)&0x8000)!=0||keyPulses[key]; };
    const int keys[]={'X','Z',VK_SPACE,VK_BACK,VK_RIGHT,VK_LEFT,VK_UP,VK_DOWN};
    for(int i=0;i<8;++i) if(held(keys[i])) result.game|=1u<<i;
    if(held(VK_RETURN)) result.game|=gambatte::InputGetter::A;
    if(held(VK_ESCAPE)) result.game|=gambatte::InputGetter::B;
    const bool load=held(VK_SHIFT)&&held(VK_F1);
    result.actions={held(VK_F2),!load&&(held('1')||held(VK_F1)),load,
        held(VK_UP),held(VK_DOWN),held(VK_LEFT),held(VK_RIGHT),held(VK_RETURN)||held('X'),held('Z')||held(VK_ESCAPE),held('2')||held(VK_F3),held('Q'),held('W'),held('B'),held('N'),held(VK_F4)};
    for(size_t i=0; i<menuPulses.size(); ++i) result.actions[i] |= menuPulses[i];
    keyPulses.fill(false);
    menuPulses.fill(false);
    return result;
}
std::wstring wide(const std::string& text){int length=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);std::wstring result(length,0);if(length)MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),length);return result;}

void drawWindowMenu(HDC dc, const RECT& client, const std::wstring& title,
                    const std::vector<std::wstring>& rows, int selected) {
    const RECT panel{12, 12, client.right - 12, std::min(client.bottom - 44, 408L)};
    SetDCBrushColor(dc, RGB(20, 24, 32));
    FillRect(dc, &panel, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    const HFONT font = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Yu Gothic UI");
    const auto previousFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(235, 235, 245));
    TextOutW(dc, 28, 22, title.c_str(), static_cast<int>(title.size()));
    constexpr int pageSize = 10;
    const int first = (selected / pageSize) * pageSize;
    for (int i = first; i < static_cast<int>(rows.size()) && i < first + pageSize; ++i) {
        const int y = 56 + (i - first) * 28;
        if (i == selected) {
            const RECT highlight{20, y, panel.right - 8, y + 27};
            SetDCBrushColor(dc, RGB(44, 64, 104));
            FillRect(dc, &highlight, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }
        RECT line{28, y, panel.right - 12, y + 27};
        DrawTextW(dc, rows[i].c_str(), static_cast<int>(rows[i].size()), &line,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    const wchar_t help[] = L"上下: 選択    左右: 変更    Enter / A: 決定    F2 / B: 戻る";
    SetTextColor(dc, RGB(180, 190, 200));
    TextOutW(dc, 28, panel.bottom - 36, help, static_cast<int>(std::size(help) - 1));
    SelectObject(dc, previousFont);
    DeleteObject(font);
}

void render(HWND window, const std::array<gambatte::video_pixel_t,160*144>& pixels,
            const BITMAPINFO& bitmap, bool menu, int row, int slot, bool swap, bool swapXY,
            const std::wstring& status, const fs::path& saves, const fs::path& rom,bool skipLogo,int speedHalf,int crtMode,const RewardSupport& rewards,const CreationSupport& creation,const MpSupport& mp,const PartySupport& party,bool characterMenu,const SpellCharacter& selectedCharacter,SpellSupport& spells,gambatte::GB& gb,ItemMenu& items) {
    RECT r; GetClientRect(window,&r);
    if (r.right <= 0 || r.bottom <= 0) return;
    const LONG contentBottom=std::max(1L,r.bottom-44);
    const RECT viewport=gameViewport(window,160,144);
    const int scale=std::max(1,static_cast<int>(viewport.right-viewport.left)/160);
    HDC dc=GetDC(window);
    HDC buffer=CreateCompatibleDC(dc); HBITMAP image=CreateCompatibleBitmap(dc,std::max(1L,r.right),std::max(1L,r.bottom));
    HGDIOBJ old=SelectObject(buffer,image);
    PatBlt(buffer,0,0,r.right,r.bottom,BLACKNESS);
    StretchDIBits(buffer,viewport.left,viewport.top,viewport.right-viewport.left,viewport.bottom-viewport.top,0,0,160,144,pixels.data(),&bitmap,DIB_RGB_COLORS,SRCCOPY);
    drawScanlines(buffer,viewport,144,crtMode);
    SetBkMode(buffer,TRANSPARENT);
    if(menu) {
        std::vector<std::wstring> lines={L"スロット "+std::to_wstring(slot)+L" / 5  （左右で選択）",
            L"ステート保存",L"ステート読込",L"再開",std::wstring(L"A / B 入れ替え : ")+(swap?L"ON":L"OFF"),L"ROM選択",L"リセット",std::wstring(L"ロゴスキップ : ")+(skipLogo?L"ON":L"OFF"),
            L"速度 : "+(speedHalf==3?std::wstring(L"1.5"):std::to_wstring(speedHalf/2))+L"倍 （左右で変更・音声1倍）",
            L"獲得経験値 : "+(rewards.experienceSupported()?L"×"+std::to_wstring(rewards.experience()):std::wstring(L"対象外（外伝1〜3・GBC版I〜III）")),
            L"獲得お金 : "+(rewards.goldSupported()?L"×"+std::to_wstring(rewards.gold()):std::wstring(L"対象外（外伝1〜3・GBC版I〜III）")),
            L"作成ボーナス最大 : "+(creation.supported()?std::wstring(creation.enabled()?L"ON":L"OFF"):std::wstring(L"対象外（外伝1〜3・GBC版）")),L"キャラクターサポート",L"MP消費なし : "+(mp.supported()?std::wstring(mp.enabled()?L"ON":L"OFF"):std::wstring(L"対象外（外伝1）")),L"善悪混合 : "+(party.supported()?std::wstring(party.enabled()?L"ON":L"OFF"):std::wstring(L"対象外（外伝1）")),L"迷宮マップ・移動（F3 / RT）",std::wstring(L"X / Y 入れ替え : ")+(swapXY?L"ON":L"OFF"),L"パーティーのアイテム編集",
            std::wstring(L"ブラウン管  ")+(crtMode==0?L"OFF":crtMode==1?L"弱":L"標準")};
        if(characterMenu){
            auto choice=[](int index,const auto& labels){
                return index>=0?std::wstring(L"‹ ")+labels[index]+L" ›":std::wstring(L"対象外");
            };
            const wchar_t* alignments[]={L"善",L"中立",L"悪"};
            lines={L"対象  "+wide(selectedCharacter.name),L"全呪文習得",L"属性 : "+choice(spells.alignment(gb,selectedCharacter.slot),alignments)};
            if(spells.characterTraitCount(CharacterTrait::Race)){
                const wchar_t* races[]={L"人間",L"エルフ",L"ドワーフ",L"ノーム",L"ホビット",L"フェアリー",L"ドラコン",L"ラウルフ",L"ムーク",L"リズマン"};
                const wchar_t* jobs[]={L"戦士",L"魔法使い",L"僧侶",L"盗賊",L"ビショップ",L"サムライ",L"ロード",L"ニンジャ",L"アルケミスト",L"バード",L"レインジャー",L"バルキリー"};
                lines.push_back(L"種族 : "+choice(spells.characterTrait(gb,selectedCharacter.slot,CharacterTrait::Race),races));
                lines.push_back(L"職業 : "+choice(spells.characterTrait(gb,selectedCharacter.slot,CharacterTrait::Job),jobs));
            }
        }
        const std::wstring heading=characterMenu?L"キャラクター編集":L"機能メニュー";
        drawWindowMenu(buffer,r,heading,lines,row);
        if(!characterMenu) {
            auto path=saves/rom.stem(); path+=L".slot"+std::to_wstring(slot)+L".state";
            const auto* info=fs::exists(path)?L"選択スロット : 保存済み":L"選択スロット : 空";
            TextOutW(buffer,28,std::min(r.bottom-44,408L)-65,info,static_cast<int>(wcslen(info)));
        }
    }
    if(items.open){
        int left=viewport.left,top=viewport.top;
        RECT screen{left,top,left+160*scale,top+144*scale};FillRect(buffer,&screen,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        HPEN pen=CreatePen(PS_SOLID,std::max(1,scale/2),RGB(220,220,210));auto oldPen=SelectObject(buffer,pen);auto oldBrush=SelectObject(buffer,GetStockObject(NULL_BRUSH));
        Rectangle(buffer,left,top,left+80*scale,top+98*scale);Rectangle(buffer,left+80*scale,top,left+160*scale,top+98*scale);
        HFONT font=CreateFontW(-8*scale,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,NONANTIALIASED_QUALITY,FIXED_PITCH,L"ＭＳ ゴシック");auto oldFont=SelectObject(buffer,font);
        for(const auto& line:items.lines(gb)){
            RECT box{left+line.x*scale,top+line.y*scale,left+(line.x+line.w)*scale,top+(line.y+line.h)*scale};
            HBRUSH brush=CreateSolidBrush(line.selected?RGB(230,230,215):RGB(0,0,0));FillRect(buffer,&box,brush);DeleteObject(brush);
            if(WizardryItemText::raster(line.text)){
                unsigned columns=std::min<unsigned>((line.text.size()-5)/16,line.w/8),width=columns*8;
                if(width){
                    std::vector<unsigned> glyphs(width*8,line.selected?0xe6e6d7:0);
                    for(unsigned c=0;c<columns;c++)for(unsigned y=0;y<8;y++){
                        unsigned bits=WizardryItemText::row(line.text,c,y);
                        for(unsigned x=0;x<8;x++)if(bits&(128u>>x))glyphs[y*width+c*8+x]=line.selected?0:0xe6e6d7;
                    }
                    BITMAPINFO glyphInfo=bitmap;glyphInfo.bmiHeader.biWidth=width;glyphInfo.bmiHeader.biHeight=-8;
                    StretchDIBits(buffer,box.left,box.top+(line.h-8)*scale/2,width*scale,8*scale,0,0,width,8,glyphs.data(),&glyphInfo,DIB_RGB_COLORS,SRCCOPY);
                }
            }else{
                SetTextColor(buffer,line.selected?RGB(0,0,0):RGB(230,230,215));auto text=wide(line.text);
                DrawTextW(buffer,text.c_str(),static_cast<int>(text.size()),&box,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
            }
        }
        SelectObject(buffer,oldFont);DeleteObject(font);SelectObject(buffer,oldBrush);SelectObject(buffer,oldPen);DeleteObject(pen);
    }
    if(!status.empty()) {
        RECT bar{8,contentBottom+4,r.right-8,r.bottom-4};
        FillRect(buffer,&bar,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        SetTextColor(buffer,RGB(255,230,150));
        HFONT font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"Yu Gothic UI");
        auto oldFont=SelectObject(buffer,font);
        DrawTextW(buffer,status.c_str(),static_cast<int>(status.size()),&bar,DT_WORDBREAK|DT_END_ELLIPSIS|DT_NOPREFIX);
        SelectObject(buffer,oldFont);DeleteObject(font);
    }
    BitBlt(dc,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY);
    SelectObject(buffer,old); DeleteObject(image); DeleteDC(buffer); ReleaseDC(window,dc);
}
std::optional<fs::path> selectRom(HWND owner,const fs::path& initialFolder) {
    wchar_t filename[32768]{};
    auto initial=initialFolder.wstring();
    OPENFILENAMEW dialog{sizeof(dialog)}; dialog.hwndOwner=owner;
    dialog.lpstrTitle=L"ROM選択";
#ifdef WIZPLAYER_SFC
    dialog.lpstrFilter=L"対応ROM (*.nes;*.fc;*.gb;*.gbc;*.sfc;*.smc)\0*.nes;*.fc;*.gb;*.gbc;*.sfc;*.smc\0すべてのファイル (*.*)\0*.*\0\0";
#else
    dialog.lpstrFilter=L"対応ROM (*.nes;*.fc;*.gb;*.gbc)\0*.nes;*.fc;*.gb;*.gbc\0すべてのファイル (*.*)\0*.*\0\0";
#endif
    dialog.lpstrFile=filename; dialog.nMaxFile=32768; dialog.lpstrInitialDir=initial.c_str();
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetOpenFileNameW(&dialog)) return fs::path(filename);
    if(CommDlgExtendedError()!=0) throw std::runtime_error("ROM selection dialog failed");
    return std::nullopt;
}
void write(const fs::path& path, const void* data, size_t size) {
    auto temp = path; temp += L".tmp";
    std::ofstream f(temp, std::ios::binary|std::ios::trunc);
    f.write(static_cast<const char*>(data), size); f.close();
    if(!f) throw std::runtime_error("Save write failed");
    if(!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Save replace failed");
}
LRESULT CALLBACK windowProc(HWND w, UINT m, WPARAM a, LPARAM b) {
    if(m==WM_LBUTTONDOWN)itemClick=POINT{static_cast<short>(LOWORD(b)),static_cast<short>(HIWORD(b))};
    if(m==WM_KEYDOWN && a<256 && !(b&(1LL<<30))) keyPulses[a]=true;
    if(m==WM_GETMINMAXINFO) {
        auto info=reinterpret_cast<MINMAXINFO*>(b); info->ptMinTrackSize={520,530}; return 0;
    }
    if(m==WM_COMMAND && HIWORD(a)==0 && b==0) {
        switch(LOWORD(a)) {
            case ChooseRom: menuPulses[RomSelect]=true; return 0;
            case OpenMenu: menuPulses[Menu]=true; return 0;
            case OpenMap: menuPulses[Map]=true; return 0;
            case OpenItems: menuPulses[Items]=true; return 0;
        }
    }
    if(m==WM_ERASEBKGND) return 1;
    if(m==WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(w,m,a,b);
}
bool pumpWindowMessages() {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) return false;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return true;
}

void resetWindowInput() {
    keyPulses.fill(false);
    menuPulses.fill(false);
    itemClick.reset();
}

void setGameWindow(HWND window, const fs::path& rom, bool wizardry) {
    SetWindowTextW(window, (L"WizPlayer | " + rom.filename().wstring()).c_str());
    const auto menu = GetMenu(window);
    const UINT enable = wizardry ? MF_ENABLED : MF_GRAYED;
    EnableMenuItem(menu, OpenMap, MF_BYCOMMAND | enable);
    EnableMenuItem(menu, OpenItems, MF_BYCOMMAND | enable);
    DrawMenuBar(window);
}

void validateGb(const fs::path& rom, const std::vector<char>& bytes, const fs::path& saves) {
    Input input;
    auto core = loadCoreBytes(bytes, &input);
    restoreBattery(*core, saves / (rom.stem().wstring() + L".sav"));
}

std::optional<WindowsRom> runGb(HWND window, fs::path rom, std::vector<char> romData,
                              const fs::path& saves, bool smoke) {
    Input input;auto profile=identifyGame(romData);
    auto gb=loadCoreBytes(romData,&input);
    std::cout << "ROM: " << rom.filename().u8string() << " | "
              << (gb->isCgb() ? "GBC" : "GB") << "\n";
    std::array<gambatte::video_pixel_t,160*144> pixels{};
    std::array<gambatte::uint_least32_t,40000> sound{};
    // Headless smoke check does not touch user save files.
    if(smoke) {
        for(int i=0;i<180;++i) { unsigned samples=35112; gb->runFor(pixels.data(),160,sound.data(),sound.size(),samples); }
        std::vector<char> state(gb->stateSize()); gb->saveState(state.data());
        if(!gb->loadState(state.data(),state.size())) throw std::runtime_error("State round trip failed");
        bool varied=false; for(auto p:pixels) if(p!=pixels[0]) varied=true;
        if(!varied) throw std::runtime_error("No varied frame produced");
        std::cout << "PASS: 180 frames, video output, state round trip; mode=" << (gb->isCgb()?"GBC":"GB") << "\n";
        return std::nullopt;
    }
    fs::create_directories(saves);
    const auto settingsDirectory=saves/L"settings"/(gb->isCgb()?L"GBC":L"GB");
    fs::create_directories(settingsDirectory);
    const auto settings=settingsDirectory/(rom.stem().wstring()+L".ini");
    const auto legacySettings=saves/L"settings.ini";
    auto setting=[&](const wchar_t* group,const wchar_t* key,int fallback) {
        return GetPrivateProfileIntW(group,key,
            GetPrivateProfileIntW(group,key,fallback,legacySettings.c_str()),settings.c_str());
    };
    bool skipLogo=setting(L"support",L"skip_logo",1)!=0;
    fs::path save=saves/rom.stem(); save += L".sav";
    restoreBattery(*gb,save);
    auto skipped=skipLogo?skipBootLogos(gb,profile,romData,&input,pixels):BootSkipResult{};
    TurboRunner turbo;turbo.attach(*gb,romData,profile);
    DungeonMap dungeonMap;dungeonMap.attach(romData);ItemMenu items;items.attach(romData);
    unsigned experienceMultiplier=setting(L"support",L"experience_multiplier",1);
    unsigned goldMultiplier=setting(L"support",L"gold_multiplier",1);
    if(!RewardSupport::valid(experienceMultiplier))experienceMultiplier=1;
    if(!RewardSupport::valid(goldMultiplier))goldMultiplier=1;
    turbo.rewards().set(turbo.rewards().experienceSupported()?experienceMultiplier:1,turbo.rewards().goldSupported()?goldMultiplier:1);
    bool maxBonus=setting(L"support",L"max_bonus",0)!=0;
    turbo.creation().enable(maxBonus);
    bool noMpCost=setting(L"support",L"no_mp_cost",0)!=0;
    turbo.mp().enable(noMpCost);
    bool mixedParty=setting(L"support",L"mixed_party",0)!=0;
    turbo.party().enable(mixedParty);
    int speedHalf=setting(L"play",L"speed_half",2);
    if(!turbo.speed(speedHalf))speedHalf=2;
    std::cout<<"Profile: "<<profile.name<<" | boot frames skipped: "<<skipped.frames<<"\n";
    BITMAPINFO bitmap{}; bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth=160; bitmap.bmiHeader.biHeight=-144;
    bitmap.bmiHeader.biPlanes=1; bitmap.bmiHeader.biBitCount=32; bitmap.bmiHeader.biCompression=BI_RGB;
    SdlRuntime runtime; Controllers controllers; Audio audio;
    bool swap=setting(L"controls",L"swap_ab",0)!=0;
    bool swapXY=setting(L"controls",L"swap_xy",0)!=0;
    int crtMode=std::clamp(static_cast<int>(setting(L"play",L"crt_mode",0)),0,2);
    int slot=std::clamp(static_cast<int>(setting(L"states",L"slot",1)),1,5);
    bool menu=false, active=true, releaseGame=false;
    std::optional<WindowsRom> nextSystem;
    std::vector<SpellCharacter> spellCharacters;
    bool characterMenu=false;int characterRow=1;SpellCharacter selectedCharacter{};
    int row=0; Edges edges;
    std::wstring status=audio.available()?L"":L"音声デバイスが使えません。無音で続行します。";
    auto statusUntil=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    auto deadline=std::chrono::steady_clock::now(); auto lastFlush=deadline;
    auto flush=[&] { if(gb->savedata_size()) write(save,gb->savedata_ptr(),gb->savedata_size()); };
    auto notice=[&](std::wstring text) { status=std::move(text); statusUntil=std::chrono::steady_clock::now()+std::chrono::seconds(4); };
    auto persist=[&] {
        if(!WritePrivateProfileStringW(L"play",L"crt_mode",std::to_wstring(crtMode).c_str(),settings.c_str()))
            notice(L"設定の保存に失敗しました");
        if(!WritePrivateProfileStringW(L"controls",L"swap_xy",swapXY?L"1":L"0",settings.c_str()) ||
           !WritePrivateProfileStringW(L"controls",L"swap_ab",swap?L"1":L"0",settings.c_str()) ||
           !WritePrivateProfileStringW(L"states",L"slot",std::to_wstring(slot).c_str(),settings.c_str()))
            notice(L"設定の保存に失敗しました");
        if(!WritePrivateProfileStringW(L"support",L"skip_logo",skipLogo?L"1":L"0",settings.c_str()))notice(L"設定の保存に失敗しました");
        if(!WritePrivateProfileStringW(L"play",L"speed_half",std::to_wstring(speedHalf).c_str(),settings.c_str()))notice(L"設定の保存に失敗しました");
        if(!WritePrivateProfileStringW(L"support",L"experience_multiplier",std::to_wstring(experienceMultiplier).c_str(),settings.c_str())||
           !WritePrivateProfileStringW(L"support",L"gold_multiplier",std::to_wstring(goldMultiplier).c_str(),settings.c_str()))notice(L"設定の保存に失敗しました");
        if(!WritePrivateProfileStringW(L"support",L"max_bonus",maxBonus?L"1":L"0",settings.c_str()))notice(L"設定の保存に失敗しました");
        if(!WritePrivateProfileStringW(L"support",L"no_mp_cost",noMpCost?L"1":L"0",settings.c_str()))notice(L"設定の保存に失敗しました");
        if(!WritePrivateProfileStringW(L"support",L"mixed_party",mixedParty?L"1":L"0",settings.c_str()))notice(L"設定の保存に失敗しました");
    };
    auto stateAction=[&](int chosen,bool load) {
        auto path=saves/rom.stem(); path+=L".slot"+std::to_wstring(chosen)+L".state";
        try {
            if(load) {
                if(!fs::exists(path)) { notice(L"このスロットには保存がありません"); return false; }
                auto state=read(path);
                if(!turbo.loadState(state)) {
                    notice(L"ステートを読み込めません。現在の状態を保持しました"); return false;
                }
                if(turbo.speed()!=speedHalf)turbo.speed(speedHalf);
                audio.clear();
            } else {
                auto state=turbo.saveState();
                write(path,state.data(),state.size()); flush();
            }

            return true;
        } catch(const std::exception& e) {
            std::cerr<<e.what()<<"\n";
            notice(L"保存・読込に失敗しました。保存先を確認してください");
            return false;
        }
    };
    auto updateTitle=[&] {
        const auto id = profile.id;
        setGameWindow(window, rom, id == GameId::Gaiden1 || id == GameId::Gaiden2 || id == GameId::Gaiden3 ||
                                  id == GameId::Wizardry1 || id == GameId::Wizardry2 || id == GameId::Wizardry3);
    };
    updateTitle();
    auto chooseGame=[&] {
        try {
            auto chosen=selectRom(window,rom.parent_path());
            if(chosen) {
                // Validate a separate core first. A failed load never replaces the current game.
                auto selected=readRomWithPatchChoice(window,*chosen);
                if(!selected)return;
                auto& nextRomData=*selected;
                flush();
                if(isNesRom(nextRomData)||isSfcRom(*chosen,nextRomData))
                    validateConsole(*chosen,nextRomData,saves);
                else validateGb(*chosen,nextRomData,saves);
                audio.clear();
                input.value=0;
                nextSystem=WindowsRom{std::move(*chosen),std::move(nextRomData)};
                return;
            }
        } catch(const std::exception& e) {
            std::cerr<<e.what()<<"\n";
            notice(L"ROMを切り替えられません。元のゲームを保持しました");
        }
        keyPulses.fill(false); edges.previous.fill(true); releaseGame=true;
        deadline=std::chrono::steady_clock::now();
    };
    while(pumpWindowMessages()) {
        Buttons buttons=keyboard(); controllers.poll(buttons,swapXY);
        bool focused=GetForegroundWindow()==window && !IsIconic(window);
        auto pressed=edges.update(buttons);
        if(pressed[RomSelect]) {
            chooseGame();
            if(nextSystem) return nextSystem;
            continue;
        }
        if(!focused) {
            if(active) { audio.clear(); input.value=0; releaseGame=true; try { flush(); } catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; notice(L"通常セーブの保存に失敗しました"); } }
            active=false;
            std::this_thread::sleep_for(std::chrono::milliseconds(20)); deadline=std::chrono::steady_clock::now(); continue;
        }
        if(!active) { active=true; releaseGame=true; pressed.fill(false); }
        if(items.open){
            if(itemClick) {
                const RECT view = gameViewport(window, 160, 144);
                if (PtInRect(&view, *itemClick))
                    items.tap(*gb, turbo.spells(), (itemClick->x-view.left)*160/(view.right-view.left),
                              (itemClick->y-view.top)*144/(view.bottom-view.top));
            }
            if(pressed[Menu]||pressed[Items])items.open=false;
            else{unsigned bits=(pressed[Up]?64:0)|(pressed[Down]?128:0)|(pressed[Left]?32:0)|(pressed[Right]?16:0)|(pressed[Confirm]?1:0)|(pressed[Cancel]?2:0)|((pressed[FloorUp]||pressed[PreviousCharacter])?256:0)|((pressed[FloorDown]||pressed[NextCharacter])?512:0);items.key(*gb,turbo.spells(),bits);}
            releaseGame=true;audio.clear();
        }else if(dungeonMap.open){
            if(pressed[Menu]||pressed[Map])dungeonMap.open=false;
            else {unsigned bits=(pressed[Up]?64:0)|(pressed[Down]?128:0)|(pressed[Left]?32:0)|(pressed[Right]?16:0)|(pressed[Confirm]?1:0)|(pressed[Cancel]?2:0)|(pressed[FloorUp]?256:0)|(pressed[FloorDown]?512:0);
                if(dungeonMap.key(bits)) {
                    if(!dungeonMap.inDungeon(*gb))notice(L"現在の状態では移動できません");
                    else if(stateAction(1,false)) {
                        input.value=0;
                        auto frame=pixels;
                        if(moveDungeonMapAfterBackup(dungeonMap,*gb,[&] {
                            unsigned samples=35112;
                            gb->runFor(frame.data(),160,sound.data(),sound.size(),samples);
                        }))turbo.resetInput();
                        else notice(L"現在の状態では移動できません");
                    }
                }
            }releaseGame=true;audio.clear();
        }else if(pressed[Items]){
            if(items.show(*gb,turbo.spells())){menu=false;releaseGame=true;audio.clear();}
        }else if(pressed[Map]){if(dungeonMap.browse(*gb)){menu=false;releaseGame=true;audio.clear();}else notice(L"マップは対応するウィザードリィの迷宮・キャンプ中に使用できます（戦闘・画面更新中は不可）");
        }else if(pressed[Menu] || (menu&&pressed[Cancel]&&!characterMenu)) {
            characterMenu=false;
            menu=!menu; releaseGame=true; audio.clear();
        } else if(menu) {
          if(characterMenu){
            auto& spells=turbo.spells();
            int count=spells.characterTraitCount(CharacterTrait::Race)?5:3;
            if(pressed[Up])characterRow=(characterRow+count-1)%count;
            if(pressed[Down])characterRow=(characterRow+1)%count;
            if(pressed[Cancel])characterMenu=false;
            else if(characterRow==0&&(pressed[Left]||pressed[Right])) {
                const auto found=std::find_if(spellCharacters.begin(),spellCharacters.end(),
                    [&](const auto& member){return member.slot==selectedCharacter.slot;});
                const auto count=spellCharacters.size();
                const auto offset=found-spellCharacters.begin();
                selectedCharacter=spellCharacters[(offset+count+(pressed[Right]?1:count-1))%count];
            }
            else if((pressed[Confirm]&&characterRow==1)||(characterRow>=2&&(pressed[Left]||pressed[Right]))){
                bool applied=false;
                try{flush();const auto& target=selectedCharacter;
                    auto next=[&](int current,int choices){return (current+(pressed[Right]?1:choices-1))%choices;};
                    bool success;
                    if(characterRow==1)success=spells.learn(*gb,target.slot);
                    else if(characterRow==2){
                        int current=spells.alignment(*gb,target.slot);
                        success=current>=0&&spells.setAlignment(*gb,target.slot,next(current,3));
                    }else{
                        auto trait=characterRow==3?CharacterTrait::Race:CharacterTrait::Job;
                        int current=spells.characterTrait(*gb,target.slot,trait);
                        success=current>=0&&spells.setCharacterTrait(*gb,target.slot,trait,next(current,spells.characterTraitCount(trait)));
                    }
                    if(success){applied=true;flush();}
                    else notice(L"戦闘中・データ更新中は実行できません。操作を終えてから選び直してください");
                }catch(const std::exception& e){std::cerr<<e.what()<<"\n";notice(applied?L"変更は適用済みですが通常セーブを保存できません":L"通常セーブの保存に失敗したため適用を中止しました");}
            }
          }else{
            if(pressed[Up]) row=(row+18)%19;
            if(pressed[Down]) row=(row+1)%19;
            if(row==0&&(pressed[Left]||pressed[Right])) { slot=(slot-1+(pressed[Right]?1:4))%5+1; persist(); }
            if(row==4&&(pressed[Left]||pressed[Right]||pressed[Confirm])) { swap=!swap; persist(); }
            if(row==18&&(pressed[Left]||pressed[Right]||pressed[Confirm])) {
                crtMode=(crtMode+(pressed[Left]?2:1))%3;
                persist();
            }
            if(row==16&&(pressed[Left]||pressed[Right]||pressed[Confirm])) { swapXY=!swapXY; persist(); }
            if(row==7&&(pressed[Left]||pressed[Right]||pressed[Confirm])) {skipLogo=!skipLogo;persist();}
            if(row==8&&(pressed[Left]||pressed[Right])){
                const int speeds[]={2,3,4,6,8,10,20};auto current=std::find(std::begin(speeds),std::end(speeds),speedHalf)-std::begin(speeds);
                int next=speeds[(current+(pressed[Right]?1:6))%7];
                if(turbo.speed(next)){speedHalf=next;audio.clear();persist();}else notice(L"このROMは倍速に対応していません");
            }
            if((row==9||row==10)&&(pressed[Left]||pressed[Right])){
                if(!(row==9?turbo.rewards().experienceSupported():turbo.rewards().goldSupported()))notice(row==9?L"獲得経験値の倍率は対応版の外伝1〜3・GBC版I〜III・DQ3で使用できます":L"獲得お金の倍率は対応版の外伝1〜3・GBC版I〜III・DQ3で使用できます");
                else{
                    const unsigned values[]={1,8,16,32,64,128,256};
                    auto& current=row==9?experienceMultiplier:goldMultiplier;
                    auto index=std::find(std::begin(values),std::end(values),current)-std::begin(values);
                    current=values[(index+(pressed[Right]?1:6))%7];
                    turbo.rewards().set(turbo.rewards().experienceSupported()?experienceMultiplier:1,turbo.rewards().goldSupported()?goldMultiplier:1);persist();
                }
            }
            if(row==11&&(pressed[Left]||pressed[Right])){
                if(turbo.creation().enable(!maxBonus)){maxBonus=!maxBonus;persist();}
                else notice(L"作成ボーナス最大は対応版の外伝1〜3・GBC版Wizardry1〜3で使用できます");
            }
            if(row==13&&(pressed[Left]||pressed[Right])){
                if(turbo.mp().enable(!noMpCost)){noMpCost=!noMpCost;persist();}
                else notice(L"MP消費なしは対応版のGB外伝1〜3で使用できます");
            }
            if(row==14&&(pressed[Left]||pressed[Right])){
                if(turbo.party().enable(!mixedParty)){mixedParty=!mixedParty;persist();}
                else notice(L"善悪混合は対応版の外伝1〜3・GBC版Wizardry1〜3で使用できます");
            }
            if(pressed[Confirm]) {
                if(row==17){if(items.show(*gb,turbo.spells())){menu=false;releaseGame=true;audio.clear();}else notice(L"アイテム編集には１人以上のパーティーが必要です（戦闘・データ更新中は不可）");}
                if(row==15){if(dungeonMap.browse(*gb)){menu=false;releaseGame=true;audio.clear();}else notice(L"マップは対応するウィザードリィの迷宮・キャンプ中に使用できます");}
                if(row==3||((row==1||row==2)&&stateAction(slot,row==2))) {
                    menu=false;
                    releaseGame=true;
                    audio.clear();
                }
                if(row==5) { chooseGame(); if(nextSystem) return nextSystem; }
                if(row==6) {
                    try {
                        flush();turbo.detach();input.value=0;gb->reset();pixels.fill(0);if(skipLogo)skipBootLogos(gb,profile,romData,&input,pixels);
                        dungeonMap.attach(romData);items.attach(romData);turbo.attach(*gb,romData,profile);if(!turbo.speed(speedHalf))speedHalf=2;audio.clear();
                        turbo.rewards().set(turbo.rewards().experienceSupported()?experienceMultiplier:1,turbo.rewards().goldSupported()?goldMultiplier:1);
                        turbo.creation().enable(maxBonus);turbo.mp().enable(noMpCost);turbo.party().enable(mixedParty);
                        menu=false; row=0; releaseGame=true;

                    } catch(const std::exception& e) {
                        std::cerr<<e.what()<<"\n";
                        notice(L"通常セーブの保存に失敗したためリセットを中止しました");
                    }
                }
                if(row==12){
                    if(!turbo.spells().supported())notice(L"全呪文習得は対応版の外伝1〜3・GBC版I〜IIIで使用できます");
                    else if(!turbo.spells().available(*gb))notice(turbo.spells().learnsMaximumMp()?L"戦闘中またはデータ更新中は使用できません":L"戦闘中またはデータ更新中は使用できません");
                    else{spellCharacters=turbo.spells().characters(*gb);if(spellCharacters.empty())notice(L"登録キャラクターがいません");else{selectedCharacter=spellCharacters.front();characterMenu=true;characterRow=0;}}
                }
            }
          }
        } else {
            if(pressed[Save1]) stateAction(1,false);
            if(pressed[Load1]) { stateAction(1,true); releaseGame=true; }
        }
        itemClick.reset();
        if(!buttons.game) releaseGame=false;
        input.value=(!menu&&!dungeonMap.open&&!items.open&&!releaseGame)?swapAB(buttons.game,swap):0;
        if(!menu&&!dungeonMap.open&&!items.open) {unsigned held=input.value;unsigned samples=turbo.run(held,input.value,pixels,sound);audio.push(sound.data(),samples);}
        if(std::chrono::steady_clock::now()>statusUntil) status.clear();
        auto display=pixels;if(dungeonMap.open)drawDungeonMap(dungeonMap,*gb,display,true);
        if(!IsIconic(window)) render(window,display,bitmap,menu,characterMenu?characterRow:row,slot,swap,swapXY,status,saves,rom,skipLogo,speedHalf,dungeonMap.open||items.open?0:crtMode,turbo.rewards(),turbo.creation(),turbo.mp(),turbo.party(),characterMenu,selectedCharacter,turbo.spells(),*gb,items);
        deadline+=std::chrono::nanoseconds(16742706); std::this_thread::sleep_until(deadline);
        auto now=std::chrono::steady_clock::now(); if(now-lastFlush>std::chrono::seconds(10)) {
            try { flush(); } catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; notice(L"通常セーブの保存に失敗しました"); } lastFlush=now;
        }
        if(now-deadline>std::chrono::milliseconds(100)) deadline=now;
    }
    flush();
    return std::nullopt;
}

std::optional<std::vector<char>> readRomWithPatchChoice(HWND window,const fs::path& rom) {
    auto bytes=read(rom);
    const auto patch=builtInBugFix(bytes);
    if(patch.empty())return bytes;

    const auto prompt=rom.filename().wstring()+L" に内蔵バグ修正パッチを適用しますか？\n\n"
        L"はい = ON　いいえ = OFF";
    const int choice=MessageBoxW(window,prompt.c_str(),L"WizPlayer | IPSパッチ",
                                 MB_YESNOCANCEL|MB_ICONQUESTION|MB_DEFBUTTON2);
    if(choice!=IDYES&&choice!=IDNO)return std::nullopt;
    if(choice==IDYES)applyIpsPatch(bytes,patch);
    return bytes;
}

int wmain(int argc, wchar_t** argv) try {
    if (argc > 2 && std::wstring(argv[2]) == L"--smoke") {
        auto bytes = read(fs::absolute(argv[1]));
        if (isNesRom(bytes)||isSfcRom(argv[1],bytes)) smokeConsole(argv[1],bytes);
        else runGb(nullptr, argv[1], std::move(bytes), {}, true);
        return 0;
    }
    fs::path saves;
    for (int i = 2; i < argc; ++i)
        if (std::wstring(argv[i]) == L"--data-dir" && i + 1 < argc) saves = fs::absolute(argv[++i]);
    if (saves.empty()) {
        wchar_t roaming[MAX_PATH];
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, roaming)))
            throw std::runtime_error("AppData unavailable");
        saves = fs::path(roaming) / L"WizPlayer";
    }
    fs::create_directories(saves);

    const auto commonSettings=saves/L"settings.ini";
    auto rememberRomFolder=[&](const fs::path& rom) {
        WritePrivateProfileStringW(L"paths",L"rom_folder",rom.parent_path().c_str(),commonSettings.c_str());
    };
    fs::path rom;
    if (argc > 1) rom = argv[1];
    else {
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        auto defaultFolder=fs::path(executable).parent_path()/L"Rom";
        if(!fs::is_directory(defaultFolder)) {
            wchar_t documents[MAX_PATH]{};
            if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_PERSONAL,nullptr,0,documents))) {
                const auto documentsRom=fs::path(documents)/L"WizPlayer"/L"rom";
                defaultFolder=fs::is_directory(documentsRom)?documentsRom:fs::path(documents);
            }
        }
        wchar_t remembered[32768]{};
        GetPrivateProfileStringW(L"paths",L"rom_folder",L"",remembered,
                                 static_cast<DWORD>(std::size(remembered)),commonSettings.c_str());
        auto chosen=selectRom(nullptr,fs::is_directory(remembered)?fs::path(remembered):defaultFolder);
        if (!chosen) return 0;
        rom = *chosen;
    }
    rom = fs::absolute(rom);
    auto selected=readRomWithPatchChoice(nullptr,rom);
    if(!selected)return 0;
    auto bytes=std::move(*selected);
    rememberRomFolder(rom);

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"WizPlayer";
    windowClass.hIcon = LoadIconW(windowClass.hInstance, MAKEINTRESOURCEW(IDI_WIZPLAYER));
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassW(&windowClass)) throw std::runtime_error("Window registration failed");
    const auto menu = CreateMenu();
    if (!menu) throw std::runtime_error("Menu creation failed");
    AppendMenuW(menu, MF_STRING, ChooseRom, L"ROM選択");
    AppendMenuW(menu, MF_STRING, OpenMenu, L"機能メニュー (F2)");
    AppendMenuW(menu, MF_STRING, OpenMap, L"マップ (F3)");
    AppendMenuW(menu, MF_STRING, OpenItems, L"アイテム (F4)");
    HWND window = CreateWindowW(L"WizPlayer", L"WizPlayer", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 820, 840, nullptr, menu, windowClass.hInstance, nullptr);
    if (!window) {
        DestroyMenu(menu);
        throw std::runtime_error("Window creation failed");
    }
    try {
        while (true) {
            resetWindowInput();
            auto next = (isNesRom(bytes)||isSfcRom(rom,bytes)) ? runConsole(window, rom, bytes, saves) :
                                        runGb(window, rom, std::move(bytes), saves, false);
            if (!next) break;
            rom = std::move(next->path);
            rememberRomFolder(rom);
            bytes = std::move(next->bytes);
        }
    } catch (...) {
        if (IsWindow(window)) {
            MessageBoxW(window, L"ROMの起動または保存に失敗しました。", L"WizPlayer", MB_OK | MB_ICONERROR);
            DestroyWindow(window);
        }
        throw;
    }
    if (IsWindow(window)) DestroyWindow(window);
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; }
