#include <cstddef>
#include "core.h"
#include "../map_direction.h"
#include "libretro.h"
#if defined(__ANDROID__) || defined(_WIN32) || defined(WIZPLAYER_ANDROID_TURBO_CHECK) || defined(WIZPLAYER_ANDROID_MAP_CHECK)
#include "snes9x.h"
#endif
#if defined(__ANDROID__) || defined(_WIN32) || defined(WIZPLAYER_ANDROID_TURBO_CHECK)
#include "apu/apu.h"
#endif
#if defined(__ANDROID__) || defined(_WIN32) || defined(WIZPLAYER_ANDROID_MAP_CHECK)
#include "memmap.h"
#include "dq5_options.h"
#endif
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

SfcCore* SfcCore::active = nullptr;

#if defined(__ANDROID__) || defined(_WIN32) || defined(WIZPLAYER_ANDROID_MAP_CHECK)
extern "C" bool wiizv_can_move();
extern "C" bool wiizv_can_leave_camp();
extern "C" bool wiizv_request_move(int x,int y,unsigned floor);
extern "C" void wiizv_cancel_move();
extern "C" unsigned wiizv_move_status();
extern "C" bool wizardryCanEditItems();
extern "C" bool wizardryCanEditCharacter();
extern "C" bool wizardryRequestItemRecalc(unsigned slot);
extern "C" unsigned wizardryItemRecalcStatus();
extern "C" void wizardryCancelItemRecalc();
extern "C" void sfcSetRewardFactors(unsigned profile,unsigned experience,unsigned gold);
extern "C" void wiizvAssistGameChanged(bool supported);
extern "C" bool wiizv_set_effects(unsigned mask);
extern "C" bool wiizv_set_max_bonus(bool enabled);
extern "C" bool wiizv_set_skip_logo(bool enabled);
extern "C" bool wiizv_set_mixed_alignment(bool enabled);
#endif

namespace {
bool hasSfcHeader(const std::vector<char>& rom,size_t prefix) {
    for (const size_t offset : {0x7fc0u,0xffc0u,0x40ffc0u}) {
        const size_t header=prefix+offset;
        if (rom.size()<header+64) continue;
        const auto* bytes=reinterpret_cast<const uint8_t*>(rom.data()+header);
        const unsigned map=bytes[21]&0xef;
        const unsigned reset=bytes[60]|(bytes[61]<<8);
        if (map>=0x20 && map<=0x2a && reset>=0x8000 && reset!=0xffff &&
            bytes[23]>=5 && bytes[23]<=14) return true;
    }
    return false;
}
#if defined(__ANDROID__) || defined(_WIN32) || defined(WIZPLAYER_ANDROID_MAP_CHECK)
bool wizardryMapProfile(const std::vector<char>& rom) {
    auto match=[&](size_t offset,std::initializer_list<uint8_t> bytes) {
        return rom.size()>=offset+bytes.size()&&std::equal(bytes.begin(),bytes.end(),rom.begin()+offset,
            [](uint8_t a,char b){return a==static_cast<uint8_t>(b);});
    };
    constexpr char title[]="WIZARDRY V";
    return rom.size()>=0x100000&&std::equal(title,title+sizeof(title)-1,rom.begin()+0x7fc0)&&
        match(0x215,{0xe6,0x1e,0x80,0xfc,0x8b})&&
        match(0x28113,{0xa5,0x18,0x30,0x22,0x89,0x40})&&
        match(0x2ada4,{0xa5,0xf0,0xeb,0xa5,0xb5,0x85,0xf0});
}

const uint8_t* systemRam() {
    if(retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM)<0xe000)return nullptr;
    return static_cast<const uint8_t*>(retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM));
}
// The original 00:9DFC record check uses tables from the loaded ROM.
bool updateRosterCheck(std::array<uint8_t,128>& record,const uint8_t* rom) {
    record[0x7e]=record[0x7f]=0;
    uint8_t low=0xff^record[0],high=0xff^record[1];
    for(unsigned i=2;i<record.size();++i) {
        const unsigned upper=low>>4,index=low^rom[0x1eb9+upper];
        if(index>=16)return false;
        const auto nextLow=high^rom[0x1ec9+upper]^rom[0x1ee9+index];
        high=record[i]^rom[0x1ed9+upper]^rom[0x1ef9+index];
        low=static_cast<uint8_t>(nextLow);
    }
    record[0x7e]=low;record[0x7f]=high;
    return true;
}
std::string wizardryGlyphs(const uint8_t* codes,size_t count) {
    const auto* vram=static_cast<const uint8_t*>(retro_get_memory_data(RETRO_MEMORY_VIDEO_RAM));
    if(!vram||retro_get_memory_size(RETRO_MEMORY_VIDEO_RAM)<0x5000||!count)return {};
    std::string out="@ROM:";
    constexpr char hex[]="0123456789abcdef";
    for(size_t i=0;i<count;++i)for(unsigned y=0;y<8;++y) {
        const auto* row=vram+0x4000+codes[i]*16+y*2;
        const unsigned value=row[0]&row[1];
        out+=hex[value>>4];out+=hex[value&15];
    }
    return out;
}
void writeRosterRecord(unsigned slot,const std::array<uint8_t,128>& record,uint8_t* ram,uint8_t* sram) {
    const unsigned offset=slot*128;
    std::copy(record.begin(),record.end(),ram+0x1000+offset);
    std::copy(record.begin(),record.end(),sram+offset);
    std::copy(record.begin(),record.end(),sram+0xa00+offset);
}
#endif
}

