//====== Copyright © 1996-2005, Valve Corporation, All rights reserved. =====//
//
// Purpose: Client-side CBasePlayer.
//
//			- Manages the player's flashlight effect.
//
//===========================================================================//
#define lc_baseplayer_cpp

#include "cbase.h"
#include "c_baseplayer.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lbaseplayer_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static int CBasePlayer_GetLocalPlayer (lua_State *L) {
  lua_pushplayer(L, CBasePlayer::GetLocalPlayer());
  return 1;
}


static const luaL_Reg CBasePlayermeta[] = {
  {"GetLocalPlayer", CBasePlayer_GetLocalPlayer},
  {NULL, NULL}
};


/*
** Open CBasePlayer object
*/
LUALIB_API int luaopen_CBasePlayer (lua_State *L) {
  luaL_getmetatable(L, LUA_BASEPLAYERLIBNAME);
  if (lua_isnoneornil(L, -1)) {
    lua_pop(L, 1);
    luaL_newmetatable(L, LUA_BASEPLAYERLIBNAME);
  }
  luaL_register(L, NULL, CBasePlayermeta);

  // HL2SB: expose a global LocalPlayer() that returns the local player entity,
  // so GMod-style scripts (and the player model menu's colour tab) can call
  // LocalPlayer():SetPlayerColor(...) / GetPlayerColor().  engine.GetLocalPlayer()
  // only returns the ent index, which is useless for entity method calls.
  lua_pushcfunction(L, CBasePlayer_GetLocalPlayer);
  lua_setglobal(L, "LocalPlayer");

  return 1;
}
