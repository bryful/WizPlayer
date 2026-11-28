static unsigned wizardry_effect_address(unsigned game,unsigned index)
{
    static const unsigned addresses[][3]={{0x27f,0x281,0x280},{0x77ef,0x77f0,0x77f1},{0x6d99,0x6d9a,0x6d9b}};
    return addresses[game-1][index];
}

static void wizardry_write_effect(WiizCore* core,unsigned index)
{
    const unsigned game=core->map_profile;
    const unsigned addr=wizardry_effect_address(game,index);
    const uint8_t value=index==1?(game==1?254:2):255;
    if(addr<0x800)core->e->s.c.ram[addr]=value;
    else if(core->e->s.c.prg_ram[addr-0x6000]!=value) {
        core->e->s.c.prg_ram[addr-0x6000]=value;
        core->e->s.m.prg_ram_updated=true;
    }
}

int wiiz_core_wizardry_assists_supported(const WiizCore* core)
{
    return wiiz_core_wizardry_rewards_supported(core);
}

int wiiz_core_wizardry_assist_enabled(const WiizCore* core,unsigned bit)
{
    return (core->wizardry_assists&bit)!=0;
}

int wiiz_core_set_wizardry_effects(WiizCore* core,unsigned mask)
{
    if(mask>7 || !wiiz_core_wizardry_assists_supported(core))return 0;
    core->wizardry_assists=(core->wizardry_assists&~7u)|mask;
    return 1;
}

int wiiz_core_set_wizardry_assist(WiizCore* core,unsigned bit,int enabled)
{
    if((bit!=WIIZ_ASSIST_BONUS && bit!=WIIZ_ASSIST_MIXED && bit!=WIIZ_ASSIST_MP) ||
       !wiiz_core_wizardry_assists_supported(core))return 0;
    if(bit==WIIZ_ASSIST_MIXED) {
        static const unsigned offsets[]={0x18ad5,0x1f34b,0x1f3c7};
        const bool first=core->map_profile==1;
        uint8_t* code=core->e->ci.prg_data+offsets[core->map_profile-1];
        const uint8_t original[]={first?0x25:0xd0,first?0x0f:0x06};
        // FC I returns zero for an eligible candidate. II/III skip the reject branch.
        const uint8_t mixed[]={first?0xa9:0xea,first?0x00:0xea};
        if(memcmp(code,original,2) && memcmp(code,mixed,2))return 0;
        memcpy(code,enabled?mixed:original,2);
    }
    if(enabled)core->wizardry_assists|=bit;
    else core->wizardry_assists&=~bit;
    return 1;
}

static bool wizardry_assists_in_dungeon(WiizCore* core)
{
    const unsigned floor=(wiiz_core_map_position(core)>>16)&255;
    return floor>=1 && floor<=(core->map_profile==1?10u:6u);
}

static void wizardry_refresh_effects(WiizCore* core)
{
    if(!(core->wizardry_assists&7) || core->wizardry_audio_role==2 ||
       !wizardry_assists_in_dungeon(core))return;
    for(unsigned index=0;index<3;++index)
        if(core->wizardry_assists&(1u<<index))wizardry_write_effect(core,index);
}

static bool wizardry_assist_write(WiizCore* core,unsigned pc,unsigned bank,unsigned addr,unsigned value)
{
    const unsigned mask=core->wizardry_assists;
    if(!mask || core->wizardry_audio_role==2)return false;
    const unsigned game=core->map_profile;
    if((mask&WIIZ_ASSIST_BONUS) &&
       ((game==1 && bank/2==6 && pc==0x9d93 && addr==0x254) ||
        (game==2 && bank==14 && pc==0xdc14 && addr==0x58) ||
        (game==3 && bank==14 && pc==0xdbe4 && addr==0x58))) {
        unsigned points=60;
        if(game<3) {
            unsigned base=0;
            const unsigned first=game==1?0x254:0x53;
            for(unsigned field=first;field<first+6;++field)
                base+=field==addr?value:core->e->s.c.ram[field];
            points=base<=108?108-base:0;
        }
        core->e->s.c.ram[game==1?0x25b:0x40]=(uint8_t)points;
    }
    if(!(mask&(7|WIIZ_ASSIST_MP)) || !wizardry_assists_in_dungeon(core))return false;
    for(unsigned index=0;index<3;++index)
        if((mask&(1u<<index)) && addr==wizardry_effect_address(game,index)) {
            wizardry_write_effect(core,index);
            return true;
        }
    if((mask&WIIZ_ASSIST_MP) && addr>=0x6000 && addr<0x8000) {
        const unsigned offset=addr-0x6000,start=game==1?0:0x400,stride=game==1?256:96;
        if(offset>=start && offset<start+20*stride) {
            const unsigned field=(offset-start)%stride;
            const uint8_t* record=core->e->s.c.prg_ram+offset-field;
            const unsigned old=record[field];
            if(record[0] && game==1 && field>=0x29 && field<0x37 && old<=9 && old==value+1)return true;
            if(record[0] && game!=1 && field>=0x27 && field<0x35 &&
               (old>>4)<=9 && (old&15)<=9 && old==value+16)return true;
        }
    }
    return false;
}
