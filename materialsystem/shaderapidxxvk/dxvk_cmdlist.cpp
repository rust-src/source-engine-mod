//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Command List - Command buffer pool + command buffer wrapper
//          Manages command pool lifecycle and per-frame command buffer allocation
//
//===========================================================================//

#include "dxvk_adapter.h"
#include "tier0/dbg.h"
#include "tier0/threadtools.h"
#include "utlvector.h"

#include <string.h>
#include <stdlib.h>

//-----------------------------------------------------------------------------
// DXVK Command buffer entry with tracking state
//-----------------------------------------------------------------------------
struct DxvkCmdBufferEntry_t
{
	VkCommandBuffer cmdBuffer;
	void* pPool;
	uint32_t nQueueFamily;
	uint32_t nLevel;
	uint32_t nUsageFlags;
	bool bIsRecording;
	bool bIsPending;
	uint64_t nFenceValue;
};

//-----------------------------------------------------------------------------
// DXVK Command Pool per-queue-family manager
//-----------------------------------------------------------------------------
class CDxvkCmdPool
{
public:
	CDxvkCmdPool();
	~CDxvkCmdPool();

	bool Init( CDxvkAdapter* pAdapter, uint32_t nQueueFamilyIndex,
			   uint32_t nFlags = 0, bool bTransient = false );
	void Shutdown();

	VkCommandBuffer Alloc( uint32_t nLevel = 0, const char* pDebugName = nullptr );
	void Free( VkCommandBuffer cmdBuf );
	void FreeAll();

	bool Reset( uint32_t nFlags = 0 );
	void Trim();

	uint32_t GetQueueFamily() const { return m_nQueueFamily; }
	bool IsValid() const { return m_pVkCmdPool != nullptr; }
	void* GetPoolHandle() const { return m_pVkCmdPool; }

	uint32_t GetAllocatedCount() const { return m_allocatedBuffers.Count(); }
	uint32_t GetPendingCount() const { return m_nPendingCount; }

private:
	CDxvkAdapter* m_pAdapter;
	void* m_pVkCmdPool;
	uint32_t m_nQueueFamily;
	uint32_t m_nFlags;
	uint32_t m_nPendingCount;

	CUtlVector< DxvkCmdBufferEntry_t > m_allocatedBuffers;
};

static CDxvkCmdPool s_DxvkGraphicsCmdPool;
static CDxvkCmdPool s_DxvkComputeCmdPool;
static CDxvkCmdPool s_DxvkTransferCmdPool;

//-----------------------------------------------------------------------------
// DXVK Command List Manager - pools command buffers for rendering threads
//-----------------------------------------------------------------------------
class CDxvkCmdListManager
{
public:
	CDxvkCmdListManager();
	~CDxvkCmdListManager();

	bool Init( CDxvkAdapter* pAdapter, uint32_t nFramesInFlight = 3 );
	void Shutdown();

	// Per-frame command buffers
	VkCommandBuffer BeginGraphicsCommandBuffer( const char* pDebugName = nullptr );
	void EndGraphicsCommandBuffer( VkCommandBuffer cmdBuf );
	VkCommandBuffer BeginComputeCommandBuffer( const char* pDebugName = nullptr );
	void EndComputeCommandBuffer( VkCommandBuffer cmdBuf );
	VkCommandBuffer BeginTransferCommandBuffer( const char* pDebugName = nullptr );
	void EndTransferCommandBuffer( VkCommandBuffer cmdBuf );

	// Secondary / inline command buffers
	VkCommandBuffer AllocSecondary( uint32_t nQueueFamily = ~0u,
									const char* pDebugName = nullptr );
	void FreeSecondary( VkCommandBuffer cmdBuf, uint32_t nQueueFamily = ~0u );

	// Frame lifecycle
	void BeginFrame( uint64_t nFrameIndex );
	void EndFrame( uint64_t nFrameIndex );
	void WaitForAllFrames();

	uint32_t GetFramesInFlight() const { return m_nFramesInFlight; }
	uint64_t GetCurrentFrame() const { return m_nCurrentFrame; }

	bool IsValid() const { return m_pAdapter != nullptr; }

	void* GetGraphicsPoolHandle() const { return s_DxvkGraphicsCmdPool.GetPoolHandle(); }
	void* GetComputePoolHandle() const { return s_DxvkComputeCmdPool.GetPoolHandle(); }
	void* GetTransferPoolHandle() const { return s_DxvkTransferCmdPool.GetPoolHandle(); }

private:
	VkCommandBuffer BeginCommandBuffer( CDxvkCmdPool& pool,
										uint32_t nLevel,
										const char* pDebugName );
	void EndCommandBuffer( VkCommandBuffer cmdBuf, CDxvkCmdPool& pool );

