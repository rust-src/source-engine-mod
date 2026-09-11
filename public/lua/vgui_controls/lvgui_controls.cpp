//====== Copyright © 1996-2005, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================
#define lvgui_controls_cpp

#include "cbase.h"
#include "lua.hpp"
#include "lControls.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


/*
** ===========================================================================
** HL2SB: vgui.Create( className, parent, name )
**
** Garry's Mod implements this in C++ and its own
** lua/includes/extensions/client/panel/scriptedpanels.lua wraps it:
**
**     vgui.CreateX = vgui.Create                     -- captures the C++ one
**     function vgui.Create( classname, parent, name ) -- the class registry
**       ... return vgui.CreateX( classname, parent, name or classname )
**     end
**
** HL2SB's vgui table only ever had per-class factories (vgui.Panel,
** vgui.Label, vgui.Frame, ...), so `vgui.CreateX = vgui.Create` captured nil
** and GMod's scriptedpanels.lua could not run.  This is the dispatch it needs.
**
** The trailing argument differs per factory, so it is spelled out:
**
**     Panel (parent, name)                 ModelPanel      (parent, name)
**     TextEntry (parent, name)             Label           (parent, name, text)
**     Frame (parent, name, bShowClose)     CheckButton     (parent, name, text)
**     Button (parent, name, text, ...)     EditablePanel   (parent, name)
**
** Only Button hard-requires argument 3 (luaL_checkstring(L, 3) in
** luasrc_Button); the others take it optionally or ignore it, so passing "" or
** true is safe for every one of them.
** ===========================================================================
*/
static int lua_vgui_Create (lua_State *L) {
  const char *pszClass = luaL_checkstring(L, 1);
  const int nTop = lua_gettop(L);

  lua_getglobal(L, "vgui");
  if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_pushnil(L); return 1; }

  lua_getfield(L, -1, pszClass);
  lua_remove(L, -2);
  if (!lua_isfunction(L, -1)) { lua_pop(L, 1); lua_pushnil(L); return 1; }

  const int nBase = lua_gettop(L);                       // [factory]
  for (int i = 2; i <= nTop && i <= 3; ++i)
    lua_pushvalue(L, i);                                 // parent, name
  while (lua_gettop(L) < nBase + 2)
    lua_pushnil(L);                                      // pad a missing parent/name

  if (!Q_stricmp(pszClass, "Frame"))                        lua_pushboolean(L, 1);
  else if (!Q_stricmp(pszClass, "Button"))                  lua_pushstring(L, "");
  else if (!Q_stricmp(pszClass, "Label"))                   lua_pushstring(L, "");
  else if (!Q_stricmp(pszClass, "CheckButton"))             lua_pushstring(L, "");

  lua_call(L, lua_gettop(L) - nBase, 1);
  return 1;
}

static const luaL_Reg vgui_funcs[] = {
  {"Create", lua_vgui_Create},
  {NULL, NULL}
};

/*
** Open vgui library
*/
LUALIB_API int luaopen_vgui (lua_State *L) {
  luaopen_vgui_Button(L);
  luaopen_vgui_EditablePanel(L);
  luaopen_vgui_Panel(L);
  luaopen_vgui_CheckButton(L);
  luaopen_vgui_Frame(L);
  luaopen_vgui_PropertyDialog(L);
  luaopen_vgui_PropertyPage(L);
  luaopen_vgui_ModelPanel(L);

  luaL_register(L, "vgui", vgui_funcs);
  lua_pop(L, 1);

  /*
  ** HL2SB: vgui.EditablePanel -- the constructor, not just the metatable.
  **
  ** luaopen_vgui_EditablePanel only builds the "EditablePanel" metatable; it
  ** never registers a factory, so vgui.EditablePanel did not exist.  That is
  ** exactly the class Garry's Mod bases DFrame on
  ** (derma.DefineControl( "DFrame", "A simple window", PANEL, "EditablePanel" )),
  ** and scriptedpanels.lua resolves it through vgui.CreateX -- so without this
  ** Derma cannot build a single frame.
  **
  ** There is no scripted LEditablePanel in this tree to instantiate (the C++
  ** vgui::EditablePanel has no Lua-table plumbing).  GMod's DFrame implements
  ** all of its own chrome -- title bar, close button, dragging, Paint -- in Lua
  ** and only needs a scripted panel underneath, which is precisely what the
  ** scripted "Panel" factory provides.  So that constructor is published under
  ** this name too.  Same reasoning the player-model menu used when it based its
  ** own controls on "Panel".
  */
  lua_getglobal(L, "vgui");
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "Panel");
    if (lua_isfunction(L, -1))
      lua_setfield(L, -2, "EditablePanel");
    else
      lua_pop(L, 1);
  }
  lua_pop(L, 1);

  return 0;
}

