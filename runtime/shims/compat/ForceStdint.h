#pragma once

// RN public headers use uint16_t without including <cstdint>. Apple libc++
// often makes those types visible through other system headers; libstdc++
// does not. Force-include this on Linux instead of patching the RN submodule.
#include <cstdint>
