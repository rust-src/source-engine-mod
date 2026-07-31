//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Render Pass - Vulkan render pass wrapper
//          Manages VkRenderPass with subpass dependencies and attachments
//
//===========================================================================//

#include "dxvk_adapter.h"
#include "tier0/dbg.h"
#include "tier0/threadtools.h"
#include "utlvector.h"

#include <string.h>
#include <stdlib.h>

//-----------------------------------------------------------------------------
// DXVK Attachment description for render pass creation
//-----------------------------------------------------------------------------
struct DxvkAttachmentDesc_t
{
	uint32_t nFormat;
	uint32_t nSamples;
	uint32_t nLoadOp;
	uint32_t nStoreOp;
	uint32_t nStencilLoadOp;
	uint32_t nStencilStoreOp;
	uint32_t nInitialLayout;
	uint32_t nFinalLayout;
	uint32_t nFlags;
};

//-----------------------------------------------------------------------------
// DXVK Subpass description
//-----------------------------------------------------------------------------
struct DxvkSubpassDesc_t
{
	uint32_t nPipelineBindPoint;
	uint32_t nInputAttachmentCount;
	const uint32_t* pInputAttachmentReferences;
	const uint32_t* pInputAttachmentLayouts;
	uint32_t nColorAttachmentCount;
	const uint32_t* pColorAttachmentReferences;
	const uint32_t* pColorAttachmentLayouts;
	const uint32_t* pResolveAttachmentReferences;
	const uint32_t* pResolveAttachmentLayouts;
	uint32_t nDepthStencilAttachmentReference;
	uint32_t nDepthStencilAttachmentLayout;
	uint32_t nPreserveAttachmentCount;
	const uint32_t* pPreserveAttachments;
};

//-----------------------------------------------------------------------------
// DXVK Subpass dependency
//-----------------------------------------------------------------------------
struct DxvkSubpassDependency_t
{
	uint32_t nSrcSubpass;
	uint32_t nDstSubpass;
	uint32_t nSrcStageMask;
	uint32_t nDstStageMask;
	uint32_t nSrcAccessMask;
	uint32_t nDstAccessMask;
	uint32_t nDependencyFlags;
};

//-----------------------------------------------------------------------------
// DXVK Render Pass class - wraps VkRenderPass + compatible framebuffer info
//-----------------------------------------------------------------------------
class CDxvkRenderPass
{
public:
	CDxvkRenderPass();
	~CDxvkRenderPass();

	bool Create( CDxvkAdapter* pAdapter,
				 uint32_t nAttachmentCount,
				 const DxvkAttachmentDesc_t* pAttachments,
				 uint32_t nSubpassCount,
				 const DxvkSubpassDesc_t* pSubpasses,
				 uint32_t nDependencyCount,
				 const DxvkSubpassDependency_t* pDependencies,
				 const char* pDebugName = nullptr );
	void Destroy();

	// Accessors
	uint32_t GetAttachmentCount() const { return m_attachments.Count(); }
	const DxvkAttachmentDesc_t* GetAttachment( uint32_t nIndex ) const
	{
		return ( nIndex < m_attachments.Count() ) ? &m_attachments[ nIndex ] : nullptr;
	}
	uint32_t GetSubpassCount() const { return m_subpasses.Count(); }
	const DxvkSubpassDesc_t* GetSubpass( uint32_t nIndex ) const
	{
		return ( nIndex < m_subpasses.Count() ) ? &m_subpasses[ nIndex ] : nullptr;
	}
	uint32_t GetDependencyCount() const { return m_dependencies.Count(); }

	bool IsValid() const { return m_pVkRenderPass != nullptr; }
	void* GetRenderPassHandle() const { return m_pVkRenderPass; }

	void GetDebugName( char* pOutBuf, uint32_t nBufSize ) const
	{
		if ( pOutBuf && nBufSize > 0 )
			Q_strncpy( pOutBuf, m_szDebugName, nBufSize );
	}

	// Compatibility check - can a framebuffer be used with this pass?
	bool IsCompatible( uint32_t nAttachmentCount,
					   const uint32_t* pAttachmentFormats,
					   uint32_t nSamples ) const;

	// Get clear values count expected by BeginRenderPass
	uint32_t GetExpectedClearValueCount() const
	{
		return GetAttachmentCount();
	}

private:
	bool ValidateAttachments();
	bool ValidateSubpasses();

