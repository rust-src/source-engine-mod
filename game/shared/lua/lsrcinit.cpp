//========== Copyleft © 2011, Team Sandbox, Some rights reserved. ===========//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//


#define lsrcinit_cpp

#include "cbase.h"
#include "lua.hpp"

#include "luasrclib.h"
#include "lauxlib.h"


static const luaL_Reg luasrclibs[] = {
  // HL2SB: ported from Experiment: Source.  Fills _E with the shared enums.
  {LUA_SHAREDENUMNAME, luaopen_SharedEnumerations},
  // HL2SB: ported from Experiment: Source.  The rest of the _E tables.  They are
  // opened right after luaopen_SharedEnumerations because each one does
  // lua_getglobal("_E") + lua_setfield, and Experiment's gmod_compatibility shim
  // reads them -- sh_enumerations.lua error()s on a missing key.  luaopen_ACTIVITY
  // additionally takes ownership of the activity list away from the world entity
  // (see the LUA_SDK branch of REGISTER_SHARED_ACTIVITY in activitylist.h).
  {LUA_ACTIVITYENUMNAME, luaopen_ACTIVITY},
  {LUA_BUTTONENUMNAME, luaopen_BUTTON},
  {LUA_EFLIBNAME, luaopen_EF},
  {LUA_ENGINEFLAGSENUMLIBNAME, luaopen_FL},
  {LUA_FLEDICTLIBNAME, luaopen_FL_EDICT},
  {LUA_GESTURESLOTLIBNAME, luaopen_GESTURE_SLOT},
  {LUA_LIFELIBNAME, luaopen_LIFE},
  {LUA_MOVECOLLIDELIBNAME, luaopen_MOVECOLLIDE},
  {LUA_MOVETYPELIBNAME, luaopen_MOVETYPE},
  {LUA_OBSMODELIBNAME, luaopen_OBS_MODE},
  {LUA_SOLIDFLAGLIBNAME, luaopen_SOLIDFLAG},
  {LUA_SOLIDLIBNAME, luaopen_SOLID},
  {LUA_BASEANIMATINGLIBNAME, luaopen_CBaseAnimating},
  {LUA_BASEANIMATINGLIBNAME, luaopen_CBaseAnimating_shared},
  {LUA_BASECOMBATWEAPONLIBNAME, luaopen_CBaseCombatWeapon},
  {LUA_BASEENTITYLIBNAME, luaopen_CBaseEntity},
  {LUA_BASEENTITYLIBNAME, luaopen_CBaseEntity_shared},
  // HL2SB: ported from Experiment: Source.  Registered after luaopen_Entities so it
  // merges its Entities.CreateClientEntity into the same `ents` table.
  {LUA_CBASEFLEXLIBNAME, luaopen_CBaseFlex_shared},
  {LUA_BASEPLAYERLIBNAME, luaopen_CBasePlayer},
  {LUA_BASEPLAYERLIBNAME, luaopen_CBasePlayer_shared},
  {LUA_EFFECTDATALIBNAME, luaopen_CEffectData},
  {LUA_GAMETRACELIBNAME, luaopen_CGameTrace},
#ifndef CLIENT_DLL
  {LUA_EFFECTSLIBNAME, luaopen_Effects},
  {LUA_HL2MPPLAYERLIBNAME, luaopen_CHL2MP_Player},
#endif
  {LUA_HL2MPPLAYERLIBNAME, luaopen_CHL2MP_Player_shared},
  {LUA_COLORLIBNAME, luaopen_Color},
  {LUA_CONCOMMANDLIBNAME, luaopen_ConCommand},
  {LUA_CONTENTSLIBNAME, luaopen_CONTENTS},
  {LUA_CONVARLIBNAME, luaopen_ConVar},
  {LUA_PASFILTERLIBNAME, luaopen_CPASFilter},
  {LUA_RECIPIENTFILTERLIBNAME, luaopen_CRecipientFilter},
  {LUA_TAKEDAMAGEINFOLIBNAME, luaopen_CTakeDamageInfo},
  {LUA_CVARLIBNAME, luaopen_cvar},
  {LUA_DBGLIBNAME, luaopen_dbg},
  {LUA_DEBUGOVERLAYLIBNAME, luaopen_debugoverlay},
  {LUA_ENGINELIBNAME, luaopen_engine},
#ifdef CLIENT_DLL
  // FIXME: obsolete? should be passing VPANELs, but passes Panel instead,
  // which always ends up being invalid (we can't access them by pointer)
  {LUA_ENGINEVGUILIBNAME, luaopen_enginevgui},
#endif
  {LUA_FCVARLIBNAME, luaopen_FCVAR},
  {LUA_FILESYSTEMLIBNAME, luaopen_filesystem},
#ifdef CLIENT_DLL
  {LUA_FONTFLAGLIBNAME, luaopen_FONTFLAG},
#endif
#ifndef CLIENT_DLL
  {LUA_ENTLISTLIBNAME, luaopen_gEntList},
#endif
  {LUA_GLOBALSLIBNAME, luaopen_gpGlobals},
  // HL2SB: ported from Experiment: Source.  gameevent.Listen.
  {LUA_GAMEEVENTSLIBNAME, luaopen_GameEvents},
  {LUA_HL2SBLIBNAME, luaopen_hl2sb},
#ifdef CLIENT_DLL
  {LUA_CLIENTSHADOWMGRLIBNAME, luaopen_g_pClientShadowMgr},
  {LUA_FONTLIBNAME, luaopen_HFont},
  {LUA_HSCHEMELIBNAME, luaopen_HScheme},
#endif
  {LUA_MATERIALLIBNAME, luaopen_IMaterial},
  {LUA_MOVEHELPERLIBNAME, luaopen_IMoveHelper},
  {LUA_INLIBNAME, luaopen_IN},
#ifndef CLIENT_DLL
  {LUA_NETCHANNELINFOLIBNAME, luaopen_INetChannelInfo},
#endif
  {LUA_INETWORKSTRINGTABLELIBNAME, luaopen_INetworkStringTable},
#ifdef CLIENT_DLL
  {LUA_INPUTLIBNAME, luaopen_input},
#endif
  {LUA_PHYSICSOBJECTLIBNAME, luaopen_IPhysicsObject},
  {LUA_PHYSICSSURFACEPROPSLIBNAME, luaopen_IPhysicsSurfaceProps},
  {LUA_PREDICTIONSYSTEMLIBNAME, luaopen_IPredictionSystem},
#ifdef CLIENT_DLL
  {LUA_ISCHEMELIBNAME, luaopen_IScheme},
#endif
//  {LUA_STEAMFRIENDSLIBNAME, luaopen_ISteamFriends},
  {LUA_KEYVALUESLIBNAME, luaopen_KeyValues},
  // HL2SB: ported from Experiment: Source
  {LUA_LOCALIZATIONLIBNAME, luaopen_Localizations},
  // HL2SB: ported from Experiment: Source.
  {LUA_PARTICLESYSTEMLIBNAME, luaopen_ParticleSystem},
  {LUA_SYSTEMSLIBNAME, luaopen_Systems},
  // HL2SB: ported from Experiment: Source.  `Entities` is merged onto the same
  // global table as the Team Sandbox era `ents` (Create/GetByIndex).
  {LUA_ENTITIESLIBNAME, luaopen_Entities},
  // HL2SB: ported from Experiment: Source.  AudioChannel and the URL/file
  // streaming bindings were dropped -- they depend on their BASS manager.
  {LUA_SOUNDSLIBNAME, luaopen_Sounds},
  // TODO(port): Files / FileHandle still need a decision on how they coexist with
  // the Team Sandbox era `filesystem` library (LUA_FILESYSTEMLIBNAME) before they
  // can be registered.
  // {LUA_FILESLIBNAME, luaopen_Files},
  // {LUA_FILEHANDLEMETANAME, luaopen_FileHandle},
#ifdef CLIENT_DLL
  {LUA_CLIENTENUMNAME, luaopen_ClientEnumerations},
#else
  {LUA_SERVERENUMNAME, luaopen_ServerEnumerations},
#endif
  {LUA_MASKLIBNAME, luaopen_MASK},
  {LUA_MATHLIBLIBNAME, luaopen_mathlib},
  {LUA_MATRIXLIBNAME, luaopen_matrix3x4_t},
  {LUA_NETLIBNAME, luaopen_net},
  {LUA_NETWORKSTRINGTABLELIBNAME, luaopen_networkstringtable},
#ifdef CLIENT_DLL
  {LUA_PANELLIBNAME, luaopen_Panel},
#endif
  {LUA_PHYSENVLIBNAME, luaopen_physenv},
#ifdef CLIENT_DLL
  {LUA_PREDICTIONLIBNAME, luaopen_prediction},
#endif
  {LUA_QANGLELIBNAME, luaopen_QAngle},
  {LUA_RANDOMLIBNAME, luaopen_random},
#ifdef CLIENT_DLL
  // HL2SB: ported from Experiment: Source.  `Renders` carries GMod's render.*
  // (render.SetColorModulation, render.DrawSprite, render.PushRenderTarget, ...);
  // the Lua content aliases the global `render` onto it.  ITexture is the userdata
  // those bindings hand back, so its metatable is installed first.
  {LUA_ITEXTUREMETANAME, luaopen_ITexture},
  {LUA_RENDERSLIBNAME, luaopen_render},
  // HL2SB: ported from Experiment: Source.  GMod's chat.* lives here.
  {LUA_CHATSLIBNAME, luaopen_Chats},
#endif
#ifdef CLIENT_DLL
  {LUA_SCHEMELIBNAME, luaopen_scheme},
#endif
//  {LUA_STEAMAPICONTEXTLIBNAME, luaopen_steamapicontext},
  {LUA_SURFLIBNAME, luaopen_SURF},
#ifdef CLIENT_DLL
  {LUA_SURFACELIBNAME, luaopen_surface},
#endif
  {LUA_UTILLIBNAME, luaopen_UTIL},
  {LUA_UTILLIBNAME, luaopen_UTIL_shared},
#ifndef CLIENT_DLL
  {LUA_UNDOLIBNAME, luaopen_undo},
#endif
  {LUA_VECTORLIBNAME, luaopen_Vector},
#ifdef CLIENT_DLL
  {LUA_VGUILIBNAME, luaopen_vgui},
#endif
  {LUA_VMATRIXLIBNAME, luaopen_VMatrix},
  {NULL, NULL}
};


LUALIB_API void luasrc_openlibs (lua_State *L) {
  const luaL_Reg *lib = luasrclibs;
  for (; lib->func; lib++) {
    lua_pushcfunction(L, lib->func);
    lua_pushstring(L, lib->name);
    lua_call(L, 1, 0);
  }
}

