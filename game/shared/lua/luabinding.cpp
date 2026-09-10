//=============================================================================//
//
// Purpose: Registrar for the Experiment-style Lua binding framework.
//          Ported from Experiment: Source (src/game/shared/lsrcinit.cpp,
//          luaL_register overload).
//
//          Upstream uses luaL_getsubtable() + LUA_LOADED_TABLE, which are Lua
//          5.2+ APIs.  HL2SB runs Lua 5.1, so the same behaviour is written out
//          with 5.1 primitives; it compiles and behaves identically on 5.4.
//
//=============================================================================//

#include "cbase.h"

#include "luabinding.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Builds a Lua table (or writes into the table on top of the stack when libname
// is NULL) from a CUtlVector registry.  Mirrors Lua 5.1's luaL_register:
//   * package.loaded[libname] and _G[libname] both end up pointing at the table,
//   * an existing table is reused rather than replaced,
//   * the library table is left on the stack.
void luaL_register( lua_State *L, const char *libname, CUtlVector< LuaRegEntry > &luaRegistry )
{
	if ( libname )
	{
		lua_getglobal( L, "package" );
		if ( lua_istable( L, -1 ) )
		{
			lua_getfield( L, -1, "loaded" );
			if ( lua_istable( L, -1 ) )
			{
				lua_getfield( L, -1, libname );
				if ( !lua_istable( L, -1 ) )
				{
					lua_pop( L, 1 );
					lua_newtable( L );
					lua_pushvalue( L, -1 );
					lua_setfield( L, -3, libname );	// package.loaded[libname] = new table
				}
				lua_remove( L, -2 );				// remove package.loaded
			}
			else
			{
				lua_pop( L, 2 );					// no package.loaded: leave package
				lua_newtable( L );
			}
			lua_remove( L, -2 );					// remove package
		}
		else
		{
			lua_pop( L, 1 );						// no package table at all
			lua_newtable( L );
		}

		lua_pushvalue( L, -1 );
		lua_setglobal( L, libname );			// _G[libname] = table
	}

	for ( int i = 0; i < luaRegistry.Count(); i++ )
	{
		lua_pushcfunction( L, luaRegistry[i].function );
		lua_setfield( L, -2, luaRegistry[i].name );
	}
}