bool SfcCore::environment(unsigned command, void* data) {
    switch(command) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        active->pixelFormat = *static_cast<retro_pixel_format*>(data);
        return active->pixelFormat <= RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *static_cast<bool*>(data) = true;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *static_cast<bool*>(data) = false;
        return true;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
#if defined(__ANDROID__) || defined(_WIN32) || defined(WIZPLAYER_ANDROID_TURBO_CHECK)
        *static_cast<int*>(data) = active->renderVideo ? 3 : 2;
#else
        *static_cast<int*>(data) = 3;
#endif
        return true;
    default:
        return false;
    }
}

void SfcCore::video(const void* data, unsigned w, unsigned h, size_t pitch) {
    if (!data || !w || !h) return;
    const auto format = active->pixelFormat;
    for (unsigned y=0; y<height; ++y) {
        const auto* row = static_cast<const uint8_t*>(data) + (y*h/height)*pitch;
        for (unsigned x=0; x<width; ++x) {
            const unsigned source = x*w/width;
            uint32_t color;
            if (format == RETRO_PIXEL_FORMAT_XRGB8888) {
                std::memcpy(&color, row+4*source, sizeof(color));
            } else {
                uint16_t value;
                std::memcpy(&value, row+2*source, sizeof(value));
                const bool rgb565 = format == RETRO_PIXEL_FORMAT_RGB565;
                const unsigned red = (value >> (rgb565 ? 11 : 10)) & 31;
                const unsigned green = (value >> 5) & (rgb565 ? 63 : 31);
                const unsigned blue = value & 31;
                color = ((red*255/31)<<16) | ((green*255/(rgb565 ? 63 : 31))<<8) | (blue*255/31);
            }
            active->pixels[y*width+x] = color | 0xff000000u;
        }
    }
}

size_t SfcCore::audio(const int16_t* data, size_t frames) {
    for (size_t i=0; i<frames; ++i) {
        active->audioPhase += 32768;
        while (active->audioPhase >= active->sampleRate) {
            active->audioPhase -= active->sampleRate;
            active->pcm.push_back(data[i*2]);
            active->pcm.push_back(data[i*2+1]);
        }
    }
    return frames;
}

void SfcCore::sample(int16_t left,int16_t right) {
    const int16_t stereo[] = {left,right};
    audio(stereo,1);
}

int16_t SfcCore::input(unsigned port, unsigned device, unsigned, unsigned id) {
    if (port || device != RETRO_DEVICE_JOYPAD) return 0;
    constexpr std::array<unsigned,12> keys = {
        RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_SELECT,
        RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_RIGHT, RETRO_DEVICE_ID_JOYPAD_LEFT,
        RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_L,
        RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_Y
    };
    for (size_t i=0; i<keys.size(); ++i)
        if (id == keys[i]) return (active->buttons >> i) & 1;
    return 0;
}

bool SfcCore::load() {
    retro_game_info info{"game.sfc",rom.data(),rom.size(),nullptr};
    if (!retro_load_game(&info)) return false;
#if defined(__ANDROID__) || defined(_WIN32) || defined(WIZPLAYER_ANDROID_MAP_CHECK)
    sfcSetRewardFactors(static_cast<unsigned>(rewardProfile),experienceFactor,goldFactor);
    if(dqOptionProfile)dq5::applyOptionPatches(Memory.ROM,dqOptions);
    wiizvAssistGameChanged(assistsSupported());
    if(assistsSupported()) {
        wiizv_set_max_bonus(maxBonus);
        wiizv_set_mixed_alignment(mixedParty);
        wiizv_set_skip_logo(skipLogo);
        wiizv_set_effects(effects|(noMpCost?16:0));
    }
#endif
    retro_set_controller_port_device(0,RETRO_DEVICE_JOYPAD);
    retro_system_av_info av{};
    retro_get_system_av_info(&av);
    if (av.timing.fps > 1) framePeriod=std::llround(1e9/av.timing.fps);
    if (av.timing.sample_rate > 1) sampleRate=static_cast<unsigned>(std::lround(av.timing.sample_rate));
#if defined(__ANDROID__) || defined(_WIN32) || defined(WIZPLAYER_ANDROID_TURBO_CHECK)
    applySpeed();
#endif
    return true;
}

