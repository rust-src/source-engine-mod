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
  // HL2SB: ported from Experiment: Source.  Label is the base every Derma text
  // control sits on (DLabel, DButton, DTextEntry), so it has to be opened after
  // Panel, whose metatable it extends.
  {LUA_LABELMETANAME, luaopen_Label},
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


/*
** ===========================================================================
** HL2SB: metatable names.
**
** Garry's Mod exposes two C globals that all of its Lua framework (and the
** Experiment: Source gmod_compatibility shim) is built on:
**
**   FindMetaTable( name )          -> the metatable registered under `name`
**   RegisterMetaTable( name, tbl ) -> register one (Derma does this)
**
** GMod registers its metatables under the bare class names ("Entity", "Player",
** "Weapon", "Angle", ...); the Team Sandbox era code HL2SB inherited registers
** them under the C++ class names ("CBaseEntity", "CBasePlayer", "QAngle", ...).
** This table bridges the two, and the aliases are mirrored into the registry so
** that a plain `_R.Entity` works as well -- base_open already publishes the
** registry as `_R`, and Experiment's own Lua reads `_R.Entity` directly instead
** of going through FindMetaTable.
**
** A name with no entry falls through to a literal registry lookup, so classes
** HL2SB registers under the GMod name already (Panel, Frame, Button, ...) keep
** working, and RegisterMetaTable can add new ones ("DPanel", "DLabel", ...).
** ===========================================================================
*/
struct LuaMetatableAlias_t
{
  const char *pszGModName;   // name GMod code asks for
  const char *pszNativeName; // name HL2SB's libs registered it under
};

static const LuaMetatableAlias_t s_LuaMetatableAliases[] = {
  { "Entity",            LUA_BASEENTITYLIBNAME },
  { "Player",            LUA_BASEPLAYERLIBNAME },
  { "Weapon",            LUA_BASECOMBATWEAPONLIBNAME },
  { "Angle",             LUA_QANGLELIBNAME },
  { "Vector",            LUA_VECTORLIBNAME },
  { "Matrix",            LUA_MATRIXLIBNAME },
  { "Color",             LUA_COLORLIBNAME },
  { "EffectData",        LUA_EFFECTDATALIBNAME },
  { "Trace",             LUA_GAMETRACELIBNAME },
  { "ConsoleVariable",   LUA_CONVARLIBNAME },
  { "ConsoleCommand",    LUA_CONCOMMANDLIBNAME },
  { "RecipientFilter",   LUA_RECIPIENTFILTERLIBNAME },
  { "TakeDamageInfo",    LUA_TAKEDAMAGEINFOLIBNAME },
  { "Material",          LUA_MATERIALLIBNAME },
  { "MoveHelper",        LUA_MOVEHELPERLIBNAME },
  { "Texture",           LUA_ITEXTUREMETANAME },
  { "KeyValuesHandle",   LUA_KEYVALUESLIBNAME },
  { "FileHandle",        "FileHandle_t" },
  { "PhysicsObject",     LUA_PHYSICSOBJECTLIBNAME },
  { "PhysicsSurfacePropertiesHandle", LUA_PHYSICSSURFACEPROPSLIBNAME },
  { "NetChannelInfo",    LUA_NETCHANNELINFOLIBNAME },
  { "Panel",             "Panel" },
  { "Frame",             "Frame" },
  { "Button",            "Button" },
  { "CheckButton",       "CheckButton" },
  { "EditablePanel",     "EditablePanel" },
  { "ModelPanel",        "ModelPanel" },
  { "ProjectedTexture",  "ProjectedTexture" },  // TODO(port): lc_projected_texture
  { "AudioChannel",      "AudioChannel" },      // TODO(port): needs BASS
  { "MoveData",          "MoveData" },          // TODO(port): lmovedata
  // GMod's name for the user command metatable, confirmed by dumping its registry:
  // "CUserCmd", with MetaID 19 (TYPE_USERCMD).
  { "CUserCmd",          "CUserCmd" },          // TODO(port): lusercmd
  { "MessageReader",     "MessageReader" },     // TODO(port): needs bf_read
  { "MessageWriter",     "MessageWriter" },     // TODO(port): needs bf_write
  { "Label",             "Label" },             // TODO(port): scripted_controls
  { "Html",              "Html" },              // TODO(port): scripted_controls
  { "TextEntry",         "TextEntry" },         // TODO(port): scripted_controls
  { "ModelImagePanel",   "ModelImagePanel" },   // TODO(port): scripted_controls
  { "SteamFriendsHandle", "SteamFriendsHandle" },
  // Confirmed by the registry dump: these all exist in GMod.  They resolve to nil
  // in HL2SB until the owning class is ported, which is the same thing GMod does
  // before the relevant library is loaded.
  { "NPC",               "NPC" },
  { "Vehicle",           "Vehicle" },
  { "NextBot",           "NextBot" },
  { "CSEnt",             "CSEnt" },
  { "Tool",              "Tool" },
  { "ISave",             "ISave" },
  { "IRestore",          "IRestore" },
  { "IMesh",             "IMesh" },
  { "CLuaEmitter",       "CLuaEmitter" },
  { "CLuaParticle",      "CLuaParticle" },
  { "CNewParticleEffect", "CNewParticleEffect" },
  { "SurfaceInfo",       "SurfaceInfo" },
  { "PhysCollide",       "PhysCollide" },
  { "IGModAudioChannel", "IGModAudioChannel" },
  { "IVideoWriter",      "IVideoWriter" },
  { "pixelvis_handle_t", "pixelvis_handle_t" },
  { "MarkupObject",      "MarkupObject" },
  { NULL, NULL }
};

