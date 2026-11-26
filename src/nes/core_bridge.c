#include "core_bridge.h"
#include "emulator.h"
#include "../../third_party/mame-turbofile/turbofile_device.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ff2_harness.h"
#include "ff3_harness.h"
struct WiizCore { Emulator* e; unsigned buttons; float audio[4096]; size_t audio_count; unsigned sound_commands[3]; int sound_role; unsigned roster_editable,title_ready;
    unsigned queue[42], queue_count, queue_overflow, trace;
    unsigned input_speed,profile,input_timer_live,music_steps,effect_steps;
    unsigned reward_profile,reward_exp,reward_gold,max_skills,free_jobs,free_magic;
    unsigned map_profile, map_events, host_warp, host_camp;
    uint8_t explored[WIIZ_MAP_BYTES];
    unsigned wizardry_audio_role, wizardry_commands[3];
    unsigned wizardry_assists;
    WiizTurboFile turbo_file;
    uint8_t (*prg_ram_read)(Emulator*,uint16_t); };
static void sound_event(WiizCore*,unsigned,unsigned);
static bool ff_audio_write(Emulator*,uint16_t,uint8_t,void*);
static void ff_audio_apu(Emulator*,uint16_t,uint8_t,void*);

#include "battle_rewards.inc"
#include "wizardry_assists.inc"

static bool reward_only_write(Emulator* e,uint16_t addr,uint8_t value,void* userdata)
{
    (void)e;
    return !battle_rewards(userdata,addr,value);
}

void wiiz_core_reward_profile(WiizCore* core,unsigned profile)
{
    core->reward_profile=profile;
    core->e->ram_write_filter=reward_only_write;
    core->e->ram_write_user_data=core;
    if(profile==123)core->e->mapper_prg_ram_read=ff3_job_read;
}

static bool ff_input_write(Emulator* e,uint16_t addr,uint8_t value,void* userdata)
{
    WiizCore* core=userdata;
    if(battle_rewards(core,addr,value))return false;
    const unsigned pc=e->s.c.PC.val;
    const unsigned bank=pc>=0x8000 && pc<0xc000?e->s.m.prg8k_bank[(pc-0x8000)/8192]/2:15;
    const int ff3=core->profile==123;
    if(addr==(ff3?0x14:0x36))
        core->input_timer_live=pc==(ff3?0xfbd2:0xfc5c) || pc==(ff3?0xfbea:0xfc74) || pc==(ff3?0xfbe4:0xfc6e);
    if(core->input_speed<=1)return true;
    if(addr==(ff3?0x14:0x36) && (pc==(ff3?0xfbd2:0xfc5c) || pc==(ff3?0xfbea:0xfc74)))
    {
        e->s.c.ram[addr]=(uint8_t)(value*core->input_speed);return false;
    }
    if(bank==(ff3?30:14))
    {
        if(addr==0xa1 && pc==(ff3?0x9184:0x96a2))core->sound_commands[0]=2*(core->input_speed-1);
        if(addr==0x47 && pc==(ff3?0x9198:0x96b6))
        {
            if(core->sound_commands[0])
            {
                // 押下状態を保ち、待機回だけ元ルーチンの「入力なし」へ戻す。
                --core->sound_commands[0];e->s.c.PC.val=ff3?0x91a0:0x96c2;return false;
            }
            core->sound_commands[0]=core->input_speed-1;
        }
    }
    return true;
}
void wiiz_core_input_profile(WiizCore* core,unsigned profile)
{
    if(profile!=122 && profile!=123)return;
    core->profile=profile;core->sound_role=0;
    core->e->ram_write_filter=ff_input_write;core->e->ram_write_user_data=core;
    core->e->apu_write_observer=NULL;core->e->apu_write_user_data=NULL;
}

static void sound_event(WiizCore* core,unsigned type,unsigned value)
{
    // 地形音の連続通知は最新の種別へまとめ、ゲーム速度分の要求を蓄積しない。
    if(core->profile==122 && (type==9 || type==10) && core->queue_count &&
       (core->queue[core->queue_count-1]>>8)==type)return;
    if(core->queue_count>=42){core->queue_overflow=1;return;}
    core->queue[core->queue_count++]=(type<<8)|value;
    if(core->trace)printf("SOUND type=%u value=%02x pc=%04x SP=%02x\n",type,value,core->e->s.c.PC.val,core->e->s.c.S);
}

#include "ff_audio.inc"

