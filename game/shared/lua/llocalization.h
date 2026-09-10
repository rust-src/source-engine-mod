//=============================================================================//
//
// Purpose: Lua "Localizations" library (GMod's language.GetPhrase equivalent),
//          ported from Experiment: Source (src/game/shared/llocalization.cpp).
//
//=============================================================================//

#ifndef LLOCALIZATION_H
#define LLOCALIZATION_H
#ifdef _WIN32
#pragma once
#endif

#include "lua.hpp"

LUALIB_API int luaopen_Localizations( lua_State *L );

#endif  // LLOCALIZATION_H
