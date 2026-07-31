//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Texture Manager - ITexture/ITextureReg internal implementation
//          Implements Source engine texture interface via DXVK Vulkan textures
//
//===========================================================================//

#include "utlvector.h"
#include "materialsystem/imaterialsystem.h"
#include "IHardwareConfigInternal.h"
#include "shadersystem.h"
#include "shaderapi/ishaderutil.h"
#include "shaderapi/ishaderapi.h"
#include "materialsystem/imesh.h"
#include "materialsystem/itexture.h"
#include "materialsystem/idebugtextureinfo.h"
#include "tier0/dbg.h"
#include "bitmap/imageformat.h"
#include "tier1/utlbuffer.h"
#include "dxvk_adapter.h"

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class CTextureDxVk;
class ITexture;
class IVTFTexture;
class CPixelWriter;
struct Rect_t;

//-----------------------------------------------------------------------------
// Texture handle pool (DxVk-specific texture metadata)
//-----------------------------------------------------------------------------
struct DxvkTextureMeta_t
{
	int nWidth;
	int nHeight;
	int nDepth;
	int nMipLevels;
	ImageFormat format;
	int nFlags;
	bool bIsRenderTarget;
	bool bIsDepthBuffer;
	bool bIsCubeMap;
	bool bValid;
	bool bMipmapsGenerated;
	int nReferenceCount;
	void* pVkImage;
	void* pVkImageView;
	void* pAlloc;
	void* pSampler;
	char szName[ 256 ];
};

static const int kDxVkMaxTextures = 65536;
static DxvkTextureMeta_t s_DxvkTextures[ kDxVkMaxTextures ];
static int s_DxvkNextTextureIndex = 1;

//-----------------------------------------------------------------------------
// Texture filter/wrap modes cached state
//-----------------------------------------------------------------------------
struct DxvkSamplerState_t
{
	uint32_t nMinFilter;
	uint32_t nMagFilter;
	uint32_t nMipFilter;
	uint32_t nAddressU;
	uint32_t nAddressV;
	uint32_t nAddressW;
	float fMipLodBias;
	float fAnisotropyLevel;
	uint32_t nCompareFunc;
	float fBorderColor[ 4 ];
	float fMinLod;
	float fMaxLod;
	bool bCompareEnable;
};

static const int kDxVkMaxSamplers = 1024;
static DxvkSamplerState_t s_DxvkSamplerCache[ kDxVkMaxSamplers ];
static int s_DxvkNextSamplerIndex = 0;

//-----------------------------------------------------------------------------
// CTextureDxVk - Source engine ITexture interface implementation
//-----------------------------------------------------------------------------
class CTextureDxVk : public ITexture
{
public:
	CTextureDxVk();
	virtual ~CTextureDxVk();

	// ITexture methods
	virtual void IncrementReferenceCount() { m_nRefCount++; }
	virtual void DecrementReferenceCount()
	{
		if ( m_nRefCount > 0 ) m_nRefCount--;
	}
	virtual int GetReferenceCount() const { return m_nRefCount; }

	virtual void SetTextureRegisters( ShaderShader_t shader, int nVar, int nHeight );