	CDxvkAdapter* m_pAdapter;
	void* m_pVkRenderPass;

	char m_szDebugName[ 128 ];

	CUtlVector< DxvkAttachmentDesc_t > m_attachments;
	CUtlVector< DxvkSubpassDesc_t > m_subpasses;
	CUtlVector< DxvkSubpassDependency_t > m_dependencies;

	// Per-subpass flat reference storage
	CUtlVector< uint32_t > m_subpassReferenceStorage;
};

static CDxvkRenderPass s_DxvkRenderPass;

//-----------------------------------------------------------------------------
// CDxvkRenderPass Implementation
//-----------------------------------------------------------------------------
CDxvkRenderPass::CDxvkRenderPass() :
	m_pAdapter( nullptr ),
	m_pVkRenderPass( nullptr )
{
	memset( m_szDebugName, 0, sizeof(m_szDebugName) );
	m_attachments.RemoveAll();
	m_subpasses.RemoveAll();
	m_dependencies.RemoveAll();
	m_subpassReferenceStorage.RemoveAll();
}

CDxvkRenderPass::~CDxvkRenderPass()
{
	Destroy();
}

bool CDxvkRenderPass::Create( CDxvkAdapter* pAdapter,
							  uint32_t nAttachmentCount,
							  const DxvkAttachmentDesc_t* pAttachments,
							  uint32_t nSubpassCount,
							  const DxvkSubpassDesc_t* pSubpasses,
							  uint32_t nDependencyCount,
							  const DxvkSubpassDependency_t* pDependencies,
							  const char* pDebugName )
{
	if ( IsValid() ) Destroy();
	if ( !pAdapter ) return false;

	m_pAdapter = pAdapter;

	if ( pDebugName )
		Q_strncpy( m_szDebugName, pDebugName, sizeof(m_szDebugName) - 1 );

	m_attachments.EnsureCapacity( nAttachmentCount );
	for ( uint32_t i = 0; i < nAttachmentCount; ++i )
		m_attachments.AddToTail( pAttachments[ i ] );

	m_subpasses.EnsureCapacity( nSubpassCount );
	for ( uint32_t i = 0; i < nSubpassCount; ++i )
		m_subpasses.AddToTail( pSubpasses[ i ] );

	m_dependencies.EnsureCapacity( nDependencyCount );
	for ( uint32_t i = 0; i < nDependencyCount; ++i )
		m_dependencies.AddToTail( pDependencies[ i ] );

	if ( !ValidateAttachments() || !ValidateSubpasses() )
	{
		Destroy();
		return false;
	}

	return true;
}

void CDxvkRenderPass::Destroy()
{
	m_pVkRenderPass = nullptr;
	memset( m_szDebugName, 0, sizeof(m_szDebugName) );
	m_attachments.RemoveAll();
	m_subpasses.RemoveAll();
	m_dependencies.RemoveAll();
	m_subpassReferenceStorage.RemoveAll();
	m_pAdapter = nullptr;
}

bool CDxvkRenderPass::IsCompatible( uint32_t nAttachmentCount,
									const uint32_t* pAttachmentFormats,
									uint32_t nSamples ) const
{
	if ( nAttachmentCount != GetAttachmentCount() )
		return false;
	for ( uint32_t i = 0; i < nAttachmentCount; ++i )
	{
		const DxvkAttachmentDesc_t* pAtt = GetAttachment( i );
		if ( !pAtt ) return false;
		if ( pAtt->nFormat != pAttachmentFormats[ i ] )
			return false;
		if ( nSamples != 0 && pAtt->nSamples != nSamples )
			return false;
	}
	return true;
}

bool CDxvkRenderPass::ValidateAttachments()
{
	for ( int i = 0; i < m_attachments.Count(); ++i )
	{
		if ( m_attachments[ i ].nFormat == 0 )
			return false;
	}
	return true;
}

bool CDxvkRenderPass::ValidateSubpasses()
{
	for ( int i = 0; i < m_subpasses.Count(); ++i )
	{
		const DxvkSubpassDesc_t& sub = m_subpasses[ i ];
		if ( sub.nColorAttachmentCount > 0 && !sub.pColorAttachmentReferences )
			return false;
	}
	return true;
}
