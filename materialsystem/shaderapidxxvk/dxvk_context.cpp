//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Context - Immediate-mode rendering context
//          Records and submits Vulkan command buffers for draw/dispatch ops
//
//===========================================================================//

#include "dxvk_adapter.h"
#include "tier0/dbg.h"
#include "tier0/threadtools.h"
#include "utlvector.h"

#include <string.h>
#include <stdlib.h>

//-----------------------------------------------------------------------------
// DXVK Context - state-tracking command buffer recorder
//-----------------------------------------------------------------------------
class CDxvkContext
{
public:
	CDxvkContext();
	~CDxvkContext();

	bool Init( CDxvkAdapter* pAdapter );
	void Shutdown();

	void BeginRecording( VkCommandBuffer cmdBuf );
	void EndRecording();
	void FlushCommands();

	// Draw calls
	void Draw( uint32_t nVertexCount, uint32_t nInstanceCount,
			   uint32_t nFirstVertex, uint32_t nFirstInstance );
	void DrawIndexed( uint32_t nIndexCount, uint32_t nInstanceCount,
					  uint32_t nFirstIndex, int32_t nVertexOffset,
					  uint32_t nFirstInstance );

	// Compute dispatch
	void Dispatch( uint32_t nGroupCountX, uint32_t nGroupCountY,
				   uint32_t nGroupCountZ );

	// State binding
	void BindIndexBuffer( void* pBuffer, VkDeviceSize offset, uint32_t indexType );
	void BindVertexBuffers( uint32_t nFirstBinding, uint32_t nBindingCount,
							void* const* pBuffers, const VkDeviceSize* pOffsets,
							const uint32_t* pStrides );
	void BindViewport( uint32_t nFirstViewport, uint32_t nViewportCount,
					   const float* pX, const float* pY,
					   const float* pWidth, const float* pHeight,
					   const float* pMinDepth, const float* pMaxDepth );
	void BindScissor( uint32_t nFirstScissor, uint32_t nScissorCount,
					  const int32_t* pX, const int32_t* pY,
					  const uint32_t* pWidth, const uint32_t* pHeight );

	// Barriers
	void InsertMemoryBarrier( uint32_t nSrcStageMask, uint32_t nDstStageMask,
							  uint32_t nDependencyFlags );
	void InsertImageBarrier( void* pImage, uint32_t nAspectMask,
							 uint32_t nOldLayout, uint32_t nNewLayout,
							 uint32_t nSrcAccessMask, uint32_t nDstAccessMask,
							 uint32_t nSrcQueueFamily, uint32_t nDstQueueFamily,
							 uint32_t nBaseMipLevel, uint32_t nMipLevels,
							 uint32_t nBaseArrayLayer, uint32_t nLayerCount );

	bool IsRecording() const { return m_bRecording; }
	VkCommandBuffer GetCmdBuffer() const { return m_currentCmdBuf; }

private:
	bool AllocDescriptorPool();
	void FreeDescriptorPool();
	void ResetGraphicsPipelineState();

	CDxvkAdapter* m_pAdapter;
	VkCommandBuffer m_currentCmdBuf;
	bool m_bRecording;
	bool m_bPipelineDirty;

	// Cached binding state
	void* m_pBoundIndexBuffer;
	VkDeviceSize m_indexBufferOffset;
	uint32_t m_indexType;

	// Frame tracking
	uint32_t m_nCurrentFrame;
	uint32_t m_nDrawCallCount;
	uint32_t m_nDispatchCount;
};

static CDxvkContext s_DxvkContext;

//-----------------------------------------------------------------------------
// CDxvkContext Implementation
//-----------------------------------------------------------------------------
CDxvkContext::CDxvkContext() :
	m_pAdapter( nullptr ),
	m_currentCmdBuf( (VkCommandBuffer)VK_NULL_HANDLE ),
	m_bRecording( false ),
	m_bPipelineDirty( true ),
	m_pBoundIndexBuffer( nullptr ),
	m_indexBufferOffset( 0 ),
	m_indexType( 0 ),
	m_nCurrentFrame( 0 ),
	m_nDrawCallCount( 0 ),
	m_nDispatchCount( 0 )
{
}

CDxvkContext::~CDxvkContext()
{
	Shutdown();
}