static const char *LuaNativeMetatableName (const char *pszName) {
  for (int i = 0; s_LuaMetatableAliases[i].pszGModName; ++i) {
    if (!Q_stricmp(s_LuaMetatableAliases[i].pszGModName, pszName))
      return s_LuaMetatableAliases[i].pszNativeName;
  }
  return pszName;
}

/*
** GMod returns nil for a metatable that is not registered yet, which is what
** Derma relies on while it is still defining its controls.
**
** It has to be an explicit nil and not "no results": in Lua a call that returns
** zero values vanishes when it is the last argument of another call, so
** `print( FindMetaTable( "Label" ) )` collapsed to a bare `print()` and printed
** an empty line instead of "nil".
*/
static int lua_FindMetaTable (lua_State *L) {
  const char *pszName = luaL_checkstring(L, 1);
  luaL_getmetatable(L, LuaNativeMetatableName(pszName));
  if (lua_isnil(L, -1))
    lua_pushnil(L);  /* leave exactly one value: nil */
  return 1;
}

static int lua_RegisterMetaTable (lua_State *L) {
  const char *pszName = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TTABLE);
  lua_pushvalue(L, 2);
  lua_setfield(L, LUA_REGISTRYINDEX, pszName);
  return 0;
}

static const luaL_Reg lua_metatable_funcs[] = {
  {"FindMetaTable", lua_FindMetaTable},
  {"RegisterMetaTable", lua_RegisterMetaTable},
  {NULL, NULL}
};

/* Publish registry[gmodName] = the native metatable, so `_R.Entity` resolves. */
static void luasrc_install_metatable_aliases (lua_State *L) {
  for (int i = 0; s_LuaMetatableAliases[i].pszGModName; ++i) {
    const char *pszGModName = s_LuaMetatableAliases[i].pszGModName;
    const char *pszNativeName = s_LuaMetatableAliases[i].pszNativeName;

    if (!Q_stricmp(pszGModName, pszNativeName))
      continue;

    luaL_getmetatable(L, pszNativeName);
    if (lua_istable(L, -1)) {
      lua_pushvalue(L, -1);
      lua_setfield(L, LUA_REGISTRYINDEX, pszGModName);
    }
    lua_pop(L, 1);
  }
}