static bool dq4_write(Emulator* e,uint16_t addr,uint8_t value,void* userdata)
{
    WiizCore* core=userdata;
    if(battle_rewards(core,addr,value))return false;
    const unsigned pc=e->s.c.PC.val;
    if(core->sound_role!=1)return true;

    if(core->profile==3)
    {
        const unsigned bank=pc>=0x8000 && pc<0xc000?e->s.m.prg8k_bank[(pc-0x8000)/8192]/2:15;
        if(core->input_speed>1 && (addr==0x18 || addr==0x19) && pc==0xcb57)
        {
            const unsigned previous=e->s.c.ram[addr];
            if((previous==0 && value==7) || (previous==1 && value==11))
            {
                // 共通メニューの描き直し待ち2フレームも元の実時間へ合わせる。
                e->s.c.ram[addr]=(uint8_t)(value*core->input_speed+2*(core->input_speed-1));return false;
            }
        }
        if(bank!=1)return true;
        if(addr==0x6f0 && pc==0x8675)
        {
            const unsigned sp=e->s.c.S;
            const unsigned ret=e->s.c.ram[0x100+((sp+2)&255)] | (e->s.c.ram[0x100+((sp+3)&255)]<<8);
            if(ret==0x85c3 || ret==0x85ae)sound_event(core,ret==0x85c3?1:2,e->s.c.ram[0x100+((sp+1)&255)]);
        }
        if(addr==0xfa && pc==0x854a)sound_event(core,3,0);
        if(addr==0xfa && pc==0x85a2)sound_event(core,5,0);
        return true;
    }
    // 元の長押し状態は保ち、共通メニューの待ち時間だけを実時間に合わせる。
    if(core->input_speed>1 && pc>=0x8000 && pc<0xc000 && e->s.m.prg8k_bank[(pc-0x8000)/8192]/2==18)
    {
        if(addr==0x3cd && pc==0xbceb)
        {
            e->s.c.ram[addr]=(uint8_t)(value+e->s.c.X*(core->input_speed-1));return false;
        }
        if(addr==0x3e2 && pc==0xbd06)
        {
            e->s.c.ram[addr]=(uint8_t)(value+15*(core->input_speed-1));return false;
        }
    }
    if(addr==0xbf && pc==0xf07f)
    {
        const unsigned sp=e->s.c.S;
        const unsigned ret=e->s.c.ram[0x100+((sp+2)&255)] | (e->s.c.ram[0x100+((sp+3)&255)]<<8);
        if(ret==0xef5b || ret==0xf05e)sound_event(core,ret==0xef5b?1:2,e->s.c.ram[0x100+((sp+1)&255)]);
    }
    if(addr==0xbb && pc==0xf0a8)sound_event(core,5,0);
    if(addr==0xbb && pc==0xf05b)sound_event(core,3,0);
    if(addr==0xb0 && pc==0xeedf)sound_event(core,4,value&15);
    return true;
}
static void input(SystemInput* out, void* userdata, bool strobe) {
    (void)strobe;
    const unsigned b=((WiizCore*)userdata)->buttons;
    memset(out, 0, sizeof(*out));
    out->port[0].type = out->port[1].type = CONTROLLER_JOYPAD;
    JoypadInput* pad = &out->port[0].joyp;
    pad->A = !!(b & PAD_A); pad->B = !!(b & PAD_B);
    pad->select = !!(b & PAD_SELECT); pad->start = !!(b & PAD_START);
    pad->up = !!(b & PAD_UP); pad->down = !!(b & PAD_DOWN);
    pad->left = !!(b & PAD_LEFT); pad->right = !!(b & PAD_RIGHT);
}
WiizCore* wiiz_core_new(uint8_t* rom, size_t size) {
    if(size<16 || memcmp(rom,"NES\x1a",4)!=0 || ((rom[7]&0x0c)!=8 && rom[8]>8))
    {
        fprintf(stderr,"Invalid or unsupported ROM header (PRG RAM capacity)\n");return NULL;
    }
    WiizCore* core = calloc(1, sizeof(*core));
    if (!core) return NULL;
    core->input_speed=1;
    EmulatorInit init = {0};
    init.rom.data = rom; init.rom.size = size;
    init.audio_frequency = 48000; init.audio_frames = 512;
    init.ram_init = RAM_INIT_FCEUX;
    core->e = emulator_new(&init);
    if (!core->e) { free(core); return NULL; }
    core->prg_ram_read=core->e->mapper_prg_ram_read;
    // iNES形式で未指定のバッテリーRAMは8 KiBとして扱う。
    if (core->e->ci.has_bat_ram && !core->e->ci.prgram_bytes) {
        core->e->ci.prgram_bytes = 8192;
        core->e->ci.prgram8k_banks = 1;
        core->e->ci.prgram512b_banks = 16;
    }
    emulator_set_joypad_callback(core->e, input, core);

    return core;
}
void wiiz_core_delete(WiizCore* core) {
    if (!core) return;
    // The upstream destructor releases PPU storage and Emulator, but not audio data.
    free(core->e->audio_buffer.data);
    emulator_delete(core->e);
    free(core);
}
void wiiz_core_input(WiizCore* core, unsigned buttons) { core->buttons = buttons;if(buttons)core->roster_editable=0; }
unsigned wiiz_core_roster_editable(WiizCore* core){return core->roster_editable;}
unsigned wiiz_core_title_ready(WiizCore* core) {return core->title_ready;}
void wiiz_core_reset(WiizCore* core) {
    core->title_ready=0;core->roster_editable=0;
    core->buttons = 0;core->queue_count=core->queue_overflow=0;core->sound_commands[0]=0;core->input_timer_live=0;

    emulator_set_reset(core->e, true);
    emulator_set_reset(core->e, false);
}
int wiiz_core_frame(WiizCore* core) {
    Emulator* e = core->e;
    core->audio_count = 0;
    core->music_steps=core->effect_steps=0;
    if(core->queue_overflow)return 0;
    const int ff=core->profile==122 || core->profile==123;
    const unsigned mailbox=core->profile==3 || ff?0x500:0x600,data=core->profile==3 || ff?0x501:0x680;

    if(core->sound_role==2 && core->queue_count && e->s.c.ram[mailbox]==0)
    {
        static const unsigned dq4[]={0,0xef59,0xf05c,0xf055,0xeed3,0xf088};
        static const unsigned dq3[]={0,0x85c1,0x85ac,0x8542,0x7e3,0x8581};
        static const unsigned ff2[]={0,FF2_REQUEST_ADDR,0xdb2e,0xdb45,FF2_BEEP_ADDR,0xd05f,FF2_BATTLE_ADDR,0xde67,
            FF2_MUTE_ADDR,FF2_WATER_ADDR,FF2_WIND_ADDR,FF2_STOP_ADDR,FF2_DOOR_ADDR};
        static const unsigned ff3[]={0,0x600,0x604,0x608,0x60c};
        const unsigned* routines=core->profile==122?ff2:core->profile==123?ff3:core->profile==3?dq3:dq4;
        for(unsigned i=0;i<core->queue_count;++i)
        {
            const unsigned event=core->queue[i],target=routines[event>>8];
            e->s.c.ram[data+i*3]=(uint8_t)target;e->s.c.ram[data+1+i*3]=(uint8_t)(target>>8);e->s.c.ram[data+2+i*3]=(uint8_t)event;
        }
        e->s.c.ram[mailbox]=(uint8_t)(core->queue_count*3);core->queue_count=0;
    }
    Ticks until = emulator_get_ticks(e) + e->master_ticks_per_frame * 2;
    for (;;) {
        const EmulatorEvent event = emulator_run_until(e, until);
        if (event & EMULATOR_EVENT_AUDIO_BUFFER_FULL) {
            AudioBuffer* ab = emulator_get_audio_buffer(e);
            size_t count = ab->frames;
            if (core->audio_count + count > 4096) return 0;
            memcpy(core->audio + core->audio_count, ab->data, count * sizeof(float));
            core->audio_count += count;
        }
        if (event & EMULATOR_EVENT_INVALID_OPCODE) return 0;

        if (event & EMULATOR_EVENT_NEW_FRAME){wizardry_refresh_effects(core);return 1;}
        if (event & EMULATOR_EVENT_UNTIL_TICKS) return 0;
    }
}
void wiiz_core_pixels(WiizCore* core, uint32_t* rgba) { emulator_convert_frame_buffer(core->e, rgba); }
const float* wiiz_core_audio(WiizCore* core, size_t* count) { *count = core->audio_count; return core->audio; }
size_t wiiz_core_sram_size(WiizCore* core) { return core->e->ci.has_bat_ram ? core->e->ci.prgram_bytes : 0; }
int wiiz_core_sram_read(WiizCore* core, uint8_t* bytes, size_t size) {
    FileData data = {bytes, size}; return emulator_write_prg_ram(core->e, &data) == OK;
}
int wiiz_core_sram_write(WiizCore* core, uint8_t* bytes, size_t size) {
    FileData data = {bytes, size}; return emulator_read_prg_ram(core->e, &data) == OK;
}
unsigned wiiz_core_mapper(WiizCore* core) { return core->e->ci.mapper; }
static void turbo_file_port_write(void* context, uint8_t value) {
    turbofile_write(&((WiizCore*)context)->turbo_file, value);
}
static uint8_t turbo_file_port_read(void* context, unsigned port) {
    return turbofile_read(&((WiizCore*)context)->turbo_file, port);
}
void wiiz_core_turbo_file_attach(WiizCore* core) {
    core->turbo_file.enabled = 1;
    core->e->expansion_write = turbo_file_port_write;
    core->e->expansion_read = turbo_file_port_read;
    core->e->expansion_user_data = core;
}
int wiiz_core_turbo_file_read(const WiizCore* core, uint8_t* bytes, size_t size) {
    if (!core->turbo_file.enabled || size != WIIZ_TURBO_FILE_BYTES) return 0;
    memcpy(bytes, core->turbo_file.ram, size);
    return 1;
}
int wiiz_core_turbo_file_write(WiizCore* core, const uint8_t* bytes, size_t size) {
    if (!core->turbo_file.enabled || size != WIIZ_TURBO_FILE_BYTES) return 0;
    memcpy(core->turbo_file.ram, bytes, size);
    return 1;
}
/* Existing upstream S serializer + non-S presentation/resampler adapter state.
   No native pointers are written to disk. Snapshot version is ABI-specific. */
