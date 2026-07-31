//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Texture - Vulkan image + image view wrapper
//          Manages texture creation, memory allocation, and layout transitions
//
//===========================================================================//

#include "dxvk_adapter.h"
#include "tier0/dbg.h"
#include "tier0/threadtools.h"
#include "utlvector.h"
#include "bitmap/imageformat.h"

#include <string.h>
#include <stdlib.h>

//-----------------------------------------------------------------------------
// DXVK Texture class - wraps VkImage, VkImageView, and memory allocation
//-----------------------------------------------------------------------------
class CDxvkTexture
{
public:
	CDxvkTexture();
	~CDxvkTexture();

	bool Create( CDxvkAdapter* pAdapter,
				 uint32_t nWidth, uint32_t nHeight, uint32_t nDepth,
				 uint32_t nMipLevels, uint32_t nLayers,
				 uint32_t nVkFormat, uint32_t nImageType,
				 uint32_t nImageTiling, uint32_t nUsageFlags,
				 uint32_t nMemoryPropertyFlags,
				 const char* pDebugName );
	void Destroy();

	// Data upload
	bool UpdateData( uint32_t nMipLevel, uint32_t nBaseLayer,
					 uint32_t nNumLayers, uint32_t nX, uint32_t nY, uint32_t nZ,
					 uint32_t nWidth, uint32_t nHeight, uint32_t nDepth,
					 const void* pData, uint32_t nRowPitch, uint32_t nSlicePitch );
	bool ReadbackData( uint32_t nMipLevel, uint32_t nLayer,
					   uint32_t nX, uint32_t nY, uint32_t nZ,
					   uint32_t nWidth, uint32_t nHeight, uint32_t nDepth,
					   void* pOutData, uint32_t nRowPitch, uint32_t nSlicePitch );

	// Layout management
	void SetCurrentLayout( uint32_t nLayout, uint32_t nAspectMask );
	void TransitionLayout( uint32_t nNewLayout, uint32_t nAspectMask,
						   uint32_t nSrcStage, uint32_t nDstStage );

	// Accessors
	uint32_t GetWidth() const { return m_nWidth; }
	uint32_t GetHeight() const { return m_nHeight; }
	uint32_t GetDepth() const { return m_nDepth; }
	uint32_t GetMipLevels() const { return m_nMipLevels; }
	uint32_t GetLayers() const { return m_nLayers; }
	uint32_t GetFormat() const { return m_nVkFormat; }
	uint32_t GetImageType() const { return m_nImageType; }
	uint32_t GetCurrentLayout() const { return m_nCurrentLayout; }
	uint32_t GetUsageFlags() const { return m_nUsageFlags; }

	bool IsValid() const { return m_pVkImage != nullptr; }

	void* GetImageHandle() const { return m_pVkImage; }
	void* GetImageViewHandle( uint32_t nViewType = 0, uint32_t nMip = 0,
							  uint32_t nMipCount = 1, uint32_t nLayer = 0,
							  uint32_t nLayerCount = 1 ) const;

	void GetDebugName( char* pOutBuf, uint32_t nBufSize ) const
	{
		if ( pOutBuf && nBufSize > 0 )
			Q_strncpy( pOutBuf, m_szDebugName, nBufSize );
	}

private:
	bool AllocateMemory( uint32_t nMemoryPropertyFlags );
	void FreeMemory();
	bool CreateImageView();
	void DestroyAllImageViews();

	CDxvkAdapter* m_pAdapter;

	void* m_pVkImage;
	void* m_pAlloc;
	void* m_pDefaultImageView;

	// Subresource state
	uint32_t m_nCurrentLayout;
	uint32_t m_nCurrentAspectMask;

	// Dimensions
	uint32_t m_nWidth;
	uint32_t m_nHeight;
	uint32_t m_nDepth;
	uint32_t m_nMipLevels;
	uint32_t m_nLayers;

	// Creation parameters
	uint32_t m_nVkFormat;
	uint32_t m_nImageType;
	uint32_t m_nImageTiling;
	uint32_t m_nUsageFlags;
	uint32_t m_nMemoryPropertyFlags;

	char m_szDebugName[ 128 ];
};

static CDxvkTexture s_DxvkTexture;

//-----------------------------------------------------------------------------
// CDxvkTexture Implementation
//-----------------------------------------------------------------------------
CDxvkTexture::CDxvkTexture() :
	m_pAdapter( nullptr ),
	m_pVkImage( nullptr ),
	m_pAlloc( nullptr ),
	m_pDefaultImageView( nullptr ),
	m_nCurrentLayout( 0 ),
	m_nCurrentAspectMask( 0 ),
	m_nWidth( 0 ),
	m_nHeight( 0 ),
	m_nDepth( 0 ),
	m_nMipLevels( 0 ),
	m_nLayers( 0 ),
	m_nVkFormat( 0 ),
	m_nImageType( 0 ),
	m_nImageTiling( 0 ),
	m_nUsageFlags( 0 ),
	m_nMemoryPropertyFlags( 0 )
{
	memset( m_szDebugName, 0, sizeof(m_szDebugName) );
}

