#pragma once

#include <cstdint>
#include <string_view>

namespace justnbt {
struct BlockStyle {
    uint32_t rgb = 0x707070;
    bool transparent = false;
    bool water = false;
    bool known = true;
};

BlockStyle blockStyle(std::string_view name, bool waterlogged);
}