typedef struct {
    uint32_t magic, version, state_bytes, audio_prefix_bytes, audio_capacity, audio_position;
    uint32_t buttons, sound_role, sound_commands[3];
} WiizSnapshot;
enum { TURBO_FILE_STATE_BYTES = WIIZ_TURBO_FILE_BYTES + 4 };
static size_t base_state_size(const WiizCore* core) {
    return sizeof(WiizSnapshot)+sizeof(S)+sizeof(FrameBuffer)+offsetof(AudioBuffer,data)+
        (core->e->audio_buffer.end-core->e->audio_buffer.data)*sizeof(float);
}
size_t wiiz_core_state_size(WiizCore* core) {
    return base_state_size(core) + (core->turbo_file.enabled ? TURBO_FILE_STATE_BYTES : 0);
}
int wiiz_core_state_write(WiizCore* core, uint8_t* bytes, size_t size) {
    if (size != wiiz_core_state_size(core) || core->queue_overflow) return 0;
    AudioBuffer* ab=&core->e->audio_buffer;
    WiizSnapshot header={0};
    header.magic=0x31535a57;header.version=1;header.state_bytes=sizeof(S);
    header.audio_prefix_bytes=offsetof(AudioBuffer,data);header.audio_capacity=(uint32_t)(ab->end-ab->data);
    header.audio_position=(uint32_t)(ab->position-ab->data);header.buttons=core->buttons;header.sound_role=core->sound_role;
    memcpy(header.sound_commands,core->sound_commands,sizeof(header.sound_commands));
    if(core->profile==122 || core->profile==123)header.sound_commands[0]|=core->input_timer_live<<16;
    // 既存の予約フィールドを使用し、旧ステートのサイズを維持する。
    header.sound_commands[2]=core->input_speed;
    header.sound_commands[1]=core->profile;
    memset(bytes,0,size);
    if (core->turbo_file.enabled) {
        const WiizTurboFile* card = &core->turbo_file;
        uint8_t* saved = bytes + base_state_size(core);
        memcpy(saved, card->ram, WIIZ_TURBO_FILE_BYTES);
        saved += WIIZ_TURBO_FILE_BYTES;
        saved[0] = (uint8_t)card->addr;
        saved[1] = (uint8_t)(card->addr >> 8);
        saved[2] = card->bit;
        saved[3] = card->latch;
    }
    memcpy(bytes,&header,sizeof(header));bytes+=sizeof(header);
    FileData state;emulator_init_state_file_data(&state);
    const int ok=emulator_write_state(core->e,&state)==OK;
    if(ok)memcpy(bytes,state.data,state.size);free(state.data);if(!ok)return 0;
    bytes+=sizeof(S);memcpy(bytes,core->e->frame_buffer,sizeof(FrameBuffer));bytes+=sizeof(FrameBuffer);
    memcpy(bytes,ab,offsetof(AudioBuffer,data));bytes+=offsetof(AudioBuffer,data);
    memcpy(bytes,ab->data,header.audio_position*sizeof(float));

    return 1;
}
int wiiz_core_state_read(WiizCore* core, const uint8_t* bytes, size_t size) {
    const size_t base_size = base_state_size(core);
    const uint8_t* card = NULL;
    if (size != base_size) {
        if (!core->turbo_file.enabled || size != base_size + TURBO_FILE_STATE_BYTES) return 0;
        card = bytes + base_size;
        if (card[WIIZ_TURBO_FILE_BYTES + 1] >= 32 || card[WIIZ_TURBO_FILE_BYTES + 2] >= 8) return 0;
    }
    WiizSnapshot header;memcpy(&header,bytes,sizeof(header));bytes+=sizeof(header);

    AudioBuffer* ab=&core->e->audio_buffer;
    if(header.magic!=0x31535a57 || header.version!=1 || header.state_bytes!=sizeof(S) ||
       header.audio_prefix_bytes!=offsetof(AudioBuffer,data) || header.audio_capacity!=(uint32_t)(ab->end-ab->data) ||
       header.audio_position>header.audio_capacity || header.sound_role>2 || header.buttons>255 || header.sound_commands[2]>6 || (header.sound_commands[1]!=0 && header.sound_commands[1]!=3 && header.sound_commands[1]!=4 && header.sound_commands[1]!=122 && header.sound_commands[1]!=123))return 0;
    AudioBuffer prefix={0};
    memcpy(&prefix,bytes+sizeof(S)+sizeof(FrameBuffer),offsetof(AudioBuffer,data));
    if(prefix.frequency!=48000 || prefix.frames!=512 || prefix.bufferi<0 || prefix.bufferi>=128 ||
       prefix.freq_counter>=core->e->cpu_ticks_per_second)return 0;
    FileData state;emulator_init_state_file_data(&state);memcpy(state.data,bytes,sizeof(S));
    const int ok=emulator_read_state(core->e,&state)==OK;free(state.data);if(!ok)return 0;
    bytes+=sizeof(S);memcpy(core->e->frame_buffer,bytes,sizeof(FrameBuffer));bytes+=sizeof(FrameBuffer);
    memcpy(ab,bytes,offsetof(AudioBuffer,data));bytes+=offsetof(AudioBuffer,data);
    memcpy(ab->data,bytes,header.audio_capacity*sizeof(float));ab->position=ab->data+header.audio_position;
    core->buttons=header.buttons;core->audio_count=0;
    core->input_speed=header.sound_commands[2]?header.sound_commands[2]:1;
    if(header.sound_commands[1])core->profile=header.sound_commands[1];
    core->queue_count=core->queue_overflow=0;
    memcpy(core->sound_commands,header.sound_commands,sizeof(core->sound_commands));
    if(core->profile==122 || core->profile==123)
    {
        core->input_timer_live=(core->sound_commands[0]>>16)&1;
        core->sound_commands[0]&=65535;
    }
    if (card) {
        memcpy(core->turbo_file.ram, card, WIIZ_TURBO_FILE_BYTES);
        card += WIIZ_TURBO_FILE_BYTES;
        core->turbo_file.addr = card[0] | (card[1] << 8);
        core->turbo_file.bit = card[2];
        core->turbo_file.latch = card[3];
    }

    return 1;
}

void wiiz_core_profile_source(WiizCore* core,unsigned profile,int trace)
{
    if(profile!=3 && profile!=4 && profile!=122 && profile!=123)return;
    core->profile=profile;core->sound_role=1;core->trace=trace;
    core->e->ram_write_filter=profile>=122?ff_audio_write:dq4_write;core->e->ram_write_user_data=core;
    core->e->mapper_prg_ram_read=profile==123?ff3_job_read:core->prg_ram_read;
    core->e->cpu_ram_read_filter=profile==122 && core->free_magic?ff2_magic_read:NULL;
    core->e->apu_write_observer=profile>=122?ff_audio_apu:NULL;
    core->e->apu_write_user_data=core;
}
void wiiz_core_dq4_source(WiizCore* core,int trace){wiiz_core_profile_source(core,4,trace);}
unsigned wiiz_core_take_sound(WiizCore* core)
{
    if(!core->queue_count)return 0;
    const unsigned result=core->queue[0];--core->queue_count;
    memmove(core->queue,core->queue+1,core->queue_count*sizeof(unsigned));return result;
}
int wiiz_core_sound_command(WiizCore* core,unsigned event)
{

    if((event>>8)<1 || (event>>8)>(core->profile==122?12u:core->profile==123?4u:5u) || core->queue_count>=42)return 0;
    if(core->profile==122 && ((event>>8)==9 || (event>>8)==10) && core->queue_count &&
       (core->queue[core->queue_count-1]>>8)==(event>>8))return 1;
    core->queue[core->queue_count++]=event;return 1;
}
void wiiz_core_input_speed(WiizCore* core,unsigned buttons,unsigned speed)
{
    if(speed<1 || speed>6)speed=1;
    if(core->sound_role==1 && core->profile<122 && core->input_speed!=speed)
    {
        // 切替時に残っている待ち時間も、元の実時間を保って換算する。
        const unsigned previous=core->input_speed?core->input_speed:1;
        const uint8_t now=core->e->s.c.ram[0x50c];
        const unsigned timers[]={core->profile==3?0x18u:0x3cdu,core->profile==3?0x19u:0x3e2u};
        for(unsigned i=0;i<2;++i)
        {
            const unsigned addr=timers[i];
            const unsigned remaining=core->profile==3?core->e->s.c.ram[addr]:(uint8_t)(core->e->s.c.ram[addr]-now);
            if(remaining && remaining<128)
            {
                unsigned adjusted=(remaining*speed+previous-1)/previous;
                if(adjusted>127)adjusted=127;
                core->e->s.c.ram[addr]=(uint8_t)((core->profile==3?0:now)+adjusted);
            }
        }
    }
    if((core->profile==122 || core->profile==123) && core->input_speed!=speed)
    {
        const unsigned previous=core->input_speed?core->input_speed:1;
        const unsigned timer=core->profile==123?0x14:0x36;
        // 同じRAM番地を別用途で使っている場面では書き換えない。
        if(core->input_timer_live)core->e->s.c.ram[timer]=(uint8_t)((core->e->s.c.ram[timer]*speed+previous-1)/previous);
        core->sound_commands[0]=speed==1?0:(core->sound_commands[0]*speed+previous-1)/previous;
    }
    core->buttons=buttons;core->input_speed=speed;
}
unsigned wiiz_core_peek(WiizCore* core,unsigned addr)
{
    if(addr<0x800)return core->e->s.c.ram[addr];
    if(addr>=0x6000 && addr<0x8000)return core->e->s.c.prg_ram[addr-0x6000];
    return 0;
}

