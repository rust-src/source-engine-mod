#include <cbase.h>

#include <vgui_int.h>
#include <luamanager.h>
#include "luasrclib.h"
#include <lColor.h>
#include <tier1/LKeyValues.h>
#include <vgui/LVGUI.h>

#include <scripted_controls/lTextEntry.h>
#include "scripted_controls/lPanel.h"

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>

using namespace vgui;

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
LTextEntry::LTextEntry( Panel *parent, const char *panelName, lua_State *L /* = nullptr */ )
    : TextEntry( parent, panelName )
{
    m_lua_State = L;
#if defined( LUA_SDK )
    // HL2SB: Experiment keeps the Lua instance in their pudata cache; HL2SB's
    // scripted controls carry it on the panel, which is what
    // LUA_CALL_PANEL_METHOD_BEGIN looks at.
    m_lua_State = L;
    m_nTableReference = LUA_NOREF;
#endif
}

LTextEntry::~LTextEntry()
{
#if defined( LUA_SDK )
    lua_unref( m_lua_State, m_nTableReference );
#endif
}


/*
** access functions (stack -> C)
*/

LUA_API lua_TextEntry *lua_totextentry( lua_State *L, int idx )
{
    PHandle *phPanel =
        dynamic_cast< PHandle * >( ( PHandle * )lua_touserdata( L, idx ) );
    if ( phPanel == NULL )
        return NULL;
    return dynamic_cast< lua_TextEntry * >( phPanel->Get() );
}

/*
** push functions (C -> stack)
*/
LUALIB_API lua_TextEntry *luaL_checktextentry( lua_State *L, int narg )
{
    lua_TextEntry *d = lua_totextentry( L, narg );
    if ( d == NULL ) /* avoid extra test when d is not 0 */
        luaL_argerror( L, narg, "TextEntry expected, got INVALID_PANEL" );
    return d;
}

/*
** HL2SB: Experiment has no lua_pushtextentry; their bindings reach the panel
** through LUA_OVERRIDE_SINGLE_LUA_INSTANCE_METATABLE and their pudata cache.
** HL2SB's scripted controls each expose a free lua_push<panel> function.
*/
LUA_API void lua_pushtextentry (lua_State *L, vgui::TextEntry *pTextEntry) {
  PHandle *phPanel = (PHandle *)lua_newuserdata(L, sizeof(PHandle));
  phPanel->Set(pTextEntry);
  luaL_getmetatable(L, "TextEntry");
  lua_setmetatable(L, -2);
}

LUA_REGISTRATION_INIT( TextEntry )

LUA_BINDING_BEGIN( TextEntry, SetText, "class", "Sets the text of the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    const char *text = LUA_BINDING_ARGUMENT( luaL_checkstring, 2, "text" );
    textEntry->SetText( text );
    return 0;
}
LUA_BINDING_END()

/*
** HL2SB: disabled -- vgui::TextEntry in this tree has no GetCursorPos()/SetCursorPos().
** Experiment's vgui_controls/TextEntry does; HL2SB's is the stock Valve control, which
** exposes neither.  Not needed by Derma's DTextEntry, which drives the control through
** SetText/GetText/SetFont/OnTextChanged/AllowInput.
*/
#if 0
LUA_BINDING_BEGIN( TextEntry, GetCursorPosition, "class", "Gets the cursor position in the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    int position = textEntry->GetCursorPos();
    lua_pushinteger( L, position );
    return 1;
}
LUA_BINDING_END( "integer", "The cursor position" )

LUA_BINDING_BEGIN( TextEntry, SetCursorPosition, "class", "Sets the cursor position in the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    int position = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "position" );
    textEntry->SetCursorPos( position );
    return 0;
}
LUA_BINDING_END()
#endif

LUA_BINDING_BEGIN( TextEntry, GetText, "class", "Gets the text from the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    char buf[1024];
    textEntry->GetText( buf, sizeof( buf ) );
    lua_pushstring( L, buf );
    return 1;
}
LUA_BINDING_END( "string", "The text from the text entry" )

LUA_BINDING_BEGIN( TextEntry, GetTextLength, "class", "Gets the length of the text in the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    int length = textEntry->GetTextLength();
    lua_pushinteger( L, length );
    return 1;
}
LUA_BINDING_END( "integer", "The length of the text" )

LUA_BINDING_BEGIN( TextEntry, GetValue, "class", "Gets the value from the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    char buf[8092];
    textEntry->GetText( buf, sizeof( buf ) );
    lua_pushstring( L, buf );
    return 1;
}
LUA_BINDING_END( "string", "The value from the text entry" )

LUA_BINDING_BEGIN( TextEntry, GetValueAsFloat, "class", "Gets the value from the text entry as a float" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    float value = textEntry->GetValueAsFloat();
    lua_pushnumber( L, value );
    return 1;
}
LUA_BINDING_END( "number", "The value as a float" )

LUA_BINDING_BEGIN( TextEntry, GetValueAsInteger, "class", "Gets the value from the text entry as an integer" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    int value = textEntry->GetValueAsInt();
    lua_pushinteger( L, value );
    return 1;
}
LUA_BINDING_END( "integer", "The value as an integer" )

LUA_BINDING_BEGIN( TextEntry, IsTextFullySelected, "class", "Checks if the text is fully selected" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    lua_pushboolean( L, textEntry->IsTextFullySelected() );
    return 1;
}
LUA_BINDING_END( "boolean", "Whether the text is fully selected" )

LUA_BINDING_BEGIN( TextEntry, GotoLeft, "class", "Moves the cursor left" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->GotoLeft();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, GotoRight, "class", "Moves the cursor right" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->GotoRight();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, GotoUp, "class", "Moves the cursor up" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->GotoUp();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, GotoDown, "class", "Moves the cursor down" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry`