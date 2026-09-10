//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#ifndef LPANEL_H
#define LPANEL_H

#ifdef _WIN32
#pragma once
#endif

#include <vgui_controls/Panel.h>

using namespace vgui;

/* type for Panel functions */
typedef Panel lua_Panel;



/*
** access functions (stack -> C)
*/

LUA_API lua_Panel     *(lua_topanel) (lua_State *L, int idx);


/*
** push functions (C -> stack)
*/
LUA_API void  (lua_pushpanel) (lua_State *L, lua_Panel *pPanel);
LUA_API void  (lua_pushpanel) (lua_State *L, VPANEL panel);



LUALIB_API lua_Panel *(luaL_checkpanel) (lua_State *L, int narg);
LUALIB_API VPANEL     (luaL_checkvpanel) (lua_State *L, int narg);
LUALIB_API lua_Panel *(luaL_optpanel) (lua_State *L, int narg,
                                                     lua_Panel *def);

/*
** HL2SB: ported from Experiment: Source (game/client/scripted_controls/lPanel.cpp).
**
** Their panel metatable helpers reference these two -- LUA_METATABLE_INDEX_CHECK_VALID
** installs the first as the panel's IsValid, and every panel's __gc binding returns
** the second.  Both are one-liners, which is why the ported scripted controls need
** only these rather than Experiment's whole panel base.
**
** PanelCollectGarbage is a no-op in Experiment as well: they disabled the
** refcount-and-delete because Lua would then have to hold a reference to every panel.
** HL2SB's scripted controls release their own Lua table reference in their destructor.
*/
inline int PanelIsValid( lua_State *L )
{
	lua_Panel *d = lua_topanel( L, 1 );
	lua_pushboolean( L, d != NULL );
	return 1;
}

inline int PanelCollectGarbage( lua_State *L )
{
	return 0;
}


#endif // LPANEL_H