int wiiz_core_dq4_player(WiizCore* core)
{
    core->profile=4;
    static const uint8_t boot[]={0x78,0xd8,0xa2,0xff,0x9a,0xa9,0x0,0x8d,0x0,0x20,0x8d,0x1,0x20,0xa2,0x0,0x95,0x0,0x9d,0x0,0x1,0x9d,0x0,0x2,0x9d,0x0,0x3,0x9d,0x0,0x4,0x9d,0x0,0x5,0x9d,0x0,0x6,0xe8,0xd0,0xe9,0xa9,0xe,0x20,0x18,0xc1,0xa9,0x10,0x20,0x2f,0xc1,0xa9,0x19,0x8d,0x86,0x6e,0x20,0x91,0xff,0x20,0x88,0xf0,0xa9,0x80,0x8d,0x0,0x20,0x4c,0x40,0x7};
    memcpy(core->e->s.c.ram+0x700,boot,sizeof(boot));
    static const uint8_t nmi[]={0x48,0x8a,0x48,0x98,0x48,0xa2,0x0,0x8e,0x1f,0x6,0xec,0x0,0x6,0xb0,0x1e,0xbd,0x80,0x6,0x8d,0xe1,0x7,0xbd,0x81,0x6,0x8d,0xe2,0x7,0xbd,0x82,0x6,0x20,0xe0,0x7,0xae,0x1f,0x6,0xe8,0xe8,0xe8,0x8e,0x1f,0x6,0x4c,0x8a,0x7,0xa9,0x0,0x8d,0x0,0x6,0x20,0x85,0xe6,0x68,0xa8,0x68,0xaa,0x68,0x40};
    memcpy(core->e->s.c.ram+0x780,nmi,sizeof(nmi));
    core->sound_role=2;
    core->e->s.c.ram[0x7e0]=0x4c;core->e->s.c.ram[0x7f0]=0x40;
    return 1;
}

unsigned wiiz_core_sound_pending(WiizCore* core){return core->queue_count;}
unsigned wiiz_core_sound_at(WiizCore* core,unsigned index){return index<core->queue_count?core->queue[index]:0;}

int wiiz_core_profile_player(WiizCore* core,unsigned profile)
{
    if(profile==4)return wiiz_core_dq4_player(core);
    if(profile==122 || profile==123)return ff_audio_player(core,profile);

    if(profile!=3)return 0;
    core->profile=3;core->sound_role=2;
    static const uint8_t dq3_boot[]={0x78,0xd8,0xa2,0xff,0x9a,0xa9,0x0,0x8d,0x0,0x20,0x8d,0x1,0x20,0xa2,0x0,0x95,0x0,0x9d,0x0,0x1,0x9d,0x0,0x2,0x9d,0x0,0x3,0x9d,0x0,0x4,0x9d,0x0,0x5,0x9d,0x0,0x6,0xe8,0xd0,0xe9,0xa9,0x80,0x8d,0x0,0x80,0xa9,0x1,0x20,0xbd,0xff,0xa9,0xff,0x8d,0xf0,0x6,0x20,0x81,0x85,0xa9,0x80,0x8d,0x0,0x20,0x4c,0x3d,0x7};
    memcpy(core->e->s.c.ram+0x700,dq3_boot,sizeof(dq3_boot));
    static const uint8_t dq3_nmi[]={0x48,0x8a,0x48,0x98,0x48,0xa2,0x0,0x8e,0x7f,0x5,0xec,0x0,0x5,0xb0,0x1e,0xbd,0x1,0x5,0x8d,0xe1,0x7,0xbd,0x2,0x5,0x8d,0xe2,0x7,0xbd,0x3,0x5,0x20,0xe0,0x7,0xae,0x7f,0x5,0xe8,0xe8,0xe8,0x8e,0x7f,0x5,0x4c,0x8a,0x7,0xa9,0x0,0x8d,0x0,0x5,0x20,0x0,0x80,0x68,0xa8,0x68,0xaa,0x68,0x40};
    memcpy(core->e->s.c.ram+0x780,dq3_nmi,sizeof(dq3_nmi));
    core->e->s.c.ram[0x7e0]=0x4c;core->e->s.c.ram[0x7e3]=0x60;core->e->s.c.ram[0x7f0]=0x40;
    return 1;
}
unsigned wiiz_core_current_music(WiizCore* core)
{
    if(core->profile==122)return core->e->s.c.prg_ram[0xf25];
    if(core->profile==123)return core->e->s.c.prg_ram[0x1f43];
    return core->e->s.c.ram[core->profile==3?0x6f0:0xbf]>>1;
}


/* Wizardry FC host map and inventory pause points, from Wiiz. */
/* Enter the ROM's own floor reload/redraw path only at its dungeon input wait.
   The host map supplies absolute coordinates; spell costs/rules are separate. */
