#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <vector>

inline void applyIpsPatch(std::vector<char>& rom, std::string_view patch) {
    if (patch.size() < 8 || std::memcmp(patch.data(), "PATCH", 5) != 0)
        throw std::runtime_error("IPSヘッダーが不正です");

    std::size_t cursor = 5;
    auto readNumber = [&](unsigned count) {
        if (count > patch.size() - cursor)
            throw std::runtime_error("IPSデータが途中で終わっています");
        std::size_t value = 0;
        while (count-- > 0) value = (value << 8) | static_cast<unsigned char>(patch[cursor++]);
        return value;
    };

    auto patched = rom;
    while (true) {
        const auto offset = readNumber(3);
        if (offset == 0x454f46) break; // EOF
        const auto length = readNumber(2);
        const auto count = length ? length : readNumber(2);
        if (count == 0 || offset > patched.size() || count > patched.size() - offset)
            throw std::runtime_error("IPSの書き込み範囲がROMの範囲外です");
        if (length) {
            if (length > patch.size() - cursor)
                throw std::runtime_error("IPSデータが途中で終わっています");
            std::copy_n(patch.data() + cursor, length, patched.data() + offset);
            cursor += length;
        } else {
            const auto value = static_cast<char>(readNumber(1));
            std::fill_n(patched.data() + offset, count, value);
        }
    }
    if (patch.size() - cursor == 3 && readNumber(3) != rom.size())
        throw std::runtime_error("ROMサイズの変更を伴うIPSには対応していません");
    if (cursor != patch.size())
        throw std::runtime_error("IPSの終端が不正です");
    rom.swap(patched);
}
