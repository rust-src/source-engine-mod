//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Framebuffer - Vulkan framebuffer wrapper
//          Manages VkFramebuffer with attachment image views for a render pass
//
//===========================================================================//

#include "dxvk_adapter.h"
#include "tier0/dbg.h"
#include "tier0/threadtools.h"
#include "utlvector.h"

#include <string.h>
#include <stdlib.h>

//-----------------------------------------------------------------------------
// DXVK Framebuffer class - wraps VkFramebuffer with attachment views
//-----------------------------------------------------------------------------
class CDxvkFramebuffer
{
public:
	CDxvkFramebuffer();
	~CDxvkFramebuffer();

	bool Create( CDxvkAdapter* pAdapter,
				 void* pRenderPass,
				 uint32_t nAttachmentCount,
				 void* const* ppAttachmentViews,
				 uint32_t nWidth, uint32_t nHeight, uint32_t nLayers,
				 const char* pDebugName = nullptr );
	void Destroy();

	// Accessors
	uint32_t GetWidth() const { return m_nWidth; }
	uint32_t GetHeight() const { return m_nHeight; }
	uint32_t GetLayers() const { return m_nLayers; }
	uint32_t GetAttachmentCount() const { return m_attachmentViews.Count(); }
	void* GetAttachmentView( uint32_t nIndex ) const
	{
		return ( nIndex < m_attachmentViews.Count() ) ? m_attachmentViews[ nIndex ] : nullptr;
	}
	void* GetRenderPassHandle() const { return m_pRenderPass; }

	bool IsValid() const { return m_pVkFramebuffer != nullptr; }
	void* GetFramebufferHandle() const { return m_pVkFramebuffer; }

	void GetDebugName( char* pOutBuf, uint32_t nBufSize ) const
	{
		if ( pOutBuf && nBufSize > 0 )
			Q_strncpy( pOutBuf, m_szDebugName, nBufSize );
	}

	// Dynamic resizing support
	bool Resize( uint32_t nNewWidth, uint32_t nNewHeight );

	// Attachment index queries
	int FindAttachmentByView( void* pImageView ) const
	{
		for ( int i = 0; i < m_attachmentViews.Count(); ++i )
		{
			if ( m_attachmentViews[ i ] == pImageView )
				return i;
		}
		return -1;
	}

private:
	bool ValidateAgainstRenderPass();

	CDxvkAdapter* m_pAdapter;
	void* m_pVkFramebuffer;
	void* m_pRenderPass;

	uint32_t m_nWidth;
	uint32_t m_nHeight;
	uint32_t m_nLayers;

	char m_szDebugName[ 128 ];

	CUtlVector< void* > m_attachmentViews;
};

static CDxvkFramebuffer s_DxvkFramebuffer;

//-----------------------------------------------------------------------------
// CDxvkFramebuffer Implementation
//-----------------------------------------------------------------------------
CDxvkFramebuffer::CDxvkFramebuffer() :
	m_pAdapter( nullptr ),
	m_pVkFramebuffer( nullptr ),
	m_pRenderPass( nullptr ),
	m_nWidth( 0 ),
	m_nHeight( 0 ),
	m_nLayers( 0 )
{
	memset( m_szDebugName, 0, sizeof(m_szDebugName) );
	m_attachmentViews.RemoveAll();
}

CDxvkFramebuffer::~CDxvkFramebuffer()
{
	Destroy();
}

bool CDxvkFramebuffer::Create( CDxvkAdapter* pAdapter,
							   void* pRenderPass,
							   uint32_t nAttachmentCount,
							   void* const* ppAttachmentViews,
							   uint32_t nWidth, uint32_t nHeight, uint32_t nLayers,
							   const char* pDebugName )
{
	if ( IsValid() ) Destroy();
	if ( !pAdapter ) return false;
	if ( nWidth == 0 || nHeight == 0 ) return false;

	m_pAdapter = pAdapter;
	m_pRenderPass = pRenderPass;
	m_nWidth = nWidth;
	m_nHeight = nHeight;
	m_nLayers = nLayers ? nLayers : 1;

	if ( pDebugName )
		Q_strncpy( m_szDebugName, pDebugName, sizeof(m_szDebugName) - 1 );

	m_attachmentViews.EnsureCapacity( nAttachmentCount );
	for ( uint32_t i = 0; i < nAttachmentCount; ++i )
		m_attachmentViews.AddToTail( ppAttachmentViews[ i ] );

	if ( !ValidateAgainstRenderPass() )
	{
		Destroy();
		return false;
	}

	return true;
}

void CDxvkFramebuffer::Destroy()
{
	m_pVkFramebuffer = nullptr;
	m_pRenderPass = nullptr;
	m_nWidth = 0;
	m_nHeight = 0;
	m_nLayers = 0;
	memset( m_szDebugName, 0, sizeof(m_szDebugName) );
	m_attachmentViews.RemoveAll();
	m_pAdapter = nullptr;
}

bool CDxvkFramebuffer::Resize( uint32_t nNewWidth, uint32_t nNewHeight )
{
	if ( !IsValid() ) return false;
	if ( nNewWidth == 0 || nNewHeight == 0 ) return false;
	if ( nNewWidth == m_nWidth && nNewHeight == m_nHeight ) return true;

	void* pRenderPass = m_pRenderPass;
	uint32_t nLayers = m_nLayers;
	uint32_t nAttachCount = m_attachmentViews.Count();
	CUtlVector< void* > savedViews;
	savedViews.EnsureCapacity( nAttachCount );
	for ( uint32_t i = 0; i < nAttachCount; ++i )
		savedViews.AddToTail( m_attachmentViews[ i ] );

	Destroy();

	return Create( m_pAdapter ? m_pAdapter : (CDxvkAdapter*)0x1,
				   pRenderPass,
				   nAttachCount,
				   savedViews.Base(),
				   nNewWidth, nNewHeight, nLayers,
				   m_szDebugName[ 0 ] ? m_szDebugName : nullptr );
}

bool CDxvkFramebuffer::ValidateAgainstRenderPass()
{
	return true;
}
