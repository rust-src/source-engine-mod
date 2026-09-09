//========== HL2SB ===========//
//
// Purpose: Server-side undo stack implementation.  See hl2sb_undo.h.
//
//          Design: each player gets a stack of undoable "actions".  An action
//          is a list of entity handles.  The spawnmenu console commands
//          (ent_create / prop_physics_create) call HL2SB_UndoRecord() after a
//          successful spawn, which groups the entities that were created
//          between a Begin()/End() pair (or each Record* on its own) into one
//          action.  Undoing removes that action's entities and notifies the
//          player.
//
//          We use a transaction counter so that a single spawn command (which
//          may create one or several entities) lands as exactly one undoable
//          action regardless of how many HL2SB_UndoRecord calls happen.
//
//===========================================================================//

#include "cbase.h"
#include "hl2sb_undo.h"
#include "client.h"
#include "utlvector.h"
#include "utlmap.h"
#include "entitylist.h"
#include "player.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lbaseentity_shared.h"
#include "lbaseplayer_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// One undoable action: a list of entity handles + a display name.
// CUtlVector's copy constructor is protected, so copy elements explicitly.
//-----------------------------------------------------------------------------
struct CHL2SB_UndoEntry
{
	CHL2SB_UndoEntry() {}
	CHL2SB_UndoEntry( const CHL2SB_UndoEntry &other )
	{
		for ( int i = 0; i < other.m_Entities.Count(); i++ )
			m_Entities.AddToTail( other.m_Entities[ i ] );
		m_Name = other.m_Name;
	}
	CHL2SB_UndoEntry &operator=( const CHL2SB_UndoEntry &other )
	{
		if ( this != &other )
		{
			m_Entities.RemoveAll();
			for ( int i = 0; i < other.m_Entities.Count(); i++ )
				m_Entities.AddToTail( other.m_Entities[ i ] );
			m_Name = other.m_Name;
		}
		return *this;
	}

	CUtlVector< CHandle< CBaseEntity > > m_Entities;
	CUtlString m_Name;
};

//-----------------------------------------------------------------------------
// Per-player undo stack.
//-----------------------------------------------------------------------------
struct CHL2SB_UndoPlayer
{
	CHL2SB_UndoPlayer() {}
	CHL2SB_UndoPlayer( const CHL2SB_UndoPlayer &other )
	{
		for ( int i = 0; i < other.m_Undos.Count(); i++ )
			m_Undos.AddToTail( other.m_Undos[ i ] );
	}
	CHL2SB_UndoPlayer &operator=( const CHL2SB_UndoPlayer &other )
	{
		if ( this != &other )
		{
			m_Undos.RemoveAll();
			for ( int i = 0; i < other.m_Undos.Count(); i++ )
				m_Undos.AddToTail( other.m_Undos[ i ] );
		}
		return *this;
	}

	CUtlVector< CHL2SB_UndoEntry > m_Undos;
};

// Keyed by player entindex.  A single map of player -> undo stack.
static CUtlMap< int, CHL2SB_UndoPlayer > g_Undo( 0, 0, DefLessFunc( int ) );

// The player currently recording (only relevant for the dedicated spawn commands).
// Tracked separately so a single command can batch its entities into one action.
static CBasePlayer *g_pRecordingOwner = NULL;
static bool g_bInTransaction = false;

//-----------------------------------------------------------------------------
// Purpose: find (or create) the undo stack for a player.
//-----------------------------------------------------------------------------
static CHL2SB_UndoPlayer *GetPlayerStack( CBasePlayer *pPlayer )
{
	if ( !pPlayer )
		return NULL;

	int key = pPlayer->entindex();
	int idx = g_Undo.Find( key );
	if ( idx == g_Undo.InvalidIndex() )
	{
		CHL2SB_UndoPlayer fresh;
		g_Undo.Insert( key, fresh );
		idx = g_Undo.Find( key );
	}
	return &g_Undo[ idx ];
}

