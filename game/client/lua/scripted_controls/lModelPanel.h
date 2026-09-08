//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Lua-bindable 3D model preview panel (HL2SB).
//
//          CModelPanel is written for the main menu and has three properties
//          that make it unusable as-is from Lua:
//
//            * SwapModel() returns immediately while m_pModelInfo is NULL, and
//              m_pModelInfo only ever comes from ParseModelInfo(), i.e. from
//              ApplySettings() seeing a "model" sub-key.
//            * CalculateFrameDistance() frames the camera from the panel size
//              at SetupModel() time, which is wrong for arbitrary models and
//              outright wrong for ported/repacked ones.
//            * Paint() leaks render state into the world (see the comment on
//              LModelPanel::Paint()).
//
//          This subclass fixes all three and exposes a small API that mirrors
//          GMod's DModelPanel, so the player model menu can be written in Lua.
//
//=============================================================================//

#ifndef LMODELPANEL_H
#define LMODELPANEL_H
#ifdef _WIN32
#pragma once
#endif

#include "basemodelpanel.h"

class LModelPanel : public CModelPanel
{
	DECLARE_CLASS_SIMPLE( LModelPanel, CModelPanel );

public:
	LModelPanel( vgui::Panel *parent, const char *panelName, lua_State *L );
	virtual ~LModelPanel();

	// Lua method forwarding (same pattern as LPanel / LFrame).
	virtual void Paint();
	virtual void PerformLayout();
	virtual void ApplySchemeSettings( vgui::IScheme *pScheme );
	virtual void OnMousePressed( vgui::MouseCode code );
	virtual void OnMouseReleased( vgui::MouseCode code );
	virtual void OnCursorMoved( int x, int y );
	virtual void OnMouseWheeled( int delta );
	virtual void OnThink();

	//-----------------------------------------------------------------------
	// Lua-facing API.  Lua owns the interaction (drag to rotate, wheel to
	// zoom); these simply apply the resulting state to the preview.
	//-----------------------------------------------------------------------

	// Returns false when the model is not precached on the client; the caller
	// is expected to tell the user rather than showing a broken preview.
	bool		LoadModel( const char *pszModel );
	const char *GetModelPath() const;

	void		SetYaw( float flYaw );
	float		GetYaw() const			{ return m_flYaw; }

	void		SetZoom( float flZoom );
	float		GetZoom() const			{ return m_flZoom; }
	void		SetZoomLimits( float flMin, float flMax );

	virtual void SetFOV( int nFOV ) OVERRIDE;		// refits the camera

	bool		PlaySequence( const char *pszSequence );
	int			GetSequenceCount();
	const char *GetSequenceName( int nIndex );

	void		RefitCamera();

public:
	// Public so the Lua bindings can manage the reference count / script table,
	// exactly like LPanel and LFrame.
#if defined( LUA_SDK )
	lua_State	*m_lua_State;
	int			m_nTableReference;
	int			m_nRefCount;
#endif

private:
	void		EnsureModelInfo();
	void		FitCameraToModel();
	void		ApplyCamera();

	float		m_flYaw;
	float		m_flZoom;
	float		m_flZoomMin;
	float		m_flZoomMax;
	float		m_flBaseDist;
	Vector		m_vecBaseOffset;
};

/* type for ModelPanel functions */
typedef LModelPanel lua_ModelPanel;

/*
** access functions (stack -> C)
*/
LUA_API lua_ModelPanel	*(lua_tomodelpanel) (lua_State *L, int idx);

/*
** push functions (C -> stack)
*/
LUA_API void			(lua_pushmodelpanel) (lua_State *L, lua_ModelPanel *pPanel);

LUALIB_API lua_ModelPanel *(luaL_checkmodelpanel) (lua_State *L, int narg);

#endif // LMODELPANEL_H
