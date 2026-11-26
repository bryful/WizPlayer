#pragma once
#include <array>
#include <string>
#include <vector>

// GBC item labels are text programs, not zero-terminated naming-keyboard
// strings. 00 03 xx selects a kanji glyph; 00 00 ends the program.
// Keep the ROM's actual glyphs instead of guessing Unicode transcriptions.
class WizardryItemText {
    static unsigned byte(const std::vector<char>& rom,size_t at){return at<rom.size()?static_cast<unsigned char>(rom[at]):255;}
    static size_t pointer(const std::vector<char>& rom,unsigned id,bool unknown){
        size_t entry=0x28000+id*12+(unknown?9:3);
        if(entry+3>rom.size())return 0;
        unsigned address=byte(rom,entry)|(byte(rom,entry+1)<<8),bank=byte(rom,entry+2);
        if(!address&&!bank&&id)return pointer(rom,0,unknown); // Native empty entry: broken item.
        if(address<0x4000||address>=0x8000||bank==0)return 0;
        size_t at=bank*0x4000+address-0x4000;return at<rom.size()?at:0;
    }
    static bool append(std::string& result,const std::vector<char>& rom,unsigned code,bool kanji){
        size_t base=(kanji?0x41180:0x40000)+(code&127)*16;
        if(base+16>rom.size())return false;
        constexpr char hex[]="0123456789ABCDEF";
        for(unsigned row=0;row<8;row++){
            unsigned a=byte(rom,base+row*2),b=byte(rom,base+row*2+1);
            // Native BG palettes 4/6 use inverted plane 1; 5/7 use XOR.
            unsigned bits=code<128?(~b&255):(a^b);
            result.push_back(hex[bits>>4]);result.push_back(hex[bits&15]);
        }return true;
    }
public:
    static bool raster(const std::string& text){return text.compare(0,5,"@ROM:")==0&&(text.size()-5)%16==0;}
    static unsigned row(const std::string& text,unsigned glyph,unsigned y){
        size_t at=5+glyph*16+y*2;if(!raster(text)||y>=8||at+1>=text.size())return 0;
        auto digit=[](char c)->unsigned{return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:0;};
        return (digit(text[at])<<4)|digit(text[at+1]);
    }
    static std::string decode(const std::vector<char>& rom,unsigned id,bool unknown){
        size_t at=pointer(rom,id,unknown);if(!at)return {};
        std::string result="@ROM:";
        for(unsigned glyph=0;glyph<32&&at<rom.size();glyph++){
            unsigned code=byte(rom,at++);bool kanji=false;
            if(!code){
                if(at>=rom.size())return {};
                unsigned command=byte(rom,at++);if(!command)return glyph?result:std::string{};
                if(command!=3||at>=rom.size())return {};
                code=byte(rom,at++);kanji=true;
            }
            if(!append(result,rom,code,kanji))return {};
        }return {}; // Missing terminator or an unknown text instruction.
    }
    static std::string marked(const std::vector<char>& rom,const std::string& name,bool equipped){
        if(!raster(name))return std::string(equipped?"＊":" ")+name;
        std::string result="@ROM:";if(!append(result,rom,equipped?'*':' ',false))return {};
        return result+name.substr(5);
    }
};