/*
** ===========================================================================
** HL2SB: GMod type names.
**
** GMod's Lua type system is not the raw Lua one.  garrysmod/lua/includes/util.lua
** *replaces* the global `type` with:
**
**   function type( v )
**     local v_type = C_type( v )
**     if ( v_type ~= "userdata" ) then return v_type end
**     local metatable = getmetatable( v )
**     local metaName = metatable and metatable.MetaName
**     return C_type( metaName ) == "string" and metaName or "UserData"
**   end
**
** and TypeID() reads metatable.MetaID, and isentity() walks
** metatable.MetaBaseClass to find "Entity".  So a metatable without MetaName
** makes type() report "UserData" for everything the moment GMod's util.lua is
** loaded, which would break all of Derma and the spawnmenu.
**
** HL2SB's own type() (luamanager.cpp) reads __type instead, and its values are
** the Team Sandbox era lowercase ones ("entity", "vector", "panel", ...).  The
** names below are stamped as both __type and MetaName so the two agree, and they
** follow Garry's Mod rather than Experiment: Source, which disagrees in three
** places (it calls a player "Entity", a VMatrix "Matrix", and a physics object
** "PhysicsObject").  Values are Garry's Mod's TYPE_* enum
** (https://wiki.facepunch.com/gmod/Enums/TYPE).
**
** Stamping happens here, once, rather than editing the ~30 binding files that
** create the metatables: every luaopen_* has already run by this point, so this
** covers both realms, and the table stays the single place to audit.
** ===========================================================================
*/
#define LUA_TYPE_ENTITY        9
#define LUA_TYPE_VECTOR       10
#define LUA_TYPE_ANGLE        11
#define LUA_TYPE_PHYSOBJ      12
#define LUA_TYPE_DAMAGEINFO   15
#define LUA_TYPE_EFFECTDATA   16
#define LUA_TYPE_MOVEDATA     17
#define LUA_TYPE_RECIPFILTER  18
#define LUA_TYPE_USERCMD      19
#define LUA_TYPE_MATERIAL     21
#define LUA_TYPE_PANEL        22
#define LUA_TYPE_TEXTURE      25
#define LUA_TYPE_CONVAR       27
#define LUA_TYPE_MATRIX       29
#define LUA_TYPE_FILE         34
#define LUA_TYPE_PROJTEX      41
#define LUA_TYPE_USERDATA      7
// GMod's Color metatable reports MetaID 44, not the TYPE_COLOR = 255 constant --
// 255 is a networking hack for net.WriteType (see the TYPE enum), while the
// metatable itself carries 44, which equals TYPE_COUNT in that enum.  Taken from
// a dump of GMod's own registry, not from the wiki.
#define LUA_TYPE_COLOR        44
// Not in the TYPE enum of the dumped build (TYPE_COUNT is 44 there), but GMod
// does register a MarkupObject metatable and reports 45 for it.
#define LUA_TYPE_MARKUP       45

struct LuaTypeInfo_t
{
  const char *pszMetatable;   // registry name the metatable was created under
  const char *pszTypeName;    // MetaName / __type
  int iTypeID;                // MetaID
  const char *pszBaseMetatable; // MetaBaseClass, or NULL
  bool bIsTableType;          // value is a Lua table: stamp MetaName/MetaID but NOT __type
};

