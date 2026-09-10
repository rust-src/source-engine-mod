//========== Copyleft © 2011, Team Sandbox, Some rights reserved. ===========//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//


#ifndef LUASRCLIB_H
#define LUASRCLIB_H
#ifdef _WIN32
#pragma once
#endif


#define LUA_BASEANIMATINGLIBNAME		"CBaseAnimating"
LUALIB_API int (luaopen_CBaseAnimating) (lua_State *L);

#define LUA_EFFECTSLIBNAME 				"Effects"
LUALIB_API int (luaopen_Effects) (lua_State *L );

#define LUA_BASECOMBATWEAPONLIBNAME		"CBaseCombatWeapon"
LUALIB_API int (luaopen_CBaseCombatWeapon) (lua_State *L);

#define LUA_BASEENTITYLIBNAME			"CBaseEntity"
LUALIB_API int (luaopen_CBaseEntity) (lua_State *L);
LUALIB_API int (luaopen_CBaseEntity_shared) (lua_State *L);

#define LUA_BASEPLAYERLIBNAME			"CBasePlayer"
LUALIB_API int (luaopen_CBasePlayer) (lua_State *L);
LUALIB_API int (luaopen_CBasePlayer_shared) (lua_State *L);

#define LUA_EFFECTDATALIBNAME			"CEffectData"
LUALIB_API int (luaopen_CEffectData) (lua_State *L);

#define LUA_GAMETRACELIBNAME			"CGameTrace"
LUALIB_API int (luaopen_CGameTrace) (lua_State *L);

#define LUA_HL2MPPLAYERLIBNAME			"CHL2MP_Player"
LUALIB_API int (luaopen_CHL2MP_Player) (lua_State *L);
LUALIB_API int (luaopen_CHL2MP_Player_shared) (lua_State *L);

#define LUA_COLORLIBNAME				"Color"
LUALIB_API int (luaopen_Color) (lua_State *L);

#define LUA_CONCOMMANDLIBNAME			"ConCommand"
LUALIB_API int (luaopen_ConCommand) (lua_State *L);

#define LUA_CONTENTSLIBNAME				"CONTENTS"
LUALIB_API int (luaopen_CONTENTS) (lua_State *L);

#define LUA_CONVARLIBNAME				"ConVar"
LUALIB_API int (luaopen_ConVar) (lua_State *L);

#define LUA_PASFILTERLIBNAME			"CPASFilter"
LUALIB_API int (luaopen_CPASFilter) (lua_State *L);

#define LUA_RECIPIENTFILTERLIBNAME		"CRecipientFilter"
LUALIB_API int (luaopen_CRecipientFilter) (lua_State *L);

#define LUA_TAKEDAMAGEINFOLIBNAME		"CTakeDamageInfo"
LUALIB_API int (luaopen_CTakeDamageInfo) (lua_State *L);

#define LUA_CVARLIBNAME					"cvar"
LUALIB_API int (luaopen_cvar) (lua_State *L);

#define LUA_DBGLIBNAME					"dbg"
LUALIB_API int (luaopen_dbg) (lua_State *L);

#define LUA_DEBUGOVERLAYLIBNAME			"debugoverlay"
LUALIB_API int (luaopen_debugoverlay) (lua_State *L);

#define LUA_ENGINELIBNAME				"engine"
LUALIB_API int (luaopen_engine) (lua_State *L);

#define LUA_ENGINEVGUILIBNAME			"enginevgui"
LUALIB_API int (luaopen_enginevgui) (lua_State *L);

#define LUA_FCVARLIBNAME				"FCVAR"
LUALIB_API int (luaopen_FCVAR) (lua_State *L);

#define LUA_FILESYSTEMLIBNAME			"filesystem"
LUALIB_API int (luaopen_filesystem) (lua_State *L);

#define LUA_HL2SBLIBNAME				"hl2sb"
LUALIB_API int (luaopen_hl2sb) (lua_State *L);

#define LUA_FONTFLAGLIBNAME				"FONTFLAG"
LUALIB_API int (luaopen_FONTFLAG) (lua_State *L);

#define LUA_ENTLISTLIBNAME				"gEntList"
LUALIB_API int (luaopen_gEntList) (lua_State *L);

#define LUA_GLOBALSLIBNAME				"gpGlobals"
LUALIB_API int (luaopen_gpGlobals) (lua_State *L);

#define LUA_CLIENTSHADOWMGRLIBNAME		"g_pClientShadowMgr"
LUALIB_API int (luaopen_g_pClientShadowMgr) (lua_State *L);

#define LUA_FONTLIBNAME					"HFont"
LUALIB_API int (luaopen_HFont) (lua_State *L);

