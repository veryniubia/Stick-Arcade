#pragma once

#include <stdint.h>

namespace title {

void begin(uint32_t now);
bool update(uint32_t now, bool pressed);  // true when the coin has landed and the launcher can open
void draw(uint32_t now);

}  // namespace title