SfcCore::SfcCore(std::vector<char> data,const std::vector<char>* ram,bool skipBootLogo) : rom(std::move(data)) {
    const bool hasCopierHeader=hasSfcHeader(rom,512);
    if (hasCopierHeader && (rom.size()%0x8000==512 || !hasSfcHeader(rom,0)))
        rom.erase(rom.begin(),rom.begin()+512);
    else if (!hasSfcHeader(rom,0)) throw std::runtime_error("SFC ROMヘッダーが見つかりません");
#if defined(__ANDROID__) || defined(_WIN32) || defined(WIZPLAYER_ANDROID_MAP_CHECK)
    skipLogo=skipBootLogo;
    if(wizardryMapProfile(rom)) {
        constexpr unsigned char logo[]={0x22,0x0c,0xdb,0x0f,0xa9};
        constexpr unsigned char bonus[]={0x85,0x9d,0x22,0xf6,0x81};
        constexpr unsigned char tavern[]={0xd0,0x03,0x20,0xad,0x83};
        constexpr unsigned char consume[]={0xb5,0x27,0x38,0xe9,0x10};
        assistProfile=std::memcmp(rom.data()+0x491,logo,sizeof(logo))==0 &&
            std::memcmp(rom.data()+0x224a4,bonus,sizeof(bonus))==0 &&
            std::memcmp(rom.data()+0x20341,tavern,sizeof(tavern))==0 &&
            std::memcmp(rom.data()+0x2b70d,consume,sizeof(consume))==0;
        constexpr unsigned char itemEntry[]={0xa6,0x30,0xa9,0x0a,0x85,0x4b,0x20,0x0f};
        itemProfile=std::memcmp(rom.data()+0x1fa2,itemEntry,sizeof(itemEntry))==0;
        constexpr unsigned char experienceEntry[]={0xa5,0x92,0x22,0xbb,0x9c,0x00,0xa9,0x00,0xeb};
        constexpr unsigned char goldEntry[]={0x22,0xbb,0x9c,0x00,0x20,0x74,0xc6};
        constexpr unsigned char goldCarry[]={0x65,0x28,0x95,0x11,0xd8,0x88,0x10,0xc9,0x60};
        if(std::memcmp(rom.data()+0x2b4a1,experienceEntry,sizeof(experienceEntry))==0 &&
            std::memcmp(rom.data()+0x2465a,goldEntry,sizeof(goldEntry))==0 &&
            std::memcmp(rom.data()+0x246aa,goldCarry,sizeof(goldCarry))==0)
            rewardProfile=RewardProfile::WizardryV;
        try {
            const auto floors=readSfcDungeonMaps(std::vector<uint8_t>(rom.begin(),rom.end()));
            for(size_t floor=0;floor<floors.size();++floor) {
                for(const auto& cell:floors[floor]) {
                    // 離れた区画をROMの8×8ブロック番号で分ける。
                    const int block=cell.explorationIndex/64;
                    const size_t page=floor==0 ? (block==15 ? 1 : 0) :
                        floor==7 ? (block<9 ? 8 : block<11 ? 9 : block<13 ? 10 : 11) : floor+1;
                    mapPages[page].push_back(cell);
                }
            }
        }
        catch(const std::runtime_error&) {mapPages={};}
    }
    constexpr char dqTitle[]="DRAGONQUEST5";
    constexpr unsigned char dqExperience[]={0x18,0x6d,0xfc,0x10,0x8d,0xfc,0x10,0xe2,0x20,0x90,0x10,0xee,0xfe,0x10};
    constexpr unsigned char dqGold[]={0x18,0x6d,0xff,0x10,0x8d,0xff,0x10,0xe2,0x20,0x90,0x10,0xee,0x01,0x11};
    constexpr unsigned char dqRestore[]={0xc2,0x30,0x7a,0xfa,0x68,0x28,0x6b};
    if(rom.size()>=0x180000 && std::memcmp(rom.data()+0x7fc0,dqTitle,sizeof(dqTitle)-1)==0 &&
       static_cast<unsigned char>(rom[0x7fdb])==0 &&
       std::memcmp(rom.data()+0x140213,dqExperience,sizeof(dqExperience))==0 &&
       std::memcmp(rom.data()+0x14024f,dqGold,sizeof(dqGold))==0 &&
       std::memcmp(rom.data()+0x14022e,dqRestore,sizeof(dqRestore))==0 &&
       std::memcmp(rom.data()+0x14026a,dqRestore,sizeof(dqRestore))==0)
    {
        rewardProfile=RewardProfile::DragonQuestV;
        dqOptionProfile=dq5::optionProfileMatches(reinterpret_cast<const uint8_t*>(rom.data()),rom.size());
    }
#endif
    auto* previous=active;
    std::vector<char> backup;
    if (previous) {
        backup=previous->state();
        retro_unload_game();
    } else {
        active=this;
        retro_set_environment(environment);
        retro_set_video_refresh(video);
        retro_set_audio_sample(sample);
        retro_set_audio_sample_batch(audio);
        retro_set_input_poll([]{});
        retro_set_input_state(input);
        retro_init();
    }
    active=this;
    const bool loaded=load();
    const bool validRam=loaded && (!ram || ram->size()==retro_get_memory_size(RETRO_MEMORY_SAVE_RAM));
    if (!validRam) {
        if (loaded) retro_unload_game();
        active=previous;
        if (previous && previous->load()) previous->loadState(backup);
        else {
            active=nullptr;
            retro_deinit();
        }
        throw std::runtime_error(loaded ? "SFC通常セーブのサイズが一致しません" : "SFC ROMを読み込めません");
    }
    if (ram && !ram->empty())
        std::memcpy(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM),ram->data(),ram->size());
}