	virtual void GetRef( void** out ) const { *out = (void*)const_cast< CTextureDxVk* >( this ); }
	virtual int GetWidth( void ) const { return m_nWidth; }
	virtual int GetHeight( void ) const { return m_nHeight; }
	virtual int GetDepth( void ) const { return m_nDepth; }
	virtual int GetMipMappingLevels( void ) const { return m_nMipLevels; }
	virtual int GetResourceFlags( void ) const { return m_nFlags; }
	virtual bool IsRenderTarget( void ) const { return m_bIsRenderTarget; }
	virtual bool IsCubeMap( void ) const { return m_bIsCubeMap; }
	virtual bool IsNormalMap( void ) const { return m_bIsNormalMap; }
	virtual bool IsProcedural( void ) const { return m_bIsProcedural; }
	virtual bool IsManaged( void ) { return m_bIsManaged; }
	virtual int GetRenderTargetSurface( void ) { return 0; }
	virtual bool IsError( void ) { return m_bIsError; }
	virtual ImageFormat GetImageFormat( void ) const { return m_Format; }
	virtual const char* GetName( void ) const { return m_szName; }
	virtual void GetLowResColorSample( int s, int t, int u, float* color ) const;
	virtual void* GetResource() { return m_pDxVkImage; }
	virtual void Download( int nSlice, int nFace, ImageFormat dstFormat, unsigned char* buf, int nDstStride = 0 );
	virtual void ReadPixels( int x, int y, int width, int height, unsigned char* data, ImageFormat dstFormat );
	virtual int GetNumAnimationFrames() { return 1; }
	virtual bool HasAnimationFactor() { return false; }
	virtual float GetAnimationFactor() { return 0.0f; }
	virtual bool IsMipMaped() { return ( m_nFlags & TEXTUREFLAGS_NOMIP ) == 0; }
	virtual int GetAlphaTestGranularity( void ) const { return 0; }
	virtual ShaderAPITextureHandle_t GetShaderAPITextureHandle() const
	{
		return (ShaderAPITextureHandle_t)m_nDxVkIndex;
	}
	virtual void SetFilter( int nStage, int nFilter ) { m_nFilterMode = nFilter; }
	virtual void SetWrapMode( int nStage, TextureStage_t nTextureStage, int nWrapMode );
	virtual void SetBorderColor( int nStage, int nBorderColor ) {}

	// Debugging
	virtual bool HasLod() const { return m_bHasLod; }
	virtual int GetActualLodWidth( void ) const { return m_nWidth; }
	virtual int GetActualLodHeight( void ) const { return m_nHeight; }
	virtual int GetTextureMemoryUsed( void ) const { return m_nTextureMemoryUsed; }

	virtual bool ConvertImageFormatIfNeeded( int width, int height, int depth,
											 ImageFormat srcImageFormat, const char* pSrcData,
											 int nSrcStride,
											 ImageFormat& dstImageFormat,
											 unsigned char* pDstData, int nDstStride );

	virtual void CopyToBuffer( unsigned char* pBuffer, int nBufferSize, ImageFormat dstFormat );
	virtual void Upload( int nSlice, int nFace, int nMipLevel, int nX, int nY,
						 int nWidth, int nHeight, ImageFormat srcImageFormat,
						 int nSrcStride, const char* pSrcData, int nBufferedFrames = 1 );
	virtual void UpdateFromVTF( int nFrame, int nFace, IVTFTexture* pVTF,
								int nVTFFrame, int nFlags, int nSrcMinMip = 0, int nSrcMaxMip = -1 );

	virtual void GetResourceInfo( int& width, int& height, ImageFormat& format ) const
	{
		width = m_nWidth; height = m_nHeight; format = m_Format;
	}
	virtual void SetPriority( char priority ) { m_nPriority = priority; }
	virtual void Precache() {}

	// DXVK-specific
	void InitDxVk( const char* pName, int w, int h, int d, int nMips,
				   ImageFormat fmt, int flags, bool bIsRenderTarget,
				   bool bIsDepthBuffer, bool bIsCubeMap,
				   ShaderAPITextureHandle_t handle );
	void ShutdownDxVk();

	void* GetVkImage() const { return m_pDxVkImage; }
	void* GetVkImageView() const { return m_pDxVkImageView; }
	int GetDxVkIndex() const { return m_nDxVkIndex; }

private:
	int m_nRefCount;
	int m_nWidth;
	int m_nHeight;
	int m_nDepth;
	int m_nMipLevels;
	int m_nFlags;
	int m_nDxVkIndex;
	int m_nFilterMode;
	int m_nTextureMemoryUsed;
	char m_nPriority;

	ImageFormat m_Format;

	bool m_bIsRenderTarget;
	bool m_bIsDepthBuffer;
	bool m_bIsCubeMap;
	bool m_bIsNormalMap;
	bool m_bIsProcedural;
	bool m_bIsManaged;
	bool m_bIsError;
	bool m_bHasLod;