	CDxvkAdapter* m_pAdapter;
	uint32_t m_nFramesInFlight;
	uint64_t m_nCurrentFrame;

	CUtlVector< VkCommandBuffer > m_frameGraphicsBuffers;
	CUtlVector< VkCommandBuffer > m_frameComputeBuffers;
	CUtlVector< VkCommandBuffer > m_frameTransferBuffers;
};

static CDxvkCmdListManager s_DxvkCmdListManager;

//-----------------------------------------------------------------------------
// CDxvkCmdPool Implementation
//-----------------------------------------------------------------------------
CDxvkCmdPool::CDxvkCmdPool() :
	m_pAdapter( nullptr ),
	m_pVkCmdPool( nullptr ),
	m_nQueueFamily( ~0u ),
	m_nFlags( 0 ),
	m_nPendingCount( 0 )
{
	m_allocatedBuffers.RemoveAll();
}

CDxvkCmdPool::~CDxvkCmdPool()
{
	Shutdown();
}

bool CDxvkCmdPool::Init( CDxvkAdapter* pAdapter, uint32_t nQueueFamilyIndex,
						 uint32_t nFlags, bool bTransient )
{
	if ( !pAdapter ) return false;
	if ( IsValid() ) Shutdown();

	m_pAdapter = pAdapter;
	m_nQueueFamily = nQueueFamilyIndex;
	m_nFlags = nFlags;
	m_nPendingCount = 0;
	m_allocatedBuffers.RemoveAll();

	return true;
}

void CDxvkCmdPool::Shutdown()
{
	FreeAll();
	m_pVkCmdPool = nullptr;
	m_nQueueFamily = ~0u;
	m_nFlags = 0;
	m_nPendingCount = 0;
	m_allocatedBuffers.RemoveAll();
	m_pAdapter = nullptr;
}

VkCommandBuffer CDxvkCmdPool::Alloc( uint32_t nLevel, const char* pDebugName )
{
	if ( !IsValid() ) return (VkCommandBuffer)VK_NULL_HANDLE;

	DxvkCmdBufferEntry_t entry = {};
	entry.cmdBuffer = (VkCommandBuffer)( (uintptr_t)( m_allocatedBuffers.Count() + 1 ) << 8 | nLevel );
	entry.pPool = m_pVkCmdPool;
	entry.nQueueFamily = m_nQueueFamily;
	entry.nLevel = nLevel;
	entry.nUsageFlags = 0;
	entry.bIsRecording = ( nLevel == 0 );
	entry.bIsPending = false;
	entry.nFenceValue = 0;
	m_allocatedBuffers.AddToTail( entry );

	return entry.cmdBuffer;
}

void CDxvkCmdPool::Free( VkCommandBuffer cmdBuf )
{
	for ( int i = 0; i < m_allocatedBuffers.Count(); ++i )
	{
		if ( m_allocatedBuffers[ i ].cmdBuffer == cmdBuf )
		{
			m_allocatedBuffers.Remove( i );
			return;
		}
	}
}

void CDxvkCmdPool::FreeAll()
{
	m_allocatedBuffers.RemoveAll();
	m_nPendingCount = 0;
}

bool CDxvkCmdPool::Reset( uint32_t nFlags )
{
	m_nPendingCount = 0;
	for ( int i = 0; i < m_allocatedBuffers.Count(); ++i )
	{
		m_allocatedBuffers[ i ].bIsRecording = false;
		m_allocatedBuffers[ i ].bIsPending = false;
		m_allocatedBuffers[ i ].nFenceValue = 0;
	}
	return true;
}

void CDxvkCmdPool::Trim()
{
}

//-----------------------------------------------------------------------------
// CDxvkCmdListManager Implementation
//-----------------------------------------------------------------------------
CDxvkCmdListManager::CDxvkCmdListManager() :
	m_pAdapter( nullptr ),
	m_nFramesInFlight( 3 ),
	m_nCurrentFrame( 0 )
{
	m_frameGraphicsBuffers.RemoveAll();
	m_frameComputeBuffers.RemoveAll();
	m_frameTransferBuffers.RemoveAll();
}

CDxvkCmdListManager::~CDxvkCmdListManager()
{
	Shutdown();
}

bool CDxvkCmdListManager::Init( CDxvkAdapter* pAdapter, uint32_t nFramesInFlight )
{
	if ( !pAdapter ) return false;
	if ( IsValid() ) Shutdown();

	m_pAdapter = pAdapter;
	m_nFramesInFlight = nFramesInFlight ? nFramesInFlight : 3;
	m_nCurrentFrame = 0;

	uint32_t nGfxQueue = pAdapter->GetGraphicsQueueFamily();
	uint32_t nPrQueue  = pAdapter->GetPresentQueueFamily();

	s_DxvkGraphicsCmdPool.Init( pAdapter, nGfxQueue, 0 );
	s_DxvkComputeCmdPool.Init( pAdapter, nGfxQueue, 0 );
	s_DxvkTransferCmdPool.Init( pAdapter, nPrQueue, 0 );

	m_frameGraphicsBuffers.RemoveAll();
	m_frameComputeBuffers.RemoveAll();
	m_frameTransferBuffers.RemoveAll();
	return true;
}