SfcCore::~SfcCore() {
    if (active==this) {
        retro_unload_game();
        retro_deinit();
        active=nullptr;
    }
}

void SfcCore::step(unsigned inputButtons) {
    if (active!=this) throw std::runtime_error("SFCセッションが終了しました");
    buttons=inputButtons;
    pcm.clear();
#if defined(__ANDROID__) || defined(_WIN32) || defined(WIZPLAYER_ANDROID_TURBO_CHECK)
    frameRemainder+=speedHalf;
    const unsigned frames=frameRemainder/2;
    frameRemainder%=2;
    if(speedHalf>2) {
        // Count repeat delays in host frames and keep each press visible for
        // two game frames, including when a host frame runs ten game frames.
        for(unsigned i=0;i<inputPulses.size();++i) {
            const unsigned mask=1u<<i;
            auto& pulse=inputPulses[i];
            if(!(inputButtons&mask))continue;
            if(!(heldButtons&mask))pulse={2,18};
            else if(i>=4&&i<=7&&--pulse.repeatTicks==0)pulse={2,6};
        }
        heldButtons=inputButtons;
    }
    for(unsigned i=0;i<frames;++i) {
        if(speedHalf>2) {
            buttons=0;
            for(unsigned bit=0;bit<inputPulses.size();++bit) {
                auto& pulse=inputPulses[bit];
                if(!pulse.frames)continue;
                buttons|=1u<<bit;
                --pulse.frames;
            }
        }
        renderVideo=i+1==frames;
        retro_run();
    }
    renderVideo=true;
#else
    retro_run();
#endif
}

#if defined(__ANDROID__) || defined(_WIN32) || defined(WIZPLAYER_ANDROID_TURBO_CHECK)
void SfcCore::clearInputPulses() {
    heldButtons=0;
    inputPulses={};
}

void SfcCore::applySpeed() {
    // Run the game CPU faster with the SPC at its original wall-clock rate.
    // Keep the resampler's input/output ratio unchanged after slowing the SPC.
    Settings.SoundInputRate=32040*speedHalf/2;
    S9xAPUTimingSetSpeedup(256-128*static_cast<int>(speedHalf));
    S9xUpdateDynamicRate();
}

bool SfcCore::setSpeed(unsigned halfSpeed) {
    switch(halfSpeed) {
        case 2: case 3: case 4: case 6: case 8: case 10: case 20: break;
        default: return false;
    }
    if(active!=this)return false;
    if(speedHalf==halfSpeed)return true;
    speedHalf=halfSpeed;
    frameRemainder=0;
    clearInputPulses();
    applySpeed();
    return true;
}
#endif

void SfcCore::reset() {
    retro_reset();
    audioPhase=0;
#if defined(__ANDROID__) || defined(_WIN32) || defined(WIZPLAYER_ANDROID_TURBO_CHECK)
    frameRemainder=0;
    clearInputPulses();
    applySpeed();
#endif
}

std::vector<char> SfcCore::battery() const {
    const auto size=retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (!size || active!=this) return {};
    const auto* data=static_cast<const char*>(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM));
    return {data,data+size};
}

std::vector<char> SfcCore::state() const {
    if (active!=this) throw std::runtime_error("SFCセッションが終了しました");
    std::vector<char> result(retro_serialize_size());
    if (!retro_serialize(result.data(),result.size())) throw std::runtime_error("SFCステートを取得できません");
    return result;
}

bool SfcCore::loadState(const std::vector<char>& state) {
    if (active!=this || state.size()!=retro_serialize_size() ||
        !retro_unserialize(state.data(),state.size())) return false;
    audioPhase=0;
#if defined(__ANDROID__) || defined(_WIN32) || defined(WIZPLAYER_ANDROID_TURBO_CHECK)
    frameRemainder=0;
    clearInputPulses();
    applySpeed();
#endif
    return true;
}