static void host_warp_at_wait(WiizCore* core) {
    Emulator* e=core->e;
    uint8_t* ram=e->s.c.ram;
    const unsigned p=core->host_warp;
    const unsigned base=core->map_profile==1?0x3b:0xea;
    ram[base]=(uint8_t)p;ram[base+1]=(uint8_t)(p>>8);ram[base+2]=(uint8_t)(p>>16);
    core->host_warp=(p&0x00ff0000u)?0x40000000u:0;core->map_events=0;core->buttons=0;
    if(core->map_profile==1) {
        ram[0x14]=0;
        if(!ram[base+2]) {e->s.c.PC.val=0xd8e6;return;} /* original dungeon-to-castle cleanup */
        /* Match the original MALOR return at $D928: reload, then $D293.
           $D265 also runs room-entry events and can wait for another input. */
        ram[0x100+e->s.c.S--]=0xd2;ram[0x100+e->s.c.S--]=0x92;
        e->s.c.PC.val=0xde38;
    } else {
        ram[0x19]=0;ram[0xc2]=0;
        /* III's native dungeon-exit routine ($A75B) clears the split-screen
           height before the travel/save code starts waiting for VBlank. */
        if(core->map_profile==3) {
            ram[0x75]=0;
            /* Native dungeon input dispatch ($A606) enables battery access
               before calling into bank 7. The wait loop leaves it disabled. */
            e->mapper_write(e,0xa001,0x80);
        }
        ram[0xed]=ram[0xea];ram[0xee]=ram[0xeb];ram[0xef]=ram[0xec];
        /* Original MMC3 bank switch, then the ROM's travel/reload routine. */
        const unsigned entry=core->map_profile==2?0x80bf:0x808b;
        ram[0x100+e->s.c.S--]=(uint8_t)((entry-1)>>8);
        ram[0x100+e->s.c.S--]=(uint8_t)(entry-1);
        e->s.c.X=7;e->s.c.PC.val=core->map_profile==2?0xe4bc:0xffd6;
    }
}
static bool wizardry_write(Emulator* e, uint16_t addr, uint8_t value, void* userdata) {
    WiizCore* core = userdata;
    const unsigned profile = core->map_profile;
    const unsigned pc = e->s.c.PC.val;
    const unsigned bank = pc >= 0x8000 ? e->s.m.prg8k_bank[(pc-0x8000)/8192] & (e->ci.prg8k_banks-1) : 0;
    if(profile==1 && bank/2==6 && pc==0x800d && addr==0x600 && value==1)core->title_ready=1;
    if(profile>1 && bank==4 && pc==(profile==2?0x88a2u:0x890cu) && addr==0x603 && value==1)core->title_ready=1;
    if(wizardry_assist_write(core,pc,bank,addr,value))return false;
    multiply_battle_reward(core,pc,bank,addr);
        /* FC-I can stop for a tile event before its ordinary movement loop.
           The floor and view have already been rebuilt at this point. */
        if(profile==1 && core->host_warp==0x40000000u &&
           addr==0x19 && pc==0xc14a &&
           e->s.c.ram[0x100+((e->s.c.S+1)&255)]==0xb1 &&
           e->s.c.ram[0x100+((e->s.c.S+2)&255)]==0xd2) {
            core->host_warp=0;
            core->map_events|=4;
            wiiz_core_map_record_position(core);
        }
        if(core->host_warp && ((profile==1 && ((addr==0x279 && pc==0xd2d9) ||
           (core->host_camp && addr==0x2aa && pc==0xd28d))) ||
           (profile>1 && bank==13 && addr==0xbf && (pc&0x1fff)==(profile==2?0x00cau:0x05dcu)))) {
            if(core->host_warp&0x80000000u) {
                host_warp_at_wait(core);
                core->host_camp=0;
                return true;
            }
            /* The ROM has finished reloading and reached its next input wait. */
            core->host_warp=0;
        }
    if((profile==1 && addr==0x279 && pc==0xd2d9) ||
       (profile>1 && bank==13 && addr==0xbf && (pc&0x1fff)==(profile==2?0x00ca:0x05dc)))
        wiiz_core_map_record_position(core);
    if(addr>=0x600 && addr<=(profile==1?0x602:0x603)) {
        const bool driver=profile==1 ? pc>=0x8000 && pc<0xc000 && bank/2==2 : bank==5;
        if(!driver) {
            if(core->wizardry_audio_role==1 && addr<=0x602)
                core->wizardry_commands[addr-0x600]=0x100|value;
            return core->wizardry_audio_role!=2;
        }
    }
    return true;
}
void wiiz_core_wizardry(WiizCore* core, unsigned scenario) {
    core->map_profile = scenario;
    core->e->ram_write_filter = wizardry_write;
    core->e->ram_write_user_data = core;
}
void wiiz_core_wizardry_audio(WiizCore* core,unsigned role) {
    core->wizardry_audio_role=role;
    memset(core->wizardry_commands,0,sizeof(core->wizardry_commands));
}
unsigned wiiz_core_wizardry_take_sound(WiizCore* core,unsigned reg) {
    if(reg>=3)return 0;
    const unsigned command=core->wizardry_commands[reg];
    core->wizardry_commands[reg]=0;
    return command;
}
void wiiz_core_wizardry_sound(WiizCore* core,unsigned reg,unsigned value) {
    if(reg<4)core->e->s.c.ram[0x600+reg]=(uint8_t)value;
}
void wiiz_core_wizardry_repeat(WiizCore* core,unsigned held,unsigned due) {
    const unsigned directions[]={PAD_UP,PAD_DOWN,PAD_LEFT,PAD_RIGHT};
    for(unsigned i=0;i<4;++i) {
        const unsigned bit=directions[i];
        if(core->map_profile>1) {
            uint8_t* timer=&core->e->s.c.ram[0x13-i];
            if(!(held&bit))*timer=0x0e;
            else if(due&bit)*timer=0xff;
            else if(*timer!=0xff && *timer!=0x0e && *timer!=0x0f)*timer=0xf4;
        } else {
            uint8_t* timer=&core->e->s.c.ram[0x1d+i];
            if(!(held&bit))*timer=0xfe;
            else if(due&bit)*timer=0xff;
            else if(*timer!=0xff)*timer=0xf4;
        }
    }
}
void wiiz_core_clear_audio_source(WiizCore* core) {
    core->queue_count=core->queue_overflow=0;
    core->sound_role=0;core->profile=0;core->input_speed=1;
    core->e->cpu_ram_read_filter=core->reward_profile==122&&core->free_magic?ff2_magic_read:NULL;
    core->e->apu_write_observer=NULL;
    core->e->mapper_prg_ram_read=core->reward_profile==123?ff3_job_read:core->prg_ram_read;
    if(core->map_profile) {
        wiiz_core_wizardry_audio(core,0);
        wiiz_core_wizardry(core,core->map_profile);
    } else core->e->ram_write_filter=core->reward_profile?reward_only_write:NULL;
}
unsigned wiiz_core_map_position(WiizCore* core) {
    const uint8_t* ram=core->e->s.c.ram;
    if(core->map_profile>1)return ram[0xea]|(ram[0xeb]<<8)|(ram[0xec]<<16)|((ram[0xe9]&3)<<24);
    return ram[0x3b]|(ram[0x3c]<<8)|(ram[0x3d]<<16)|((ram[0x3e]&3)<<24);
}
const uint8_t* wiiz_core_map_explored(WiizCore* core) { return core->explored; }
void wiiz_core_map_set_explored(WiizCore* core,const uint8_t* data) {
    memcpy(core->explored,data,sizeof(core->explored));
}
void wiiz_core_map_record_position(WiizCore* core) {
    const unsigned p=wiiz_core_map_position(core);
    const unsigned floor=(p>>16)&255, x=p&255, y=(p>>8)&255;
    if(!core->map_profile||!floor||floor>(core->map_profile==1?10u:6u)||x>=20||y>=20)return;
    const unsigned cell=(floor-1)*400+y*20+x;
    core->explored[cell/8]|=(uint8_t)(1u<<(cell%8));
}
static int map_in_camp(WiizCore* core) {
    /* Active JSR returns from the native dungeon's camp call, including nested
       character/spell/item menus. Do not interpret unrelated town/battle input. */
    const unsigned ret=core->map_profile==1?0xd287:core->map_profile==2?0xf8e0:0xf96c;
    for(unsigned sp=(unsigned)core->e->s.c.S+1;sp<255;++sp)
        if((core->e->s.c.ram[0x100+sp]|((unsigned)core->e->s.c.ram[0x101+sp]<<8))==ret)return 1;
    return 0;
}
int wiiz_core_map_warp(WiizCore* core,unsigned floor,unsigned x,unsigned y) {
    if(!core->map_profile||floor>(core->map_profile==1?10u:6u)||x>=20||y>=20||(!floor&&(x||y)))return 0;
    if(wiiz_core_items_battle(core))return 0;
    const size_t size=wiiz_core_state_size(core);
    uint8_t* backup=malloc(size);
    if(!backup)return 0;
    if(!wiiz_core_state_write(core,backup,size)){free(backup);return 0;}
    const unsigned events=core->map_events;
    uint8_t explored[WIIZ_MAP_BYTES];
    memcpy(explored,core->explored,sizeof(explored));
    const int camp=map_in_camp(core);
    core->host_camp=camp;
    core->buttons=0;core->host_warp=0x80000000u|(floor<<16)|(y<<8)|x;
    /* Allow the ROM's reload/redraw to finish before reporting success.
       An unavailable initial dungeon wait still times out after 120 frames. */
    for(unsigned frame=0;frame<1200;++frame) {
        if(core->host_warp&0x80000000u) {
            if(frame>=(camp?360u:120u))break;
            /* Let the ROM unwind/save its own camp menus using cancel input.
               Stop sending B as soon as the active camp call has returned. */
            core->buttons=camp&&map_in_camp(core)&&frame%16>=3&&frame%16<6?PAD_B:0;
        } else core->buttons=0;
        if(!wiiz_core_frame(core))break;
        if(!core->host_warp){core->host_camp=0;wiiz_core_map_record_position(core);free(backup);return 1;}
    }
    core->host_warp=0;core->host_camp=0;wiiz_core_state_read(core,backup,size);core->map_events=events;
    wiiz_core_map_set_explored(core,explored);
    free(backup);return 0;
}
unsigned wiiz_core_items_battle(WiizCore* core) {
    Emulator* e=core->e;
    if(core->map_profile>1)return e->s.c.ram[0x90]!=0;
    // FC-I's battle dispatcher jumps into $E0E9..$E5F4 in the fixed bank.
    // Nested menus, sound and NMI retain a return into this dispatcher. Match
    // an actual JSR in the ROM, not arbitrary bytes in unused stack memory.
    unsigned pc=e->s.c.PC.val;
    if(pc>=0xe0e9&&pc<0xe5f5)return 1;
    for(unsigned sp=(unsigned)e->s.c.S+1;sp<255;++sp) {
        unsigned ret=e->s.c.ram[0x100+sp]|((unsigned)e->s.c.ram[0x101+sp]<<8);
        if(ret>=0xe0e9&&ret<0xe5f5&&e->ci.prg_data[0x1c000+ret-0xc000-2]==0x20)return 1;
    }
    return 0;
}
static int items_copy_address(unsigned game,unsigned pc) {
    if(game==1)return (pc>=0x94fa&&pc<0x9544)||(pc>=0xae51&&pc<0xae6f);
    return game==2?(pc>=0xc97e&&pc<0xc9cd):(pc>=0xc99d&&pc<0xc9e6);
}
static int items_transferring(WiizCore* core) {
    Emulator* e=core->e;unsigned game=core->map_profile,pc=e->s.c.PC.val;
    unsigned bank=e->s.m.prg8k_bank[0]&(e->ci.prg8k_banks-1);
    if((game!=1||bank==12)&&items_copy_address(game,pc))return 1;
    // A vblank handler may currently interrupt the native copy. Wait for its
    // return too, rather than exposing a half-loaded or half-written inventory.
    for(unsigned sp=(unsigned)e->s.c.S+1;sp<255;++sp) {
        unsigned ret=e->s.c.ram[0x100+sp]|((unsigned)e->s.c.ram[0x101+sp]<<8);
        if(items_copy_address(game,ret))return 1;
    }
    return 0;
}
int wiiz_core_items_pause(WiizCore* core) {
    if(!core->map_profile||wiiz_core_items_battle(core))return 0;
    // Every scene is eligible. Complete an in-flight native inventory copy
    // and the current CPU instruction, without waiting for a dungeon event.
    for(unsigned n=0;n<30000;++n) {
        if(core->e->s.c.bits==0&&!items_transferring(core))return !wiiz_core_items_battle(core);
        if(emulator_step(core->e)&EMULATOR_EVENT_INVALID_OPCODE)return 0;
    }
    return 0;
}

