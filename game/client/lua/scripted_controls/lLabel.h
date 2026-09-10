#ifndef LLABEL_H
#define LLABEL_H

#ifdef _WIN32
#pragma once
#endif

#include <vgui_controls/Label.h>
// vgui_controls/lPanel.h declares lua_pushpanel, which the
// LUA_CALL_PANEL_METHOD_BEGIN expansion below needs; scripted_controls/lPanel.h
// is HL2SB's own LPanel and does not.
#include <vgui_controls/lPanel.h>
#include "scripted_controls/lPanel.h"

// Declared before the class: LLabel::PushLuaInstanceSafe below forwards to it.
LUA_API void lua_pushlabel ( lua_State *L, vgui::Label *pLabel );

namespace vgui
{

class LLabel : public Label
{
    DECLARE_CLASS_SIMPLE( LLabel, Label );

    LUA_OVERRIDE_SINGLE_LUA_INSTANCE_METATABLE( LLabel, "Label" );

    public:
    // Experiment's bindings call Class::PushLuaInstanceSafe; HL2SB pushes the
    // panel through its own function, which also carries the table reference.
    static void PushLuaInstanceSafe( lua_State *L, LLabel *pLabel )
    {
        lua_pushlabel( L, pLabel );
    }

    public:
    LLabel( Panel *parent, const char *panelName, const char *text, lua_State *L = nullptr );
    ~LLabel();

    public:
#if defined( LUA_SDK )
    lua_State          *m_lua_State;
    int                 m_nTableReference;
#endif

    protected:
    virtual void ApplySchemeSettings( vgui::IScheme *pScheme )
    {
        LUA_CALL_PANEL_METHOD_BEGIN( "ApplySchemeSettings" );
        LUA_CALL_PANEL_METHOD_END( 0, 0 );

        BaseClass::ApplySchemeSettings( pScheme );
    }
};

}  // namespace vgui

/* type for Label functions */
typedef LLabel lua_Label;

/*
** access functions (stack -> C)
*/

LUA_API lua_Label *( lua_tolabel )( lua_State *L, int idx );

/*
** push functions (C -> stack)
*/
LUALIB_API lua_Label *( luaL_checklabel )( lua_State *L, int narg );

#endif  // LLABEL_H
