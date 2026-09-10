//========== HL2SB ===========//
//
// Purpose: Bridge between the engine's spawn console commands and Garry's
//          Mod's Lua undo module.
//
//          The undo stack is NOT implemented here any more.  It is GMod's
//          lua/includes/modules/undo.lua, loaded from lua/includes/modules/ on
//          both realms: it owns the per-player stack, the CanCreateUndo /
//          PreUndo / PostUndo / CanUndo force conditions, the OnUndo client
//          notification and the `undo` / `gmod_undo` / `gmod_undonum` console
//          commands.
//
//          The previous C++ stack kept a second, private copy of all of that
//          and registered the same `hl2sb_undo` / `hl2sb_undoclear` command
//          names as the Lua module, so one silently shadowed the other and the
//          command could drain an empty stack.
//
//          This file now only lets `ent_create` / `prop_physics_create`
//          (engine console commands, not Lua) register what they spawned:
//
//              undo.Create( name )
//              undo.AddEntity( ent )
//              undo.SetPlayer( ply )
//              undo.Finish( name )
//
//          which is exactly what a GMod spawnmenu does from Lua.
//
//===========================================================================//

#include "cbase.h"
#include "hl2sb_undo.h"
#include "player.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lbaseentity_shared.h"
#include "lbaseplayer_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The shared Lua state (same one lnet.cpp / the HUD hooks use).
extern lua_State *L;

//-----------------------------------------------------------------------------
// Purpose: Run one undo.<pszFunc>() with the `undo` table at the top of the
//          stack and nArgs values pushed above it.  Leaves exactly the `undo`
//          table behind on every path -- a missed pop here would corrupt the
//          Lua state for the rest of the level.
//-----------------------------------------------------------------------------
static void HL2SB_CallUndoFunc( lua_State *pL, const char *pszFunc, int nArgs )
{
	// [ undo, args... ]
	lua_getfield( pL, -( nArgs + 1 ), pszFunc );	// [ undo, args..., func ]

	if ( !lua_isfunction( pL, -1 ) )
	{
		lua_pop( pL, nArgs + 1 );					// [ undo ]
		return;
	}

	// [ undo, args..., func ] -> [ undo, func, args... ] so pcall sees
	// func followed by its arguments.
	lua_insert( pL, -( nArgs + 1 ) );

	luasrc_pcall( pL, nArgs, 0, 0 );				// [ undo ]
}

//-----------------------------------------------------------------------------
// Purpose: HL2SB_UndoRecord - register a freshly spawned entity with GMod's
//          Lua undo module as one undoable action.
//-----------------------------------------------------------------------------
void HL2SB_UndoRecord( CBasePlayer *pOwner, CBaseEntity *pEnt )
{
	if ( !pOwner || !pEnt )
		return;

	lua_State *pL = L;
	if ( !pL )
		return;

	const char *pszName = pEnt->GetClassname();
	if ( !pszName || !pszName[0] )
		pszName = "entity";

	const int nBase = lua_gettop( pL );

	lua_getglobal( pL, "undo" );					// [ undo ]
	if ( !lua_istable( pL, -1 ) )
	{
		lua_settop( pL, nBase );
		return;
	}

	// undo.Create( name )
	lua_pushstring( pL, pszName );
	HL2SB_CallUndoFunc( pL, "Create", 1 );

	// undo.AddEntity( ent )
	lua_pushentity( pL, pEnt );
	HL2SB_CallUndoFunc( pL, "AddEntity", 1 );

	// undo.SetPlayer( ply )
	lua_pushplayer( pL, pOwner );
	HL2SB_CallUndoFunc( pL, "SetPlayer", 1 );

	// undo.Finish( name ) -- NiceText shown by the client notification
	lua_pushstring( pL, pszName );
	HL2SB_CallUndoFunc( pL, "Finish", 1 );

	lua_settop( pL, nBase );
}