// Keep an open original-game inventory screen in sync after a transfer.
static int sync_item_cache(WiizCore* core,const uint8_t* before,const uint8_t* after,size_t size) {
    if(size!=8192||!core->map_profile)return 0;
    uint8_t* ram=core->e->s.c.ram;unsigned game=core->map_profile;
    unsigned address=game==1?((unsigned)ram[0x2f]<<8):ram[0x3d]|((unsigned)ram[0x3e]<<8);
    unsigned base,stride=game==1?256:96,start=game==1?0:0x400;
    if(address<0x6000+start||address>=0x6000+start+20*stride||(address-0x6000-start)%stride)return 1;
    base=address-0x6000;
    unsigned count=game==1?0x5c:0x53,flags=game==1?0x4c:0x43;
    unsigned count_cache=game==1?0x4d:0x57,id_cache=game==1?0x215:0x48,flag_cache=game==1?0x22a:0x40;
    unsigned n=before[base+count],next=after[base+count];
    if(n>8||next>8||ram[count_cache]!=n)return 1;
    // Only the currently edited party member may own this cache.
    unsigned party_count=before[game==1?0x1def:0xb81],party_list=game==1?0x1ddc:0xb82;
    unsigned reference=game==1?address>>8:(base-start)/stride;int owner=0;
    for(unsigned i=0;i<party_count&&i<6;++i)if(before[party_list+i]==reference)owner=1;
    if(!owner||(memcmp(before+base+flags,after+base+flags,16)==0&&n==next))return 1;
    for(unsigned i=0;i<n;++i)if(ram[id_cache+i]!=before[base+flags+8+i]||ram[flag_cache+i]!=before[base+flags+i])return 1;
    unsigned remap[8]={0},used=0;
    for(unsigned i=0;i<n;++i) {
        remap[i]=next?(i<next?i:next-1):0;
        for(unsigned exact=0;exact<2;++exact) {
            int found=0;
            for(unsigned j=0;j<next;++j)if(!(used&(1u<<j))&&before[base+flags+8+i]==after[base+flags+8+j]&&
                (exact||before[base+flags+i]==after[base+flags+j])) {remap[i]=j;used|=1u<<j;found=1;break;}
            if(found)break;
        }
    }
    if(game==1) {
        if(ram[0x2c]<n)ram[0x2c]=(uint8_t)remap[ram[0x2c]];
        for(unsigned i=0;i<8;++i)if(ram[0x21e + i]<n)ram[0x21e + i]=(uint8_t)remap[ram[0x21e + i]];
    } else if(ram[0x51]<n)ram[0x51]=(uint8_t)remap[ram[0x51]];
    else if(ram[0x51]>=0x4b&&ram[0x51]<0x4b+n)ram[0x51]=(uint8_t)(0x4b+remap[ram[0x51]-0x4b]);
    for(unsigned i=0;i<8;++i) {ram[id_cache+i]=after[base+flags+8+i];ram[flag_cache+i]=after[base+flags+i];}
    ram[count_cache]=(uint8_t)next;
    return 1;
}

// Rebuild the original game's check fields and mirror for a changed record.
static void repair_roster_record(uint8_t* ram,unsigned game,unsigned slot) {
    uint8_t* record=ram+(game==1?slot*256:0x400+slot*96);
    if(game==1) {
        for(unsigned group=0;group<14;++group) {
            unsigned sum=8;
            for(unsigned i=0;i<8;++i)sum=(sum&255)+(sum>>8)+record[group*8+i];
            record[0x70+group]=(uint8_t)sum;
        }
    }
    const unsigned size=game==1?126:94;
    uint16_t check=0xffff;
    for(unsigned i=0;i<size;++i) {
        check^=(uint16_t)record[i]<<8;
        for(unsigned bit=0;bit<8;++bit)
            check=(uint16_t)((check<<1)^((check&0x8000)?0x1021:0));
    }
    record[size]=(uint8_t)(check>>8);record[size+1]=(uint8_t)check;
    if(game==1) {
        for(unsigned i=0;i<128;++i) {
            const unsigned v=record[i]^255;
            record[255-i]=(uint8_t)((v<<4)|(v>>4));
        }
    } else memcpy(ram+0x1800+slot*96,record,96);
}

static uint8_t* roster_character(WiizCore* core,uint8_t* ram,unsigned slot) {
    if(!core->map_profile||slot>=20||wiiz_core_sram_size(core)!=8192||
       !wiiz_core_sram_read(core,ram,8192))return NULL;
    uint8_t* record=ram+(core->map_profile==1?slot*256:0x400+slot*96);
    return record[0]&&record[1]&&record[1]<=8?record:NULL;
}

int wiiz_core_character_alignment(WiizCore* core,unsigned slot) {
    uint8_t ram[8192];
    const uint8_t* character=roster_character(core,ram,slot);
    if(!character)return -1;
    const int alignment=character[core->map_profile==1?12:10]&3;
    return alignment<3?alignment:-1;
}

int wiiz_core_set_character_alignment(WiizCore* core,unsigned slot,unsigned alignment) {
    if(alignment>2||!wiiz_core_items_pause(core))return 0;
    uint8_t ram[8192];
    uint8_t* character=roster_character(core,ram,slot);
    if(!character)return 0;
    uint8_t* attr=character+(core->map_profile==1?12:10);
    if((*attr&3)==alignment)return 1;
    *attr=(uint8_t)((*attr&0xfc)|alignment);
    repair_roster_record(ram,core->map_profile,slot);
    return wiiz_core_sram_write(core,ram,sizeof(ram));
}

