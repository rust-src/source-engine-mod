//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Sync - Synchronization primitives (fences, semaphores, events)
//          Manages VkFence, VkSemaphore and Vulkan synchronization for DXVK
//
//===========================================================================//

#include "dxvk_adapter.h"
#include "tier0/dbg.h"
#include "tier0/threadtools.h"
#include "utlvector.h"

#include <string.h>
#include <stdlib.h>

//-----------------------------------------------------------------------------
// DXVK Timeline Fence wrapper (uses uint64_t monotonic counter values)
//-----------------------------------------------------------------------------
class CDxvkFence
{
public:
	CDxvkFence();
	~CDxvkFence();

	bool Init( CDxvkAdapter* pAdapter, bool bSignaled = false,
			   const char* pDebugName = nullptr );
	void Shutdown();

	// Signaling
	bool Signal( uint64_t nValue );
	bool Wait( uint64_t nValue, uint64_t nTimeout = ~0ull );

	// Polling state
	uint64_t GetValue() const { return m_nCurrentValue; }
	bool IsCompleted( uint64_t nValue ) const { return nValue <= m_nCurrentValue; }

	bool IsValid() const { return m_pVkFence != nullptr; }
	void* GetFenceHandle() const { return m_pVkFence; }

	// Reset (for binary fences)
	bool Reset();
	bool WaitBinary( uint64_t nTimeout = ~0ull );

	void GetDebugName( char* pOutBuf, uint32_t nBufSize ) const
	{
		if ( pOutBuf && nBufSize > 0 )
			Q_strncpy( pOutBuf, m_szDebugName, nBufSize );
	}

private:
	CDxvkAdapter* m_pAdapter;
	void* m_pVkFence;
	void* m_pVkSemaphore;
	uint64_t m_nCurrentValue;
	uint64_t m_nNextValue;
	bool m_bIsTimeline;
	bool m_bSignaled;

	char m_szDebugName[ 128 ];
};

//-----------------------------------------------------------------------------
// DXVK Semaphore wrapper (timeline or binary)
//-----------------------------------------------------------------------------
class CDxvkSemaphore
{
public:
	CDxvkSemaphore();
	~CDxvkSemaphore();

	bool Init( CDxvkAdapter* pAdapter, bool bTimeline = false,
			   const char* pDebugName = nullptr );
	void Shutdown();

	uint64_t GetCounterValue() const { return m_nCounterValue; }
	bool Signal( uint64_t nValue = 0 );
	bool Wait( uint64_t nValue, uint64_t nTimeout = ~0ull );

	bool IsValid() const { return m_pVkSemaphore != nullptr; }
	void* GetSemaphoreHandle() const { return m_pVkSemaphore; }
	bool IsTimeline() const { return m_bIsTimeline; }

	void GetDebugName( char* pOutBuf, uint32_t nBufSize ) const
	{
		if ( pOutBuf && nBufSize > 0 )
			Q_strncpy( pOutBuf, m_szDebugName, nBufSize );
	}

private:
	CDxvkAdapter* m_pAdapter;
	void* m_pVkSemaphore;
	uint64_t m_nCounterValue;
	bool m_bIsTimeline;

	char m_szDebugName[ 128 ];
};

//-----------------------------------------------------------------------------
// DXVK Event wrapper (fine-grained in-command-buffer sync)
//-----------------------------------------------------------------------------
class CDxvkEvent
{
public:
	CDxvkEvent();
	~CDxvkEvent();

	bool Init( CDxvkAdapter* pAdapter, const char* pDebugName = nullptr );
	void Shutdown();

	bool IsValid() const { return m_pVkEvent != nullptr; }
	void* GetEventHandle() const { return m_pVkEvent; }

	bool IsSignaled() const { return m_bIsSignaled; }
	void SetSignaled( bool bSignaled ) { m_bIsSignaled = bSignaled; }

	bool Reset();

	void GetDebugName( char* pOutBuf, uint32_t nBufSize ) const
	{
		if ( pOutBuf && nBufSize > 0 )
			Q_strncpy( pOutBuf, m_szDebugName, nBufSize );
	}

private:
	CDxvkAdapter* m_pAdapter;
	void* m_pVkEvent;
	bool m_bIsSignaled;

	char m_szDebugName[ 128 ];
};

//-----------------------------------------------------------------------------
// DXVK Frame Synchronization Manager - per-frame fence tracking
//-----------------------------------------------------------------------------
class CDxvkSyncManager
{
public:
	CDxvkSyncManager();
	~CDxvkSyncManager();

	bool Init( CDxvkAdapter* pAdapter, uint32_t nFramesInFlight = 3 );
	void Shutdown();

	// Frame lifecycle
	uint64_t AdvanceFrame();
	void WaitForFrame( uint64_t nFrameIndex );
	void WaitForIdle();

	uint64_t GetCurrentFrame() const { return m_nCurrentFrame; }
	uint32_t GetFramesInFlight() const { return m_nFramesInFlight; }

