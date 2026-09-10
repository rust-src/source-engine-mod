//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef LCOLOR_H
#define LCOLOR_H

#ifdef _WIN32
#pragma once
#endif

/* type for Color functions */
typedef Color lua_Color;



/*
** access functions (stack -> C)
*/

LUA_API lua_Color      &(lua_tocolor) (lua_State *L, int idx);


/*
** push functions (C -> stack)
*/
LUA_API void  (lua_pushcolor) (lua_State *L, const lua_Color &clr);



LUALIB_API lua_Color &(luaL_checkcolor) (lua_State *L, int narg);

// HL2SB: ported from Experiment: Source.  Returns by value rather than by
// reference: upstream's `return luaL_opt(L, luaL_checkcolor, narg, def)` hands back
// either the caller's temporary or a reference into the Lua stack, neither of which
// outlives the call.  Callers only ever copy it into a local Color.
LUALIB_API lua_Color (luaL_optcolor) (lua_State *L, int narg, lua_Color def);


#endif // LCOLOR_H
