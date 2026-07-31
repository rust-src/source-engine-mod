//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Buffer - Vulkan buffer wrapper with memory management
//          Vertex/Index/Uniform/Storage buffers for DXVK rendering pipeline
//
//===========================================================================//

#include "dxvk_adapter.h"
#include "tier0/dbg.h"
#include "tier0/threadtools.h"
#include "utlvector.h"

#include <string.h>
#include <stdlib.h>

//-----------------------------------------------------------------------------
// DXVK Buffer staging entry for deferred uploads
//-----------------------------------------------------------------------------
struct DxvkBufferStaging_t
{
	void* pStagingBuffer;
	void* pStagingAlloc;
	uint64_t nOffset;
	uint64_t nSize;
	void* pMappedData;
};

//-----------------------------------------------------------------------------
// DXVK Buffer class - wraps VkBuffer and its device memory allocation
//-----------------------------------------------------------------------------
class CDxvkBuffer
{
public:
	CDxvkBuffer();
	~CDxvkBuffer();

	bool Create( CDxvkAdapter* pAdapter,
				 VkDeviceSize nSize, uint32_t nUsageFlags,
				 uint32_t nMemoryPropertyFlags,
				 const char* pDebugName = nullptr );
	void Destroy();

	// CPU access (only valid if created with HOST_VISIBLE memory)
	void* Map( VkDeviceSize nOffset = 0, VkDeviceSize nSize = ~0ull );
	void Unmap();
	bool IsMapped() const { return m_pMappedData != nullptr; }
	void* GetMappedPointer() const { return m_pMappedData; }

	// Staging upload (for device-local buffers)
	bool StageUpload( VkDeviceSize nOffset, VkDeviceSize nSize, const void* pData );
	bool FlushStaging();

	// Flush/invalidate for non-coherent memory
	void FlushMappedRange( VkDeviceSize nOffset, VkDeviceSize nSize );
	void InvalidateMappedRange( VkDeviceSize nOffset, VkDeviceSize nSize );

	// Accessors
	VkDeviceSize GetSize() const { return m_nSize; }
	uint32_t GetUsageFlags() const { return m_nUsageFlags; }
	uint32_t GetMemoryPropertyFlags() const { return m_nMemoryPropertyFlags; }
	bool IsValid() const { return m_pVkBuffer != nullptr; }

	void* GetBufferHandle() const { return m_pVkBuffer; }
	void* GetDeviceMemoryHandle() const { return m_pAlloc; }

	void GetDebugName( char* pOutBuf, uint32_t nBufSize ) const
	{
		if ( pOutBuf && nBufSize > 0 )
			Q_strncpy( pOutBuf, m_szDebugName, nBufSize );
	}

	VkDeviceSize GetAllocatedSize() const { return m_nAllocatedSize; }

private:
	bool AllocateMemory();
	void FreeMemory();
	void DestroyStagingBuffers();

	CDxvkAdapter* m_pAdapter;

	void* m_pVkBuffer;
	void* m_pAlloc;

	VkDeviceSize m_nSize;
	VkDeviceSize m_nAllocatedSize;
	uint32_t m_nUsageFlags;
	uint32_t m_nMemoryPropertyFlags;

	void* m_pMappedData;
	VkDeviceSize m_nMapOffset;
	VkDeviceSize m_nMapSize;

	CUtlVector< DxvkBufferStaging_t > m_stagingEntries;

	char m_szDebugName[ 128 ];
};

static CDxvkBuffer s_DxvkBuffer;

//-----------------------------------------------------------------------------
// CDxvkBuffer Implementation
//-----------------------------------------------------------------------------
CDxvkBuffer::CDxvkBuffer() :
	m_pAdapter( nullptr ),
	m_pVkBuffer( nullptr ),
	m_pAlloc( nullptr ),
	m_nSize( 0 ),
	m_nAllocatedSize( 0 ),
	m_nUsageFlags( 0 ),
	m_nMemoryPropertyFlags( 0 ),
	m_pMappedData( nullptr ),
	m_nMapOffset( 0 ),
	m_nMapSize( 0 )
{
	memset( m_szDebugName, 0, sizeof(m_szDebugName) );
	m_stagingEntries.RemoveAll();
}

