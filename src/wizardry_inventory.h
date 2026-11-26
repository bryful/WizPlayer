#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct WizardryItemLabel {
    std::string glyphs;
    std::wstring fallback;
    std::wstring status;
    uint8_t flags;
};

struct WizardryPartyMember {
    std::string name;
    std::vector<WizardryItemLabel> items;
};