int wiiz_core_character_trait(WiizCore* core,unsigned slot,int race) {
    uint8_t ram[8192];
    const uint8_t* character=roster_character(core,ram,slot);
    if(!character)return -1;
    const unsigned at=core->map_profile==1&&!race?11:10;
    const unsigned shift=core->map_profile==1?0:race?5:2;
    const unsigned value=(character[at]>>shift)&7;
    return value<(race?5u:8u)?(int)value:-1;
}

int wiiz_core_set_character_trait(WiizCore* core,unsigned slot,int race,unsigned value) {
    if(value>=(race?5u:8u)||!wiiz_core_items_pause(core))return 0;
    uint8_t ram[8192];
    uint8_t* character=roster_character(core,ram,slot);
    if(!character)return 0;
    const unsigned at=core->map_profile==1&&!race?11:10;
    const unsigned shift=core->map_profile==1?0:race?5:2;
    const unsigned mask=7u<<shift;
    character[at]=(uint8_t)((character[at]&~mask)|(value<<shift));
    repair_roster_record(ram,core->map_profile,slot);
    return wiiz_core_sram_write(core,ram,sizeof(ram));
}

int wiiz_core_character_learn_spells(WiizCore* core,const uint8_t* rom,size_t rom_size,unsigned slot) {
    if(!wiiz_core_items_pause(core))return 0;
    uint8_t ram[8192];
    uint8_t* character=roster_character(core,ram,slot);
    if(!character)return 0;
    const unsigned game=core->map_profile;
    if(game==1) {
        memset(character+0x45,255,6);
        character[0x4b]|=3;
        memset(character+0x29,9,28);
    } else {
        if(rom_size<16)return 0;
        const size_t table=(game==2?0x1a9ac:0x1ae6c)+((rom[6]&4)?512:0);
        if(table+16>rom_size)return 0;
        // Each school has eight native boundaries delimiting its seven levels.
        for(unsigned school=0;school<2;++school)for(unsigned level=0;level<7;++level) {
            const size_t at=table+school*8+level;
            const unsigned count=(unsigned)rom[at+1]-rom[at];
            if(count>8)return 0;
            character[0x35+school*7+level]|=(uint8_t)((1u<<count)-1);
        }
        // Current and maximum MP share a byte for each spell level.
        memset(character+0x27,0x99,14);
    }
    repair_roster_record(ram,game,slot);
    return wiiz_core_sram_write(core,ram,sizeof(ram));
}

static int items_call(WiizCore* core,unsigned entry) {
    Emulator* e=core->e;
    uint8_t code[]={0x20,(uint8_t)entry,(uint8_t)(entry>>8),0x4c,3,7};
    memcpy(e->s.c.ram+0x700,code,sizeof(code));
    e->s.c.PC.val=0x700;e->s.c.S=0xff;
    // binjnes preloads the next micro-op: cpu0 is decode, step points past 781.
    e->s.c.bits=0;e->s.c.step=782;e->s.c.next_step=781;
    e->s.c.req_reset=e->s.c.has_reset=e->s.c.has_irq=false;e->s.c.I=true;
    e->s.p.ppuctrl&=0x7f;
    e->s.sc.when[SCHED_NMI]=UINT64_MAX;
    e->s.sc.next=UINT64_MAX;
    for(unsigned i=0;i<SCHED_COUNT;++i)
        if(e->s.sc.when[i]<e->s.sc.next)e->s.sc.next=e->s.sc.when[i];
    for(unsigned n=0;n<300000;++n) {
        if(e->s.c.bits==0&&e->s.c.PC.val<0x8000&&
           (e->s.c.PC.val<0x700||e->s.c.PC.val>0x706))return 0;
        e->s.c.req_nmi=e->s.c.has_nmi=false;
        if(emulator_step(e)&EMULATOR_EVENT_INVALID_OPCODE)return 0;
        if(e->s.c.bits==0&&e->s.c.PC.val==0x703)return 1;
    }
    return 0;
}
static int recalculate_item_stats(WiizCore* core,unsigned index) {
    Emulator* e=core->e;unsigned game=core->map_profile;
    if(!game||index>=6)return 0;
    e->ram_write_filter=NULL;
    // III disables battery access during parts of dungeon rendering. The
    // disposable core must enable it before calling the original character code.
    e->s.m.prg_ram_en=true;e->s.m.prg_ram_write_en=true;
    for(unsigned i=0;i<4;++i){e->s.m.prg8k_bank[i]=(uint16_t)(12+i);e->prg_rom_map[i]=e->ci.prg_data+(12+i)*8192;}
    if(game==1) {
        // The interrupted scene may have left a partial MMC1 serial write.
        e->s.m.mmc1.bits=5;e->s.m.mmc1.data=0;
        e->s.m.mmc1.ctrl|=0x0c;e->s.m.prg_bank[0]=6;
        e->s.c.ram[0xfb]=6;e->s.c.ram[0x2e]=0;e->s.c.ram[0x4e]=(uint8_t)index;
        unsigned page=e->s.c.prg_ram[0x1ddc+index];
        if(page<0x60||page>=0x74)return 0;
        const uint8_t* record=e->s.c.prg_ram+(page-0x60)*256;
        e->s.c.prg_ram[0x1dca+index]=record[0x23];
        e->s.c.prg_ram[0x1dc4+index]=record[0x24];
        return items_call(core,0xae6f);
    }
    unsigned slot=e->s.c.prg_ram[0xb82+index],address=0x6400+slot*96;
    if(slot>=20)return 0;
    e->s.c.ram[0x3d]=(uint8_t)address;e->s.c.ram[0x3e]=(uint8_t)(address>>8);
    e->s.c.ram[0x0b]=(uint8_t)index;e->s.c.ram[0x7e]=12;e->s.c.ram[0x7f]=13;
    const unsigned ac=game==2?0x4ea:0x50a;
    const int old_ac=(int8_t)e->s.c.prg_ram[address-0x6000+0x26];
    const int live_ac=(int8_t)e->s.c.ram[ac+index];
    if(!items_call(core,game==2?0xd05a:0xd05c))return 0;
    int next_ac=live_ac+(int8_t)e->s.c.prg_ram[address-0x6000+0x26]-old_ac;
    if(next_ac < -99)next_ac=-99;if(next_ac>99)next_ac=99;
    e->s.c.ram[ac+index]=(uint8_t)next_ac;
    e->s.c.ram[0x3d]=(uint8_t)address;e->s.c.ram[0x3e]=(uint8_t)(address>>8);
    e->s.c.ram[0x0b]=(uint8_t)index;
    return items_call(core,game==2?0xbded:0x9e3d);
}
// FC I uses party effect arrays even outside battle.
static int item_stats_live(WiizCore* core) {
    if(core->map_profile!=1)return 0;
    Emulator* e=core->e;
    unsigned pc=e->s.c.PC.val;
    int dungeon=pc>=0xd23c&&pc<0xd521;
    for(unsigned sp=(unsigned)e->s.c.S+1;!dungeon&&sp<255;++sp) {
        unsigned ret=e->s.c.ram[0x100+sp]|((unsigned)e->s.c.ram[0x101+sp]<<8);
        if(ret>=0xd23c&&ret<0xd521&&e->ci.prg_data[0x1c000+ret-0xc000-2]==0x20)dungeon=1;
    }
    if(!dungeon)return 0;
    unsigned count=e->s.c.prg_ram[0x1def];
    if(!count||count>6)return 0;
    static const unsigned addresses[]={0x1c08,0x1bde,0x1c5c,0x1c32,0x1dca,0x1dc4,0x1bb4};
    for(unsigned i=0;i<count;++i) {
        unsigned slot=e->s.c.prg_ram[0x1ddc+i];
        if(slot<0x60||slot>=0x74)return 0;
        const uint8_t* record=e->s.c.prg_ram+(slot-0x60)*256;
        for(unsigned j=0;j<7;++j)if(record[0x1f+j]!=e->s.c.prg_ram[addresses[j]+i])return 0;
    }
    return 1;
}