static const LuaTypeInfo_t s_LuaTypeInfo[] = {
  // Entities.  GMod reports Player and Weapon separately and chains them onto
  // Entity, which is what isentity() walks.
  { LUA_BASEENTITYLIBNAME,      "Entity",          LUA_TYPE_ENTITY,     NULL },
  { LUA_BASEPLAYERLIBNAME,      "Player",          LUA_TYPE_ENTITY,     LUA_BASEENTITYLIBNAME },
  { "CHL2MP_Player",            "Player",          LUA_TYPE_ENTITY,     LUA_BASEENTITYLIBNAME },
  { LUA_BASECOMBATWEAPONLIBNAME,"Weapon",          LUA_TYPE_ENTITY,     LUA_BASEENTITYLIBNAME },
  { "CBaseAnimating",           "Entity",          LUA_TYPE_ENTITY,     LUA_BASEENTITYLIBNAME },
  { "CBaseFlex",                "Entity",          LUA_TYPE_ENTITY,     LUA_BASEENTITYLIBNAME },
  { "CBaseCombatCharacter",     "Entity",          LUA_TYPE_ENTITY,     LUA_BASEENTITYLIBNAME },

  // Value types.
  { LUA_VECTORLIBNAME,          "Vector",          LUA_TYPE_VECTOR,     NULL },
  { LUA_QANGLELIBNAME,          "Angle",           LUA_TYPE_ANGLE,      NULL },
  { LUA_COLORLIBNAME,           "Color",           LUA_TYPE_COLOR,      NULL, /*bIsTableType*/ true },
  { LUA_VMATRIXLIBNAME,         "VMatrix",         LUA_TYPE_MATRIX,     NULL },
  { LUA_MATRIXLIBNAME,          "VMatrix",         LUA_TYPE_MATRIX,     NULL },
  { LUA_GAMETRACELIBNAME,       "Trace",           LUA_TYPE_USERDATA,   NULL },
  { LUA_KEYVALUESLIBNAME,       "KeyValues",       LUA_TYPE_USERDATA,   NULL },

  // engine objects
  { LUA_TAKEDAMAGEINFOLIBNAME,  "CTakeDamageInfo", LUA_TYPE_DAMAGEINFO, NULL },
  { LUA_EFFECTDATALIBNAME,      "CEffectData",     LUA_TYPE_EFFECTDATA, NULL },
  { LUA_RECIPIENTFILTERLIBNAME, "CRecipientFilter",LUA_TYPE_RECIPFILTER,NULL },
  { LUA_PASFILTERLIBNAME,       "CRecipientFilter",LUA_TYPE_RECIPFILTER,LUA_RECIPIENTFILTERLIBNAME },
  { LUA_MATERIALLIBNAME,        "IMaterial",       LUA_TYPE_MATERIAL,   NULL },
  { LUA_ITEXTUREMETANAME,       "ITexture",        LUA_TYPE_TEXTURE,    NULL },
  { LUA_CONVARLIBNAME,          "ConVar",          LUA_TYPE_CONVAR,     NULL },
  { LUA_CONCOMMANDLIBNAME,      "ConCommand",      LUA_TYPE_CONVAR,     NULL },
  { LUA_PHYSICSOBJECTLIBNAME,   "PhysObj",         LUA_TYPE_PHYSOBJ,    NULL },
  // The metatable is registered as "FileHandle_t" (lfilesystem.cpp);
  // LUA_FILEHANDLEMETANAME is the *library* name, not the metatable name.
  { "FileHandle_t",             "File",            LUA_TYPE_FILE,       NULL },
  { LUA_MOVEHELPERLIBNAME,      "MoveHelper",      LUA_TYPE_MOVEDATA,   NULL },

  // All vgui controls report "Panel" in GMod, with the class chain in
  // MetaBaseClass -- this is what derma's panels all rely on.
  { "Panel",                    "Panel",           LUA_TYPE_PANEL,      NULL },
  { "EditablePanel",            "Panel",           LUA_TYPE_PANEL,      "Panel" },
  { "Frame",                    "Panel",           LUA_TYPE_PANEL,      "EditablePanel" },
  { "Button",                   "Panel",           LUA_TYPE_PANEL,      "Panel" },
  { "CheckButton",              "Panel",           LUA_TYPE_PANEL,      "Button" },
  { "ModelPanel",               "Panel",           LUA_TYPE_PANEL,      "Panel" },
  { "PropertyDialog",           "Panel",           LUA_TYPE_PANEL,      "Frame" },
  { "PropertyPage",             "Panel",           LUA_TYPE_PANEL,      "EditablePanel" },

  { NULL, NULL, 0, NULL }
};

static void luasrc_install_type_names (lua_State *L) {
  for (int i = 0; s_LuaTypeInfo[i].pszMetatable; ++i) {
    luaL_getmetatable(L, s_LuaTypeInfo[i].pszMetatable);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      continue;  // not opened in this realm
    }

    // metatable.MetaName is what GMod's type() reads for userdata.
    lua_pushstring(L, s_LuaTypeInfo[i].pszTypeName);
    lua_setfield(L, -2, "MetaName");

    // metatable.__type is what HL2SB's own type() (luamanager.cpp) reads, and it is
    // skipped for the types GMod implements as plain Lua tables (Color): HL2SB's
    // type() reports __type for anything carrying a metatable, while GMod's type()
    // short-circuits on the raw Lua type and answers "table" there.  Verified by
    // dumping GMod: `Color() -> type = table`, and its Color metatable still has
    // MetaName "Color" / MetaID 44.
    if (!s_LuaTypeInfo[i].bIsTableType) {
      lua_pushstring(L, s_LuaTypeInfo[i].pszTypeName);
      lua_setfield(L, -2, "__type");
    }

    lua_pushinteger(L, s_LuaTypeInfo[i].iTypeID);
    lua_setfield(L, -2, "MetaID");

    if (s_LuaTypeInfo[i].pszBaseMetatable) {
      luaL_getmetatable(L, s_LuaTypeInfo[i].pszBaseMetatable);
      if (lua_istable(L, -1))
        lua_setfield(L, -2, "MetaBaseClass");
      else
        lua_pop(L, 1);
    }

    lua_pop(L, 1);
  }
}

LUALIB_API void luasrc_openlibs (lua_State *L) {
  const luaL_Reg *lib = luasrclibs;
  for (; lib->func; lib++) {
    lua_pushcfunction(L, lib->func);
    lua_pushstring(L, lib->name);
    lua_call(L, 1, 0);
  }

  /* Every lib is open now, so the metatables exist and can be aliased. */
  luasrc_install_metatable_aliases(L);
  luasrc_install_type_names(L);

  luaL_register(L, "_G", lua_metatable_funcs);
  lua_pop(L, 1);
}

