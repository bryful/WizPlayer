#pragma once

#include "../windows_shell.h"

bool isNesRom(const std::vector<char>& bytes);
bool isSfcRom(const std::filesystem::path& rom,const std::vector<char>& bytes);
std::optional<std::vector<char>> readRomWithPatchChoice(HWND window,const std::filesystem::path& rom);
void validateConsole(const std::filesystem::path& rom, std::vector<char>& bytes,
                     const std::filesystem::path& saves);
std::optional<WindowsRom> runConsole(HWND window, const std::filesystem::path& rom,
    std::vector<char>& bytes, const std::filesystem::path& saves);
void smokeConsole(const std::filesystem::path& rom,std::vector<char>& bytes);
