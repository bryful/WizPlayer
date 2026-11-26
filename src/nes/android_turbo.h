#pragma once

#include "core_bridge.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <vector>

// Game and audio cores share the same ROM code, with private vectors for
// NesDQ's standalone sound drivers. The source ROM is never modified.
class FcTurbo {
    using Core=std::unique_ptr<WiizCore,decltype(&wiiz_core_delete)>;
    static constexpr char stateMagic[]="WZFCST01";
    static constexpr size_t headerSize=32;
    static constexpr size_t maxPending=512;
    WiizCore* game;
    unsigned scenario,profile=0;
    std::vector<char> audioRom;
    Core player{nullptr,wiiz_core_delete};
    std::deque<unsigned> pending;
    unsigned halfSpeed=2,remainder=0,tick=0,previous=0;
    std::array<unsigned,4> repeatAt{};

    static bool validSpeed(unsigned speed) {
        switch(speed){case 2:case 3:case 4:case 6:case 8:case 10:case 20:return true;default:return false;}
    }
    static unsigned soundProfile(const std::vector<char>& rom) {
        if(rom.size()<16||rom[5]!=0||(rom[6]&4)||(rom[7]&0x0c)==8)return 0;
        const unsigned banks=static_cast<uint8_t>(rom[4]);
        const unsigned mapper=(static_cast<uint8_t>(rom[6])>>4)|(rom[7]&0xf0);
        if(rom.size()<16+banks*16384u)return 0;
        auto matches=[&](size_t offset,std::initializer_list<uint8_t> signature) {
            return offset+signature.size()<=banks*16384u&&
                std::equal(signature.begin(),signature.end(),rom.begin()+16+offset,
                           [](uint8_t a,char b){return a==static_cast<uint8_t>(b);});
        };
        if(banks==16&&mapper==1&&matches(0x45c1,{0x20,0x63,0x86,0x0a,0xb0,0x66})&&
           matches(0x3ffbd,{0x8d,0xd5,0x06,0xea,0xea,0x8d,0xff,0xff}))return 3;
        if(banks==32&&mapper==1&&matches(0x7ef59,{0x20,0x78,0xf0,0xaa,0x0a,0xb0})&&
           matches(0x7e685,{0xa5,0xbf,0x29,0x01,0xd0,0x01,0x60}))return 4;
        if(banks==16&&mapper==2&&matches(0x3db2e,{0xa9,0x7d,0x8d,0x04,0x40,0xa9})&&
           matches(0x3c156,{0x4c,0xc9,0xc0})&&matches(0x3c8df,{0xa5,0x34,0xd0,0x03,0x4c,0x6e,0xc9}))return 122;
        if(banks==32&&mapper==4&&matches(0x6c000,{0x4c,0xc3,0x81,0xad,0x43,0x7f,0xc9,0x37})&&
           matches(0x6c01f,{0x20,0x30,0x80,0x20,0xab,0x80,0x0e,0x40}))return 123;
        return 0;
    }
    Core makePlayer(bool warmUp=true) {
        Core next(wiiz_core_new(reinterpret_cast<uint8_t*>(audioRom.data()),audioRom.size()),wiiz_core_delete);
        if(!next)throw std::runtime_error("FC音声コアを起動できません");
        if(scenario) {
            wiiz_core_wizardry(next.get(),scenario);
            wiiz_core_wizardry_audio(next.get(),2);
            if(!warmUp)return next;
            for(unsigned frame=0;frame<600;++frame)
                if(!wiiz_core_frame(next.get()))throw std::runtime_error("FC音声ドライバーの起動に失敗しました");
            for(unsigned reg=0;reg<3;++reg)wiiz_core_wizardry_sound(next.get(),reg,0);
            for(unsigned i=0;i<2;++i)
                if(!wiiz_core_frame(next.get()))throw std::runtime_error("FC音声ドライバーの起動に失敗しました");
        } else {
            if(!wiiz_core_profile_player(next.get(),profile))throw std::runtime_error("FC音声ドライバーが未対応です");
            if(!warmUp)return next;
            for(unsigned i=0;i<4;++i)
                if(!wiiz_core_frame(next.get()))throw std::runtime_error("FC音声ドライバーの起動に失敗しました");
        }
        return next;
    }
    void bindSource() {
        if(scenario)wiiz_core_wizardry_audio(game,1);
        else wiiz_core_profile_source(game,profile,0);
    }
    void start() {
        auto next=makePlayer();
        if(scenario) {
            wiiz_core_wizardry_sound(next.get(),0,wiiz_core_peek(game,0x600));
        } else {
            const unsigned song=wiiz_core_current_music(game);
            if(profile==123) {
                wiiz_core_sound_command(next.get(),0x300|song);
                wiiz_core_sound_command(next.get(),0x201);
            } else if(profile!=122||song<31) {
                wiiz_core_sound_command(next.get(),(profile==122?0x140:0x100)|song);
            }
        }
        bindSource();
        player=std::move(next);
    }
    unsigned repeat(unsigned buttons) {
        constexpr unsigned directions[]={PAD_UP,PAD_DOWN,PAD_LEFT,PAD_RIGHT};
        unsigned result=0;
        ++tick;
        for(size_t i=0;i<repeatAt.size();++i) {
            unsigned direction=directions[i];
            if(!(buttons&direction)||!(previous&direction))repeatAt[i]=tick+12;
            else if(tick>=repeatAt[i]){result|=direction;repeatAt[i]=tick+6;}
        }
        previous=buttons;
        return result;
    }
    void resetInput() {
        remainder=tick=previous=0;
        repeatAt.fill(0);
    }
    static std::vector<char> snapshot(WiizCore* core) {
        std::vector<char> result(wiiz_core_state_size(core));
        if(!wiiz_core_state_write(core,reinterpret_cast<uint8_t*>(result.data()),result.size()))
            throw std::runtime_error("FCステートを取得できません");
        return result;
    }
    static bool restore(WiizCore* core,const char* data,size_t size) {
        return wiiz_core_state_read(core,reinterpret_cast<const uint8_t*>(data),size);
    }
    static void put(std::vector<char>& data,size_t pos,unsigned value) {
        for(unsigned i=0;i<4;++i)data[pos+i]=static_cast<char>(value>>(i*8));
    }
    static unsigned get(const std::vector<char>& data,size_t pos) {
        unsigned value=0;
        for(unsigned i=0;i<4;++i)value|=static_cast<unsigned>(static_cast<uint8_t>(data[pos+i]))<<(i*8);
        return value;
    }
    bool validEvent(unsigned event) const {
        const auto operation=event>>8;
        return operation>=1&&operation<=(profile==122?12u:profile==123?4u:5u);
    }
public:
    FcTurbo(WiizCore* core,const std::vector<char>& rom,unsigned wizardry) : game(core),scenario(wizardry) {
        if(scenario) {audioRom=rom;return;}
        profile=soundProfile(rom);
        if(!profile)return;
        wiiz_core_reward_profile(game,profile);
        // Ignore trailing copier data when locating the PRG interrupt vectors.
        const size_t prgEnd=16+static_cast<uint8_t>(rom[4])*16384u;
        audioRom.assign(rom.begin(),rom.begin()+prgEnd);
        const size_t vectors=prgEnd-6;
        std::array<uint8_t,6> replacements{0x80,7,0,7,0xf0,7};
        if(profile==122) {
            audioRom[16+0x3c156]=0x60;
            audioRom[16+0x3c8df]=0x60;
            replacements={0xf8,7,0,7,0xfb,7};
        } else if(profile==123) replacements={0xf0,7,0,7,0xf0,7};
        std::copy(replacements.begin(),replacements.end(),audioRom.begin()+vectors);
    }
    unsigned speed() const{return halfSpeed;}
    bool setSpeed(unsigned speed) {
        if(!validSpeed(speed)||(speed>2&&audioRom.empty()))return false;
        if(speed>2&&!player)start();
        halfSpeed=speed;
        resetInput();
        return true;
    }
    const float* step(unsigned buttons,size_t* count) {
        remainder+=halfSpeed;
        const unsigned frames=remainder/2;
        remainder%=2;
        const unsigned due=scenario&&player&&halfSpeed>2?repeat(buttons):0;
        if(player&&!scenario)wiiz_core_input_speed(game,buttons,std::min(6u,(halfSpeed+1)/2));
        else wiiz_core_input(game,buttons);
        unsigned musicSteps=0;
        for(unsigned frame=0;frame<frames;++frame) {
            if(scenario&&player&&halfSpeed>2)wiiz_core_wizardry_repeat(game,buttons,frame?0:due);
            if(!wiiz_core_frame(game))throw std::runtime_error("FCフレーム処理に失敗しました");
            if(!player)continue;
            if(scenario) {
                if(scenario>1)wiiz_core_wizardry_sound(player.get(),3,wiiz_core_peek(game,0x603));
                for(unsigned reg=0;reg<3;++reg) {
                    const auto command=wiiz_core_wizardry_take_sound(game,reg);
                    if(command&0x100)wiiz_core_wizardry_sound(player.get(),reg,command&255);
                }
            } else {
                musicSteps=std::max(musicSteps,wiiz_core_music_steps(game));
                while(const auto event=wiiz_core_take_sound(game)) {
                    if(pending.size()>=maxPending)throw std::runtime_error("FC音声コマンドが処理上限を超えました");
                    pending.push_back(event);
                }
            }
        }
        if(!player)return wiiz_core_audio(game,count);
        while(!pending.empty()&&wiiz_core_sound_command(player.get(),pending.front()))pending.pop_front();
        if(profile>=122&&!wiiz_core_ff_clock(player.get(),musicSteps))
            throw std::runtime_error("FC音声クロックを設定できません");
        if(!wiiz_core_frame(player.get()))throw std::runtime_error("FC音声の再生に失敗しました");
        return wiiz_core_audio(player.get(),count);
    }
    void skipBootLogo() {
        if(!scenario || !wiiz_core_wizardry_assists_supported(game))return;
        const auto before=snapshot(game);
        for(unsigned frame=0;frame<900;++frame) {
            if(wiiz_core_title_ready(game))return;
            if(!wiiz_core_frame(game))break;
        }
        if(!restore(game,before.data(),before.size()))
            throw std::runtime_error("FC起動状態を復元できません");
    }
    void reset(bool skipLogo=false) {
        wiiz_core_clear_audio_source(game);
        wiiz_core_reset(game);
        pending.clear();
        player.reset();
        resetInput();
        if(skipLogo)skipBootLogo();
        if(halfSpeed>2)start();
    }
    std::vector<char> saveState() const {
        auto main=snapshot(game);
        if(!player)return main;
        auto audio=snapshot(player.get());
        // The sound-player bridge can hold commands queued for its next NMI.
        // Save those ahead of any commands still waiting in the frontend.
        const auto queued=wiiz_core_sound_pending(player.get());
        std::vector<char> result(headerSize+main.size()+audio.size()+4*(queued+pending.size()));
        std::memcpy(result.data(),stateMagic,8);
        put(result,8,static_cast<unsigned>(main.size()));
        put(result,12,static_cast<unsigned>(audio.size()));
        put(result,16,scenario?scenario:profile);
        put(result,20,halfSpeed);
        put(result,24,remainder);
        put(result,28,static_cast<unsigned>(queued+pending.size()));
        std::copy(main.begin(),main.end(),result.begin()+headerSize);
        std::copy(audio.begin(),audio.end(),result.begin()+headerSize+main.size());
        size_t pos=headerSize+main.size()+audio.size();
        for(unsigned i=0;i<queued;++i,pos+=4)put(result,pos,wiiz_core_sound_at(player.get(),i));
        for(auto event:pending){put(result,pos,event);pos+=4;}
        return result;
    }
    bool loadState(const std::vector<char>& data) {
        if(data.size()<8||std::memcmp(data.data(),stateMagic,8)!=0) {
            if(!restore(game,data.data(),data.size()))return false;
            wiiz_core_clear_audio_source(game);
            player.reset();pending.clear();halfSpeed=2;resetInput();
            return true;
        }
        if(data.size()<headerSize||audioRom.empty()||get(data,16)!=(scenario?scenario:profile))return false;
        const size_t mainSize=get(data,8),audioSize=get(data,12),queued=get(data,28);
        const unsigned speed=get(data,20),phase=get(data,24);
        if(mainSize>data.size()-headerSize||audioSize>data.size()-headerSize-mainSize||!validSpeed(speed)||
           phase>1||(speed!=3&&phase)||queued>maxPending+42||
           headerSize+mainSize+audioSize+queued*4!=data.size())return false;
        std::deque<unsigned> events;
        for(size_t i=0;i<queued;++i) {
            const auto event=get(data,headerSize+mainSize+audioSize+i*4);
            if(scenario||!validEvent(event))return false;
            events.push_back(event);
        }
        auto next=makePlayer(false);
        if(!restore(next.get(),data.data()+headerSize+mainSize,audioSize))return false;
        const auto backup=snapshot(game);
        if(!restore(game,data.data()+headerSize,mainSize)) {
            restore(game,backup.data(),backup.size());
            return false;
        }
        bindSource();
        player=std::move(next);
        pending=std::move(events);
        halfSpeed=speed;
        resetInput();
        remainder=phase;
        return true;
    }
};
