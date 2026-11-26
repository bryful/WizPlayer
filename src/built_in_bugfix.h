#pragma once

#include "built_in_ips_data.h"
#include "game_support.h"
#include "ips_patch.h"
#include "nes/wizardry.h"

inline std::string_view builtInBugFix(const std::vector<char>& rom) {
    if(rom.size()>=16&&std::memcmp(rom.data(),"NES\x1a",4)==0) {
        // FC版のIPSは16バイトヘッダーの直後にPRGが始まるROM用。
        if(!(rom[6]&4)&&WizardryFeatures(rom).scenario==1)return builtInIps::fcWizardry1;
        return {};
    }
    const auto title=identifyGameHeader(rom).id;
    if(title!=identifyGameLayout(rom).id)return {};
    switch(title) {
        case GameId::Gaiden1: return builtInIps::gaiden1;
        case GameId::Gaiden2: return builtInIps::gaiden2;
        case GameId::Gaiden3: return builtInIps::gaiden3;
        case GameId::Wizardry1: return builtInIps::gbcWizardry1;
        case GameId::Wizardry2: return builtInIps::gbcWizardry2;
        case GameId::Wizardry3: return builtInIps::gbcWizardry3;
        default: return {};
    }
}
