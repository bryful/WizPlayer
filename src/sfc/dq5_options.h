#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
namespace dq5 {
// Native branch and BRK patches from SnesDQ5, applied to the loaded ROM buffer.
enum GameOption : unsigned { Recruit=1, Drop=2, Equipment=4, Items=8, Level=16, AllOptions=31 };
struct OptionByte { unsigned feature; uint32_t address; uint8_t original,value; };
inline constexpr OptionByte optionBytes[]{
    {Recruit,0x20c27d,0xd0,0xf0},{Recruit,0x20c27e,0xc0,0x38},
    {Recruit,0x20c29a,0xd0,0xf0},{Recruit,0x20c29b,0xa3,0x1b},
    {Recruit,0x20c2b7,0xd0,0xf0},{Recruit,0x20c2b8,0x86,0x00},
    {Drop,0x2589a2,0xf0,0x80},{Equipment,0x21a562,0x91,0x8a},
    // Skip the removal call after the original consumable-item predicate.
    // Keep the effect dispatch and shared sell/discard/transfer routines intact.
    {Items,0x1188d9,0x90,0x80},{Items,0x20ba61,0x90,0x80},
    {Level,0x23eb5c,0x00,0x22},{Level,0x23eb5d,0x89,0x00},
    {Level,0x23eb5e,0x80,0xfd},{Level,0x23eb5f,0x28,0x0b},
    {Level,0x23ed70,0x00,0x22},{Level,0x23ed71,0x89,0x00},
    {Level,0x23ed72,0x80,0xfd},{Level,0x23ed73,0x28,0x0b}
};
inline constexpr size_t romOffset(uint32_t address) {
    return ((address>>16)&0x7f)*0x8000+(address&0x7fff);
}
inline bool optionProfileMatches(const uint8_t* rom,size_t size) {
    if(size<0x180000) return false;
    for(const auto& b:optionBytes) {
        if(rom[romOffset(b.address)]!=b.original) return false;
    }
    if(!std::all_of(rom+0x5fd00,rom+0x5fd80,[](uint8_t b){return b==0;})) return false;
    return true;
}
inline void applyOptionPatches(uint8_t* rom,unsigned flags) {
    for(const auto& b:optionBytes) rom[romOffset(b.address)]=(flags&b.feature)||b.feature==Level?b.value:b.original;
    {
        // Reuse the game's own cap/species queries. Only monster species $80..$a9
        // receive a cap of 99; cached character data and equipment flags are untouched.
        // Keep the trampoline installed when OFF: a snapshot may be inside its BRK
        // handler. Only the separate enable byte changes, so return addresses stay valid.
        const uint8_t code[]{
            0x08,0x48,0x00,0x89,0x80,0x28,0xaf,0x7f,0xfd,0x0b,0xf0,0x1b,0xa5,0x46,0x48,
            0x00,0x89,0x80,0x2a,0xa5,0x46,0xc9,0x80,0x90,0x0b,
            0xc9,0xaa,0xb0,0x07,0x68,0xa9,0x63,0x85,0x46,0x80,0x03,
            0x68,0x85,0x46,0x68,0x28,0x6b
        };
        rom[romOffset(0x0bfd7f)]=(flags&Level)?1:0;
        std::copy_n(code,sizeof(code),rom+romOffset(0x0bfd00));
    }
}
}
