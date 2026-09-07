#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <memory>
#include <vector>
#include <array>
#include <stdexcept>
#include <sstream>

// Windows headers define BYTE/WORD/DWORD as Win32 ABI types (DWORD is unsigned long
// in MSVC).  Do not redeclare them with std::uint32_t on Windows: that breaks
// minwindef.h/winnt.h when a translation unit later includes <windows.h>.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
using BYTE  = std::uint8_t;
using WORD  = std::uint16_t;
using DWORD = std::uint32_t;
#endif

#include "fourcc.h"
#include "../graphics/vector.h"
#include "../graphics/angle.h"
#include "../graphics/gamma.h"
