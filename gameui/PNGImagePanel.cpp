//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=======================================================================================//

#include "PNGImagePanel.h"
#include "vgui/ISurface.h"
#include "filesystem.h"
#include "tier1/utlbuffer.h"
#include "tier1/utlmemory.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

#include <stdlib.h> // free()

// decode PNGs with stb_image (single-header, reliable) instead of the libpng path
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_JPEG
#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_HDR
#define STBI_NO_GIF
#include "stb/stb_image.h"

CPNGImagePanel::CPNGImagePanel( vgui::Panel *parent, const char *name ) : BaseClass( parent, name )
{
	m_iTextureID = -1;
	m_bHasValidTexture = false;
	m_bLoadedTexture = false;
	m_bScaleToFit = true;
	m_bPreserveAspect = true;
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

		// Read the file through the filesystem (which resolves the mod's search
		// paths, including custom mounts) then decode from the buffer with stb_image.
		CUtlBuffer bufFileContents;
		unsigned char *pRGBA = NULL;
		const char *apszPathIds[] = { "GAME", "MOD", NULL };
		bool bRead = false;
		for ( int i = 0; i < ARRAYSIZE( apszPathIds ); ++i )
		{
			bufFileContents.Clear();
			if ( g_pFullFileSystem->ReadFile( m_szImagePath, apszPathIds[i], bufFileContents ) && bufFileContents.TellPut() > 0 )
			{
				bRead = true;
				break;
			}
		}

		if ( bRead )
		{
			int nChannels = 0;
			pRGBA = stbi_load_from_memory( (const stbi_uc *)bufFileContents.Base(), bufFileContents.TellPut(), &w, &h, &nChannels, 4 );
		}

		if ( pRGBA )
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
			stbi_image_free( pRGBA );
		}
	}

	int wide, tall;
	GetSize( wide, tall );

	if ( m_bHasValidTexture )
	{
		vgui::surface()->DrawSetTexture( m_iTextureID );
		vgui::surface()->DrawSetColor( 255, 255, 255, 255 );

		if ( m_bPreserveAspect && m_iImageWidth > 0 && m_iImageHeight > 0 )
		{
			// Fit the image inside the panel, preserving aspect ratio (letterbox).
			float fPanelAspect = ( tall > 0 ) ? (float)wide / (float)tall : 0.f;
			float fImgAspect = (float)m_iImageWidth / (float)m_iImageHeight;

			int drawW = wide, drawH = tall;
			if ( fImgAspect > fPanelAspect )
			{
				drawH = (int)( wide / fImgAspect );
			}
			else
			{
				drawW = (int)( tall * fImgAspect );
			}

			int drawX = (wide - drawW) / 2;
			int drawY = (tall - drawH) / 2;

			vgui::surface()->DrawTexturedRect( drawX, drawY, drawX + drawW, drawY + drawH );
		}
		else if ( m_bScaleToFit )
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
		// no thumbnail -> black
		vgui::surface()->DrawSetColor( 0, 0, 0, 255 );
		vgui::surface()->DrawFilledRect( 0, 0, wide, tall );
	}
}
