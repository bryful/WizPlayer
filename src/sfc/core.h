#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#if defined(__ANDROID__) || defined(_WIN32) || defined(WIZPLAYER_ANDROID_MAP_CHECK)
#include "wizardry_map.h"
#include "../wizardry_inventory.h"
#include "../nes/core_bridge.h"
#endif

class SfcCore {
    std::vector<char> rom;
    unsigned buttons = 0;
    unsigned sampleRate = 32000;
    unsigned audioPhase = 0;
    int64_t framePeriod = 16639267;
    unsigned pixelFormat = 0;
#if defined(__ANDROID__) || defined(_WIN32) || defined(WIZPLAYER_ANDROID_TURBO_CHECK)
    unsigned speedHalf = 2;
    unsigned frameRemainder = 0;
    unsigned heldButtons = 0;
    struct InputPulse {
        unsigned frames = 0;
        unsigned repeatTicks = 0;
    };
    std::array<InputPulse,12> inputPulses{};
    bool renderVideo = true;
    void applySpeed();
    void clearInputPulses();
#endif
    bool load();
    static SfcCore* active;
    static bool environment(unsigned command, void* data);
    static void video(const void* data, unsigned width, unsigned height, size_t pitch);
    static size_t audio(const int16_t* data, size_t frames);
    static void sample(int16_t left, int16_t right);
    static int16_t input(unsigned port, unsigned device, unsigned index, unsigned id);
#if defined(__ANDROID__) || defined(_WIN32) || defined(WIZPLAYER_ANDROID_MAP_CHECK)
    static constexpr std::array<int,12> mapFloors{1,1,2,3,4,5,6,7,8,8,8,8};
    std::array<SfcDungeonFloor,mapFloors.size()> mapPages;
    int mapPage=0,mapX=0,mapY=0;
    bool mapOpen=false;
    bool itemProfile=false;
    enum class RewardProfile : unsigned { None, WizardryV, DragonQuestV };
    RewardProfile rewardProfile=RewardProfile::None;
    unsigned experienceFactor=1,goldFactor=1;
    unsigned effects=0;
    unsigned dqOptions=0;
    bool dqOptionProfile=false;
    bool assistProfile=false;
    bool maxBonus=false,mixedParty=false,skipLogo=false,noMpCost=false;
    WiizItemTransfer editInventory(unsigned from,unsigned item,unsigned to,WiizItemAction action);
    const uint8_t* characterRecord(unsigned slot)const;
    bool updateCharacterRecord(unsigned slot,std::array<uint8_t,128>& record);
    bool setIdentityField(unsigned slot,unsigned value,unsigned shift,unsigned mask,unsigned limit);
#endif
public:
    static constexpr int width = 256;
    static constexpr int height = 224;
    SfcCore(std::vector<char> data, const std::vector<char>* ram,bool skipBootLogo=false);
    ~SfcCore();
    SfcCore(const SfcCore&) = delete;
    void step(unsigned input);
    void reset();
#if defined(__ANDROID__) || defined(_WIN32) || defined(WIZPLAYER_ANDROID_TURBO_CHECK)
    bool setSpeed(unsigned halfSpeed);
    unsigned speed() const { return speedHalf; }
#endif
    int64_t period() const { return framePeriod; }
    std::vector<char> battery() const;
    std::vector<char> state() const;
    bool loadState(const std::vector<char>& state);
#if defined(__ANDROID__) || defined(_WIN32) || defined(WIZPLAYER_ANDROID_MAP_CHECK)
    bool openMap();
    bool rewardsSupported()const{return rewardProfile!=RewardProfile::None;}
    bool setRewards(unsigned experience,unsigned gold);
    bool assistsSupported()const{return assistProfile;}
    bool noMpCostEnabled()const{return noMpCost;}
    bool setMaxBonus(bool enabled);
    bool setMixedParty(bool enabled);
    bool setSkipLogo(bool enabled);
    bool setNoMpCost(bool enabled);
    bool setEffects(unsigned mask);
    unsigned gameOptionsSupported()const{return dqOptionProfile?31:0;}
    bool setGameOptions(unsigned flags);
    void closeMap(){mapOpen=false;}
    bool mapIsOpen()const{return mapOpen;}
    int selectedFloor()const{return mapOpen?mapFloors[mapPage]:0;}
    int selectedRegion()const{
        if(!mapOpen)return 0;
        if(mapPage<2)return mapPage+1;
        return mapPage>=8?mapPage-7:0;
    }
    bool mapKey(unsigned bits);
    bool moveMap();
    std::vector<uint32_t> mapFrame()const;
    bool characterEditorSupported()const{return itemProfile;}
    bool canEditCharacters()const;
    std::array<std::string,20> characterNames()const;
    int alignment(unsigned slot)const;
    int characterTrait(unsigned slot,bool race)const;
    bool setAlignment(unsigned slot,unsigned value);
    bool setCharacterTrait(unsigned slot,bool race,unsigned value);
    bool learnSpells(unsigned slot);
    std::vector<WizardryPartyMember> items()const;
    WiizItemTransfer transferItem(unsigned from,unsigned item,unsigned to);
    WiizItemTransfer editItem(unsigned member,unsigned item,WiizItemAction action);
#endif
    std::vector<uint32_t> pixels = std::vector<uint32_t>(width * height,0xff000000u);
    std::vector<int16_t> pcm;
};
