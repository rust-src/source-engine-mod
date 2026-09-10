#ifndef LTEXTENTRY_H
#define LTEXTENTRY_H

#ifdef _WIN32
#pragma once
#endif

#include <vgui_controls/TextEntry.h>
// vgui_controls/lPanel.h declares lua_pushpanel, which the
// LUA_CALL_PANEL_METHOD_BEGIN expansion below needs; scripted_controls/lPanel.h
// is HL2SB's own LPanel and does not.
#include <vgui_controls/lPanel.h>
#include "scripted_controls/lPanel.h"

// Declared before the class: LTextEntry::PushLuaInstanceSafe below forwards to it.
LUA_API void lua_pushtextentry ( lua_State *L, vgui::TextEntry *pTextEntry );

namespace vgui
{

class LTextEntry : public TextEntry
{
    DECLARE_CLASS_SIMPLE( LTextEntry, TextEntry );

    LUA_OVERRIDE_SINGLE_LUA_INSTANCE_METATABLE( LTextEntry, "TextEntry" );

    public:
    LTextEntry( Panel *parent, const char *panelName, lua_State *L = NULL );
    ~LTextEntry();

    // Experiment's bindings call Class::PushLuaInstanceSafe; HL2SB pushes the panel
    // through its own function, which also carries the Lua table reference.
    static void PushLuaInstanceSafe( lua_State *L, LTextEntry *pTextEntry )
    {
        lua_pushtextentry( L, pTextEntry );
    }

    // LUA_GET_REF_TABLE (luamanager.h) calls this when the panel has no Lua table
    // yet.  Leaves nothing on the stack: the macro calls lua_getref straight after.
    void SetupRefTable( lua_State *L )
    {
        lua_newtable( L );
        m_nTableReference = luaL_ref( L, LUA_REGISTRYINDEX );
    }

    public:
#if defined( LUA_SDK )
    lua_State          *m_lua_State;
    int                 m_nTableReference;
#endif

    protected:
    virtual void ApplySchemeSettings( vgui::IScheme *pScheme )
    {
        // Not sure why this is the order that causes text to be drawn in the Lua specified colour :/
        LUA_CALL_PANEL_METHOD_BEGIN( "ApplySchemeSettings" );
        LUA_CALL_PANEL_METHOD_END( 0, 0 );

        BaseClass::ApplySchemeSettings( pScheme );
    }
};

}  // namespace vgui

/* type for TextEntry functions */
typedef LTextEntry lua_TextEntry;

/*
** access functions (stack -> C)
*/

LUA_API lua_TextEntry *( lua_totextentry )( lua_State *L, int idx );

/*
** push functions (C -> stack)
*/
LUALIB_API lua_TextEntry *( luaL_checktextentry )( lua_State *L, int narg );

#endif  // LTEXTENTRY_H