	void* m_pDxVkImage;
	void* m_pDxVkImageView;
	void* m_pDxVkAlloc;
	void* m_pDxVkSampler;

	char m_szName[ 256 ];
};

static CTextureDxVk s_DefaultErrorTexture;
static CTextureDxVk* s_pErrorTexture = nullptr;
static CUtlVector< CTextureDxVk* > s_DxvkTextureInstances;

//-----------------------------------------------------------------------------
// CTextureDxVk Implementation
//-----------------------------------------------------------------------------
CTextureDxVk::CTextureDxVk() :
	m_nRefCount( 0 ),
	m_nWidth( 1 ),
	m_nHeight( 1 ),
	m_nDepth( 1 ),
	m_nMipLevels( 1 ),
	m_nFlags( 0 ),
	m_nDxVkIndex( 0 ),
	m_nFilterMode( 0 ),
	m_nTextureMemoryUsed( 0 ),
	m_nPriority( 0 ),
	m_Format( IMAGE_FORMAT_BGRA8888 ),
	m_bIsRenderTarget( false ),
	m_bIsDepthBuffer( false ),
	m_bIsCubeMap( false ),
	m_bIsNormalMap( false ),
	m_bIsProcedural( false ),
	m_bIsManaged( false ),
	m_bIsError( false ),
	m_bHasLod( false ),
	m_pDxVkImage( nullptr ),
	m_pDxVkImageView( nullptr ),
	m_pDxVkAlloc( nullptr ),
	m_pDxVkSampler( nullptr )
{
	memset( m_szName, 0, sizeof(m_szName) );
}

CTextureDxVk::~CTextureDxVk()
{
	ShutdownDxVk();
}

void CTextureDxVk::InitDxVk( const char* pName, int w, int h, int d, int nMips,
							 ImageFormat fmt, int flags, bool bIsRenderTarget,
							 bool bIsDepthBuffer, bool bIsCubeMap,
							 ShaderAPITextureHandle_t handle )
{
	if ( pName )
		Q_strncpy( m_szName, pName, sizeof(m_szName) - 1 );
	m_nWidth = w > 0 ? w : 1;
	m_nHeight = h > 0 ? h : 1;
	m_nDepth = d > 0 ? d : 1;
	m_nMipLevels = nMips > 0 ? nMips : 1;
	m_Format = fmt;
	m_nFlags = flags;
	m_bIsRenderTarget = bIsRenderTarget;
	m_bIsDepthBuffer = bIsDepthBuffer;
	m_bIsCubeMap = bIsCubeMap;
	m_nDxVkIndex = (int)handle;
	m_nTextureMemoryUsed = m_nWidth * m_nHeight * m_nDepth * 4;
}

void CTextureDxVk::ShutdownDxVk()
{
	m_pDxVkImage = nullptr;
	m_pDxVkImageView = nullptr;
	m_pDxVkAlloc = nullptr;
	m_pDxVkSampler = nullptr;
}

void CTextureDxVk::SetTextureRegisters( ShaderShader_t shader, int nVar, int nHeight )
{
}

void CTextureDxVk::GetLowResColorSample( int s, int t, int u, float* color ) const
{
	if ( !color ) return;
	color[ 0 ] = 1.0f;
	color[ 1 ] = 0.0f;
	color[ 2 ] = 1.0f;
	color[ 3 ] = 1.0f;
}

void CTextureDxVk::Download( int nSlice, int nFace, ImageFormat dstFormat,
							unsigned char* buf, int nDstStride )
{
	int nRowBytes = m_nWidth * 4;
	int nStride = ( nDstStride > 0 ) ? nDstStride : nRowBytes;
	for ( int y = 0; y < m_nHeight; ++y )
		memset( buf + y * nStride, 0, nRowBytes );
}

void CTextureDxVk::ReadPixels( int x, int y, int width, int height,
								unsigned char* data, ImageFormat dstFormat )
{
	int nRowBytes = width * 4;
	memset( data, 0, height * nRowBytes );
}