static int update_item_stats(WiizCore* live,uint8_t* rom,size_t rom_size,uint8_t* after,
    unsigned from,unsigned to,uint8_t* src,uint8_t* dst) {
    const size_t size=wiiz_core_state_size(live);
    uint8_t* state=malloc(size);
    if(!state)return 0;
    WiizCore* clone=NULL;
    if(wiiz_core_state_write(live,state,size))clone=wiiz_core_new(rom,rom_size);
    if(clone && live->turbo_file.enabled)wiiz_core_turbo_file_attach(clone);
    const unsigned game=live->map_profile;
    const int loaded=clone&&wiiz_core_state_read(clone,state,size);
    free(state);
    if(!loaded){if(clone)wiiz_core_delete(clone);return 0;}
    clone->map_profile=game;
    const int ok=wiiz_core_sram_write(clone,after,8192)&&
        recalculate_item_stats(clone,from)&&(from==to||recalculate_item_stats(clone,to));
    if(ok) {
        const uint8_t* computed=clone->e->s.c.prg_ram;
        const unsigned ac=game==1?0x28:0x26;
        src[ac]=computed[src-after+ac];dst[ac]=computed[dst-after+ac];
        if(game!=1){src[0x59]=computed[src-after+0x59];dst[0x59]=computed[dst-after+0x59];}
        if(item_stats_live(live)) {
            const unsigned arrays[]={0x1ad6,0x1adc,0x1ae2,0x1b0c,0x1b36,0x1b60,0x1b8a,0x1d82,0x1d88,0x1d8e,0x1db2,0x1db8,0x1dd0};
            for(unsigned i=0;i<sizeof(arrays)/sizeof(*arrays);++i)memcpy(after+arrays[i],computed+arrays[i],6);
        }
    }
    wiiz_core_delete(clone);
    return ok;
}

static const uint8_t* item_metadata(const uint8_t* rom,size_t size,unsigned game,unsigned id) {
    if(size<16||id>=(game==1?101u:game==2?110u:138u))return NULL;
    const size_t trainer=(rom[6]&4)?512:0;
    size_t at=game==1?0x10110+id*64+34:0x10010+id*31+4;
    if(game==2) {
        const size_t pos=0x10010+id*2+trainer;
        if(pos+1>=size)return NULL;
        const unsigned ptr=rom[pos]|(rom[pos+1]<<8);
        if(ptr<0x8000||ptr>=0xc000)return NULL;
        at=0x10010+ptr-0x8000+34;
    }
    at+=trainer;
    return at+14<size?rom+at:NULL;
}

static WiizItemTransfer toggle_item_equip(uint8_t* record,unsigned game,unsigned item,
    unsigned flags,unsigned count,const uint8_t* rom,size_t size) {
    uint8_t* selected=record+flags+item;
    if(*selected&64)return WIIZ_ITEM_CURSED;
    if(*selected&128){*selected&=127;return WIIZ_ITEM_MOVED;}
    if(*selected&32)return WIIZ_ITEM_INELIGIBLE;
    const uint8_t* data=item_metadata(rom,size,game,selected[8]);
    const unsigned job=game==1?(record[11]&7):((record[10]>>2)&7);
    const unsigned alignment=record[game==1?12:10]&3;
    if(!data||data[0]>5||!(data[game==1?14:13]&(1u<<job)))return WIIZ_ITEM_INELIGIBLE;
    for(unsigned i=0;i<count;++i) {
        if(i==item||!(record[flags+i]&128))continue;
        const uint8_t* equipped=item_metadata(rom,size,game,record[flags+8+i]);
        if(!equipped)return WIIZ_ITEM_UNAVAILABLE;
        if(equipped[0]!=data[0])continue;
        if(record[flags+i]&64)return WIIZ_ITEM_CURSED;
        record[flags+i]&=127;
    }
    const int aligned=game==1?(data[1]&(1u<<alignment))!=0:data[1]>=128||data[1]==alignment;
    *selected|=(uint8_t)(128|((data[2]||!aligned)?64:0));
    return WIIZ_ITEM_MOVED;
}

static WiizItemTransfer edit_inventory(WiizCore* core,uint8_t* rom,size_t rom_size,
    unsigned from,unsigned item,unsigned to,WiizItemAction action) {
    if(!wiiz_core_items_pause(core)||wiiz_core_sram_size(core)!=8192)return WIIZ_ITEM_UNAVAILABLE;
    const unsigned game=core->map_profile;
    uint8_t before[8192],after[8192];
    if(!wiiz_core_sram_read(core,before,sizeof(before)))return WIIZ_ITEM_UNAVAILABLE;
    const unsigned count=before[game==1?0x1def:0xb81];
    const unsigned list=game==1?0x1ddc:0xb82;
    const unsigned ref=game==1?0x60:0;
    if(count>6||from>=count||to>=count)return WIIZ_ITEM_UNAVAILABLE;
    const unsigned source=before[list+from]-ref,target=before[list+to]-ref;
    if(source>=20||target>=20||(action==WIIZ_ITEM_GIVE&&source==target))return WIIZ_ITEM_UNAVAILABLE;
    memcpy(after,before,sizeof(after));
    uint8_t* src=after+(game==1?source*256:0x400+source*96);
    uint8_t* dst=after+(game==1?target*256:0x400+target*96);
    const unsigned flags=game==1?0x4c:0x43,count_at=flags+16;
    const unsigned n=src[count_at],m=dst[count_at];
    if(!src[0]||!dst[0]||n>8||m>8||item>=n)return WIIZ_ITEM_UNAVAILABLE;
    const unsigned id=src[flags+8+item];
    if(id>=(game==1?101u:game==2?110u:138u))return WIIZ_ITEM_UNAVAILABLE;
    if(action==WIIZ_ITEM_TOGGLE_EQUIP) {
        WiizItemTransfer result=toggle_item_equip(src,game,item,flags,n,rom,rom_size);
        if(result!=WIIZ_ITEM_MOVED)return result;
    } else if(action==WIIZ_ITEM_MOVE_UP||action==WIIZ_ITEM_MOVE_DOWN) {
        const unsigned target_row=action==WIIZ_ITEM_MOVE_UP?item-1:item+1;
        if(target_row>=n)return WIIZ_ITEM_UNAVAILABLE;
        for(unsigned field=flags;field<=flags+8;field+=8) {
            uint8_t previous=src[field+item];
            src[field+item]=src[field+target_row];src[field+target_row]=previous;
        }
    } else {
        if(src[flags+item]&0xc0)return WIIZ_ITEM_EQUIPPED;
        if(action==WIIZ_ITEM_GIVE) {
            if(m==8)return WIIZ_ITEM_FULL;
            dst[flags+m]=src[flags+item];
            dst[flags+8+m]=(uint8_t)id;
            dst[count_at]=(uint8_t)(m+1);
        }
        memmove(src+flags+item,src+flags+item+1,n-item-1);
        memmove(src+flags+8+item,src+flags+8+item+1,n-item-1);
        src[flags+n-1]=0;
        src[flags+8+n-1]=255;
        src[count_at]=(uint8_t)(n-1);
    }
    if(!update_item_stats(core,rom,rom_size,after,from,to,src,dst))return WIIZ_ITEM_UNAVAILABLE;
    repair_roster_record(after,game,source);
    if(source!=target)repair_roster_record(after,game,target);
    if(!wiiz_core_sram_write(core,after,sizeof(after)))return WIIZ_ITEM_UNAVAILABLE;
    sync_item_cache(core,before,after,sizeof(after));
    return WIIZ_ITEM_MOVED;
}
WiizItemTransfer wiiz_core_item_transfer(WiizCore* core,uint8_t* rom,size_t rom_size,unsigned from,unsigned item,unsigned to) {
    return edit_inventory(core,rom,rom_size,from,item,to,WIIZ_ITEM_GIVE);
}
WiizItemTransfer wiiz_core_item_edit(WiizCore* core,uint8_t* rom,size_t rom_size,unsigned member,unsigned item,WiizItemAction action) {
    return edit_inventory(core,rom,rom_size,member,item,member,action);
}
