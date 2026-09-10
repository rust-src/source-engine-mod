//=============================================================================//
//
// Purpose: Lua "Localizations" library.  Ported from Experiment: Source
//          (src/game/shared/llocalization.cpp) with the binding framework from
//          the same project, so the file body matches upstream.
//
//          GMod exposes the same functionality as language.GetPhrase /
//          language.Add from lua/includes/modules/language.lua; this is the
//          engine side of it.
//
//=============================================================================//

#include "cbase.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "luabinding.h"
#include "llocalization.h"

#include "vgui/ILocalize.h"
#include "tier3/tier3.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LUA_REGISTRATION_INIT( Localizations );

LUA_BINDING_BEGIN( Localizations, Find, "library", "Finds a localized string." )
{
	const char *token = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "string" );

	if ( !g_pVGuiLocalize )
	{
		lua_pushnil( L );
		return 1;
	}

	wchar_t *pTranslation = g_pVGuiLocalize->Find( token );
	if ( !pTranslation )
	{
		lua_pushnil( L );
		return 1;
	}

	// GMod's language.GetPhrase returns a UTF-8 string.
	char utf8[4096];
	V_UnicodeToUTF8( pTranslation, utf8, sizeof( utf8 ) );
	lua_pushstring( L, utf8 );
	return 1;
}
LUA_BINDING_END( "string", "Returns the localized string." )

LUA_BINDING_BEGIN( Localizations, AddString, "library", "Adds a localized string." )
{
	const char *token = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "string" );
	const char *translation = LUA_BINDING_ARGUMENT( luaL_checkstring, 2, "string" );

	if ( !g_pVGuiLocalize )
		return 0;

	wchar_t translationBuffer[4096];
	translationBuffer[0] = 0;
	V_UTF8ToUnicode( translation, translationBuffer, sizeof( translationBuffer ) );

	g_pVGuiLocalize->AddString( token, translationBuffer, "" );
	return 0;
}
LUA_BINDING_END()

/*
** Open localization library
*/
LUALIB_API int luaopen_Localizations( lua_State *L )
{
	LUA_REGISTRATION_COMMIT_LIBRARY( Localizations );

	return 1;
}