#define LUA_HSCHEMELIBNAME				"HScheme"
LUALIB_API int (luaopen_HScheme) (lua_State *L);

#define LUA_MATERIALLIBNAME				"IMaterial"
LUALIB_API int (luaopen_IMaterial) (lua_State *L);

#define LUA_MOVEHELPERLIBNAME			"IMoveHelper"
LUALIB_API int (luaopen_IMoveHelper) (lua_State *L);

#define LUA_INLIBNAME					"IN"
LUALIB_API int (luaopen_IN) (lua_State *L);

#define LUA_NETCHANNELINFOLIBNAME		"INetChannelInfo"
LUALIB_API int (luaopen_INetChannelInfo) (lua_State *L);

#define LUA_INETWORKSTRINGTABLELIBNAME	"INetworkStringTable"
LUALIB_API int (luaopen_INetworkStringTable) (lua_State *L);

#define LUA_INPUTLIBNAME				"input"
LUALIB_API int (luaopen_input) (lua_State *L);

#define LUA_PHYSICSOBJECTLIBNAME		"IPhysicsObject"
LUALIB_API int (luaopen_IPhysicsObject) (lua_State *L);

#define LUA_PHYSICSSURFACEPROPSLIBNAME	"IPhysicsSurfaceProps"
LUALIB_API int (luaopen_IPhysicsSurfaceProps) (lua_State *L);

#define LUA_PREDICTIONSYSTEMLIBNAME		"IPredictionSystem"
LUALIB_API int (luaopen_IPredictionSystem) (lua_State *L);

#define LUA_ISCHEMELIBNAME				"IScheme"
LUALIB_API int (luaopen_IScheme) (lua_State *L);

// HL2SB: ported from Experiment: Source.  Localizations backs GMod's
// language.GetPhrase / language.Add.
#define LUA_LOCALIZATIONLIBNAME			"Localizations"
LUALIB_API int (luaopen_Localizations) (lua_State *L);

// HL2SB: ported from Experiment: Source.  Populates _E with the shared
// enumeration tables (ACTIVITY, BUTTON, DAMAGE_TYPE, COLLISION_GROUP, ...) that
// GMod scripts read as _E.<LIB>.<MEMBER>.  The library name is "" because it
// installs no global table of its own.
#define LUA_SHAREDENUMNAME				""
LUALIB_API int (luaopen_SharedEnumerations) (lua_State *L);

// Enumeration table names used by the ported enumeration library.  These are not
// libraries with their own luaopen_*; luaopen_SharedEnumerations installs each as
// _E.<name>.
#define LUA_EFLIBNAME					"ENTITY_EFFECT"
#define LUA_ENGINEFLAGSENUMLIBNAME		"ENGINE_FLAG"
#define LUA_FLEDICTLIBNAME				"EDICT_FLAG"
#define LUA_GESTURESLOTLIBNAME			"GESTURE_SLOT"
#define LUA_LIFELIBNAME					"LIFE"
#define LUA_MOVECOLLIDELIBNAME			"MOVE_COLLIDE"
#define LUA_MOVETYPELIBNAME				"MOVE_TYPE"
#define LUA_OBSMODELIBNAME				"OBSERVER_MODE"
#define LUA_SOLIDFLAGLIBNAME			"SOLID_FLAG"
#define LUA_SOLIDLIBNAME				"SOLID"

// HL2SB: ported from Experiment: Source.
#define LUA_PARTICLESYSTEMLIBNAME		"ParticleSystems"
LUALIB_API int (luaopen_ParticleSystem) (lua_State *L);

#define LUA_SYSTEMSLIBNAME				"Systems"
LUALIB_API int (luaopen_Systems) (lua_State *L);

// Realm-specific enumeration tables (no global of their own, like the shared set).
#define LUA_SERVERENUMNAME				""
LUALIB_API int (luaopen_ServerEnumerations) (lua_State *L);

#define LUA_CLIENTENUMNAME				""
LUALIB_API int (luaopen_ClientEnumerations) (lua_State *L);

#define LUA_STEAMFRIENDSLIBNAME			"ISteamFriends"
LUALIB_API int (luaopen_ISteamFriends) (lua_State *L);

#define LUA_KEYVALUESLIBNAME			"KeyValues"
LUALIB_API int (luaopen_KeyValues) (lua_State *L);

#define LUA_MASKLIBNAME					"MASK"
LUALIB_API int (luaopen_MASK) (lua_State *L);

// HL2SB: ported from Experiment: Source.  Files/FileHandle back GMod's file
// library, Sounds/AudioChannel back sound.Play*, Entities backs ents.*.
#define LUA_FILESLIBNAME				"Files"
LUALIB_API int (luaopen_Files) (lua_State *L);