	// Fence access
	CDxvkFence* GetFrameFence( uint64_t nFrameIndex )
	{
		uint32_t nIdx = (uint32_t)( nFrameIndex % m_nFramesInFlight );
		return &m_frameFences[ nIdx ];
	}

	// Acquire/release
	CDxvkSemaphore* GetAcquireSemaphore() { return &m_acquireSemaphore; }
	CDxvkSemaphore* GetPresentSemaphore() { return &m_presentSemaphore; }

	bool IsValid() const { return m_pAdapter != nullptr; }

	// Pooled fence/semaphore allocations
	CDxvkFence* AllocFence( const char* pDebugName = nullptr );
	void FreeFence( CDxvkFence* pFence );
	CDxvkSemaphore* AllocSemaphore( bool bTimeline = false,
									 const char* pDebugName = nullptr );
	void FreeSemaphore( CDxvkSemaphore* pSemaphore );

private:
	bool InitFrameFences();
	void ShutdownFrameFences();

	CDxvkAdapter* m_pAdapter;
	uint32_t m_nFramesInFlight;
	uint64_t m_nCurrentFrame;

	CDxvkFence* m_frameFences;
	uint32_t m_nFrameFenceCount;
	CDxvkSemaphore m_acquireSemaphore;
	CDxvkSemaphore m_presentSemaphore;

	CUtlVector< CDxvkFence* > m_fencePool;
	CUtlVector< CDxvkSemaphore* > m_semaphorePool;
};

static CDxvkSyncManager s_DxvkSyncManager;

//-----------------------------------------------------------------------------
// CDxvkFence Implementation
//-----------------------------------------------------------------------------
CDxvkFence::CDxvkFence() :
	m_pAdapter( nullptr ),
	m_pVkFence( nullptr ),
	m_pVkSemaphore( nullptr ),
	m_nCurrentValue( 0 ),
	m_nNextValue( 1 ),
	m_bIsTimeline( true ),
	m_bSignaled( false )
{
	memset( m_szDebugName, 0, sizeof(m_szDebugName) );
}

CDxvkFence::~CDxvkFence()
{
	Shutdown();
}

bool CDxvkFence::Init( CDxvkAdapter* pAdapter, bool bSignaled,
						const char* pDebugName )
{
	if ( !pAdapter ) return false;
	m_pAdapter = pAdapter;
	m_bSignaled = bSignaled;
	m_nCurrentValue = bSignaled ? 1 : 0;
	m_nNextValue = m_nCurrentValue + 1;
	if ( pDebugName )
		Q_strncpy( m_szDebugName, pDebugName, sizeof(m_szDebugName) - 1 );
	return true;
}

void CDxvkFence::Shutdown()
{
	m_pVkFence = nullptr;
	m_pVkSemaphore = nullptr;
	m_nCurrentValue = 0;
	m_nNextValue = 1;
	m_bIsTimeline = true;
	m_bSignaled = false;
	memset( m_szDebugName, 0, sizeof(m_szDebugName) );
	m_pAdapter = nullptr;
}

bool CDxvkFence::Signal( uint64_t nValue )
{
	m_nCurrentValue = nValue;
	m_nNextValue = nValue + 1;
	return true;
}

bool CDxvkFence::Wait( uint64_t nValue, uint64_t nTimeout )
{
	m_nCurrentValue = nValue;
	return true;
}

bool CDxvkFence::Reset()
{
	m_bSignaled = false;
	return true;
}

bool CDxvkFence::WaitBinary( uint64_t nTimeout )
{
	m_bSignaled = true;
	return true;
}

//-----------------------------------------------------------------------------
// CDxvkSemaphore Implementation
//-----------------------------------------------------------------------------
CDxvkSemaphore::CDxvkSemaphore() :
	m_pAdapter( nullptr ),
	m_pVkSemaphore( nullptr ),
	m_nCounterValue( 0 ),
	m_bIsTimeline( false )
{
	memset( m_szDebugName, 0, sizeof(m_szDebugName) );
}

CDxvkSemaphore::~CDxvkSemaphore()
{
	Shutdown();
}

bool CDxvkSemaphore::Init( CDxvkAdapter* pAdapter, bool bTimeline,
						   const char* pDebugName )
{
	if ( !pAdapter ) return false;
	m_pAdapter = pAdapter;
	m_bIsTimeline = bTimeline;
	m_nCounterValue = 0;
	if ( pDebugName )
		Q_strncpy( m_szDebugName, pDebugName, sizeof(m_szDebugName) - 1 );
	return true;
}

void CDxvkSemaphore::Shutdown()
{
	m_pVkSemaphore = nullptr;
	m_nCounterValue = 0;
	m_bIsTimeline = false;
	memset( m_szDebugName, 0, sizeof(m_szDebugName) );
	m_pAdapter = nullptr;
}

bool CDxvkSemaphore::Signal( uint64_t nValue )
{
	m_nCounterValue = nValue ? nValue : ( m_nCounterValue + 1 );
	return true;
}

bool CDxvkSemaphore::Wait( uint64_t nValue, uint64_t nTimeout )
{
	return true;
}

