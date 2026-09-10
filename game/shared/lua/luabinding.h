//=============================================================================//
//
// Purpose: HL2SB Lua binding framework, ported from Experiment: Source
//          (src/game/shared/luasrclib.h).  Kept intentionally identical so
//          binding files from that project can be dropped in unchanged.
//
//          Usage:
//              LUA_REGISTRATION_INIT( MyLib )        // once, file scope
//
//              LUA_BINDING_BEGIN( MyLib, DoThing, "library", "Does a thing." )
//              {
//                  int n = LUA_BINDING_ARGUMENT( luaL_checkint, 1, "number" );
//                  lua_pushinteger( L, n * 2 );
//                  return 1;
//              }
//              LUA_BINDING_END( "integer", "Twice the input." )
//
//              LUALIB_API int luaopen_MyLib( lua_State *L )
//              {
//                  LUA_REGISTRATION_COMMIT_LIBRARY( MyLib );
//                  return 1;
//              }
//
//          The extra macro arguments (concept / description / argument names)
//          are documentation metadata; the code-generation tooling that turns
//          them into a reference site is not ported (see docs/AGENTS notes).
//
//=============================================================================//

#ifndef LUABINDING_H
#define LUABINDING_H
#ifdef _WIN32
#pragma once
#endif

// Included from luasrclib.h, which every Lua binding file pulls in; that header
// is always included after cbase.h, so CUtlVector and lua_State are available.
// (Deliberately no #include "cbase.h" here: luasrclib.h is itself included from
// places that would then recurse.)
#include "lua.hpp"

#include "lua.hpp"

// One entry in a library's function table.  The standard luaL_Reg cannot be
// used because the tables are built at static-init time (see LUA_REGISTER_METHOD).
struct LuaRegEntry
{
	const char *name;
	lua_CFunction function;
};

// Starts a library's registry.  Must appear at file scope, before the
// LUA_BINDING_BEGIN blocks that fill it.
#define LUA_REGISTRATION_INIT( ClassName ) \
	static CUtlVector< LuaRegEntry > ClassName##_luaRegistry;

// Publishes the registry as the metatable/table on top of the stack.
#define LUA_REGISTRATION_COMMIT( ClassName ) \
	luaL_register( L, NULL, ClassName##_luaRegistry );

// Publishes the registry as a global library table (libname == "ClassName").
#define LUA_REGISTRATION_COMMIT_LIBRARY( ClassName ) \
	luaL_register( L, #ClassName, ClassName##_luaRegistry );

// Self-registering helper: the static instance adds {name, func} to the registry
// before main() runs.
#define LUA_REGISTER_METHOD( Registry, name, func ) \
	static struct RegHelper_##func                  \
	{                                               \
		RegHelper_##func()                          \
		{                                           \
			Registry.AddToTail( LuaRegEntry{ name, func } );   \
		}                                           \
	} regHelper_##func;

// Defines one bound function: forward declaration, registration, then the body.
#define LUA_BINDING_BEGIN( ClassName, FunctionName, Concept, DocumentationDescription, ... ) \
	static int ClassName##_##FunctionName( lua_State *L );                                   \
	LUA_REGISTER_METHOD( ClassName##_luaRegistry, #FunctionName, ClassName##_##FunctionName ) \
	static int ClassName##_##FunctionName( lua_State *L )                                    \
	{

#define LUA_BINDING_ARGUMENT( CheckFunction, ArgIndex, DocumentationName ) \
	CheckFunction( L, ArgIndex )

#define LUA_BINDING_ARGUMENT_NILLABLE( CheckFunction, ArgIndex, DocumentationName ) \
	CheckFunction( L, ArgIndex )

#define LUA_BINDING_ARGUMENT_WITH_EXTRA( CheckFunction, ArgIndex, Extra, DocumentationName ) \
	CheckFunction( L, ArgIndex, Extra )

#define LUA_BINDING_ARGUMENT_WITH_DEFAULT( OptCheckFunction, ArgIndex, OptValue, DocumentationName ) \
	OptCheckFunction( L, ArgIndex, OptValue )

#define LUA_BINDING_END( ... ) \
	}

// Enumerations are passed as numbers so scripts can do arithmetic on them and
// the value is then cut down to an integer.
#define LUA_BINDING_ARGUMENT_ENUM( EnumType, ArgIndex, DocumentationName ) \
	( EnumType )( int )luaL_checknumber( L, ArgIndex )

#define LUA_BINDING_ARGUMENT_ENUM_WITH_DEFAULT( EnumType, ArgIndex, Default, DocumentationName ) \
	( EnumType )( int )luaL_optnumber( L, ArgIndex, Default )

// For enumerations that are really just #defines.
#define LUA_BINDING_ARGUMENT_ENUM_DEFINE( EnumType, ArgIndex, DocumentationName ) \
	( int )luaL_checknumber( L, ArgIndex )

// Builds a table (or the global table when libname is NULL) from a registry.
// Overloads the Lua 5.1 luaL_register(L, libname, const luaL_Reg *).
void luaL_register( lua_State *L, const char *libname, CUtlVector< LuaRegEntry > &luaRegistry );

// luaL_checkboolean / luaL_optboolean already exist in luamanager.h (Team Sandbox
// era); the ported bindings use those.

#endif  // LUABINDING_H
