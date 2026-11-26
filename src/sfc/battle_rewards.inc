static unsigned sfcExperienceFactor = 1;
static unsigned sfcGoldFactor = 1;
static unsigned sfcRewardProfile;

extern "C" void sfcSetRewardFactors(unsigned profile, unsigned experience, unsigned gold)
{
    sfcRewardProfile = profile;
    sfcExperienceFactor = experience;
    sfcGoldFactor = gold;
}

static inline void wizardryRewardHook()
{
    unsigned factor;
    switch (Registers.PBPC)
    {
    case 0x05b4a1: factor = sfcExperienceFactor; break;
    case 0x04c65a: factor = sfcGoldFactor; break;
    case 0x04c6ae:
        if (sfcGoldFactor != 1 && CheckCarry())
            memset(Memory.RAM + Registers.X.W + 0x11, 0x99, 6);
        return;
    default: return;
    }
    if (factor == 1) return;

    // These six packed-BCD bytes hold the reward before party division.
    auto* reward = Memory.RAM + 0x28;
    uint64_t amount = 0;
    for (unsigned byte = 0; byte < 6; ++byte)
    {
        const unsigned high = reward[byte] >> 4, low = reward[byte] & 15;
        if (high > 9 || low > 9) return;
        amount = amount * 100 + high * 10 + low;
    }
    constexpr uint64_t maximum = 999999999999ULL;
    amount *= factor;
    if (amount > maximum) amount = maximum;
    for (unsigned byte = 6; byte > 0; --byte)
    {
        const unsigned pair = amount % 100;
        reward[byte - 1] = (pair / 10) * 16 + pair % 10;
        amount /= 100;
    }
}

static inline void dragonQuestRewardHook()
{
    const uint32_t pc = Registers.PBPC & 0x7fffff;
    if (pc != 0x288213 && pc != 0x28824f) return;
    const bool gold = pc == 0x28824f;
    const unsigned factor = gold ? sfcGoldFactor : sfcExperienceFactor;
    if (factor == 1 || CheckMemory() || Registers.D.W ||
        ((Registers.DB & 0x40) && Registers.DB != 0x7e)) return;

    auto* total = Memory.RAM + (gold ? 0x10ff : 0x10fc);
    const unsigned initial = total[0] | (total[1] << 8) | (total[2] << 16);
    unsigned result = initial + Registers.A.W * factor;
    if (result > 0xffffff) result = 0xffffff;
    for (unsigned i = 0; i < 3; ++i) total[i] = static_cast<uint8>(result >> (8 * i));
    // Resume the original register/flag restore after the 24-bit addition.
    S9xSetPCBase(Registers.PBPC + 0x1b);
}

static inline void sfcRewardHook()
{
    if ((sfcExperienceFactor == 1 && sfcGoldFactor == 1) || Registers.D.W) return;
    if (sfcRewardProfile == 1) wizardryRewardHook();
    else if (sfcRewardProfile == 2) dragonQuestRewardHook();
}
