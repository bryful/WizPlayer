#pragma once

#include "core_bridge.h"
#include "../wizardry_inventory.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

struct WizardryFloor {
    std::array<uint8_t, 400> walls{};
};

struct WizardryFeatures {
    unsigned scenario = 0;
    std::vector<WizardryFloor> floors;

    explicit WizardryFeatures(const std::vector<char>& rom) {
        if (rom.size() < 16 || rom[4] != 8 || rom[5] != 16) return;
        const size_t trainer = (rom[6] & 4) ? 512 : 0;
        const size_t prg = 16 + trainer;
        if (rom.size() < prg + 0x40000) return;
        const unsigned mapper = (static_cast<uint8_t>(rom[6]) >> 4) | (rom[7] & 0xf0);
        auto matches = [&](size_t at, std::initializer_list<uint8_t> signature) {
            return std::equal(signature.begin(), signature.end(), rom.begin() + at + trainer,
                              [](uint8_t a, char b) { return a == static_cast<uint8_t>(b); });
        };
        if (mapper == 1 && matches(0x1ae7f, {0xa6, 0x4e, 0xbd, 0xdc, 0x7d})) scenario = 1;
        if (mapper == 4 && matches(0x1bdfd, {0xa6, 0x0b, 0xa0, 0x22}) &&
            matches(0x1d06a, {0xa9, 0x0a, 0x85, 0x02})) scenario = 2;
        if (mapper == 4 && matches(0x19e4d, {0xa6, 0x0b, 0xa0, 0x22}) &&
            matches(0x1d06c, {0xa9, 0x0a, 0x85, 0x02})) scenario = 3;
        if (!scenario) return;

        const size_t base = prg + (scenario == 1 ? 0x10000 : 0x12000);
        const int count = scenario == 1 ? 10 : 6;
        for (int floor = 1; floor <= count; ++floor) {
            const size_t pointer = base + (scenario == 1 ? 0x5c : 0) + floor * 2;
            const unsigned address = static_cast<uint8_t>(rom[pointer]) |
                (static_cast<unsigned>(static_cast<uint8_t>(rom[pointer + 1])) << 8);
            if (address < 0x8000 || base + address - 0x8000 + 400 > prg + 0x20000) {
                floors.clear();
                scenario = 0;
                return;
            }
            WizardryFloor data;
            std::copy_n(rom.begin() + base + address - 0x8000, 400, data.walls.begin());
            floors.push_back(data);
        }
    }

    std::vector<WizardryPartyMember> items(WiizCore* core,const std::vector<char>& rom) const {
        if (!scenario || !wiiz_core_items_pause(core))
            throw std::runtime_error("Inventory is unavailable");
        std::array<uint8_t, 8192> ram{};
        if (wiiz_core_sram_size(core) != ram.size() || !wiiz_core_sram_read(core, ram.data(), ram.size()))
            throw std::runtime_error("Cannot read inventory");
        const unsigned count = ram[scenario == 1 ? 0x1def : 0xb81];
        const unsigned list = scenario == 1 ? 0x1ddc : 0xb82;
        if (!count || count > 6) return {};
        std::vector<WizardryPartyMember> result;
        for (unsigned member = 0; member < count; ++member) {
            const int ref = ram[list + member] - (scenario == 1 ? 0x60 : 0);
            if (ref < 0 || ref >= 20) throw std::runtime_error("Invalid party reference");
            const auto* record = ram.data() + (scenario == 1 ? ref * 256 : 0x400 + ref * 96);
            const unsigned size = record[scenario == 1 ? 0x5c : 0x53];
            const unsigned flags = scenario == 1 ? 0x4c : 0x43;
            if (!record[0] || size > 8) throw std::runtime_error("Invalid inventory record");
            auto& character = result.emplace_back();
            character.name=glyphs(rom,record+2,std::min<unsigned>(record[1],8));
            for (unsigned item = 0; item < size; ++item) {
                const auto id = record[flags + 8 + item];
                if (id >= (scenario == 1 ? 101 : scenario == 2 ? 110 : 138))
                    throw std::runtime_error("Invalid item ID");
                const auto state=record[flags+item];
                auto codes=itemCodes(rom,id,(state&32)!=0);
                auto name=glyphs(rom,codes.data(),codes.size());
                std::wstring status;
                if (state&128) status+=L"装備中 ";
                if (state&64) status+=L"呪い ";
                if (state&32) status+=L"未鑑定";
                character.items.push_back({std::move(name),L"アイテム "+std::to_wstring(id),std::move(status),state});
            }
        }
        return result;
    }

    std::array<std::string,20> rosterNames(WiizCore* core,const std::vector<char>& rom) const {
        std::array<std::string,20> names;
        std::array<uint8_t,8192> ram{};
        if(!scenario||!wiiz_core_items_pause(core)||wiiz_core_sram_size(core)!=ram.size()||
           !wiiz_core_sram_read(core,ram.data(),ram.size()))return names;
        for(size_t slot=0;slot<names.size();++slot) {
            const auto* record=ram.data()+(scenario==1?slot*256:0x400+slot*96);
            if(record[0]&&record[1]&&record[1]<=8)names[slot]=glyphs(rom,record+2,record[1]);
        }
        return names;
    }

private:
    std::vector<uint8_t> itemCodes(const std::vector<char>& rom,unsigned id,bool unknown) const {
        const size_t trainer=(rom[6]&4)?512:0;
        const size_t prgEnd=16+trainer+0x20000;
        auto byte=[&](size_t at){return static_cast<uint8_t>(rom[at]);};
        auto pointer=[&](size_t at,size_t bank) -> size_t {
            if(at+1>=prgEnd)return 0;
            const unsigned word=byte(at)|(byte(at+1)<<8);
            return word>=0x8000&&word<0xc000?bank+word-0x8000:0;
        };
        auto readName=[&](size_t at,unsigned length,uint8_t end,bool requireEnd) {
            std::vector<uint8_t> result;
            for(unsigned i=0;i<length&&at+i<prgEnd;++i) {
                const auto code=byte(at+i);
                if(code==end)return result;
                result.push_back(code);
            }
            return requireEnd?std::vector<uint8_t>{}:result;
        };
        if(scenario==1) {
            const size_t name=pointer((unknown?0xb010:0xb0da)+2*id+trainer,0x8010+trainer);
            return name?readName(name,48,255,true):std::vector<uint8_t>{};
        }
        const size_t bank=0x10010+trainer;
        if(scenario==2) {
            auto record=pointer(bank+2*id,bank);
            if(record&&unknown)record=pointer(record+32,bank);
            return record?readName(record+16,16,0,false):std::vector<uint8_t>{};
        }
        const auto record=pointer(bank+31*id+(unknown?2:0),bank);
        if(!record)return {};
        for(unsigned i=0;i<96&&record+i<prgEnd;++i)
            if(byte(record+i)==0)return readName(record+i+1,48,0,true);
        return {};
    }

    std::string glyphs(const std::vector<char>& rom,const uint8_t* codes,size_t count) const {
        if(!count)return {};
        const size_t font=16+((rom[6]&4)?512:0)+0x20000+(scenario==1?4:1)*4096;
        if(font+4096>rom.size())return {};
        constexpr char hex[]="0123456789ABCDEF";
        std::string result="@ROM:";
        result.reserve(5+count*16);
        for(size_t i=0;i<count;++i)for(size_t row=0;row<8;++row) {
            const auto bits=static_cast<uint8_t>(rom[font+codes[i]*16+row]);
            result.push_back(hex[bits>>4]);
            result.push_back(hex[bits&15]);
        }
        return result;
    }
};