#if defined(__ANDROID__) || defined(_WIN32) || defined(WIZPLAYER_ANDROID_MAP_CHECK)
bool SfcCore::setRewards(unsigned experience,unsigned gold) {
    auto valid=[](unsigned factor){return factor && factor<=256 && !(factor&(factor-1));};
    if(active!=this || !rewardsSupported() || !valid(experience) || !valid(gold))return false;
    experienceFactor=experience;
    goldFactor=gold;
    sfcSetRewardFactors(static_cast<unsigned>(rewardProfile),experienceFactor,goldFactor);
    return true;
}

bool SfcCore::setMaxBonus(bool enabled) {
    if(active!=this || !assistsSupported())return false;
    return wiizv_set_max_bonus(maxBonus=enabled);
}

bool SfcCore::setMixedParty(bool enabled) {
    if(active!=this || !assistsSupported())return false;
    return wiizv_set_mixed_alignment(mixedParty=enabled);
}

bool SfcCore::setSkipLogo(bool enabled) {
    if(active!=this || !assistsSupported())return false;
    return wiizv_set_skip_logo(skipLogo=enabled);
}

bool SfcCore::setNoMpCost(bool enabled) {
    if(active!=this || !assistsSupported())return false;
    noMpCost=enabled;
    return wiizv_set_effects(effects|(noMpCost?16:0));
}

bool SfcCore::setEffects(unsigned mask) {
    if(active!=this || !assistsSupported() || mask>15)return false;
    effects=mask;
    return wiizv_set_effects(effects|(noMpCost?16:0));
}

bool SfcCore::setGameOptions(unsigned flags) {
    if(active!=this || !dqOptionProfile || (flags&~dq5::AllOptions))return false;
    dqOptions=flags;
    dq5::applyOptionPatches(Memory.ROM,dqOptions);
    return true;
}

bool SfcCore::openMap() {
    if(active!=this||mapPages.front().empty()||(!wiizv_can_move()&&!wiizv_can_leave_camp()))return false;
    const auto* ram=systemRam();
    if(!ram)return false;
    const auto first=std::find(mapFloors.begin(),mapFloors.end(),ram[0xb7]);
    if(first==mapFloors.end())return false;
    mapPage=static_cast<int>(first-mapFloors.begin());
    mapX=static_cast<int8_t>(ram[0xb5]);
    mapY=static_cast<int8_t>(ram[0xb6]);
    for(int page=mapPage;page<int(mapPages.size())&&mapFloors[page]==ram[0xb7];++page) {
        if(findSfcDungeonCell(mapPages[page],mapX,mapY)) {
            mapPage=page;
            mapOpen=true;
            return true;
        }
    }
    if(mapPages[mapPage].empty())return false;
    mapX=mapPages[mapPage].front().x;
    mapY=mapPages[mapPage].front().y;
    mapOpen=true;
    return true;
}

bool SfcCore::mapKey(unsigned bits) {
    if(!mapOpen)return false;
    if(bits&2){closeMap();return false;}
    const int pageDelta=((bits&(512|8))!=0)-((bits&(256|4))!=0);
    if(pageDelta) {
        // B1-1～B8-1の後に、B1-2、B8-2～B8-4を並べる。
        constexpr std::array<int,mapFloors.size()> pageOrder{0,2,3,4,5,6,7,8,1,9,10,11};
        const int count=static_cast<int>(pageOrder.size());
        int index=static_cast<int>(std::find(pageOrder.begin(),pageOrder.end(),mapPage)-pageOrder.begin());
        do {
            index=(index+pageDelta+count)%count;
            mapPage=pageOrder[index];
        } while(mapPages[mapPage].empty());
    }
    const auto& cells=mapPages[mapPage];
    if(pageDelta) {
        const auto nearest=std::min_element(cells.begin(),cells.end(),[&](const auto& a,const auto& b) {
            return std::abs(a.x-mapX)+std::abs(a.y-mapY)<std::abs(b.x-mapX)+std::abs(b.y-mapY);
        });
        mapX=nearest->x;mapY=nearest->y;
    }
    const int dx=((bits&16)!=0)-((bits&32)!=0),dy=((bits&64)!=0)-((bits&128)!=0);
    if(dx||dy) {
        const SfcDungeonCell* nearest=nullptr;
        int distance=1000000;
        for(const auto& point:cells) {
            const int along=(point.x-mapX)*dx+(point.y-mapY)*dy;
            const int across=std::abs((point.x-mapX)*dy-(point.y-mapY)*dx);
            const int rank=across*512+along;
            if(along>0&&rank<distance){distance=rank;nearest=&point;}
        }
        if(nearest){mapX=nearest->x;mapY=nearest->y;}
    }
    return (bits&1)&&findSfcDungeonCell(cells,mapX,mapY);
}

