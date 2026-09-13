//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef ACTIVITYLIST_H
#define ACTIVITYLIST_H
#ifdef _WIN32
#pragma once
#endif

#include <KeyValues.h>

typedef struct activityentry_s activityentry_t;

class CActivityRemap
{
public:

	CActivityRemap()
	{
		pExtraBlock = NULL;
	}

	void SetExtraKeyValueBlock ( KeyValues *pKVBlock )
	{
		pExtraBlock = pKVBlock;
	}

	KeyValues *GetExtraKeyValueBlock ( void ) { return pExtraBlock; }

	Activity 		activity;
	Activity		mappedActivity;

private:

	KeyValues		*pExtraBlock;
};


class CActivityRemapCache
{
public:

	CActivityRemapCache() = default;

	CActivityRemapCache( const CActivityRemapCache& src )
	{
		int c = src.m_cachedActivityRemaps.Count();
		for ( int i = 0; i < c; i++ )
		{
			m_cachedActivityRemaps.AddToTail( src.m_cachedActivityRemaps[ i ] );
		}
	}

	CActivityRemapCache& operator = ( const CActivityRemapCache& src )
	{
		if ( this == &src )
			return *this;

		int c = src.m_cachedActivityRemaps.Count();
		for ( int i = 0; i < c; i++ )
		{
			m_cachedActivityRemaps.AddToTail( src.m_cachedActivityRemaps[ i ] );
		}

		return *this;
	}

	CUtlVector< CActivityRemap > m_cachedActivityRemaps;
};

void UTIL_LoadActivityRemapFile( const char *filename, const char *section, CUtlVector <CActivityRemap> &entries );

//=========================================================
//=========================================================
extern void ActivityList_Init( void );
extern void ActivityList_Free( void );
extern bool ActivityList_RegisterSharedActivity( const char *pszActivityName, int iActivityIndex );
extern Activity ActivityList_RegisterPrivateActivity( const char *pszActivityName );
extern int ActivityList_IndexForName( const char *pszActivityName );
extern const char *ActivityList_NameForIndex( int iActivityIndex );
extern int ActivityList_HighestIndex();

// This macro guarantees that the names of each activity and the constant used to
// reference it in the code are identical.
#ifndef LUA_SDK
#define REGISTER_SHARED_ACTIVITY( _n ) ActivityList_RegisterSharedActivity(#_n, _n);
#else
// HL2SB: ported from Experiment: Source.  Under the Lua SDK the registration is
// owned by luaopen_ACTIVITY (public/lenumerations_shared.cpp, registered in
// game/shared/lua/lsrcinit.cpp) instead of by the world entity, so that _E.ACTIVITY
// is complete before any script runs.  Each entry is mirrored into the _E.ACTIVITY
// table that LUA_SET_ENUM_LIB_BEGIN left on top of the stack, which is why the
// global `L` (luamanager.h) has to be that same state and stack.
// Only activitylist.cpp ever expands this macro, and it includes luamanager.h
// under LUA_SDK before the expansion site.
// HL2SB: the table field is joined by a FLAT GLOBAL of the same name.  GMod
// publishes every activity as a global (that is what its own
// gamemodes/base/entities/weapons/weapon_base/sh_anim.lua reads: a bare
// ACT_HL2MP_IDLE_PISTOL, not _E.ACTIVITY[...]), and the mod side used to make
// up for it with a hard-coded 27-entry list in
// lua/includes/extensions/gmod_globals.lua -- which cannot cover the ~200 GMod
// activities declared at the end of ai_activity.h, and silently goes stale every
// time the enum grows.  An existing global is left alone (same rule as
// lua_pushenum in luamanager.h: never overwrite a name the scripts own).
#define REGISTER_SHARED_ACTIVITY( _n )              \
    ActivityList_RegisterSharedActivity( #_n, _n ); \
    lua_pushstring( L, #_n );                       \
    lua_pushinteger( L, _n );                       \
    lua_settable( L, -3 );                          \
    lua_getglobal( L, #_n );                        \
    if ( lua_isnil( L, -1 ) )                       \
    {                                               \
        lua_pushinteger( L, _n );                   \
        lua_setglobal( L, #_n );                    \
    }                                               \
    lua_pop( L, 1 );
#endif
#define REGISTER_PRIVATE_ACTIVITY( _n ) _n = ActivityList_RegisterPrivateActivity( #_n );

// Implemented in shared code
extern void ActivityList_RegisterSharedActivities( void );

class ISaveRestoreOps;
extern ISaveRestoreOps* ActivityDataOps();

#endif // ACTIVITYLIST_H
