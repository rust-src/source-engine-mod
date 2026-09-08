// hl2sb_contextmenu.h
#ifndef HL2SB_CONTEXTMENU_H
#define HL2SB_CONTEXTMENU_H

#ifdef _WIN32
#pragma once
#endif

#include <vgui_controls/Frame.h>
#include <vgui_controls/SectionedListPanel.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/Button.h>
#include <game/client/iviewport.h>
#include "basemodelpanel.h"

#define PANEL_CONTEXT_MENU "hl2sb_context_menu"

//-----------------------------------------------------------------------------
// 3D preview: drag to rotate, click to cycle animation
//-----------------------------------------------------------------------------
class CHL2SBModelPreview : public CModelPanel
{
	DECLARE_CLASS_SIMPLE( CHL2SBModelPreview, CModelPanel );

public:
	CHL2SBModelPreview( vgui::Panel *pParent, const char *pName );

	virtual void OnMousePressed( vgui::MouseCode code );
	virtual void OnMouseReleased( vgui::MouseCode code );
	virtual void OnCursorMoved( int x, int y );
	virtual void OnMouseWheeled( int delta );

	// Fit the whole model into the viewport using its render bounds.
	// Preserves the user's zoom multiplier.
	void FitCameraToModel();
	// Apply the current zoom multiplier to the fitted distance.
	void ApplyZoom();
	// Prefer a locomotion sequence (walk), then idle, then sequence 0.
	bool ApplyDefaultAnim();
	// Advance to the next sequence in the MDL and loop it.
	void CycleAnimation();
	// Play one specific sequence by index.
	bool PlaySequence( int nSeq );
	void ResetState();

private:
	bool	m_bDragging;
	bool	m_bMoved;
	int		m_nDragStartX;
	int		m_nLastX;
	int		m_nSeqIndex;
	float	m_flBaseDist;	// distance that fits the model, before zoom
	float	m_flZoomMul;	// user zoom multiplier (wheel)
	Vector	m_vecBaseOffset; // fitted y/z centring
};

//-----------------------------------------------------------------------------
// Main context menu
//-----------------------------------------------------------------------------
class CHL2SBContextMenu : public vgui::Frame, public IViewPortPanel
{
	DECLARE_CLASS_SIMPLE( CHL2SBContextMenu, vgui::Frame );

public:
	CHL2SBContextMenu( IViewPort *pViewPort );
	virtual ~CHL2SBContextMenu();

	virtual const char *GetName( void ) { return PANEL_CONTEXT_MENU; }
	virtual void SetData( KeyValues *data ) {}
	virtual void Reset( void ) {}
	virtual void Update( void );
	virtual bool NeedsUpdate( void ) { return false; }
	virtual bool HasInputElements( void ) { return true; }
	virtual void ShowPanel( bool bShow );
	virtual GameActionSet_t GetPreferredActionSet() { return GAME_ACTION_SET_MENUCONTROLS; }
	vgui::VPANEL GetVPanel( void ) { return BaseClass::GetVPanel(); }
	virtual bool IsVisible() { return BaseClass::IsVisible(); }
	virtual void SetParent( vgui::VPANEL parent ) { BaseClass::SetParent( parent ); }

	virtual void ApplySchemeSettings( vgui::IScheme *pScheme );
	virtual void PerformLayout();
	virtual void OnThink();
	virtual void OnKeyCodePressed( vgui::KeyCode code );
	virtual void OnCommand( const char *command );

	void OnOpen();
	void OnClose();

private:
	void BuildModelList();
	void BuildAnimList();
	void LoadPreviewModel( const char *pszPath );
	void UpdateCurrentInfo();
	void SelectModel( int nIndex );
	void HandleModelListClick();
	void HandleAnimListClick();
	void RefreshPreviewIfNeeded();

	IViewPort					*m_pViewPort;
	vgui::SectionedListPanel	*m_pModelList;
	CHL2SBModelPreview			*m_pModelPreview;
	vgui::Label					*m_pPreviewTitle;
	vgui::Label					*m_pAnimTitle;
	vgui::SectionedListPanel	*m_pAnimList;
	vgui::Label					*m_pStatusLabel;
	vgui::Label					*m_pCurrentModelLabel;
	vgui::Label					*m_pCurrentHandsLabel;
	vgui::Button				*m_pConfirmBtn;

	int		m_nSelectedModelIndex;
	int		m_nLastSelectedID;
	int		m_nLastModelCount;
	int		m_nLastAnimID;
	int		m_nLastFitModelIndex;
	int		m_nLastFitWide;
	int		m_nLastFitTall;
	bool	m_bPreviewReady;
	bool	m_bAnimApplied;
	bool	m_bAnimListBuilt;
};

CHL2SBContextMenu *HL2SB_GetContextMenu();

#endif