bool SfcCore::moveMap() {
    const bool fromCamp=active==this&&wiizv_can_leave_camp();
    if(active!=this||!mapOpen||!findSfcDungeonCell(mapPages[mapPage],mapX,mapY)||
       (!fromCamp&&!wiizv_can_move()))return false;
    const int mapFloor=selectedFloor();
    const auto before=state();
    auto beforePixels=pixels;
    auto restore=[&] {
        wiizv_cancel_move();
        buttons=0;
        pcm.clear();
        pixels.swap(beforePixels);
        if(!loadState(before))throw std::runtime_error("移動前の状態を復元できません");
    };
    buttons=0;
    try {
        if(fromCamp) {
            // 原作にキャンプの終了とフィールド復帰を処理させる。
            for(int frame=0;frame<180&&!wiizv_can_move();++frame) {
                buttons=frame%12<3&&wiizv_can_leave_camp()?2:0;
                pcm.clear();
                retro_run();
            }
            buttons=0;
        }
        const bool requested=wiizv_request_move(mapX,mapY,mapFloor);
        for(int frame=0;requested&&frame<180;++frame) {
            pcm.clear();
            retro_run();
            const auto* ram=systemRam();
            if(wiizv_move_status()!=2)continue;
            if(!ram||static_cast<int8_t>(ram[0xb5])!=mapX||
               static_cast<int8_t>(ram[0xb6])!=mapY||ram[0xb7]!=mapFloor)break;
            for(int refresh=0;refresh<12;++refresh){pcm.clear();retro_run();}
            wiizv_cancel_move();
            closeMap();
            pcm.clear();
            return true;
        }
    } catch(...) {
        restore();
        throw;
    }
    restore();
    return false;
}

std::vector<uint32_t> SfcCore::mapFrame()const {
    if(!mapOpen)return pixels;
    const auto& cells=mapPages[mapPage];
    const int mapFloor=selectedFloor();
    const auto* ram=systemRam();
    std::vector<uint32_t> frame(width*height,0xff080c12u);
    int minX=cells.front().x,maxX=minX,minY=cells.front().y,maxY=minY;
    for(const auto& cell:cells) {
        minX=std::min(minX,cell.x);maxX=std::max(maxX,cell.x);
        minY=std::min(minY,cell.y);maxY=std::max(maxY,cell.y);
    }
    constexpr int top=20,scale=16;
    const int cols=maxX-minX+1,rows=maxY-minY+1;
    const auto offset=[scale](int viewport,int extent,int target) {
        if(extent<=viewport)return (viewport-extent)/2;
        return -std::clamp(target+scale/2-viewport/2,0,extent-viewport);
    };
    const int left=offset(width,cols*scale,(mapX-minX)*scale);
    const int upper=top+offset(height-top,rows*scale,(maxY-mapY)*scale);
    auto dot=[&](int x,int y,uint32_t color) {
        if(x>=0&&x<width&&y>=top&&y<height)frame[y*width+x]=color;
    };
    for(const auto& cell:cells) {
        const int x=left+(cell.x-minX)*scale,y=upper+(maxY-cell.y)*scale;
        if(x>=width||x+scale<=0||y>=height||y+scale<=top)continue;
        const bool explored=isSfcDungeonCellExplored(ram,mapFloor,cell);
        const bool current=ram&&ram[0xb7]==mapFloor&&static_cast<int8_t>(ram[0xb5])==cell.x&&
            static_cast<int8_t>(ram[0xb6])==cell.y;
        const auto shade=current?0xff2d9b9bu:explored?0xff2a7042u:0xff26374cu;
        for(int yy=0;yy<scale;++yy)for(int xx=0;xx<scale;++xx)dot(x+xx,y+yy,shade);
        for(unsigned dir=0;dir<4;++dir) {
            const unsigned wall=(cell.walls>>(dir*2))&3;
            if(!wall)continue;
            const auto color=wall==2?0xff5ab4dcu:0xffc3cddcu;
            for(int k=0;k<scale;++k) {
                if(dir==0)dot(x+k,y,color);
                if(dir==1)dot(x+scale-1,y+k,color);
                if(dir==2)dot(x+k,y+scale-1,color);
                if(dir==3)dot(x,y+k,color);
            }
        }
        if(current)
            drawMapDirection(x+scale/2,y+scale/2,5,ram[0xef],[&](int ax,int ay){dot(ax,ay,0xfff46b64u);});
    }
    const int targetX=left+(mapX-minX)*scale,targetY=upper+(maxY-mapY)*scale;
    for(int k=0;k<scale;++k) {
        constexpr auto cursor=0xffffdc50u;
        dot(targetX+k,targetY,cursor);dot(targetX+k,targetY+scale-1,cursor);
        dot(targetX,targetY+k,cursor);dot(targetX+scale-1,targetY+k,cursor);
    }
    return frame;
}

bool SfcCore::canEditCharacters()const {
    return active==this&&itemProfile&&systemRam()&&wizardryCanEditCharacter();
}

const uint8_t* SfcCore::characterRecord(unsigned slot)const {
    if(slot>=20||!canEditCharacters())return nullptr;
    const auto* record=systemRam()+0x1000+slot*128;
    return record[0]?record:nullptr;
}