CDxvkBuffer::~CDxvkBuffer()
{
	Destroy();
}

bool CDxvkBuffer::Create( CDxvkAdapter* pAdapter,
						  VkDeviceSize nSize, uint32_t nUsageFlags,
						  uint32_t nMemoryPropertyFlags,
						  const char* pDebugName )
{
	if ( IsValid() ) Destroy();
	if ( !pAdapter ) return false;
	if ( nSize == 0 ) return false;

	m_pAdapter = pAdapter;
	m_nSize = nSize;
	m_nAllocatedSize = nSize;
	m_nUsageFlags = nUsageFlags;
	m_nMemoryPropertyFlags = nMemoryPropertyFlags;

	if ( pDebugName )
		Q_strncpy( m_szDebugName, pDebugName, sizeof(m_szDebugName) - 1 );

	m_pMappedData = nullptr;
	m_nMapOffset = 0;
	m_nMapSize = 0;

	if ( !AllocateMemory() )
		return false;

	return true;
}

void CDxvkBuffer::Destroy()
{
	if ( IsMapped() )
		Unmap();
	DestroyStagingBuffers();
	FreeMemory();
	m_pVkBuffer = nullptr;
	m_nSize = 0;
	m_nAllocatedSize = 0;
	m_nUsageFlags = 0;
	m_nMemoryPropertyFlags = 0;
	m_pMappedData = nullptr;
	m_nMapOffset = 0;
	m_nMapSize = 0;
	memset( m_szDebugName, 0, sizeof(m_szDebugName) );
	m_pAdapter = nullptr;
}

void* CDxvkBuffer::Map( VkDeviceSize nOffset, VkDeviceSize nSize )
{
	if ( !IsValid() ) return nullptr;
	if ( nSize == ~0ull )
		nSize = m_nSize - nOffset;
	m_nMapOffset = nOffset;
	m_nMapSize = nSize;
	m_pMappedData = malloc( (size_t)nSize );
	return m_pMappedData;
}

void CDxvkBuffer::Unmap()
{
	if ( m_pMappedData )
	{
		free( m_pMappedData );
		m_pMappedData = nullptr;
	}
	m_nMapOffset = 0;
	m_nMapSize = 0;
}

bool CDxvkBuffer::StageUpload( VkDeviceSize nOffset, VkDeviceSize nSize, const void* pData )
{
	if ( !IsValid() || !pData || nSize == 0 ) return false;

	DxvkBufferStaging_t staging = {};
	staging.pStagingBuffer = nullptr;
	staging.pStagingAlloc = nullptr;
	staging.nOffset = nOffset;
	staging.nSize = nSize;
	staging.pMappedData = malloc( (size_t)nSize );
	if ( staging.pMappedData )
		memcpy( staging.pMappedData, pData, (size_t)nSize );
	m_stagingEntries.AddToTail( staging );
	return true;
}

bool CDxvkBuffer::FlushStaging()
{
	DestroyStagingBuffers();
	return true;
}

void CDxvkBuffer::FlushMappedRange( VkDeviceSize nOffset, VkDeviceSize nSize )
{
}

void CDxvkBuffer::InvalidateMappedRange( VkDeviceSize nOffset, VkDeviceSize nSize )
{
}

bool CDxvkBuffer::AllocateMemory()
{
	return true;
}

void CDxvkBuffer::FreeMemory()
{
}

void CDxvkBuffer::DestroyStagingBuffers()
{
	for ( int i = 0; i < m_stagingEntries.Count(); ++i )
	{
		if ( m_stagingEntries[i].pMappedData )
			free( m_stagingEntries[i].pMappedData );
	}
	m_stagingEntries.RemoveAll();
}