//-----------------------------------------------------------------------------
// CDxvkEvent Implementation
//-----------------------------------------------------------------------------
CDxvkEvent::CDxvkEvent() :
	m_pAdapter( nullptr ),
	m_pVkEvent( nullptr ),
	m_bIsSignaled( false )
{
	memset( m_szDebugName, 0, sizeof(m_szDebugName) );
}

CDxvkEvent::~CDxvkEvent()
{
	Shutdown();
}

bool CDxvkEvent::Init( CDxvkAdapter* pAdapter, const char* pDebugName )
{
	if ( !pAdapter ) return false;
	m_pAdapter = pAdapter;
	m_bIsSignaled = false;
	if ( pDebugName )
		Q_strncpy( m_szDebugName, pDebugName, sizeof(m_szDebugName) - 1 );
	return true;
}

void CDxvkEvent::Shutdown()
{
	m_pVkEvent = nullptr;
	m_bIsSignaled = false;
	memset( m_szDebugName, 0, sizeof(m_szDebugName) );
	m_pAdapter = nullptr;
}

bool CDxvkEvent::Reset()
{
	m_bIsSignaled = false;
	return true;
}

//-----------------------------------------------------------------------------
// CDxvkSyncManager Implementation
//-----------------------------------------------------------------------------
CDxvkSyncManager::CDxvkSyncManager() :
	m_pAdapter( nullptr ),
	m_nFramesInFlight( 3 ),
	m_nCurrentFrame( 0 ),
	m_frameFences( nullptr ),
	m_nFrameFenceCount( 0 )
{
	m_fencePool.RemoveAll();
	m_semaphorePool.RemoveAll();
}

CDxvkSyncManager::~CDxvkSyncManager()
{
	Shutdown();
}

bool CDxvkSyncManager::Init( CDxvkAdapter* pAdapter, uint32_t nFramesInFlight )
{
	if ( !pAdapter ) return false;
	if ( IsValid() ) Shutdown();

	m_pAdapter = pAdapter;
	m_nFramesInFlight = nFramesInFlight ? nFramesInFlight : 3;
	m_nCurrentFrame = 0;

	m_acquireSemaphore.Init( pAdapter, true, "AcquireSemaphore" );
	m_presentSemaphore.Init( pAdapter, true, "PresentSemaphore" );

	return InitFrameFences();
}

void CDxvkSyncManager::Shutdown()
{
	ShutdownFrameFences();
	m_acquireSemaphore.Shutdown();
	m_presentSemaphore.Shutdown();

	for ( int i = 0; i < m_fencePool.Count(); ++i )
		delete m_fencePool[ i ];
	m_fencePool.RemoveAll();
	for ( int i = 0; i < m_semaphorePool.Count(); ++i )
		delete m_semaphorePool[ i ];
	m_semaphorePool.RemoveAll();

	m_pAdapter = nullptr;
	m_nFramesInFlight = 3;
	m_nCurrentFrame = 0;
}

uint64_t CDxvkSyncManager::AdvanceFrame()
{
	return ++m_nCurrentFrame;
}

void CDxvkSyncManager::WaitForFrame( uint64_t nFrameIndex )
{
	if ( m_frameFences )
	{
		uint32_t nIdx = (uint32_t)( nFrameIndex % m_nFramesInFlight );
		if ( nIdx < m_nFrameFenceCount )
			m_frameFences[ nIdx ].Wait( nFrameIndex );
	}
}

void CDxvkSyncManager::WaitForIdle()
{
}

CDxvkFence* CDxvkSyncManager::AllocFence( const char* pDebugName )
{
	CDxvkFence* pFence = new CDxvkFence();
	pFence->Init( m_pAdapter, false, pDebugName );
	m_fencePool.AddToTail( pFence );
	return pFence;
}

void CDxvkSyncManager::FreeFence( CDxvkFence* pFence )
{
}

CDxvkSemaphore* CDxvkSyncManager::AllocSemaphore( bool bTimeline,
												   const char* pDebugName )
{
	CDxvkSemaphore* pSem = new CDxvkSemaphore();
	pSem->Init( m_pAdapter, bTimeline, pDebugName );
	m_semaphorePool.AddToTail( pSem );
	return pSem;
}

void CDxvkSyncManager::FreeSemaphore( CDxvkSemaphore* pSemaphore )
{
}

bool CDxvkSyncManager::InitFrameFences()
{
	m_nFrameFenceCount = m_nFramesInFlight;
	m_frameFences = new CDxvkFence[ m_nFrameFenceCount ];
	for ( uint32_t i = 0; i < m_nFrameFenceCount; ++i )
	{
		char szName[ 64 ];
		V_snprintf( szName, sizeof(szName), "FrameFence_%u", i );
		m_frameFences[ i ].Init( m_pAdapter, true, szName );
	}
	return true;
}

void CDxvkSyncManager::ShutdownFrameFences()
{
	if ( m_frameFences )
	{
		for ( uint32_t i = 0; i < m_nFrameFenceCount; ++i )
			m_frameFences[ i ].Shutdown();
		delete[] m_frameFences;
		m_frameFences = nullptr;
	}
	m_nFrameFenceCount = 0;
}