void CTextureDxVk::SetWrapMode( int nStage, TextureStage_t nTextureStage, int nWrapMode )
{
}

bool CTextureDxVk::ConvertImageFormatIfNeeded( int width, int height, int depth,
											   ImageFormat srcImageFormat, const char* pSrcData,
											   int nSrcStride,
											   ImageFormat& dstImageFormat,
											   unsigned char* pDstData, int nDstStride )
{
	dstImageFormat = srcImageFormat;
	return false;
}

void CTextureDxVk::CopyToBuffer( unsigned char* pBuffer, int nBufferSize, ImageFormat dstFormat )
{
	if ( !pBuffer ) return;
	memset( pBuffer, 0, nBufferSize );
}

void CTextureDxVk::Upload( int nSlice, int nFace, int nMipLevel, int nX, int nY,
						   int nWidth, int nHeight, ImageFormat srcImageFormat,
						   int nSrcStride, const char* pSrcData, int nBufferedFrames )
{
}

void CTextureDxVk::UpdateFromVTF( int nFrame, int nFace, IVTFTexture* pVTF,
								  int nVTFFrame, int nFlags, int nSrcMinMip, int nSrcMaxMip )
{
}

//-----------------------------------------------------------------------------
// Global texture pool helpers
//-----------------------------------------------------------------------------
static void DxvkInitTexturePool()
{
	memset( s_DxvkTextures, 0, sizeof(s_DxvkTextures) );
	s_DxvkNextTextureIndex = 1;
	s_DxvkNextSamplerIndex = 0;
	memset( s_DxvkSamplerCache, 0, sizeof(s_DxvkSamplerCache) );

	if ( !s_pErrorTexture )
	{
		s_pErrorTexture = &s_DefaultErrorTexture;
		s_pErrorTexture->InitDxVk( "__dxvk_error_texture__", 4, 4, 1, 1,
									IMAGE_FORMAT_BGRA8888, 0, false, false, false,
									(ShaderAPITextureHandle_t)s_DxvkNextTextureIndex++ );
		s_pErrorTexture->m_bIsError = true;
		s_DxvkTextureInstances.AddToTail( s_pErrorTexture );
	}
}

static int DxvkAllocTextureIndex()
{
	for ( int i = 1; i < kDxVkMaxTextures; ++i )
	{
		int idx = ( s_DxvkNextTextureIndex + i ) % kDxVkMaxTextures;
		if ( idx == 0 ) idx = 1;
		if ( !s_DxvkTextures[ idx ].bValid )
		{
			s_DxvkNextTextureIndex = idx + 1;
			memset( &s_DxvkTextures[ idx ], 0, sizeof(DxvkTextureMeta_t) );
			s_DxvkTextures[ idx ].bValid = true;
			return idx;
		}
	}
	return 0;
}

static DxvkTextureMeta_t* DxvkGetTextureMeta( int nIndex )
{
	if ( nIndex <= 0 || nIndex >= kDxVkMaxTextures ) return nullptr;
	if ( !s_DxvkTextures[ nIndex ].bValid ) return nullptr;
	return &s_DxvkTextures[ nIndex ];
}

static void DxvkFreeTextureIndex( int nIndex )
{
	if ( nIndex > 0 && nIndex < kDxVkMaxTextures )
		memset( &s_DxvkTextures[ nIndex ], 0, sizeof(DxvkTextureMeta_t) );
}

static int DxvkAllocSamplerState( const DxvkSamplerState_t& state )
{
	for ( int i = 0; i < s_DxvkNextSamplerIndex; ++i )
	{
		if ( memcmp( &s_DxvkSamplerCache[ i ], &state, sizeof(DxvkSamplerState_t) ) == 0 )
			return i;
	}
	if ( s_DxvkNextSamplerIndex < kDxVkMaxSamplers )
	{
		s_DxvkSamplerCache[ s_DxvkNextSamplerIndex ] = state;
		return s_DxvkNextSamplerIndex++;
	}
	return -1;
}
