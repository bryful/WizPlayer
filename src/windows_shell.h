#pragma once

#include <windows.h>
#include "desktop.h"
#include <filesystem>
#include <optional>
#include <string>

struct WindowsRom {
    std::filesystem::path path;
    std::vector<char> bytes;
};

Buttons keyboard();
bool pumpWindowMessages();
void resetWindowInput();
void setGameWindow(HWND window, const std::filesystem::path& rom, bool wizardry);
std::optional<std::filesystem::path> selectRom(HWND owner, const std::filesystem::path& folder);
void validateGb(const std::filesystem::path& rom, const std::vector<char>& bytes,
                const std::filesystem::path& saves);
void drawWindowMenu(HDC dc, const RECT& client, const std::wstring& title,
                    const std::vector<std::wstring>& rows, int selected);

inline RECT gameViewport(HWND window, int width, int height) {
    RECT client{};
    GetClientRect(window, &client);
    const int available = std::max(1, static_cast<int>(client.bottom) - 44);
    const int scale = std::min(static_cast<int>(client.right) / width, available / height);
    int drawWidth = width * scale;
    int drawHeight = height * scale;
    if (scale == 0) {
        const double fit = std::min(static_cast<double>(client.right) / width,
                                    static_cast<double>(available) / height);
        drawWidth = std::max(1, static_cast<int>(width * fit));
        drawHeight = std::max(1, static_cast<int>(height * fit));
    }
    const int x = (client.right - drawWidth) / 2;
    const int y = (available - drawHeight) / 2;
    return {x, y, x + drawWidth, y + drawHeight};
}


inline void drawScanlines(HDC dc,const RECT& viewport,int rows,int mode) {
    const int height=viewport.bottom-viewport.top;
    if(mode==0||height<rows*2)return;
    const int step=mode==1?2:1;
    for(int row=step;row<=rows;row+=step)
        PatBlt(dc,viewport.left,viewport.top+row*height/rows-1,
               viewport.right-viewport.left,1,BLACKNESS);
}