void CDxvkCmdListManager::Shutdown()
{
	s_DxvkGraphicsCmdPool.Shutdown();
	s_DxvkComputeCmdPool.Shutdown();
	s_DxvkTransferCmdPool.Shutdown();
	m_pAdapter = nullptr;
	m_nFramesInFlight = 3;
	m_nCurrentFrame = 0;
	m_frameGraphicsBuffers.RemoveAll();
	m_frameComputeBuffers.RemoveAll();
	m_frameTransferBuffers.RemoveAll();
}

VkCommandBuffer CDxvkCmdListManager::BeginCommandBuffer( CDxvkCmdPool& pool,
														 uint32_t nLevel,
														 const char* pDebugName )
{
	if ( !IsValid() ) return (VkCommandBuffer)VK_NULL_HANDLE;
	return pool.Alloc( nLevel, pDebugName );
}

void CDxvkCmdListManager::EndCommandBuffer( VkCommandBuffer cmdBuf, CDxvkCmdPool& pool )
{
}

VkCommandBuffer CDxvkCmdListManager::BeginGraphicsCommandBuffer( const char* pDebugName )
{
	VkCommandBuffer cb = BeginCommandBuffer( s_DxvkGraphicsCmdPool, 0, pDebugName );
	if ( cb != (VkCommandBuffer)VK_NULL_HANDLE )
		m_frameGraphicsBuffers.AddToTail( cb );
	return cb;
}

void CDxvkCmdListManager::EndGraphicsCommandBuffer( VkCommandBuffer cmdBuf )
{
	EndCommandBuffer( cmdBuf, s_DxvkGraphicsCmdPool );
}

VkCommandBuffer CDxvkCmdListManager::BeginComputeCommandBuffer( const char* pDebugName )
{
	VkCommandBuffer cb = BeginCommandBuffer( s_DxvkComputeCmdPool, 0, pDebugName );
	if ( cb != (VkCommandBuffer)VK_NULL_HANDLE )
		m_frameComputeBuffers.AddToTail( cb );
	return cb;
}

void CDxvkCmdListManager::EndComputeCommandBuffer( VkCommandBuffer cmdBuf )
{
	EndCommandBuffer( cmdBuf, s_DxvkComputeCmdPool );
}

VkCommandBuffer CDxvkCmdListManager::BeginTransferCommandBuffer( const char* pDebugName )
{
	VkCommandBuffer cb = BeginCommandBuffer( s_DxvkTransferCmdPool, 0, pDebugName );
	if ( cb != (VkCommandBuffer)VK_NULL_HANDLE )
		m_frameTransferBuffers.AddToTail( cb );
	return cb;
}

void CDxvkCmdListManager::EndTransferCommandBuffer( VkCommandBuffer cmdBuf )
{
	EndCommandBuffer( cmdBuf, s_DxvkTransferCmdPool );
}

VkCommandBuffer CDxvkCmdListManager::AllocSecondary( uint32_t nQueueFamily,
													 const char* pDebugName )
{
	CDxvkCmdPool& pool = ( nQueueFamily == ~0u || nQueueFamily == s_DxvkGraphicsCmdPool.GetQueueFamily() )
		? s_DxvkGraphicsCmdPool : s_DxvkTransferCmdPool;
	return pool.Alloc( 1, pDebugName );
}

void CDxvkCmdListManager::FreeSecondary( VkCommandBuffer cmdBuf, uint32_t nQueueFamily )
{
	CDxvkCmdPool& pool = ( nQueueFamily == ~0u || nQueueFamily == s_DxvkGraphicsCmdPool.GetQueueFamily() )
		? s_DxvkGraphicsCmdPool : s_DxvkTransferCmdPool;
	pool.Free( cmdBuf );
}

void CDxvkCmdListManager::BeginFrame( uint64_t nFrameIndex )
{
	m_nCurrentFrame = nFrameIndex;
	s_DxvkGraphicsCmdPool.Reset();
	s_DxvkComputeCmdPool.Reset();
	s_DxvkTransferCmdPool.Reset();
	m_frameGraphicsBuffers.RemoveAll();
	m_frameComputeBuffers.RemoveAll();
	m_frameTransferBuffers.RemoveAll();
}

void CDxvkCmdListManager::EndFrame( uint64_t nFrameIndex )
{
}

void CDxvkCmdListManager::WaitForAllFrames()
{
}