std::array<std::string,20> SfcCore::characterNames()const {
    std::array<std::string,20> names;
    if(!canEditCharacters())return names;
    const auto* ram=systemRam();
    for(unsigned slot=0;slot<names.size();++slot) {
        const auto* record=ram+0x1000+slot*128;
        if(record[0])names[slot]=wizardryGlyphs(record+2,8);
    }
    return names;
}

int SfcCore::alignment(unsigned slot)const {
    const auto* record=characterRecord(slot);
    return record&&(record[0xa]&3)<3?(record[0xa]&3):-1;
}

int SfcCore::characterTrait(unsigned slot,bool race)const {
    const auto* record=characterRecord(slot);
    if(!record)return -1;
    const unsigned value=(record[0xa]>>(race?5:2))&7;
    return value<(race?5u:8u)?static_cast<int>(value):-1;
}

bool SfcCore::updateCharacterRecord(unsigned slot,std::array<uint8_t,128>& record) {
    auto* sram=static_cast<uint8_t*>(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM));
    if(!sram||retro_get_memory_size(RETRO_MEMORY_SAVE_RAM)<0x1400||
       !updateRosterCheck(record,reinterpret_cast<const uint8_t*>(rom.data())))return false;
    writeRosterRecord(slot,record,const_cast<uint8_t*>(systemRam()),sram);
    return true;
}

bool SfcCore::setIdentityField(unsigned slot,unsigned value,unsigned shift,unsigned mask,unsigned limit) {
    const auto* current=characterRecord(slot);
    if(!current||value>=limit)return false;
    std::array<uint8_t,128> record;
    std::copy_n(current,record.size(),record.begin());
    record[0xa]=static_cast<uint8_t>((record[0xa]&~mask)|(value<<shift));
    return record[0xa]==current[0xa]||updateCharacterRecord(slot,record);
}

bool SfcCore::setAlignment(unsigned slot,unsigned value) {
    return setIdentityField(slot,value,0,0x03,3);
}

bool SfcCore::setCharacterTrait(unsigned slot,bool race,unsigned value) {
    return race?setIdentityField(slot,value,5,0xe0,5):setIdentityField(slot,value,2,0x1c,8);
}

bool SfcCore::learnSpells(unsigned slot) {
    const auto* current=characterRecord(slot);
    if(!current)return false;
    std::array<uint8_t,128> record;
    std::copy_n(current,record.size(),record.begin());
    // Current and maximum MP share a byte for each spell level.
    std::fill(record.begin()+0x27,record.begin()+0x35,0x99);
    std::fill(record.begin()+0x35,record.begin()+0x43,0xff);
    return updateCharacterRecord(slot,record);
}

std::vector<WizardryPartyMember> SfcCore::items()const {
    const auto* ram=systemRam();
    if(active!=this||!itemProfile||!ram||!wizardryCanEditItems())return {};
    const auto* data=reinterpret_cast<const uint8_t*>(rom.data());
    std::vector<WizardryPartyMember> result;
    for(unsigned member=0;member<ram[0xdb];++member) {
        const auto* record=ram+0x1000+ram[0xdc+member]*128;
        if(record[0x53]>8)return {};
        auto& output=result.emplace_back();
        output.name=wizardryGlyphs(record+2,8);
        for(unsigned i=0;i<record[0x53];++i) {
            const unsigned id=record[0x4b+i],flags=record[0x43+i];
            if(id>=136)return {};
            const unsigned at=0x1bb71+id*38+((flags&32)?2:0);
            unsigned pointer=data[at]|(data[at+1]<<8);
            std::string name;
            if(pointer>=0x8000) {
                unsigned start=0x18000+(pointer&0x7fff);
                if(ram[0xd0]){while(start<0x20000&&data[start])++start;++start;}
                unsigned end=start;
                while(end<0x20000&&end-start<16&&data[end])++end;
                if(start<0x20000)name=wizardryGlyphs(data+start,end-start);
            }
            output.items.push_back({std::move(name),L"アイテム "+std::to_wstring(id),
                flags&64?L"呪い":flags&128?L"装備中":flags&32?L"未鑑定":L"",static_cast<uint8_t>(flags)});
        }
    }
    return result;
}

WiizItemTransfer SfcCore::transferItem(unsigned from,unsigned item,unsigned to) {
    return editInventory(from,item,to,WIIZ_ITEM_GIVE);
}

WiizItemTransfer SfcCore::editItem(unsigned member,unsigned item,WiizItemAction action) {
    return editInventory(member,item,member,action);
}

