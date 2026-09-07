//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Displays a PNG image loaded from a game file-system path
//
//=======================================================================================//

#ifndef PNGIMAGEPANEL_H
#define PNGIMAGEPANEL_H
#ifdef _WIN32
#pragma once
#endif

#include "vgui_controls/Panel.h"

using namespace vgui;

//-----------------------------------------------------------------------------
// Purpose: Displays a PNG image (e.g. a map thumbnail) from a game-relative path
//-----------------------------------------------------------------------------
class CPNGImagePanel : public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CPNGImagePanel, vgui::Panel );

public:
	CPNGImagePanel( vgui::Panel *parent, const char *name );
	~CPNGImagePanel();

	// Load a PNG from a game file-system relative path (e.g. "maps/thumb/foo.png").
	// Convenience overload that takes the map name and builds the thumb path.
	void SetImage( const char *pszPath );
	void SetMapImage( const char *pszMapName );

	virtual void Paint( void );

private:
	void LoadImageFromBuffer( const char *pszPath );

	int m_iTextureID;
	int m_iImageWidth, m_iImageHeight;
	bool m_bHasValidTexture, m_bLoadedTexture;
	bool m_bScaleToFit;
	char m_szImagePath[256];
};

#endif // PNGIMAGEPANEL_H
