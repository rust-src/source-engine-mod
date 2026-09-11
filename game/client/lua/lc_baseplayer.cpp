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


//-----------------------------------------------------------------------------
// Purpose: HL2SB - player:UniqueID() on the CLIENT too.
//
// GMod's Player:UniqueID() exists in BOTH realms, and the Lua HUD depends on
// that: lua/game/client/hl2sb_cl_hudpickup.lua compares
//
//     LocalPlayer():UniqueID() != userid      -- userid from item_pickup
//
// to ignore other players' pickups.  The binding only ever existed in
// game/server/lua/lplayer.cpp, so on the client this raised
// "attempt to call a nil value (method 'UniqueID')" -- and lua/includes/modules/
// hook.lua UNREGISTERS a hook that throws, so the pickup HUD died on the very
// first pickup and never came back.  That is why the pickup notification never
// appeared even though the whole event chain (item_pickup -> HUDItemPickedUp ->
// HUDWeaponPickedUp/HUDItemPickedUp/HUDAmmoPickedUp -> HudViewportPaint) was
// wired correctly.
//
// Returns the engine user ID -- the same number CBasePlayer::GetUserID() gives
// on the server, and the same one HL2MP puts in item_pickup.userid, so the
// comparison above actually holds.
//-----------------------------------------------------------------------------
static int CBasePlayer_UniqueID (lua_State *L) {
  lua_pushinteger(L, luaL_checkplayer(L, 1)->GetUserID());
  return 1;
}


static const luaL_Reg CBasePlayermeta[] = {
  {"GetLocalPlayer", CBasePlayer_GetLocalPlayer},
  {"UniqueID", CBasePlayer_UniqueID},
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