WiizItemTransfer SfcCore::editInventory(unsigned from,unsigned item,unsigned to,WiizItemAction action) {
    if(active!=this||!itemProfile||(from==to&&action==WIIZ_ITEM_GIVE)||!wizardryCanEditItems())
        return WIIZ_ITEM_UNAVAILABLE;
    auto* ram=const_cast<uint8_t*>(systemRam());
    auto* sram=static_cast<uint8_t*>(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM));
    if(!ram||!sram||retro_get_memory_size(RETRO_MEMORY_SAVE_RAM)<0x1400||from>=ram[0xdb]||to>=ram[0xdb])
        return WIIZ_ITEM_UNAVAILABLE;
    const unsigned slots[]={ram[0xdc+from],ram[0xdc+to]};
    const unsigned changes=from==to?1:2;
    std::array<std::array<uint8_t,128>,2> records;
    for(unsigned i=0;i<2;++i)std::copy_n(ram+0x1000+slots[i]*128,128,records[i].begin());
    auto& src=records[0];auto& dst=records[1];
    const unsigned n=src[0x53],m=dst[0x53];
    if(n>8||m>8||item>=n||src[0x4b+item]>=136)return WIIZ_ITEM_UNAVAILABLE;
    if(action==WIIZ_ITEM_TOGGLE_EQUIP) {
        auto& flags=src[0x43+item];
        if(flags&64)return WIIZ_ITEM_CURSED;
        if(flags&128)flags&=127;
        else {
            const size_t at=0x1bb71+src[0x4b+item]*38;
            const auto* data=reinterpret_cast<const uint8_t*>(rom.data());
            const unsigned type=data[at+4],job=(src[0xa]>>2)&7;
            if((flags&32)||type>=6||!(data[at+19]&(1u<<job)))return WIIZ_ITEM_INELIGIBLE;
            for(unsigned i=0;i<n;++i) {
                if(i==item||!(src[0x43+i]&128))continue;
                const unsigned id=src[0x4b+i];
                if(id>=136)return WIIZ_ITEM_UNAVAILABLE;
                if(data[0x1bb71+id*38+4]!=type)continue;
                if(src[0x43+i]&64)return WIIZ_ITEM_CURSED;
                src[0x43+i]&=127;
            }
            flags|=128;
            if(data[at+6]||(!(data[at+5]&128)&&data[at+5]!=(src[0xa]&3)))flags|=64;
        }
    } else if(action==WIIZ_ITEM_MOVE_UP||action==WIIZ_ITEM_MOVE_DOWN) {
        const unsigned row=action==WIIZ_ITEM_MOVE_UP?item-1:item+1;
        if(row>=n)return WIIZ_ITEM_UNAVAILABLE;
        std::swap(src[0x43+item],src[0x43+row]);
        std::swap(src[0x4b+item],src[0x4b+row]);
    } else {
        if(src[0x43+item]&0xc0)return WIIZ_ITEM_EQUIPPED;
        if(action==WIIZ_ITEM_GIVE) {
            if(m==8)return WIIZ_ITEM_FULL;
            dst[0x43+m]=src[0x43+item];dst[0x4b+m]=src[0x4b+item];++dst[0x53];
        }
        std::move(src.begin()+0x44+item,src.begin()+0x43+n,src.begin()+0x43+item);
        std::move(src.begin()+0x4c+item,src.begin()+0x4b+n,src.begin()+0x4b+item);
        src[0x43+n-1]=src[0x4b+n-1]=0;--src[0x53];
    }
    const auto before=state();
    buttons=0;
    for(unsigned i=0;i<changes;++i) {
        auto* live=ram+0x1000+slots[i]*128;
        std::copy(records[i].begin(),records[i].end(),live);
        const bool started=wizardryRequestItemRecalc(slots[i]);
        if(started)for(unsigned frame=0;frame<12&&wizardryItemRecalcStatus()!=2;++frame){pcm.clear();retro_run();}
        const bool completed=wizardryItemRecalcStatus()==2;
        if(completed) {
            for(unsigned offset:{0x26u,0x59u,0x6bu,0x6cu,0x6du,0x6eu,0x6fu})
                records[i][offset]=live[offset];
        }
        wizardryCancelItemRecalc();
        if(!loadState(before))throw std::runtime_error("編集前の状態を復元できません");
        if(!completed||!updateRosterCheck(records[i],reinterpret_cast<const uint8_t*>(rom.data())))
            return WIIZ_ITEM_UNAVAILABLE;
    }
    for(unsigned i=0;i<changes;++i) {
        const auto& record=records[i];
        const unsigned offset=slots[i]*128;
        writeRosterRecord(slots[i],record,ram,sram);
        const unsigned work=ram[0x30]|(ram[0x31]<<8),stack=ram[0x1a]|(ram[0x1b]<<8);
        if(work==0x1000+offset&&stack>=0x1f00&&stack<0x1fe0)
            for(unsigned j=stack+1;j+2<0x2000;++j)
                if(ram[j]==0x71&&ram[j+1]==0xaa&&ram[j+2]==4){
                    std::copy_n(record.begin()+0x43,17,ram+0x50);break;
                }
    }
    return WIIZ_ITEM_MOVED;
}
#endif
