//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: GMod-style raw image textures (.png / .jpg / .tga / .bmp).
//
//          See hl2sb_pngtexture.h.  The image is decoded with stb_image (a
//          single-header decoder already vendored under thirdparty/, used by
//          gameui/PNGImagePanel.cpp) and expanded into a mip chain, which is
//          then uploaded through the ordinary procedural-texture path.
//
//=======================================================================================//

#include <stdlib.h>
#include <string.h>

#include "basetypes.h"
#include "tier0/basetypes.h"
#include "tier1/utlvector.h"
#include "utlbuffer.h"
#include "filesystem.h"
#include "bitmap/imageformat.h"
#include "pixelwriter.h"
#include "materialsystem/itexture.h"
#include "materialsystem/imaterialsystem.h"
#include "hl2sb_pngtexture.h"

// stb_image is a single-header decoder.  Disable what we do not need; PNG/JPG/
// TGA/BMP stay enabled so a GMod material can point at any of them.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "../thirdparty/stb/stb_image.h"

#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Extensions we are willing to load straight from disk.
//-----------------------------------------------------------------------------
static const char *s_pImageExtensions[] =
{
	".png",
	".jpg",
	".jpeg",
	".tga",
	".bmp",
};

bool HL2SB_IsImageFileName( const char *pFileName )
{
	if ( !pFileName || !pFileName[0] )
		return false;

	for ( int i = 0; i < ARRAYSIZE( s_pImageExtensions ); ++i )
	{
		int nNameLen = Q_strlen( pFileName );
		int nExtLen = Q_strlen( s_pImageExtensions[i] );
		if ( nNameLen > nExtLen && !Q_stricmp( pFileName + nNameLen - nExtLen, s_pImageExtensions[i] ) )
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// "materials/<name>" is how texture files are addressed through the search
// paths (CTexture::GetFilename does the same thing with a .vtf suffix).
//-----------------------------------------------------------------------------
static bool HL2SB_ImageFileExists( const char *pLogicalName )
{
	char szPath[MAX_PATH];

	Q_snprintf( szPath, sizeof( szPath ), "materials/%s", pLogicalName );
	if ( g_pFullFileSystem->FileExists( szPath, "GAME" ) )
		return true;

	Q_strncpy( szPath, pLogicalName, sizeof( szPath ) );
	return g_pFullFileSystem->FileExists( szPath, "GAME" );
}

bool HL2SB_ResolveImageTexture( const char *pTextureName, char *pOutLogicalName, int nOutLogicalNameSize )
{
	if ( !pTextureName || !pTextureName[0] || !pOutLogicalName )
		return false;

	// Texture names are addressed relative to materials/, but accept a name
	// that already carries the prefix (vgui passes some through verbatim).
	const char *pName = pTextureName;
	if ( !Q_strnicmp( pName, "materials/", 10 ) )
		pName += 10;
	if ( !Q_strnicmp( pName, "materials\\", 10 ) )
		pName += 10;

	if ( HL2SB_IsImageFileName( pName ) )
	{
		if ( !HL2SB_ImageFileExists( pName ) )
			return false;

		Q_strncpy( pOutLogicalName, pName, nOutLogicalNameSize );
		return true;
	}

	// No extension (or a non-image one such as .vtf): probe for an image beside it.
	for ( int i = 0; i < ARRAYSIZE( s_pImageExtensions ); ++i )
	{
		char szCandidate[MAX_PATH];
		Q_snprintf( szCandidate, sizeof( szCandidate ), "%s%s", pName, s_pImageExtensions[i] );
		if ( HL2SB_ImageFileExists( szCandidate ) )
		{
			Q_strncpy( pOutLogicalName, szCandidate, nOutLogicalNameSize );
			return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// Holds the decoded image and expands it into every mip level the texture asks
// for (simple 2x2 box filter).
//-----------------------------------------------------------------------------
class CImageTextureRegenerator : public ITextureRegenerator
{
public:
	CImageTextureRegenerator( int nWidth, int nHeight ) :
		m_nWidth( nWidth ), m_nHeight( nHeight )
	{
		m_RGBA.SetSize( nWidth * nHeight * 4 );
	}

	unsigned char *GetImageBits() { return m_RGBA.Base(); }

	virtual void RegenerateTextureBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pSubRect )
	{
		int nMipCount = pTexture->IsMipmapped() ? pVTFTexture->MipCount() : 1;

		CUtlVector<unsigned char> src;
		src.SetSize( m_RGBA.Count() );
		Q_memcpy( src.Base(), m_RGBA.Base(), m_RGBA.Count() );

		CUtlVector<unsigned char> dst;

		for ( int iFrame = 0; iFrame < pVTFTexture->FrameCount(); ++iFrame )
		{
			for ( int iFace = 0; iFace < pVTFTexture->FaceCount(); ++iFace )
			{
				int nWidth = m_nWidth;
				int nHeight = m_nHeight;

				for ( int iMip = 0; iMip < nMipCount; ++iMip )
				{
					if ( iMip > 0 )
					{
						int nNewWidth = Max( 1, nWidth >> 1 );
						int nNewHeight = Max( 1, nHeight >> 1 );
						dst.SetSize( nNewWidth * nNewHeight * 4 );

						for ( int y = 0; y < nNewHeight; ++y )
						{
							for ( int x = 0; x < nNewWidth; ++x )
							{
								int nSum[4] = { 0, 0, 0, 0 };
								int nSamples = 0;

								for ( int nDy = 0; nDy < 2; ++nDy )
								{
									int nSrcY = y * 2 + nDy;
									if ( nSrcY >= nHeight )
										continue;

									for ( int nDx = 0; nDx < 2; ++nDx )
									{
										int nSrcX = x * 2 + nDx;
										if ( nSrcX >= nWidth )
											continue;

										const unsigned char *pSrc = &src[ ( nSrcY * nWidth + nSrcX ) * 4 ];
										for ( int i = 0; i < 4; ++i )
											nSum[i] += pSrc[i];
										++nSamples;
									}
								}

								unsigned char *pDst = &dst[ ( y * nNewWidth + x ) * 4 ];
								for ( int i = 0; i < 4; ++i )
									pDst[i] = (unsigned char)( nSum[i] / Max( 1, nSamples ) );
							}
						}

						src = dst;
						nWidth = nNewWidth;
						nHeight = nNewHeight;
					}

					CPixelWriter pixelWriter;
					pixelWriter.SetPixelMemory( pVTFTexture->Format(),
						pVTFTexture->ImageData( iFrame, iFace, iMip ),
						pVTFTexture->RowSizeInBytes( iMip ) );

					for ( int y = 0; y < nHeight; ++y )
					{
						pixelWriter.Seek( 0, y );
						for ( int x = 0; x < nWidth; ++x )
						{
							const unsigned char *pSrc = &src[ ( y * nWidth + x ) * 4 ];
							pixelWriter.WritePixel( pSrc[0], pSrc[1], pSrc[2], pSrc[3] );
						}
					}

					if ( nWidth <= 1 && nHeight <= 1 )
						break;
				}
			}
		}
	}

	virtual void Release() { delete this; }

private:
	int m_nWidth;
	int m_nHeight;
	CUtlVector<unsigned char> m_RGBA;
};

ITextureRegenerator *HL2SB_CreateImageTextureRegenerator( const char *pLogicalName, int *pOutWidth, int *pOutHeight )
{
	if ( !pLogicalName || !pLogicalName[0] )
		return NULL;

	CUtlBuffer bufFile;
	bool bRead = false;

	const char *pPathIDs[] = { "GAME", "MOD", NULL };
	for ( int i = 0; pPathIDs[i]; ++i )
	{
		char szPath[MAX_PATH];
		Q_snprintf( szPath, sizeof( szPath ), "materials/%s", pLogicalName );

		bufFile.Purge();
		if ( g_pFullFileSystem->ReadFile( szPath, pPathIDs[i], bufFile ) && bufFile.TellPut() > 0 )
		{
			bRead = true;
			break;
		}

		bufFile.Purge();
		if ( g_pFullFileSystem->ReadFile( pLogicalName, pPathIDs[i], bufFile ) && bufFile.TellPut() > 0 )
		{
			bRead = true;
			break;
		}
	}

	if ( !bRead )
		return NULL;

	int nWidth = 0, nHeight = 0, nChannels = 0;
	unsigned char *pRGBA = stbi_load_from_memory( (const stbi_uc *)bufFile.Base(), bufFile.TellPut(), &nWidth, &nHeight, &nChannels, 4 );
	if ( !pRGBA || nWidth <= 0 || nHeight <= 0 )
	{
		if ( pRGBA )
			stbi_image_free( pRGBA );
		return NULL;
	}

	CImageTextureRegenerator *pRegenerator = new CImageTextureRegenerator( nWidth, nHeight );
	Q_memcpy( pRegenerator->GetImageBits(), pRGBA, (size_t)nWidth * nHeight * 4 );
	stbi_image_free( pRGBA );

	if ( pOutWidth )
		*pOutWidth = nWidth;
	if ( pOutHeight )
		*pOutHeight = nHeight;

	return pRegenerator;
}