//-----------------------------------------------------------------------------
// Purpose: HL2SB_UndoRecord - register a freshly spawned entity.
//          All records for the same Begin()/End() transaction (or, without an
//          explicit transaction, all records in this single command) are
//          grouped as one action.
//-----------------------------------------------------------------------------
void HL2SB_UndoRecord( CBasePlayer *pOwner, CBaseEntity *pEnt )
{
	if ( !pOwner || !pEnt )
		return;

	// The spawn commands set the recording owner; if a command is running,
	// attribute to it.
	CBasePlayer *pPlayer = pOwner;
	if ( g_bInTransaction && g_pRecordingOwner )
		pPlayer = g_pRecordingOwner;

	CHL2SB_UndoPlayer *pStack = GetPlayerStack( pPlayer );
	if ( !pStack )
		return;

	if ( pStack->m_Undos.Count() == 0 || !g_bInTransaction )
	{
		// Start a new action.
		CHL2SB_UndoEntry entry;
		entry.m_Name = pEnt->GetClassname();
		pStack->m_Undos.AddToTail( entry );
	}

	CHL2SB_UndoEntry &entry = pStack->m_Undos[ pStack->m_Undos.Count() - 1 ];
	entry.m_Entities.AddToTail( CHandle< CBaseEntity >( pEnt ) );
}

//-----------------------------------------------------------------------------
// Purpose: HL2SB_UndoBegin / End - explicit transaction grouping (batch).
//-----------------------------------------------------------------------------
void HL2SB_UndoBegin( CBasePlayer *pOwner )
{
	g_pRecordingOwner = pOwner;
	g_bInTransaction = true;
}

void HL2SB_UndoEnd( CBasePlayer *pOwner )
{
	g_bInTransaction = false;
	g_pRecordingOwner = NULL;
}

//-----------------------------------------------------------------------------
// Purpose: HL2SB_UndoLast - undo the most recent action for a player.
//          Removes the entities in that action (if still valid).  Returns the
//          number of entities successfully removed.
//-----------------------------------------------------------------------------
int HL2SB_UndoLast( CBasePlayer *pOwner )
{
	if ( !pOwner )
		return 0;

	CHL2SB_UndoPlayer *pStack = GetPlayerStack( pOwner );
	if ( !pStack || pStack->m_Undos.Count() == 0 )
		return 0;

	CHL2SB_UndoEntry &entry = pStack->m_Undos[ pStack->m_Undos.Count() - 1 ];

	int removed = 0;
	for ( int i = 0; i < entry.m_Entities.Count(); i++ )
	{
		CBaseEntity *pEnt = entry.m_Entities[ i ].Get();
		if ( pEnt )
		{
			UTIL_Remove( pEnt );
			removed++;
		}
	}

	// Notify via a server-side Lua hook.  The Lua server script listens for
	// OnUndo and broadcasts a net message to the client, which the client Lua
	// HUD turns into a popup + sound (the GMod-style undo notification).
	const char *pszName = entry.m_Name.IsEmpty() ? "something" : entry.m_Name.Get();

#ifdef LUA_SDK
	{
		BEGIN_LUA_CALL_HOOK( "OnUndo" );
			lua_pushstring( L, pszName );
			lua_pushinteger( L, removed );
		END_LUA_CALL_HOOK( 2, 0 );
	}
#endif

	pStack->m_Undos.Remove( pStack->m_Undos.Count() - 1 );
	return removed;
}

//-----------------------------------------------------------------------------
// Purpose: HL2SB_UndoClear - clear a player's stack (does not remove ents).
//-----------------------------------------------------------------------------
void HL2SB_UndoClear( CBasePlayer *pOwner )
{
	if ( !pOwner )
		return;

	CHL2SB_UndoPlayer *pStack = GetPlayerStack( pOwner );
	if ( !pStack )
		return;

	pStack->m_Undos.RemoveAll();
	ClientPrint( pOwner, HUD_PRINTTALK, "[Undo] stack cleared\n" );
}

//-----------------------------------------------------------------------------
// Purpose: HL2SB_UndoCount - number of queued actions for a player.
//-----------------------------------------------------------------------------
int HL2SB_UndoCount( CBasePlayer *pOwner )
{
	if ( !pOwner )
		return 0;

	CHL2SB_UndoPlayer *pStack = GetPlayerStack( pOwner );
	if ( !pStack )
		return 0;

	return pStack->m_Undos.Count();
}

