//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef LBASEENTITY_SHARED_H
#define LBASEENTITY_SHARED_H
#ifdef _WIN32
#pragma once
#endif

/* type for CBaseEntity functions */
typedef CBaseEntity lua_CBaseEntity;



/*
** access functions (stack -> C)
*/

LUA_API lua_CBaseEntity     *(lua_toentity) (lua_State *L, int idx);


/*
** push functions (C -> stack)
*/
LUA_API void  (lua_pushentity) (lua_State *L, lua_CBaseEntity *pEntity);


/*
** Experiment: Source pushes every entity through CBaseEntity::PushLuaInstanceSafe(),
** which takes the metatable from the object's dynamic type.  HL2SB instead has one
** push function per class (lua_pushentity / lua_pushplayer / lua_pushweapon /
** lua_pushanimating), and lua_pushentity always installs the plain CBaseEntity
** metatable -- whose __index knows only the fields CBaseEntity bindings list.  An
** entity that arrives through a generic API while it is really a player or a weapon
** therefore loses every method of its own class.
**
** This helper bridges the two: it picks the push function that matches the dynamic
** type, so ported binding files can keep calling PushLuaInstanceSafe() and still get
** a userdata with the full method set.  If the metatable for the specialised class
** has not been registered (or the object is a plain entity) it falls back to
** lua_pushentity(), which is also what NULL needs.
*/
LUA_API void  (PushLuaInstanceSafe) (lua_State *L, CBaseEntity *pEntity);



LUALIB_API lua_CBaseEntity *(luaL_checkentity) (lua_State *L, int narg);
LUALIB_API lua_CBaseEntity *(luaL_optentity) (lua_State *L, int narg,
                                                            lua_CBaseEntity *def);


#endif // LBASEENTITY_SHARED_H
