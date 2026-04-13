#ifndef OSSM_ADVANCED_STRUCTS_H
#define OSSM_ADVANCED_STRUCTS_H

struct Control {
    float value;
    std::uint8_t minValue = 0;
    std::uint8_t maxValue = 100;
};

std::unordered_map<std::string, Control> advancedSettings;
std::vector<std::string> controlNames;
std::vector<std::string> modifierNames;

std::vector<uint16_t> advancedColors = {0xf860, 0xfc00, 0xffe0, 0x07e0, 0x07ff, 0x001f, 0xa87d};

#endif