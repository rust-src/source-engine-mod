//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef VGUI_LFRAME_H
#define VGUI_LFRAME_H

#ifdef _WIN32
#pragma once
#endif

#include <vgui_controls/Frame.h>

namespace vgui
{

//-----------------------------------------------------------------------------
// Purpose: Lua wrapper for a windowed frame
//-----------------------------------------------------------------------------
class LFrame : public Frame
{
	DECLARE_CLASS_SIMPLE( LFrame, Frame );

public:
	LFrame(Panel *parent, const char *panelName, bool showTaskbarIcon = true, lua_State *L = NULL);
	virtual ~LFrame();

public:
	// Lua method forwarding (same set as LPanel / LModelPanel), so a vgui.register(
	// ..., "Frame") subclass can cleanly override Paint/PerformLayout/mouse/key/
	// command from Lua.  Without these the vgui loop calls Frame::Paint() etc. and
	// never reaches the Lua script table, which is why the old Lua menu had to
	// re-apply its layout every frame from a HudViewportPaint hook.
	virtual void Paint();
	virtual void PerformLayout();
	virtual void OnMousePressed( MouseCode code );
	virtual void OnMouseReleased( MouseCode code );
	virtual void OnCursorMoved( int x, int y );
	virtual void OnCursorEntered();
	virtual void OnCursorExited();
	virtual void OnMouseWheeled( int delta );
	virtual void OnKeyCodePressed( KeyCode code );
	virtual void OnKeyCodeTyped( KeyCode code );
	virtual void OnKeyCodeReleased( KeyCode code );
	virtual void OnThink();
	virtual void OnCommand( const char *command );

public:
#if defined( LUA_SDK )
	lua_State *m_lua_State;
	int m_nTableReference;
	int m_nRefCount;
#endif
};

} // namespace vgui

/* type for Frame functions */
typedef vgui::Frame lua_Frame;



/*
** access functions (stack -> C)
*/

LUA_API lua_Frame     *(lua_toframe) (lua_State *L, int idx);


/*
** push functions (C -> stack)
*/
LUA_API void  (lua_pushframe) (lua_State *L, lua_Frame *pFrame);



LUALIB_API lua_Frame *(luaL_checkframe) (lua_State *L, int narg);

#endif // VGUI_LFRAME_H
