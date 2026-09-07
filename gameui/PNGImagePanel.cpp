//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=======================================================================================//

#include "PNGImagePanel.h"
#include "vgui/ISurface.h"
#include "filesystem.h"
#include "imageutils.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

#include <stdlib.h> // free()

CPNGImagePanel::CPNGImagePanel( vgui::Panel *parent, const char *name ) : BaseClass( parent, name )
{
	m_iTextureID = -1;
	m_bHasValidTexture = false;
	m_bLoadedTexture = false;
	m_bScaleToFit = true;
	m_iImageWidth = 0;
	m_iImageHeight = 0;
	m_szImagePath[0] = 0;

	SetPaintBackgroundEnabled( false );
}

CPNGImagePanel::~CPNGImagePanel()
{
	if ( vgui::surface() && m_iTextureID != -1 )
	{
		vgui::surface()->DestroyTextureID( m_iTextureID );
		m_iTextureID = -1;
	}
}

// Load a map thumbnail for a given map name from the standard thumb folder.
void CPNGImagePanel::SetMapImage( const char *pszMapName )
{
	char szPath[MAX_PATH];
	Q_snprintf( szPath, sizeof( szPath ), "maps/thumb/%s.png", pszMapName );

	SetImage( szPath );
}

void CPNGImagePanel::SetImage( const char *pszPath )
{
	if ( pszPath && pszPath[0] )
	{
		Q_strncpy( m_szImagePath, pszPath, sizeof( m_szImagePath ) );
	}
	else
	{
		m_szImagePath[0] = 0;
	}

	// force a reload on the next paint
	m_bLoadedTexture = false;
	m_bHasValidTexture = false;
}

void CPNGImagePanel::Paint()
{
	if ( !m_szImagePath[0] )
		return;

	if ( !m_bLoadedTexture )
	{
		m_bLoadedTexture = true;

		if ( m_iTextureID == -1 )
		{
			m_iTextureID = vgui::surface()->CreateNewTextureID( true );
		}

		int w = 0, h = 0;
		ConversionErrorType errcode = CE_ERROR_LOADING_DLL;
		unsigned char *pRGBA = ImgUtl_ReadPNGAsRGBA( m_szImagePath, w, h, errcode );
		if ( pRGBA && errcode == CE_SUCCESS )
		{
			vgui::surface()->DrawSetTextureRGBA( m_iTextureID, pRGBA, w, h, false, true );
			m_bHasValidTexture = true;
			m_iImageWidth = w;
			m_iImageHeight = h;
		}
		else
		{
			m_bHasValidTexture = false;
		}

		if ( pRGBA )
		{
			free( pRGBA );
		}
	}

	int wide, tall;
	GetSize( wide, tall );

	if ( m_bHasValidTexture )
	{
		vgui::surface()->DrawSetTexture( m_iTextureID );
		vgui::surface()->DrawSetColor( 255, 255, 255, 255 );
		if ( m_bScaleToFit )
		{
			vgui::surface()->DrawTexturedRect( 0, 0, wide, tall );
		}
		else
		{
			vgui::surface()->DrawTexturedRect( 0, 0, m_iImageWidth, m_iImageHeight );
		}
	}
	else
	{
		// placeholder
		vgui::surface()->DrawSetColor( 40, 40, 40, 255 );
		vgui::surface()->DrawFilledRect( 0, 0, wide, tall );
	}
}