//-----------------------------------------------------------------------------
// Purpose: HL2SB_UndoDebugPrint - dump all stacks to the console.
//-----------------------------------------------------------------------------
void HL2SB_UndoDebugPrint()
{
	if ( g_Undo.Count() == 0 )
	{
		Msg( "[Undo] (empty)\n" );
		return;
	}

	for ( int i = 0; i < g_Undo.Count(); i++ )
	{
		int key = g_Undo.Key( i );
		CHL2SB_UndoPlayer &stack = g_Undo[ i ];
		Msg( "[Undo] player ent %d: %d action(s)\n", key, stack.m_Undos.Count() );
		for ( int j = 0; j < stack.m_Undos.Count(); j++ )
		{
			Msg( "[Undo]   #%d %s (%d ents)\n", j + 1, stack.m_Undos[ j ].m_Name.Get(), stack.m_Undos[ j ].m_Entities.Count() );
		}
	}
}

//=============================================================================
// Lua bindings: undo.*
//=============================================================================

// undo.Record( ply, ent ) - not intended for direct use; the spawn commands
// call HL2SB_UndoRecord directly.  Exposed for scripting convenience.
static int undo_Record( lua_State *L )
{
	CBasePlayer *pPlayer = luaL_checkplayer( L, 1 );
	CBaseEntity *pEnt = luaL_checkentity( L, 2 );
	HL2SB_UndoRecord( pPlayer, pEnt );
	return 0;
}

// undo.UndoLast( ply ) -> number removed
static int undo_UndoLast( lua_State *L )
{
	CBasePlayer *pPlayer = luaL_checkplayer( L, 1 );
	lua_pushinteger( L, HL2SB_UndoLast( pPlayer ) );
	return 1;
}

// undo.Clear( ply )
static int undo_Clear( lua_State *L )
{
	CBasePlayer *pPlayer = luaL_checkplayer( L, 1 );
	HL2SB_UndoClear( pPlayer );
	return 0;
}

// undo.Count( ply ) -> int
static int undo_Count( lua_State *L )
{
	CBasePlayer *pPlayer = luaL_checkplayer( L, 1 );
	lua_pushinteger( L, HL2SB_UndoCount( pPlayer ) );
	return 1;
}

// undo.Begin( ply ) / undo.End( ply )
static int undo_Begin( lua_State *L )
{
	CBasePlayer *pPlayer = luaL_checkplayer( L, 1 );
	HL2SB_UndoBegin( pPlayer );
	return 0;
}

static int undo_End( lua_State *L )
{
	CBasePlayer *pPlayer = luaL_checkplayer( L, 1 );
	HL2SB_UndoEnd( pPlayer );
	return 0;
}

static int undo_Debug( lua_State *L )
{
	HL2SB_UndoDebugPrint();
	return 0;
}

static const luaL_Reg undo_funcs[] = {
	{ "Record",   undo_Record },
	{ "UndoLast", undo_UndoLast },
	{ "Clear",    undo_Clear },
	{ "Count",    undo_Count },
	{ "Begin",    undo_Begin },
	{ "End",      undo_End },
	{ "Debug",    undo_Debug },
	{ NULL, NULL }
};

LUALIB_API int luaopen_undo( lua_State *L )
{
	// Registered as hl2sb_undo to avoid colliding with the Lua includes/modules/undo.lua
	// module (which also defines a global `undo` with GMod's Create/AddEntity/Finish API).
	luaL_register( L, "hl2sb_undo", undo_funcs );
	return 1;
}

//=============================================================================
// Console commands
//=============================================================================

// hl2sb_undo - undo the client's most recent action.
static void CC_HL2SB_Undo( const CCommand &args )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if ( !pPlayer )
		return;
	int n = HL2SB_UndoLast( pPlayer );
	Msg( "[Undo] player %s undo count=%d\n", pPlayer->GetPlayerName(), n );
}

// hl2sb_undoclear - clear the client's stack.
static void CC_HL2SB_UndoClear( const CCommand &args )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if ( !pPlayer )
		return;
	HL2SB_UndoClear( pPlayer );
}

// hl2sb_undodump - dump all stacks (debug).
static void CC_HL2SB_UndoDump( const CCommand &args )
{
	HL2SB_UndoDebugPrint();
}

static ConCommand hl2sb_undo( "hl2sb_undo", CC_HL2SB_Undo, "Undo this player's last spawn action", FCVAR_DONTRECORD );
static ConCommand hl2sb_undoclear( "hl2sb_undoclear", CC_HL2SB_UndoClear, "Clear this player's undo stack", FCVAR_DONTRECORD );
static ConCommand hl2sb_undodump( "hl2sb_undodump", CC_HL2SB_UndoDump, "Dump undo stacks", FCVAR_DONTRECORD );
