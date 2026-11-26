#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct WiizCore WiizCore;
enum { PAD_A=1, PAD_B=2, PAD_SELECT=4, PAD_START=8, PAD_UP=16, PAD_DOWN=32, PAD_LEFT=64, PAD_RIGHT=128 };
WiizCore* wiiz_core_new(uint8_t* rom, size_t size);
void wiiz_core_delete(WiizCore*);
void wiiz_core_input(WiizCore*, unsigned buttons);
void wiiz_core_reset(WiizCore*);
unsigned wiiz_core_title_ready(WiizCore*);
int wiiz_core_frame(WiizCore*);
void wiiz_core_pixels(WiizCore*, uint32_t* rgba);
const float* wiiz_core_audio(WiizCore*, size_t* count);
size_t wiiz_core_sram_size(WiizCore*);
int wiiz_core_sram_read(WiizCore*, uint8_t* bytes, size_t size);
int wiiz_core_sram_write(WiizCore*, uint8_t* bytes, size_t size);
enum { WIIZ_TURBO_FILE_BYTES = 8192 };
void wiiz_core_turbo_file_attach(WiizCore*);
int wiiz_core_turbo_file_read(const WiizCore*, uint8_t* bytes, size_t size);
int wiiz_core_turbo_file_write(WiizCore*, const uint8_t* bytes, size_t size);
unsigned wiiz_core_mapper(WiizCore*);
size_t wiiz_core_state_size(WiizCore*);
int wiiz_core_state_write(WiizCore*, uint8_t*, size_t);
int wiiz_core_state_read(WiizCore*, const uint8_t*, size_t);
void wiiz_core_dq4_source(WiizCore*,int trace);
int wiiz_core_dq4_player(WiizCore*);
unsigned wiiz_core_take_sound(WiizCore*);
int wiiz_core_sound_command(WiizCore*,unsigned event);
void wiiz_core_input_speed(WiizCore*,unsigned buttons,unsigned speed);
void wiiz_core_input_profile(WiizCore*,unsigned profile);
unsigned wiiz_core_peek(WiizCore*,unsigned addr);
void wiiz_core_wizardry(WiizCore*,unsigned scenario);
void wiiz_core_wizardry_audio(WiizCore*,unsigned role);
unsigned wiiz_core_wizardry_take_sound(WiizCore*,unsigned reg);
void wiiz_core_wizardry_sound(WiizCore*,unsigned reg,unsigned value);
void wiiz_core_wizardry_repeat(WiizCore*,unsigned held,unsigned due);
void wiiz_core_clear_audio_source(WiizCore*);
unsigned wiiz_core_map_position(WiizCore*);
enum { WIIZ_MAP_BYTES=500 };
const uint8_t* wiiz_core_map_explored(WiizCore*);
void wiiz_core_map_set_explored(WiizCore*,const uint8_t*);
void wiiz_core_map_record_position(WiizCore*);
int wiiz_core_map_warp(WiizCore*,unsigned floor,unsigned x,unsigned y);
unsigned wiiz_core_items_battle(WiizCore*);
int wiiz_core_items_pause(WiizCore*);
int wiiz_core_character_alignment(WiizCore*,unsigned slot);
int wiiz_core_set_character_alignment(WiizCore*,unsigned slot,unsigned alignment);
int wiiz_core_character_trait(WiizCore*,unsigned slot,int race);
int wiiz_core_set_character_trait(WiizCore*,unsigned slot,int race,unsigned value);
int wiiz_core_character_learn_spells(WiizCore*,const uint8_t* rom,size_t rom_size,unsigned slot);
typedef enum { WIIZ_ITEM_UNAVAILABLE, WIIZ_ITEM_MOVED, WIIZ_ITEM_FULL, WIIZ_ITEM_EQUIPPED,
    WIIZ_ITEM_INELIGIBLE, WIIZ_ITEM_CURSED } WiizItemTransfer;
typedef enum { WIIZ_ITEM_TOGGLE_EQUIP, WIIZ_ITEM_GIVE, WIIZ_ITEM_DELETE,
    WIIZ_ITEM_MOVE_UP, WIIZ_ITEM_MOVE_DOWN } WiizItemAction;
WiizItemTransfer wiiz_core_item_edit(WiizCore*,uint8_t* rom,size_t rom_size,unsigned member,unsigned item,WiizItemAction);
WiizItemTransfer wiiz_core_item_transfer(WiizCore*,uint8_t* rom,size_t rom_size,unsigned from,unsigned item,unsigned to);
int wiiz_core_rewards(WiizCore*,unsigned experience,unsigned gold);
int wiiz_core_wizardry_rewards_supported(const WiizCore*);
void wiiz_core_reward_profile(WiizCore*,unsigned profile);
int wiiz_core_reward_supported(const WiizCore*,int gold);
enum { WIIZ_ASSIST_BONUS=16, WIIZ_ASSIST_MIXED=32, WIIZ_ASSIST_MP=64 };
int wiiz_core_set_wizardry_assist(WiizCore*,unsigned bit,int enabled);
int wiiz_core_wizardry_assist_enabled(const WiizCore*,unsigned bit);
int wiiz_core_set_wizardry_effects(WiizCore*,unsigned mask);
int wiiz_core_wizardry_assists_supported(const WiizCore*);
int wiiz_core_max_skills(WiizCore*,unsigned enabled);
int wiiz_core_free_jobs(WiizCore*,unsigned enabled);
int wiiz_core_free_magic(WiizCore*,unsigned enabled);
unsigned wiiz_core_ff_options(const WiizCore*);
unsigned wiiz_core_sound_pending(WiizCore*);
unsigned wiiz_core_sound_at(WiizCore*,unsigned index);
void wiiz_core_profile_source(WiizCore*,unsigned profile,int trace);
int wiiz_core_profile_player(WiizCore*,unsigned profile);
unsigned wiiz_core_current_music(WiizCore*);
unsigned wiiz_core_music_steps(WiizCore*);
int wiiz_core_ff_clock(WiizCore*,unsigned music_steps);
#ifdef __cplusplus
}
#endif
