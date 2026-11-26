static unsigned wizardryItemStatus, wizardryItemAddress, wizardryItemWaitStack, wizardryItemReturnStack;

static bool wizardryWaitingForInput(unsigned stack)
{
    return wizardryItemStatus!=1 && wizardryItemStatus!=3 && wiizvInputWait(stack);
}

extern "C" bool wizardryCanEditCharacter()
{
    const auto* ram=Memory.RAM;
    const unsigned stack=ram[0x1a] | (ram[0x1b]<<8);
    if(!wizardryWaitingForInput(stack))return false;
    const bool castle=stack==0x1fd3 && ram[0x35]==0xc5 && ram[0x36]==0 &&
        ram[0x1fdd]==0xb4 && ram[0x1fde]==0x85 && ram[0x1fdf]==0;
    const bool facility=ram[0xb7]==0 && stack>=0x1f80 && stack<=0x1fd8 &&
        ram[0x1fde]==0xbf && ram[0x1fdf]==0x85;
    return castle || facility || wiizvCampWait(stack) || wiizv_can_move();
}

extern "C" bool wizardryCanEditItems()
{
    const unsigned stack=Memory.RAM[0x1a] | (Memory.RAM[0x1b]<<8);
    if(stack<0x1f00 || stack>0x1fe0 || !wizardryWaitingForInput(stack))return false;
    if(!Memory.RAM[0xdb] || Memory.RAM[0xdb]>6)return false;
    unsigned seen=0;
    for(unsigned i=0;i<Memory.RAM[0xdb];++i) {
        const unsigned slot=Memory.RAM[0xdc+i];
        if(slot>=20 || !Memory.RAM[0x1000+slot*128] || (seen&(1u<<slot)))return false;
        seen|=1u<<slot;
    }
    // Check active JSL return frames for the original combat engine.
    for(unsigned i=stack+1;i+2<=0x1fff;++i) {
        const unsigned ret=Memory.RAM[i] | (Memory.RAM[i+1]<<8);
        if(Memory.RAM[i+2]!=5 || ret<0xac1b)continue;
        const unsigned offset=5*0x8000+(ret&0x7fff)-3;
        if(offset+3<Memory.CalculatedSize && Memory.ROM[offset]==0x22)return false;
    }
    return true;
}

extern "C" bool wizardryRequestItemRecalc(unsigned slot)
{
    if(slot>=20 || !wizardryCanEditItems())return false;
    wizardryItemAddress=0x1000+slot*128;
    wizardryItemWaitStack=Memory.RAM[0x1a] | (Memory.RAM[0x1b]<<8);
    wizardryItemStatus=1;
    return true;
}

extern "C" unsigned wizardryItemRecalcStatus(){return wizardryItemStatus;}
extern "C" void wizardryCancelItemRecalc(){wizardryItemStatus=0;}

static inline void wizardryItemHook()
{
    if(!wizardryItemStatus || Registers.PBPC!=0x0081f5)return;
    if(wizardryItemStatus==3 && Registers.S.W==wizardryItemReturnStack) {
        wizardryItemStatus=2;
        return;
    }
    if(wizardryItemStatus!=1 || Registers.S.W!=wizardryItemWaitStack+4 || Registers.D.W || Registers.DB ||
       !CheckMemory() || CheckIndex())return;
    Memory.RAM[0x30]=static_cast<uint8>(wizardryItemAddress);
    Memory.RAM[0x31]=static_cast<uint8>(wizardryItemAddress>>8);
    Registers.X.W=wizardryItemAddress;
    wizardryItemReturnStack=Registers.S.W;
    wizardryItemStatus=3;
    Memory.RAM[Registers.S.W--]=0;
    Memory.RAM[Registers.S.W--]=0x81;
    Memory.RAM[Registers.S.W--]=0xf4;
    S9xSetPCBase(0x009fa2);
}
