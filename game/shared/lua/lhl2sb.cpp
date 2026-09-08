//========== HL2SB ===========//
//
// Purpose: Lua bindings for the HL2SB player model configuration system.
//
//          The GMod-style player model menu is written in Lua, so it needs the
//          same data the C++ context menu uses: the cfg/playermodel entries and
//          a way to apply one.  Exposing them here keeps the Lua menu entirely
//          data-driven - adding a model is still just dropping a .cfg file.
//
//===========================================================================//

#include "cbase.h"
#include "lua.hpp"
#include "luasrclib.h"
#include "hl2sb_model_config.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
#include "c_baseplayer.h"
#endif

//-----------------------------------------------------------------------------
// Purpose: hl2sb.GetPlayerModels()
//
// Returns an array of { name, model, hands, file } for every loaded
// cfg/playermodel entry, in load order.
//-----------------------------------------------------------------------------
static int hl2sb_GetPlayerModels( lua_State *L )
{
	HL2SB_EnsureModelConfigsLoaded();

	lua_newtable( L );

	for ( int i = 0; i < g_nHL2SB_ModelConfigCount; i++ )
	{
		const HL2SB_ModelConfig_t &cfg = g_HL2SB_ModelConfigs[i];

		lua_newtable( L );

		lua_pushstring( L, cfg.szName );
		lua_setfield( L, -2, "name" );

		lua_pushstring( L, cfg.szPlayerModel );
		lua_setfield( L, -2, "model" );

		lua_pushstring( L, cfg.szHandsModel );
		lua_setfield( L, -2, "hands" );

		lua_pushstring( L, cfg.szConfigFile );
		lua_setfield( L, -2, "file" );

		lua_rawseti( L, -2, i + 1 );
	}

	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: hl2sb.GetPlayerModelCount()
//-----------------------------------------------------------------------------
static int hl2sb_GetPlayerModelCount( lua_State *L )
{
	HL2SB_EnsureModelConfigsLoaded();
	lua_pushinteger( L, g_nHL2SB_ModelConfigCount );
	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: hl2sb.FindPlayerModel( name )
// Returns the entry table, or nil when there is no such config.
//-----------------------------------------------------------------------------
static int hl2sb_FindPlayerModel( lua_State *L )
{
	const char *pszName = luaL_checkstring( L, 1 );

	HL2SB_EnsureModelConfigsLoaded();

	const HL2SB_ModelConfig_t *pCfg = HL2SB_GetModelConfigByName( pszName );
	if ( !pCfg )
	{
		lua_pushnil( L );
		return 1;
	}

	lua_newtable( L );

	lua_pushstring( L, pCfg->szName );
	lua_setfield( L, -2, "name" );

	lua_pushstring( L, pCfg->szPlayerModel );
	lua_setfield( L, -2, "model" );

	lua_pushstring( L, pCfg->szHandsModel );
	lua_setfield( L, -2, "hands" );

	lua_pushstring( L, pCfg->szConfigFile );
	lua_setfield( L, -2, "file" );

	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: hl2sb.SetPlayerModel( name )
//
// Applies a config by running the same console command the C++ menu uses, so
// both paths go through one implementation.  Returns false when the name is
// unknown, so the menu can report it instead of silently doing nothing.
//-----------------------------------------------------------------------------
static int hl2sb_SetPlayerModel( lua_State *L )
{
	const char *pszName = luaL_checkstring( L, 1 );

	HL2SB_EnsureModelConfigsLoaded();

	if ( !HL2SB_GetModelConfigByName( pszName ) )
	{
		lua_pushboolean( L, false );
		return 1;
	}

#ifdef CLIENT_DLL
	char szCmd[256];
	Q_snprintf( szCmd, sizeof( szCmd ), "hl2sb_setmodel %s\n", pszName );
	engine->ClientCmd( szCmd );
#endif

	lua_pushboolean( L, true );
	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: hl2sb.GetCurrentPlayerModel()
// Full path of the local player's model, or "" when there is none.
//-----------------------------------------------------------------------------
static int hl2sb_GetCurrentPlayerModel( lua_State *L )
{
#ifdef CLIENT_DLL
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer && pPlayer->GetModel() )
	{
		const char *pszName = modelinfo->GetModelName( pPlayer->GetModel() );
		lua_pushstring( L, pszName ? pszName : "" );
		return 1;
	}
#endif

	lua_pushstring( L, "" );
	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: hl2sb.IsModelPrecached( path )
//
// The server precaches every cfg/playermodel entry during
// CHL2MPRules::Precache(), but a model the client has not received cannot be
// rendered by vgui.ModelPanel.  The menu uses this to mark unusable entries
// instead of showing a blank preview.
//-----------------------------------------------------------------------------
static int hl2sb_IsModelPrecached( lua_State *L )
{
	const char *pszModel = luaL_checkstring( L, 1 );

#ifdef CLIENT_DLL
	lua_pushboolean( L, modelinfo->GetModelIndex( pszModel ) != -1 );
#else
	lua_pushboolean( L, true );
#endif

	return 1;
}

static const luaL_Reg hl2sblib[] = {
	{"GetPlayerModels",			hl2sb_GetPlayerModels},
	{"GetPlayerModelCount",		hl2sb_GetPlayerModelCount},
	{"FindPlayerModel",			hl2sb_FindPlayerModel},
	{"SetPlayerModel",			hl2sb_SetPlayerModel},
	{"GetCurrentPlayerModel",	hl2sb_GetCurrentPlayerModel},
	{"IsModelPrecached",		hl2sb_IsModelPrecached},
	{NULL, NULL}
};

/*
** Open hl2sb library
*/
LUALIB_API int luaopen_hl2sb( lua_State *L )
{
	luaL_register( L, LUA_HL2SBLIBNAME, hl2sblib );
	return 1;
}