bool CDxvkContext::Init( CDxvkAdapter* pAdapter )
{
	if ( !pAdapter ) return false;
	m_pAdapter = pAdapter;
	ResetGraphicsPipelineState();
	m_nCurrentFrame = 0;
	m_nDrawCallCount = 0;
	m_nDispatchCount = 0;
	AllocDescriptorPool();
	return true;
}

void CDxvkContext::Shutdown()
{
	FreeDescriptorPool();
	m_pAdapter = nullptr;
	m_currentCmdBuf = (VkCommandBuffer)VK_NULL_HANDLE;
	m_bRecording = false;
	m_bPipelineDirty = true;
	m_pBoundIndexBuffer = nullptr;
	m_indexBufferOffset = 0;
	m_indexType = 0;
	m_nCurrentFrame = 0;
	m_nDrawCallCount = 0;
	m_nDispatchCount = 0;
}

void CDxvkContext::BeginRecording( VkCommandBuffer cmdBuf )
{
	m_currentCmdBuf = cmdBuf;
	m_bRecording = true;
	m_bPipelineDirty = true;
}

void CDxvkContext::EndRecording()
{
	m_bRecording = false;
	m_currentCmdBuf = (VkCommandBuffer)VK_NULL_HANDLE;
}

void CDxvkContext::FlushCommands()
{
	m_nCurrentFrame++;
	m_bPipelineDirty = true;
}

void CDxvkContext::Draw( uint32_t nVertexCount, uint32_t nInstanceCount,
						 uint32_t nFirstVertex, uint32_t nFirstInstance )
{
	if ( !m_bRecording ) return;
	m_nDrawCallCount++;
}

void CDxvkContext::DrawIndexed( uint32_t nIndexCount, uint32_t nInstanceCount,
								uint32_t nFirstIndex, int32_t nVertexOffset,
								uint32_t nFirstInstance )
{
	if ( !m_bRecording ) return;
	m_nDrawCallCount++;
}

void CDxvkContext::Dispatch( uint32_t nGroupCountX, uint32_t nGroupCountY,
							 uint32_t nGroupCountZ )
{
	if ( !m_bRecording ) return;
	m_nDispatchCount++;
}

void CDxvkContext::BindIndexBuffer( void* pBuffer, VkDeviceSize offset, uint32_t indexType )
{
	m_pBoundIndexBuffer = pBuffer;
	m_indexBufferOffset = offset;
	m_indexType = indexType;
}

void CDxvkContext::BindVertexBuffers( uint32_t nFirstBinding, uint32_t nBindingCount,
									  void* const* pBuffers, const VkDeviceSize* pOffsets,
									  const uint32_t* pStrides )
{
}

void CDxvkContext::BindViewport( uint32_t nFirstViewport, uint32_t nViewportCount,
								 const float* pX, const float* pY,
								 const float* pWidth, const float* pHeight,
								 const float* pMinDepth, const float* pMaxDepth )
{
}

void CDxvkContext::BindScissor( uint32_t nFirstScissor, uint32_t nScissorCount,
								const int32_t* pX, const int32_t* pY,
								const uint32_t* pWidth, const uint32_t* pHeight )
{
}

void CDxvkContext::InsertMemoryBarrier( uint32_t nSrcStageMask, uint32_t nDstStageMask,
										uint32_t nDependencyFlags )
{
}

void CDxvkContext::InsertImageBarrier( void* pImage, uint32_t nAspectMask,
									   uint32_t nOldLayout, uint32_t nNewLayout,
									   uint32_t nSrcAccessMask, uint32_t nDstAccessMask,
									   uint32_t nSrcQueueFamily, uint32_t nDstQueueFamily,
									   uint32_t nBaseMipLevel, uint32_t nMipLevels,
									   uint32_t nBaseArrayLayer, uint32_t nLayerCount )
{
}

bool CDxvkContext::AllocDescriptorPool()
{
	return true;
}

void CDxvkContext::FreeDescriptorPool()
{
}

void CDxvkContext::ResetGraphicsPipelineState()
{
	m_pBoundIndexBuffer = nullptr;
	m_indexBufferOffset = 0;
	m_indexType = 0;
	m_bPipelineDirty = true;
}