#define LUA_FILEHANDLEMETANAME			"FileHandle"
LUALIB_API int (luaopen_FileHandle) (lua_State *L);

#define LUA_SOUNDSLIBNAME				"Sounds"
LUALIB_API int (luaopen_Sounds) (lua_State *L);

#define LUA_AUDIOCHANNELMETANAME		"AudioChannel"
LUALIB_API int (luaopen_AudioChannel) (lua_State *L);

#define LUA_ENTITIESLIBNAME				"Entities"
LUALIB_API int (luaopen_Entities) (lua_State *L);

#define LUA_SCRIPTEDENTITIESLIBNAME		"ScriptedEntities"

// HL2SB: ported from Experiment: Source.  Renders is the `render` library GMod
// scripts draw with; Texture is the ITexture userdata they hand to it.  Upstream
// names the entry point luaopen_render (singular) while the library it commits is
// called Renders -- both spellings are kept as they are.
#define LUA_RENDERSLIBNAME				"Renders"
LUALIB_API int (luaopen_render) (lua_State *L);

#define LUA_ITEXTUREMETANAME			"ITexture"
LUALIB_API int (luaopen_ITexture) (lua_State *L);

// HL2SB: ported from Experiment: Source.  Chats is GMod's `chat` library
// (chat.AddText, chat.Print, chat.PlaySound).
#define LUA_CHATSLIBNAME				"Chats"
LUALIB_API int (luaopen_Chats) (lua_State *L);

// HL2SB: ported from Experiment: Source.  CBaseFlex carries the flex bindings and,
// on the client, Entities.CreateClientEntity -- the ClientsideModel backing.
#define LUA_CBASEFLEXLIBNAME			"CBaseFlex"
LUALIB_API int (luaopen_CBaseFlex_shared) (lua_State *L);

#define LUA_MATHLIBLIBNAME				"mathlib"
LUALIB_API int (luaopen_mathlib) (lua_State *L);

#define LUA_MATRIXLIBNAME				"matrix3x4_t"
LUALIB_API int (luaopen_matrix3x4_t) (lua_State *L);

#define LUA_NETLIBNAME					"net"
LUALIB_API int (luaopen_net) (lua_State *L);

#define LUA_NETWORKSTRINGTABLELIBNAME	"networkstringtable"
LUALIB_API int (luaopen_networkstringtable) (lua_State *L);

#define LUA_PANELLIBNAME				"Panel"
LUALIB_API int (luaopen_Panel) (lua_State *L);

#define LUA_PHYSENVLIBNAME				"physenv"
LUALIB_API int (luaopen_physenv) (lua_State *L);

#define LUA_PREDICTIONLIBNAME			"prediction"
LUALIB_API int (luaopen_prediction) (lua_State *L);

#define LUA_QANGLELIBNAME				"QAngle"
LUALIB_API int (luaopen_QAngle) (lua_State *L);

#define LUA_RANDOMLIBNAME				"random"
LUALIB_API int (luaopen_random) (lua_State *L);

#define LUA_SCHEMELIBNAME				"scheme"
LUALIB_API int (luaopen_scheme) (lua_State *L);

#define LUA_STEAMAPICONTEXTLIBNAME		"steamapicontext"
LUALIB_API int (luaopen_steamapicontext) (lua_State *L);

#define LUA_SURFLIBNAME					"SURF"
LUALIB_API int (luaopen_SURF) (lua_State *L);

#define LUA_SURFACELIBNAME				"surface"
LUALIB_API int (luaopen_surface) (lua_State *L);

#define LUA_UTILLIBNAME					"UTIL"
LUALIB_API int (luaopen_UTIL) (lua_State *L);
LUALIB_API int (luaopen_UTIL_shared) (lua_State *L);

#ifndef CLIENT_DLL
#define LUA_UNDOLIBNAME					"hl2sb_undo"
LUALIB_API int (luaopen_undo) (lua_State *L);
#endif

#define LUA_VECTORLIBNAME				"Vector"
LUALIB_API int (luaopen_Vector) (lua_State *L);

#define LUA_VGUILIBNAME					"vgui"
LUALIB_API int (luaopen_vgui) (lua_State *L);

#define LUA_VMATRIXLIBNAME				"vmatrix"
LUALIB_API int (luaopen_VMatrix) (lua_State *L);


/* open all Source Engine libraries */
LUALIB_API void (luasrc_openlibs) (lua_State *L); 



// HL2SB: Experiment: Source declares its binding macros in this header; they live in
// luabinding.h so that ported binding files keep compiling unchanged.
#include "luabinding.h"

#endif // LUASRCLIB_H