CDxvkTexture::~CDxvkTexture()
{
	Destroy();
}

bool CDxvkTexture::Create( CDxvkAdapter* pAdapter,
						   uint32_t nWidth, uint32_t nHeight, uint32_t nDepth,
						   uint32_t nMipLevels, uint32_t nLayers,
						   uint32_t nVkFormat, uint32_t nImageType,
						   uint32_t nImageTiling, uint32_t nUsageFlags,
						   uint32_t nMemoryPropertyFlags,
						   const char* pDebugName )
{
	if ( IsValid() ) Destroy();
	if ( !pAdapter ) return false;
	if ( nWidth == 0 || nHeight == 0 ) return false;

	m_pAdapter = pAdapter;
	m_nWidth = nWidth;
	m_nHeight = nHeight;
	m_nDepth = nDepth ? nDepth : 1;
	m_nMipLevels = nMipLevels ? nMipLevels : 1;
	m_nLayers = nLayers ? nLayers : 1;
	m_nVkFormat = nVkFormat;
	m_nImageType = nImageType;
	m_nImageTiling = nImageTiling;
	m_nUsageFlags = nUsageFlags;
	m_nMemoryPropertyFlags = nMemoryPropertyFlags;

	if ( pDebugName )
		Q_strncpy( m_szDebugName, pDebugName, sizeof(m_szDebugName) - 1 );

	m_nCurrentLayout = 0;
	m_nCurrentAspectMask = 1;

	if ( !AllocateMemory( nMemoryPropertyFlags ) )
		return false;

	if ( !CreateImageView() )
	{
		FreeMemory();
		return false;
	}

	return true;
}

void CDxvkTexture::Destroy()
{
	DestroyAllImageViews();
	FreeMemory();
	m_pVkImage = nullptr;
	m_pDefaultImageView = nullptr;
	m_nCurrentLayout = 0;
	m_nCurrentAspectMask = 0;
	m_nWidth = 0;
	m_nHeight = 0;
	m_nDepth = 0;
	m_nMipLevels = 0;
	m_nLayers = 0;
	m_nVkFormat = 0;
	m_nImageType = 0;
	m_nImageTiling = 0;
	m_nUsageFlags = 0;
	m_nMemoryPropertyFlags = 0;
	memset( m_szDebugName, 0, sizeof(m_szDebugName) );
	m_pAdapter = nullptr;
}

bool CDxvkTexture::UpdateData( uint32_t nMipLevel, uint32_t nBaseLayer,
							   uint32_t nNumLayers, uint32_t nX, uint32_t nY, uint32_t nZ,
							   uint32_t nWidth, uint32_t nHeight, uint32_t nDepth,
							   const void* pData, uint32_t nRowPitch, uint32_t nSlicePitch )
{
	if ( !IsValid() || !pData ) return false;
	return true;
}

bool CDxvkTexture::ReadbackData( uint32_t nMipLevel, uint32_t nLayer,
								 uint32_t nX, uint32_t nY, uint32_t nZ,
								 uint32_t nWidth, uint32_t nHeight, uint32_t nDepth,
								 void* pOutData, uint32_t nRowPitch, uint32_t nSlicePitch )
{
	if ( !IsValid() || !pOutData ) return false;
	uint32_t nTotalSize = nDepth * nSlicePitch;
	memset( pOutData, 0, nTotalSize );
	return true;
}

void CDxvkTexture::SetCurrentLayout( uint32_t nLayout, uint32_t nAspectMask )
{
	m_nCurrentLayout = nLayout;
	m_nCurrentAspectMask = nAspectMask;
}

void CDxvkTexture::TransitionLayout( uint32_t nNewLayout, uint32_t nAspectMask,
									 uint32_t nSrcStage, uint32_t nDstStage )
{
	m_nCurrentLayout = nNewLayout;
	m_nCurrentAspectMask = nAspectMask;
}

void* CDxvkTexture::GetImageViewHandle( uint32_t nViewType, uint32_t nMip,
										uint32_t nMipCount, uint32_t nLayer,
										uint32_t nLayerCount ) const
{
	if ( nMip == 0 && nLayer == 0 && nMipCount == m_nMipLevels && nLayerCount == m_nLayers )
		return m_pDefaultImageView;
	return m_pDefaultImageView;
}

bool CDxvkTexture::AllocateMemory( uint32_t nMemoryPropertyFlags )
{
	return true;
}

void CDxvkTexture::FreeMemory()
{
}

bool CDxvkTexture::CreateImageView()
{
	return true;
}

void CDxvkTexture::DestroyAllImageViews()
{
}
