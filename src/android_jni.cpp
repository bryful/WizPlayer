#include <jni.h>
#include "game.h"
#include "built_in_bugfix.h"
#include "audio_resampler.h"
#include "game_support.h"
#include "turbo_runner.h"
#include "dungeon_map.h"
#include "item_menu.h"
#include "console_inventory.h"
#include "nes/wizardry_map.h"
#include "nes/core_bridge.h"
#include "nes/android_turbo.h"
#include "nes/wizardry.h"
#ifdef WIZPLAYER_SFC
#include "sfc/core.h"
#endif
#include <algorithm>
#include <array>
void cartridge_set_rumble(unsigned) {}
namespace {
struct Input : gambatte::InputGetter { unsigned buttons=0; unsigned operator()() override { return buttons; } };
struct Session {
    Input input;
    std::unique_ptr<gambatte::GB> core;
    Resampler audio;
    std::array<gambatte::video_pixel_t,160*144> pixels{};
    std::array<gambatte::uint_least32_t,40000> sound{};
    GameProfile profile{GameId::Unknown,"Unknown"};
    unsigned skippedFrames=0;
    std::vector<char> bootRom;
    bool skipLogo=true;
    TurboRunner turbo;
    DungeonMap dungeonMap;
    ItemMenu items;
    std::vector<char> nesRom;
    std::unique_ptr<WiizCore, decltype(&wiiz_core_delete)> nes{nullptr, wiiz_core_delete};
    std::unique_ptr<FcTurbo> nesTurbo;
    unsigned nesAudioPhase=0;
    std::array<jint,256*240> nesPixels{};
    std::unique_ptr<WizardryFeatures> wizardry;
    bool nesMapOpen=false;
    unsigned mapFloor=1,mapX=0,mapY=0;
    ConsoleInventory consoleInventory;
#ifdef WIZPLAYER_SFC
    std::unique_ptr<SfcCore> sfc;
#endif
};
Session& get(jlong handle) { return *reinterpret_cast<Session*>(handle); }
std::vector<char> bytes(JNIEnv* env,jbyteArray data) {
    if(!data) return {};
    std::vector<char> result(env->GetArrayLength(data));
    env->GetByteArrayRegion(data,0,result.size(),reinterpret_cast<jbyte*>(result.data()));
    return result;
}
jbyteArray array(JNIEnv* env,const void* data,size_t size) {
    auto result=env->NewByteArray(static_cast<jsize>(size));
    if(result && size) env->SetByteArrayRegion(result,0,size,static_cast<const jbyte*>(data));
    return result;
}
void error(JNIEnv* env,const char* message) { env->ThrowNew(env->FindClass("java/lang/IllegalStateException"),message); }
constexpr char fcMapStateTag[]="WZFCMAP1";
constexpr size_t fcMapStateBytes=8+WIIZ_MAP_BYTES;

bool usesConsoleInventory(const Session& s) {
#ifdef WIZPLAYER_SFC
    if(s.sfc)return true;
#endif
    return bool(s.nes);
}
std::vector<WizardryPartyMember> consoleItems(const Session& s) {
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->items();
#endif
    return s.wizardry?s.wizardry->items(s.nes.get(),s.nesRom):std::vector<WizardryPartyMember>{};
}
WiizItemTransfer transferConsoleItem(Session& s,unsigned from,unsigned item,unsigned to) {
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->transferItem(from,item,to);
#endif
    return wiiz_core_item_transfer(s.nes.get(),reinterpret_cast<uint8_t*>(s.nesRom.data()),s.nesRom.size(),from,item,to);
}
WiizItemTransfer editConsoleItem(Session& s,unsigned member,unsigned item,WiizItemAction action) {
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->editItem(member,item,action);
#endif
    return wiiz_core_item_edit(s.nes.get(),reinterpret_cast<uint8_t*>(s.nesRom.data()),s.nesRom.size(),member,item,action);
}
jobjectArray consoleItemLines(JNIEnv* env,const Session& s) {
    std::vector<std::wstring> lines;
    for(const auto& line:s.consoleInventory.lines())
        lines.push_back(std::to_wstring(line.x)+L"|"+std::to_wstring(line.y)+L"|"+
            std::to_wstring(line.width)+L"|"+std::to_wstring(line.height)+
            (line.selected?L"|1|":L"|0|")+line.label);
    auto result=env->NewObjectArray(static_cast<jsize>(lines.size()),env->FindClass("java/lang/String"),nullptr);
    if(!result)return nullptr;
    for(size_t i=0;i<lines.size();++i) {
        const std::vector<jchar> line(lines[i].begin(),lines[i].end());
        auto value=env->NewString(line.data(),line.size());
        if(!value)return nullptr;
        env->SetObjectArrayElement(result,i,value);
        env->DeleteLocalRef(value);
    }
    return result;
}
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_hasBuiltInBugFix(JNIEnv* env,jclass,jbyteArray rom) {
    try {
        return !builtInBugFix(bytes(env,rom)).empty();
    }catch(const std::exception& e){error(env,e.what());return false;}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_jp_gbwiz_NativeCore_applyBuiltInBugFix(JNIEnv* env,jclass,jbyteArray rom) {
    try {
        auto data=bytes(env,rom);
        const auto patch=builtInBugFix(data);
        if(patch.empty())throw std::runtime_error("対応する内蔵パッチがありません");
        applyIpsPatch(data,patch);
        return array(env,data.data(),data.size());
    }catch(const std::exception& e){error(env,e.what());return nullptr;}
}
extern "C" JNIEXPORT jstring JNICALL Java_jp_gbwiz_NativeCore_handheldCandidate(JNIEnv* env,jclass,jbyteArray rom) {
    try {
        const auto data=bytes(env,rom);
        const auto candidate=identifyGameLayout(data);
        if(candidate.id!=GameId::Unknown&&candidate.id!=identifyGameHeader(data).id)
            return env->NewStringUTF(candidate.name);
        return nullptr;
    }catch(const std::exception& e){error(env,e.what());return nullptr;}
}
extern "C" JNIEXPORT jlong JNICALL Java_jp_gbwiz_NativeCore_openConfigured(JNIEnv* env,jclass,jbyteArray rom,jbyteArray ram,jboolean skipLogo,jbyteArray turboFile,jboolean useProfile) {
    try {
        auto session=std::make_unique<Session>();
        session->skipLogo=skipLogo;
        auto romData=bytes(env,rom);
        if(romData.size()>=4 && std::memcmp(romData.data(),"NES\x1a",4)==0) {
            session->nesRom=std::move(romData);
            session->nes.reset(wiiz_core_new(reinterpret_cast<uint8_t*>(session->nesRom.data()),
                                            session->nesRom.size()));
            if(!session->nes) throw std::runtime_error("FC ROMを読み込めません");
            auto wizardry=std::make_unique<WizardryFeatures>(session->nesRom);
            if(wizardry->scenario) {
                wiiz_core_wizardry(session->nes.get(),wizardry->scenario);
                wiiz_core_turbo_file_attach(session->nes.get());
                if(turboFile) {
                    const auto card=bytes(env,turboFile);
                    if(!wiiz_core_turbo_file_write(session->nes.get(),reinterpret_cast<const uint8_t*>(card.data()),card.size()))
                        throw std::runtime_error("ターボファイルのサイズが8192バイトではありません");
                }
                session->wizardry=std::move(wizardry);
            }
            session->nesTurbo=std::make_unique<FcTurbo>(session->nes.get(),session->nesRom,
                session->wizardry?session->wizardry->scenario:0);
            if(ram) {
                auto save=bytes(env,ram);
                if(save.size()!=wiiz_core_sram_size(session->nes.get()) ||
                   (!save.empty() && !wiiz_core_sram_write(session->nes.get(),
                       reinterpret_cast<uint8_t*>(save.data()),save.size())))
                    throw std::runtime_error("通常セーブのサイズが一致しません");
            }
            if(skipLogo&&session->wizardry)session->nesTurbo->skipBootLogo();
            return reinterpret_cast<jlong>(session.release());
        }
        if(useProfile)session->profile=identifyGame(romData);
        session->core=loadCoreBytes(romData,&session->input);
        if(ram) {
            auto save=bytes(env,ram);
            if(save.size()!=session->core->savedata_size()) throw std::runtime_error("Save size mismatch");
            if(!save.empty()) std::memcpy(session->core->savedata_ptr(),save.data(),save.size());
        }
        if(useProfile&&(session->profile.id==GameId::Gaiden1||session->profile.id==GameId::Gaiden2||session->profile.id==GameId::Gaiden3||MusicPlayer::supported(romData)))session->bootRom=std::move(romData);
        if(session->skipLogo)session->skippedFrames=skipBootLogos(session->core,session->profile,session->bootRom,&session->input,session->pixels).frames;
        session->turbo.attach(*session->core,session->bootRom,session->profile);
        session->dungeonMap.attach(session->bootRom);
        session->items.attach(session->bootRom);
        return reinterpret_cast<jlong>(session.release());
    } catch(const std::exception& e) { error(env,e.what()); return 0; }
}
extern "C" JNIEXPORT jint JNICALL Java_jp_gbwiz_NativeCore_step(JNIEnv* env,jclass,jlong h,jint buttons,jintArray video,jshortArray audio) {
    try {
        auto& s=get(h);
#ifdef WIZPLAYER_SFC
        if(s.sfc) {
            s.sfc->step(buttons);
            const auto& frame=s.sfc->pixels;
            const auto& pcm=s.sfc->pcm;
            if(env->GetArrayLength(video)<static_cast<jsize>(frame.size()) ||
               env->GetArrayLength(audio)<static_cast<jsize>(pcm.size()))
                throw std::runtime_error("Output buffer too small");
            env->SetIntArrayRegion(video,0,frame.size(),reinterpret_cast<const jint*>(frame.data()));
            if(!pcm.empty()) env->SetShortArrayRegion(audio,0,pcm.size(),pcm.data());
            return pcm.size();
        }
#endif
        if(s.nes) {
            const unsigned input=(buttons&15u) | ((buttons&16u)?PAD_RIGHT:0) |
                ((buttons&32u)?PAD_LEFT:0) | ((buttons&64u)?PAD_UP:0) | ((buttons&128u)?PAD_DOWN:0);
            size_t samples=0;
            const float* mono=s.nesTurbo->step(input,&samples);
            std::array<uint32_t,256*240> rgba;
            wiiz_core_pixels(s.nes.get(),rgba.data());
            for(size_t i=0;i<rgba.size();++i) {
                const uint32_t color=rgba[i];
                s.nesPixels[i]=static_cast<jint>(0xff000000u | ((color&0xffu)<<16) |
                    (color&0xff00u) | ((color>>16)&0xffu));
            }
            std::vector<jshort> pcm;
            pcm.reserve(samples*2);
            for(size_t i=0;i<samples;++i) {
                s.nesAudioPhase+=32768;
                if(s.nesAudioPhase<48000) continue;
                s.nesAudioPhase-=48000;
                const auto sample=static_cast<jshort>(std::clamp(mono[i],-1.0f,1.0f)*32767);
                pcm.push_back(sample);
                pcm.push_back(sample);
            }
            if(env->GetArrayLength(video)<static_cast<jsize>(s.nesPixels.size()) ||
               env->GetArrayLength(audio)<static_cast<jsize>(pcm.size()))
                throw std::runtime_error("Output buffer too small");
            env->SetIntArrayRegion(video,0,s.nesPixels.size(),s.nesPixels.data());
            if(!pcm.empty()) env->SetShortArrayRegion(audio,0,pcm.size(),pcm.data());
            return pcm.size();
        }
        unsigned samples=s.turbo.run(buttons,s.input.buttons,s.pixels,s.sound);
        std::array<jint,160*144> pixels;
        for(size_t i=0;i<pixels.size();++i) pixels[i]=0xff000000u|s.pixels[i];
        auto pcm=s.audio.convert(s.sound.data(),samples);
        if(env->GetArrayLength(video)<static_cast<jsize>(pixels.size()) || env->GetArrayLength(audio)<static_cast<jsize>(pcm.size()))
            throw std::runtime_error("Output buffer too small");
        env->SetIntArrayRegion(video,0,pixels.size(),pixels.data());
        env->SetShortArrayRegion(audio,0,pcm.size(),pcm.data());
        return pcm.size();
    } catch(const std::exception& e) { error(env,e.what()); return 0; }
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_jp_gbwiz_NativeCore_battery(JNIEnv* env,jclass,jlong h) {
    try {
        auto& s=get(h);
#ifdef WIZPLAYER_SFC
        if(s.sfc) {auto ram=s.sfc->battery();return array(env,ram.data(),ram.size());}
#endif
        if(s.nes) {
            std::vector<uint8_t> ram(wiiz_core_sram_size(s.nes.get()));
            if(!ram.empty() && !wiiz_core_sram_read(s.nes.get(),ram.data(),ram.size()))
                throw std::runtime_error("FC通常セーブを取得できません");
            return array(env,ram.data(),ram.size());
        }
        return array(env,s.core->savedata_ptr(),s.core->savedata_size());
    } catch(const std::exception& e) {error(env,e.what());return nullptr;}
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_jp_gbwiz_NativeCore_exploration(JNIEnv* env,jclass,jlong h) {
    const auto& s=get(h);
    return s.wizardry?array(env,wiiz_core_map_explored(s.nes.get()),WIIZ_MAP_BYTES):nullptr;
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_jp_gbwiz_NativeCore_turboFile(JNIEnv* env,jclass,jlong h) {
    const auto& s=get(h);
    std::array<uint8_t,WIIZ_TURBO_FILE_BYTES> card;
    if(!s.nes||!wiiz_core_turbo_file_read(s.nes.get(),card.data(),card.size()))return nullptr;
    return array(env,card.data(),card.size());
}
extern "C" JNIEXPORT void JNICALL Java_jp_gbwiz_NativeCore_loadExploration(JNIEnv* env,jclass,jlong h,jbyteArray data) {
    const auto& s=get(h);
    if(!s.wizardry||!data||env->GetArrayLength(data)!=WIIZ_MAP_BYTES)return;
    std::array<uint8_t,WIIZ_MAP_BYTES> map{};
    env->GetByteArrayRegion(data,0,map.size(),reinterpret_cast<jbyte*>(map.data()));
    wiiz_core_map_set_explored(s.nes.get(),map.data());
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_jp_gbwiz_NativeCore_state(JNIEnv* env,jclass,jlong h) {
    try {
        auto& s=get(h);
#ifdef WIZPLAYER_SFC
        if(s.sfc) {auto state=s.sfc->state();return array(env,state.data(),state.size());}
#endif
        if(s.nes) {
            auto state=s.nesTurbo->saveState();
            if(s.wizardry) {
                const auto* map=wiiz_core_map_explored(s.nes.get());
                state.insert(state.end(),map,map+WIIZ_MAP_BYTES);
                state.insert(state.end(),fcMapStateTag,fcMapStateTag+8);
            }
            return array(env,state.data(),state.size());
        }
        auto data=s.turbo.saveState();return array(env,data.data(),data.size());
    }catch(const std::exception& e){error(env,e.what());return nullptr;}
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_loadState(JNIEnv* env,jclass,jlong h,jbyteArray state) {
    try{auto& s=get(h);auto data=bytes(env,state);
#ifdef WIZPLAYER_SFC
        if(s.sfc)return s.sfc->loadState(data);
#endif
        if(s.nes) {
            const bool hasMap=s.wizardry&&data.size()>=fcMapStateBytes&&
                std::memcmp(data.data()+data.size()-8,fcMapStateTag,8)==0;
            std::array<uint8_t,WIIZ_MAP_BYTES> map{};
            if(hasMap) {
                std::copy_n(data.end()-fcMapStateBytes,WIIZ_MAP_BYTES,map.begin());
                data.resize(data.size()-fcMapStateBytes);
            }
            if(!s.nesTurbo->loadState(data))return false;
            if(hasMap)wiiz_core_map_set_explored(s.nes.get(),map.data());
            s.nesAudioPhase=0;return true;
        }
        if(!s.turbo.loadState(data))return false;s.audio.reset();return true;}
    catch(const std::exception& e){error(env,e.what());return false;}
}
extern "C" JNIEXPORT jint JNICALL Java_jp_gbwiz_NativeCore_speed(JNIEnv*,jclass,jlong h){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->speed();
#endif
    if(s.nes)return s.nesTurbo->speed();
    return s.core?s.turbo.speed():2;
}
extern "C" JNIEXPORT jlong JNICALL Java_jp_gbwiz_NativeCore_framePeriod(JNIEnv*,jclass,jlong h) {
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->period();
#endif
    return s.nes?16639267:16742706;
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_sfcAvailable(JNIEnv*,jclass) {
#ifdef WIZPLAYER_SFC
    return true;
#else
    return false;
#endif
}
extern "C" JNIEXPORT jlong JNICALL Java_jp_gbwiz_NativeCore_openSfc(JNIEnv* env,jclass,jbyteArray rom,jbyteArray ram,jboolean skipLogo) {
#ifdef WIZPLAYER_SFC
    try {
        auto session=std::make_unique<Session>();
        auto save=bytes(env,ram);
        session->sfc=std::make_unique<SfcCore>(bytes(env,rom),ram?&save:nullptr,skipLogo);
        return reinterpret_cast<jlong>(session.release());
    } catch(const std::exception& e) {error(env,e.what());}
#else
    error(env,"このAPKにはSFC実行部がありません");
#endif
    return 0;
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_rewardsSupported(JNIEnv*,jclass,jlong h){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->rewardsSupported();
#endif
    if(s.nes)return wiiz_core_reward_supported(s.nes.get(),0);
    return s.turbo.rewards().supported();
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_goldRewardsSupported(JNIEnv*,jclass,jlong h){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->rewardsSupported();
#endif
    if(s.nes)return wiiz_core_reward_supported(s.nes.get(),1);
    return s.turbo.rewards().goldSupported();
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_setRewards(JNIEnv*,jclass,jlong h,jint experience,jint gold){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->setRewards(experience,gold);
#endif
    if(s.nes)return wiiz_core_rewards(s.nes.get(),experience,gold);
    return s.turbo.rewards().set(experience,gold);
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_maxBonusSupported(JNIEnv*,jclass,jlong h){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->assistsSupported();
#endif
    if(s.nes)return wiiz_core_wizardry_assists_supported(s.nes.get());
    return s.turbo.creation().supported();
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_setMaxBonus(JNIEnv*,jclass,jlong h,jboolean enabled){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->setMaxBonus(enabled);
#endif
    if(s.nes)return wiiz_core_set_wizardry_assist(s.nes.get(),WIIZ_ASSIST_BONUS,enabled);
    return s.turbo.creation().enable(enabled);
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_mpSupported(JNIEnv*,jclass,jlong h){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->assistsSupported();
#endif
    if(s.nes)return wiiz_core_wizardry_assists_supported(s.nes.get());
    return s.turbo.mp().supported();
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_setNoMpCost(JNIEnv*,jclass,jlong h,jboolean enabled){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->setNoMpCost(enabled);
#endif
    if(s.nes)return wiiz_core_set_wizardry_assist(s.nes.get(),WIIZ_ASSIST_MP,enabled);
    return s.turbo.mp().enable(enabled);
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_noMpCost(JNIEnv*,jclass,jlong h){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->noMpCostEnabled();
#endif
    if(s.nes)return wiiz_core_wizardry_assist_enabled(s.nes.get(),WIIZ_ASSIST_MP);
    return s.turbo.mp().enabled();
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_partySupported(JNIEnv*,jclass,jlong h){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->assistsSupported();
#endif
    if(s.nes)return wiiz_core_wizardry_assists_supported(s.nes.get());
    return s.turbo.party().supported();
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_setMixedParty(JNIEnv*,jclass,jlong h,jboolean enabled){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->setMixedParty(enabled);
#endif
    if(s.nes)return wiiz_core_set_wizardry_assist(s.nes.get(),WIIZ_ASSIST_MIXED,enabled);
    return s.turbo.party().enable(enabled);
}
extern "C" JNIEXPORT jint JNICALL Java_jp_gbwiz_NativeCore_effectsSupported(JNIEnv*,jclass,jlong h){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->assistsSupported()?15:0;
#endif
    if(s.nes)return wiiz_core_wizardry_assists_supported(s.nes.get())?7:0;
    return 0;
}
extern "C" JNIEXPORT jint JNICALL Java_jp_gbwiz_NativeCore_gameOptionsSupported(JNIEnv*,jclass,jlong h){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->gameOptionsSupported();
#endif
    return s.nes?wiiz_core_ff_options(s.nes.get()):0;
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_setGameOptions(JNIEnv*,jclass,jlong h,jint mask){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->setGameOptions(mask);
#endif
    if(!s.nes)return false;
    auto* core=s.nes.get();
    const unsigned supported=wiiz_core_ff_options(core);
    if(!supported || (static_cast<unsigned>(mask)&~supported))return false;
    return wiiz_core_max_skills(core,(mask&1)!=0) &&
        wiiz_core_free_magic(core,(mask&2)!=0) && wiiz_core_free_jobs(core,(mask&4)!=0);
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_setEffects(JNIEnv*,jclass,jlong h,jint mask){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->setEffects(mask);
#endif
    if(s.nes)return wiiz_core_set_wizardry_effects(s.nes.get(),mask);
    return 0;
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_spellsSupported(JNIEnv*,jclass,jlong h){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->characterEditorSupported();
#endif
    return s.wizardry||(s.core&&s.turbo.spells().supported());
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_characterEditorSupported(JNIEnv*,jclass,jlong h) {
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->characterEditorSupported();
#endif
    return s.wizardry||(s.core&&s.turbo.spells().supported());
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_canEditCharacters(JNIEnv*,jclass,jlong h) {
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->canEditCharacters();
#endif
    if(s.nes)return s.wizardry&&wiiz_core_items_pause(s.nes.get());
    return s.core&&s.turbo.spells().available(*s.core);
}
extern "C" JNIEXPORT jobjectArray JNICALL Java_jp_gbwiz_NativeCore_spellNames(JNIEnv* env,jclass,jlong h){
    try{auto& s=get(h);auto names=env->NewObjectArray(20,env->FindClass("java/lang/String"),nullptr);
        if(!names)return nullptr;
        if(usesConsoleInventory(s)) {
            std::array<std::string,20> roster;
#ifdef WIZPLAYER_SFC
            if(s.sfc)roster=s.sfc->characterNames();
#endif
            if(s.wizardry)roster=s.wizardry->rosterNames(s.nes.get(),s.nesRom);
            for(size_t slot=0;slot<roster.size();++slot)if(!roster[slot].empty()) {
                auto name=env->NewStringUTF(roster[slot].c_str());
                env->SetObjectArrayElement(names,slot,name);
                env->DeleteLocalRef(name);
            }
            return names;
        }
        if(!s.core)return names;
        for(const auto& character:s.turbo.spells().characters(*s.core)){auto name=env->NewStringUTF(character.name.c_str());env->SetObjectArrayElement(names,character.slot,name);env->DeleteLocalRef(name);}return names;
    }catch(const std::exception& e){error(env,e.what());return nullptr;}
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_learnSpells(JNIEnv*,jclass,jlong h,jint slot){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->learnSpells(slot);
#endif
    if(s.nes)return s.wizardry&&wiiz_core_character_learn_spells(s.nes.get(),
        reinterpret_cast<const uint8_t*>(s.nesRom.data()),s.nesRom.size(),slot);
    return s.core&&s.turbo.spells().learn(*s.core,slot);
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_restoreMp(JNIEnv*,jclass,jlong h,jint slot){auto& s=get(h);return s.turbo.spells().restoreMp(*s.core,slot);}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_setAlignment(JNIEnv*,jclass,jlong h,jint slot,jint alignment){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->setAlignment(slot,alignment);
#endif
    if(s.nes)return s.wizardry&&wiiz_core_set_character_alignment(s.nes.get(),slot,alignment);
    return s.core&&s.turbo.spells().setAlignment(*s.core,slot,alignment);
}
extern "C" JNIEXPORT jint JNICALL Java_jp_gbwiz_NativeCore_alignment(JNIEnv*,jclass,jlong h,jint slot){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->alignment(slot);
#endif
    if(s.nes)return s.wizardry?wiiz_core_character_alignment(s.nes.get(),slot):-1;
    return s.core?s.turbo.spells().alignment(*s.core,slot):-1;
}
extern "C" JNIEXPORT jint JNICALL Java_jp_gbwiz_NativeCore_characterTraitCount(JNIEnv*,jclass,jlong h,jboolean race){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->characterEditorSupported()?(race?5:8):0;
#endif
    if(s.nes)return s.wizardry?(race?5:8):0;
    return s.core?s.turbo.spells().characterTraitCount(race?CharacterTrait::Race:CharacterTrait::Job):0;
}
extern "C" JNIEXPORT jint JNICALL Java_jp_gbwiz_NativeCore_characterTrait(JNIEnv*,jclass,jlong h,jint slot,jboolean race){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->characterTrait(slot,race);
#endif
    if(s.nes)return s.wizardry?wiiz_core_character_trait(s.nes.get(),slot,race):-1;
    return s.core?s.turbo.spells().characterTrait(*s.core,slot,race?CharacterTrait::Race:CharacterTrait::Job):-1;
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_setCharacterTrait(JNIEnv*,jclass,jlong h,jint slot,jboolean race,jint value){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->setCharacterTrait(slot,race,value);
#endif
    if(s.nes)return s.wizardry&&wiiz_core_set_character_trait(s.nes.get(),slot,race,value);
    return s.core&&s.turbo.spells().setCharacterTrait(*s.core,slot,race?CharacterTrait::Race:CharacterTrait::Job,value);
}
extern "C" JNIEXPORT jint JNICALL Java_jp_gbwiz_NativeCore_skippedFrames(JNIEnv*,jclass,jlong h){return get(h).skippedFrames;}
extern "C" JNIEXPORT void JNICALL Java_jp_gbwiz_NativeCore_setSkipLogo(JNIEnv*,jclass,jlong h,jboolean enabled){auto& s=get(h);
    s.skipLogo=enabled;
#ifdef WIZPLAYER_SFC
    if(s.sfc)s.sfc->setSkipLogo(enabled);
#endif
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_setSpeed(JNIEnv* env,jclass,jlong h,jint halfSpeed){
    try{
        auto& s=get(h);
#ifdef WIZPLAYER_SFC
        if(s.sfc)return s.sfc->setSpeed(halfSpeed);
#endif
        if(s.nes)return s.nesTurbo->setSpeed(halfSpeed);
        return s.core?s.turbo.speed(halfSpeed):halfSpeed==2;
    }catch(const std::exception& e){error(env,e.what());return false;}
}
extern "C" JNIEXPORT void JNICALL Java_jp_gbwiz_NativeCore_reset(JNIEnv* env,jclass,jlong h) {
    try{auto& s=get(h);if(s.nes){s.nesTurbo->reset(s.skipLogo);s.nesAudioPhase=0;return;}
#ifdef WIZPLAYER_SFC
        if(s.sfc){s.sfc->reset();return;}
#endif
        int speed=s.turbo.speed();unsigned exp=s.turbo.rewards().experience(),gold=s.turbo.rewards().gold();bool bonus=s.turbo.creation().enabled(),noMpCost=s.turbo.mp().enabled(),mixedParty=s.turbo.party().enabled();s.turbo.detach();s.input.buttons=0;s.core->reset();s.pixels.fill(0);
        s.skippedFrames=s.skipLogo?skipBootLogos(s.core,s.profile,s.bootRom,&s.input,s.pixels).frames:0;
        s.turbo.attach(*s.core,s.bootRom,s.profile);s.turbo.speed(speed);s.turbo.rewards().set(exp,gold);s.turbo.creation().enable(bonus);s.turbo.mp().enable(noMpCost);s.turbo.party().enable(mixedParty);s.audio.reset();
    }catch(const std::exception& e){error(env,e.what());}
}
extern "C" JNIEXPORT void JNICALL Java_jp_gbwiz_NativeCore_close(JNIEnv*,jclass,jlong h) { delete reinterpret_cast<Session*>(h); }

extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_openMap(JNIEnv*,jclass,jlong h){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->openMap();
#endif
    if(s.nes) {
        if(!s.wizardry||wiiz_core_items_battle(s.nes.get()))return false;
        const auto position=wiiz_core_map_position(s.nes.get());
        const auto floor=(position>>16)&255;
        s.mapFloor=floor&&floor<=s.wizardry->floors.size()?floor:1;
        s.mapX=(position&255)%20;
        s.mapY=((position>>8)&255)%20;
        wiiz_core_map_record_position(s.nes.get());
        s.nesMapOpen=true;
        return true;
    }
    return s.core&&s.dungeonMap.browse(*s.core);
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_mapKey(JNIEnv*,jclass,jlong h,jint bits){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->mapKey(bits);
#endif
    if(!s.nes)return s.dungeonMap.key(bits);
    if(!s.nesMapOpen)return false;
    if(bits&2){s.nesMapOpen=false;return false;}
    const auto levels=s.wizardry->floors.size();
    if(bits&(256|4))s.mapFloor=s.mapFloor==1?levels:s.mapFloor-1;
    if(bits&(512|8))s.mapFloor=s.mapFloor==levels?1:s.mapFloor+1;
    if(bits&16)s.mapX=(s.mapX+1)%20;
    if(bits&32)s.mapX=(s.mapX+19)%20;
    if(bits&64)s.mapY=(s.mapY+1)%20;
    if(bits&128)s.mapY=(s.mapY+19)%20;
    return (bits&1)!=0;
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_mapIsOpen(JNIEnv*,jclass,jlong h){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->mapIsOpen();
#endif
    return s.nes?s.nesMapOpen:s.dungeonMap.open;
}
extern "C" JNIEXPORT jint JNICALL Java_jp_gbwiz_NativeCore_mapFloor(JNIEnv*,jclass,jlong h){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc)return s.sfc->selectedFloor();
#endif
    return s.nesMapOpen?s.mapFloor:0;
}
extern "C" JNIEXPORT jint JNICALL Java_jp_gbwiz_NativeCore_mapRegion(JNIEnv*,jclass,jlong h){
#ifdef WIZPLAYER_SFC
    auto& s=get(h);
    if(s.sfc)return s.sfc->selectedRegion();
#endif
    return 0;
}
extern "C" JNIEXPORT void JNICALL Java_jp_gbwiz_NativeCore_closeMap(JNIEnv*,jclass,jlong h){
    auto& s=get(h);s.dungeonMap.open=false;s.nesMapOpen=false;
#ifdef WIZPLAYER_SFC
    if(s.sfc)s.sfc->closeMap();
#endif
}
extern "C" JNIEXPORT void JNICALL Java_jp_gbwiz_NativeCore_mapFrame(JNIEnv* env,jclass,jlong h,jintArray video){
    auto& s=get(h);
#ifdef WIZPLAYER_SFC
    if(s.sfc) {
        const auto frame=s.sfc->mapFrame();
        if(env->GetArrayLength(video)>=static_cast<jsize>(frame.size()))
            env->SetIntArrayRegion(video,0,frame.size(),reinterpret_cast<const jint*>(frame.data()));
        return;
    }
#endif
    if(s.nes) {
        auto frame=s.nesPixels;
        if(s.nesMapOpen)renderNesMap(s.nes.get(),*s.wizardry,s.mapFloor,s.mapX,s.mapY,frame);
        if(env->GetArrayLength(video)>=static_cast<jsize>(frame.size()))env->SetIntArrayRegion(video,0,frame.size(),frame.data());
        return;
    }
    auto frame=s.pixels;if(s.dungeonMap.open)drawDungeonMap(s.dungeonMap,*s.core,frame,true);
    std::array<jint,160*144> output{};for(size_t i=0;i<output.size();i++)output[i]=0xff000000u|frame[i];
    if(env->GetArrayLength(video)>=static_cast<jsize>(output.size()))env->SetIntArrayRegion(video,0,output.size(),output.data());
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_mapMoveCanStart(JNIEnv*,jclass,jlong h) {
    auto& s=get(h);
    return !s.core||s.dungeonMap.inDungeon(*s.core);
}
// Java calls this only after the slot1 write has completed and been read back.
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_moveMapAfterBackup(JNIEnv* env,jclass,jlong h){
    try{auto& s=get(h);
#ifdef WIZPLAYER_SFC
        if(s.sfc)return s.sfc->moveMap();
#endif
        if(s.nes) {
            if(!s.nesMapOpen||!wiiz_core_map_warp(s.nes.get(),s.mapFloor,s.mapX,s.mapY))return false;
            s.nesMapOpen=false;s.nesAudioPhase=0;return true;
        }
        auto& map=s.dungeonMap;
        auto& core=*s.core;
        const unsigned held=s.input.buttons;
        s.input.buttons=0;
        auto pixels=s.pixels;
        const bool moved=moveDungeonMapAfterBackup(map,core,[&] {
            unsigned samples=35112;
            core.runFor(pixels.data(),160,s.sound.data(),s.sound.size(),samples);
        });
        s.input.buttons=held;
        if(!moved)return false;
        s.turbo.resetInput();s.audio.reset();return true;}
    catch(const std::exception& e){error(env,e.what());return false;}
}

extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_learnsMaximumMp(JNIEnv*,jclass,jlong h){return get(h).turbo.spells().learnsMaximumMp();}

extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_openItems(JNIEnv*,jclass,jlong h){
    auto& s=get(h);
    if(usesConsoleInventory(s)) {
        try{
            return s.consoleInventory.show(consoleItems(s));
        }
        catch(const std::runtime_error&){return false;}
    }
    s.items.androidControls=true;
    return s.core&&s.items.show(*s.core,s.turbo.spells());
}
extern "C" JNIEXPORT jboolean JNICALL Java_jp_gbwiz_NativeCore_itemKey(JNIEnv* env,jclass,jlong h,jint bits,jint x,jint y){
    try{auto& s=get(h);
        if(usesConsoleInventory(s)) {
            return s.consoleInventory.key(bits,x,y,[&]{return consoleItems(s);},
                [&](unsigned from,unsigned item,unsigned to){return transferConsoleItem(s,from,item,to);},
                [&](unsigned member,unsigned item,WiizItemAction action){return editConsoleItem(s,member,item,action);});
        }
        if(x>=0)s.items.tap(*s.core,s.turbo.spells(),x,y);else s.items.key(*s.core,s.turbo.spells(),bits);return s.items.open;}
    catch(const std::exception& e){error(env,e.what());return false;}
}
extern "C" JNIEXPORT void JNICALL Java_jp_gbwiz_NativeCore_closeItems(JNIEnv*,jclass,jlong h){
    auto& s=get(h);s.items.open=false;s.consoleInventory.open=false;
}
extern "C" JNIEXPORT jobjectArray JNICALL Java_jp_gbwiz_NativeCore_itemLines(JNIEnv* env,jclass,jlong h){
    auto& s=get(h);if(usesConsoleInventory(s))return consoleItemLines(env,s);
    auto lines=s.items.lines(*s.core);auto result=env->NewObjectArray(static_cast<jsize>(lines.size()),env->FindClass("java/lang/String"),nullptr);
    for(size_t i=0;i<lines.size();i++){auto& l=lines[i];auto text=std::to_string(l.x)+"|"+std::to_string(l.y)+"|"+std::to_string(l.w)+"|"+std::to_string(l.h)+"|"+(l.selected?"1|":"0|")+l.text;auto value=env->NewStringUTF(text.c_str());env->SetObjectArrayElement(result,i,value);env->DeleteLocalRef(value);}return result;
}
