//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Memory - Device memory allocator and sub-allocator
//          Implements slab/buddy allocator on top of VkDeviceMemory blocks
//
//===========================================================================//

#include "dxvk_adapter.h"
#include "tier0/dbg.h"
#include "tier0/threadtools.h"
#include "utlvector.h"
#include "utlmap.h"

#include <string.h>
#include <stdlib.h>

//-----------------------------------------------------------------------------
// DXVK Memory Heap properties (Source engine memory allocation entry
//-----------------------------------------------------------------------------
struct DxvkMemoryHeapInfo_t
{
	uint32_t nMemoryTypeIndex;
	uint32_t nPropertyFlags;
	uint32_t nHeapIndex;
	VkDeviceSize nHeapSize;
	VkDeviceSize nAllocatedSize;
	uint32_t nAllocationCount;
};

//-----------------------------------------------------------------------------
// DXVK Memory Sub-allocation (slice of a larger VkDeviceMemory block)
//-----------------------------------------------------------------------------
struct DxvkMemorySuballocation_t
{
	void* pDeviceMemory;
	VkDeviceSize nOffset;
	VkDeviceSize nSize;
	uint32_t nMemoryType;
	uint32_t nMapCount;
	void* pMappedPtr;
	uint32_t nFlags;
	uint64_t nAllocationId;
};

//-----------------------------------------------------------------------------
// DXVK Memory Block (single VkDeviceMemory with internal free list)
//-----------------------------------------------------------------------------
class CDxvkMemoryBlock
{
public:
	struct FreeRegion
	{
		VkDeviceSize nOffset;
		VkDeviceSize nSize;
		FreeRegion* pNext;
	};

	CDxvkMemoryBlock();
	~CDxvkMemoryBlock();

	bool Init( CDxvkAdapter* pAdapter,
			   VkDeviceSize nBlockSize, uint32_t nMemoryTypeIndex,
			   uint32_t nPropertyFlags, const char* pDebugName );
	void Shutdown();

	// Sub-allocation
	bool Alloc( VkDeviceSize nSize, VkDeviceSize nAlignment,
				DxvkMemorySuballocation_t* pOutAlloc );
	void Free( const DxvkMemorySuballocation_t& alloc );

	// Stats
	VkDeviceSize GetBlockSize() const { return m_nBlockSize; }
	VkDeviceSize GetUsedSize() const { return m_nUsedSize; }
	uint32_t GetAllocCount() const { return m_nAllocCount; }
	float GetUtilization() const
	{
		return m_nBlockSize ? (float)m_nUsedSize / (float)m_nBlockSize;
	}
	uint32_t GetMemoryType() const { return m_nMemoryType; }
	uint32_t GetPropertyFlags() const { return m_nPropertyFlags; }

	bool IsValid() const { return m_pDeviceMemory != nullptr; }
	void* GetDeviceMemoryHandle() const { return m_pDeviceMemory; }

	void* Map( VkDeviceSize nOffset, VkDeviceSize nSize );
	void Unmap();
	bool IsMapped() const { return m_pMappedBase != nullptr; }
	void* GetMappedBase() const { return m_pMappedBase; }

	void GetDebugName( char* pOutBuf, uint32_t nBufSize ) const
	{
		if ( pOutBuf && nBufSize > 0 )
			Q_strncpy( pOutBuf, m_szDebugName, nBufSize );
	}

private:
	bool CoalesceFreeRegions();

	CDxvkAdapter* m_pAdapter;
	void* m_pDeviceMemory;
	VkDeviceSize m_nBlockSize;
	VkDeviceSize m_nUsedSize;
	uint32_t m_nMemoryType;
	uint32_t m_nPropertyFlags;
	uint32_t m_nAllocCount;
	uint64_t m_nNextAllocId;

	void* m_pMappedBase;
	uint32_t m_nMapRefCount;

	FreeRegion* m_pFreeList;
	uint32_t m_nFreeRegionCount;

	char m_szDebugName[ 128 ];
};

//-----------------------------------------------------------------------------
// DXVK Memory Allocator - global memory manager
//-----------------------------------------------------------------------------
class CDxvkMemoryAllocator
{
public:
	CDxvkMemoryAllocator();
	~CDxvkMemoryAllocator();

	bool Init( CDxvkAdapter* pAdapter,
			   VkDeviceSize nDefaultBlockSize = 64 * 1024 * 1024,
			   uint32_t nMemoryTypeCount = 32 );
	void Shutdown();

	// High-level allocation
	bool Alloc( VkDeviceSize nSize, VkDeviceSize nAlignment,
			   uint32_t nMemoryTypeBits, uint32_t nRequiredPropertyFlags,
			   DxvkMemorySuballocation_t* pOutAlloc,
			   const char* pDebugName = nullptr );
	void Free( const DxvkMemorySuballocation_t& alloc );

	// Statistics
	VkDeviceSize GetTotalAllocated( uint32_t nPropertyFlags = ~0u ) const;
	VkDeviceSize GetTotalReserved( uint32_t nPropertyFlags = ~0u ) const;
	uint32_t GetAllocationCount() const { return m_nTotalAllocationCount; }
	uint32_t GetBlockCount() const { return m_blocks.Count(); }

	bool IsValid() const { return m_pAdapter != nullptr; }

	// Map helpers
	void* MapMemory( const DxvkMemorySuballocation_t& alloc );
	void UnmapMemory( const DxvkMemorySuballocation_t& alloc );
	void FlushMappedRange( const DxvkMemorySuballocation_t& alloc,
							  VkDeviceSize nOffset, VkDeviceSize nSize );
	void InvalidateMappedRange( const DxvkMemorySuballocation_t& alloc,
								VkDeviceSize nOffset, VkDeviceSize nSize );

	// Memory type queries
	uint32_t FindMemoryType( uint32_t nTypeBits,
						   uint32_t nRequiredFlags ) const;
	const DxvkMemoryHeapInfo_t* GetMemoryHeapInfo( uint32_t nTypeIndex ) const
	{
		return ( nTypeIndex < m_heapInfos.Count() ) ? &m_heapInfos[ nTypeIndex ] : nullptr;
	}

	// Defragmentation / cleanup
	void TrimEmptyBlocks();
	void DumpStats() const;

private:
	CDxvkMemoryBlock* FindOrCreateBlock( uint32_t nMemoryType,
									  uint32_t nPropertyFlags,
									  VkDeviceSize nMinSize,
									  const char* pDebugName );
	bool QueryMemoryHeaps();

	CDxvkAdapter* m_pAdapter;
	VkDeviceSize m_nDefaultBlockSize;
	uint32_t m_nMemoryTypeCount;
	uint32_t m_nTotalAllocationCount;
	uint64_t m_nNextGlobalAllocId;

	CUtlVector< CDxvkMemoryBlock* > m_blocks;
	CUtlVector< DxvkMemoryHeapInfo_t > m_heapInfos;

	mutable CThreadMutex m_mutex;
};

static CDxvkMemoryAllocator s_DxvkMemoryAllocator;

//-----------------------------------------------------------------------------
// CDxvkMemoryBlock Implementation
//-----------------------------------------------------------------------------
CDxvkMemoryBlock::CDxvkMemoryBlock() :
	m_pAdapter( nullptr ),
	m_pDeviceMemory( nullptr ),
	m_nBlockSize( 0 ),
	m_nUsedSize( 0 ),
	m_nMemoryType( 0 ),
	m_nPropertyFlags( 0 ),
	m_nAllocCount( 0 ),
	m_nNextAllocId( 1 ),
	m_pMappedBase( nullptr ),
	m_nMapRefCount( 0 ),
	m_pFreeList( nullptr ),
	m_nFreeRegionCount( 0 )
{
	memset( m_szDebugName, 0, sizeof(m_szDebugName) );
}

CDxvkMemoryBlock::~CDxvkMemoryBlock()
{
	Shutdown();
}

bool CDxvkMemoryBlock::Init( CDxvkAdapter* pAdapter,
						   VkDeviceSize nBlockSize, uint32_t nMemoryTypeIndex,
						   uint32_t nPropertyFlags, const char* pDebugName )
{
	if ( !pAdapter || nBlockSize == 0 ) return false;

	m_pAdapter = pAdapter;
	m_nBlockSize = nBlockSize;
	m_nMemoryType = nMemoryTypeIndex;
	m_nPropertyFlags = nPropertyFlags;
	m_nUsedSize = 0;
	m_nAllocCount = 0;
	m_nNextAllocId = 1;
	m_pMappedBase = nullptr;
	m_nMapRefCount = 0;

	if ( pDebugName )
		Q_strncpy( m_szDebugName, pDebugName, sizeof(m_szDebugName) - 1 );

	m_pFreeList = new FreeRegion();
	m_pFreeList->nOffset = 0;
	m_pFreeList->nSize = nBlockSize;
	m_pFreeList->pNext = nullptr;
	m_nFreeRegionCount = 1;

	return true;
}

void CDxvkMemoryBlock::Shutdown()
{
	while ( m_pFreeList )
	{
		FreeRegion* pNext = m_pFreeList->pNext;
		delete m_pFreeList;
		m_pFreeList = pNext;
	}
	m_nFreeRegionCount = 0;

	m_pDeviceMemory = nullptr;
	m_nBlockSize = 0;
	m_nUsedSize = 0;
	m_nAllocCount = 0;
	m_nNextAllocId = 1;
	m_pMappedBase = nullptr;
	m_nMapRefCount = 0;
	memset( m_szDebugName, 0, sizeof(m_szDebugName) );
	m_pAdapter = nullptr;
}

bool CDxvkMemoryBlock::Alloc( VkDeviceSize nSize, VkDeviceSize nAlignment,
						   DxvkMemorySuballocation_t* pOutAlloc )
{
	if ( !pOutAlloc ) return false;
	if ( nSize == 0 ) return false;
	if ( m_nUsedSize + nSize > m_nBlockSize ) return false;

	VkDeviceSize nAlign = nAlignment ? nAlignment : 1;

	FreeRegion** ppPrev = &m_pFreeList;
	FreeRegion* pCur = m_pFreeList;
	while ( pCur )
	{
		VkDeviceSize nAlignedOffset = ( pCur->nOffset + nAlign - 1 ) & ~( nAlign - 1 );
		VkDeviceSize nAlignedEnd = nAlignedOffset + nSize;

		if ( nAlignedEnd <= pCur->nOffset + pCur->nSize )
		{
			memset( pOutAlloc, 0, sizeof(*pOutAlloc) );
			pOutAlloc->pDeviceMemory = m_pDeviceMemory;
			pOutAlloc->nOffset = nAlignedOffset;
			pOutAlloc->nSize = nSize;
			pOutAlloc->nMemoryType = m_nMemoryType;
			pOutAlloc->nMapCount = 0;
			pOutAlloc->pMappedPtr = nullptr;
			pOutAlloc->nFlags = m_nPropertyFlags;
			pOutAlloc->nAllocationId = m_nNextAllocId++;

			m_nUsedSize += nSize;
			m_nAllocCount++;

			VkDeviceSize nBeforeSize = nAlignedOffset - pCur->nOffset;
			VkDeviceSize nAfterOffset = nAlignedEnd;
			VkDeviceSize nAfterSize = ( pCur->nOffset + pCur->nSize ) - nAlignedEnd;

			if ( nBeforeSize == 0 && nAfterSize == 0 )
			{
				*ppPrev = pCur->pNext;
				delete pCur;
				m_nFreeRegionCount--;
			}
			else if ( nBeforeSize == 0 )
			{
				pCur->nOffset = nAfterOffset;
				pCur->nSize = nAfterSize;
			}
			else if ( nAfterSize == 0 )
			{
				pCur->nSize = nBeforeSize;
			}
			else
			{
				FreeRegion* pAfter = new FreeRegion();
				pAfter->nOffset = nAfterOffset;
				pAfter->nSize = nAfterSize;
				pAfter->pNext = pCur->pNext;
				pCur->nSize = nBeforeSize;
				pCur->pNext = pAfter;
				m_nFreeRegionCount++;
			}
			return true;
		}

		ppPrev = &pCur->pNext;
		pCur = pCur->pNext;
	}
	return false;
}

void CDxvkMemoryBlock::Free( const DxvkMemorySuballocation_t& alloc )
{
	if ( alloc.pDeviceMemory != m_pDeviceMemory ) return;
	if ( m_nUsedSize = ( m_nUsedSize >= alloc.nSize ) ? m_nUsedSize - alloc.nSize : 0;
	m_nAllocCount = ( m_nAllocCount > 0 ) ? m_nAllocCount - 1 : 0;

	FreeRegion* pNew = new FreeRegion();
	pNew->nOffset = alloc.nOffset;
	pNew->nSize = alloc.nSize;

	FreeRegion** ppPrev = &m_pFreeList;
	FreeRegion* pCur = m_pFreeList;
	while ( pCur && pCur->nOffset < pNew->nOffset )
	{
		ppPrev = &pCur->pNext;
		pCur = pCur->pNext;
	}
	pNew->pNext = pCur;
	*ppPrev = pNew;
	m_nFreeRegionCount++;
	CoalesceFreeRegions();
}

void* CDxvkMemoryBlock::Map( VkDeviceSize nOffset, VkDeviceSize nSize )
{
	if ( !IsMapped() )
	{
		m_pMappedBase = malloc( (size_t)m_nBlockSize );
		memset( m_pMappedBase, 0, (size_t)m_nBlockSize );
	}
	m_nMapRefCount++;
	return m_pMappedBase ? ( (uint8_t*)m_pMappedBase + nOffset ) : nullptr;
}

void CDxvkMemoryBlock::Unmap()
{
	if ( m_nMapRefCount > 0 )
		m_nMapRefCount--;
	if ( m_nMapRefCount == 0 && m_pMappedBase )
	{
		free( m_pMappedBase );
		m_pMappedBase = nullptr;
	}
}

bool CDxvkMemoryBlock::CoalesceFreeRegions()
{
	bool bCoalesced = false;
	FreeRegion* pCur = m_pFreeList;
	while ( pCur && pCur->pNext )
	{
		if ( pCur->nOffset + pCur->nSize == pCur->pNext->nOffset )
		{
			FreeRegion* pNext = pCur->pNext;
			pCur->nSize += pNext->nSize;
			pCur->pNext = pNext->pNext;
			delete pNext;
			m_nFreeRegionCount--;
			bCoalesced = true;
		}
		else
		{
			pCur = pCur->pNext;
		}
	}
	return bCoalesced;
}

//-----------------------------------------------------------------------------
// CDxvkMemoryAllocator Implementation
//-----------------------------------------------------------------------------
CDxvkMemoryAllocator::CDxvkMemoryAllocator() :
	m_pAdapter( nullptr ),
	m_nDefaultBlockSize( 64 * 1024 * 1024 ),
	m_nMemoryTypeCount( 32 ),
	m_nTotalAllocationCount( 0 ),
	m_nNextGlobalAllocId( 1 )
{
	m_blocks.RemoveAll();
	m_heapInfos.RemoveAll();
}

CDxvkMemoryAllocator::~CDxvkMemoryAllocator()
{
	Shutdown();
}

bool CDxvkMemoryAllocator::Init( CDxvkAdapter* pAdapter,
								   VkDeviceSize nDefaultBlockSize,
								   uint32_t nMemoryTypeCount )
{
	if ( !pAdapter ) return false;
	if ( IsValid() ) Shutdown();

	m_pAdapter = pAdapter;
	m_nDefaultBlockSize = nDefaultBlockSize ? nDefaultBlockSize : ( 64 * 1024 * 1024 );
	m_nMemoryTypeCount = nMemoryTypeCount ? nMemoryTypeCount : 32;
	m_nTotalAllocationCount = 0;
	m_nNextGlobalAllocId = 1;

	m_blocks.RemoveAll();
	return QueryMemoryHeaps();
}

void CDxvkMemoryAllocator::Shutdown()
{
	AUTO_LOCK( m_mutex );
	for ( int i = 0; i < m_blocks.Count(); ++i )
	{
		m_blocks[ i ]->Shutdown();
		delete m_blocks[ i ];
	}
	m_blocks.RemoveAll();
	m_heapInfos.RemoveAll();
	m_pAdapter = nullptr;
	m_nDefaultBlockSize = 64 * 1024 * 1024;
	m_nMemoryTypeCount = 32;
	m_nTotalAllocationCount = 0;
	m_nNextGlobalAllocId = 1;
}

bool CDxvkMemoryAllocator::Alloc( VkDeviceSize nSize, VkDeviceSize nAlignment,
								  uint32_t nMemoryTypeBits,
								  uint32_t nRequiredPropertyFlags,
								  DxvkMemorySuballocation_t* pOutAlloc,
								  const char* pDebugName )
{
	if ( !pOutAlloc || nSize == 0 ) return false;
	AUTO_LOCK( m_mutex );

	uint32_t nMemType = FindMemoryType( nMemoryTypeBits, nRequiredPropertyFlags );
	if ( nMemType == ~0u ) return false;

	uint32_t nHeapIdx = 0;
	if ( nMemType < m_heapInfos.Count() )
		nHeapIdx = m_heapInfos[ nMemType ].nHeapIndex;

	for ( int i = 0; i < m_blocks.Count(); ++i )
	{
		CDxvkMemoryBlock* pBlock = m_blocks[ i ];
		if ( pBlock->GetMemoryType() == nMemType &&
			 pBlock->GetUsedSize() + nSize <= pBlock->GetBlockSize() )
		{
			if ( pBlock->Alloc( nSize, nAlignment, pOutAlloc ) )
			{
				m_nTotalAllocationCount++;
				if ( nMemType < m_heapInfos.Count() )
				{
					m_heapInfos[ nMemType ].nAllocatedSize += nSize;
					m_heapInfos[ nMemType ].nAllocationCount++;
				}
				return true;
			}
		}
	}

	VkDeviceSize nMinBlockSize = MAX( m_nDefaultBlockSize, nSize * 2 );
	CDxvkMemoryBlock* pNewBlock = FindOrCreateBlock( nMemType, nRequiredPropertyFlags,
													nMinBlockSize, pDebugName );
	if ( !pNewBlock ) return false;

	if ( pNewBlock->Alloc( nSize, nAlignment, pOutAlloc ) )
	{
		m_nTotalAllocationCount++;
		if ( nMemType < m_heapInfos.Count() )
		{
			m_heapInfos[ nMemType ].nAllocatedSize += nSize;
			m_heapInfos[ nMemType ].nAllocationCount++;
		}
		return true;
	}
	return false;
}

void CDxvkMemoryAllocator::Free( const DxvkMemorySuballocation_t& alloc )
{
	if ( alloc.pDeviceMemory == nullptr ) return;
	AUTO_LOCK( m_mutex );

	for ( int i = 0; i < m_blocks.Count(); ++i )
	{
		CDxvkMemoryBlock* pBlock = m_blocks[ i ];
		if ( pBlock->GetDeviceMemoryHandle() == alloc.pDeviceMemory )
		{
			VkDeviceSize nAllocSize = alloc.nSize;
			pBlock->Free( alloc );
			m_nTotalAllocationCount = ( m_nTotalAllocationCount > 0 ) ?
				m_nTotalAllocationCount - 1 : 0;
			uint32_t nMemType = pBlock->GetMemoryType();
			if ( nMemType < m_heapInfos.Count() )
			{
				m_heapInfos[ nMemType ].nAllocatedSize =
					( m_heapInfos[ nMemType ].nAllocatedSize >= nAllocSize ) ?
					m_heapInfos[ nMemType ].nAllocatedSize - nAllocSize : 0;
				m_heapInfos[ nMemType ].nAllocationCount =
					( m_heapInfos[ nMemType ].nAllocationCount > 0 ) ?
					m_heapInfos[ nMemType ].nAllocationCount - 1 : 0;
			}
			return;
		}
	}
}

VkDeviceSize CDxvkMemoryAllocator::GetTotalAllocated( uint32_t nPropertyFlags ) const
{
	AUTO_LOCK( m_mutex );
	VkDeviceSize nTotal = 0;
	for ( int i = 0; i < m_blocks.Count(); ++i )
	{
		if ( ( m_blocks[ i ]->GetPropertyFlags() & nPropertyFlags ) == nPropertyFlags )
			nTotal += m_blocks[ i ]->GetUsedSize();
	}
	return nTotal;
}

VkDeviceSize CDxvkMemoryAllocator::GetTotalReserved( uint32_t nPropertyFlags ) const
{
	AUTO_LOCK( m_mutex );
	VkDeviceSize nTotal = 0;
	for ( int i = 0; i < m_blocks.Count(); ++i )
	{
		if ( ( m_blocks[ i ]->GetPropertyFlags() & nPropertyFlags ) == nPropertyFlags )
			nTotal += m_blocks[ i ]->GetBlockSize();
	}
	return nTotal;
}

void* CDxvkMemoryAllocator::MapMemory( const DxvkMemorySuballocation_t& alloc )
{
	AUTO_LOCK( m_mutex );
	for ( int i = 0; i < m_blocks.Count(); ++i )
	{
		if ( m_blocks[ i ]->GetDeviceMemoryHandle() == alloc.pDeviceMemory )
		{
			void* pBase = m_blocks[ i ]->Map( alloc.nOffset, alloc.nSize );
			return pBase;
		}
	}
	return nullptr;
}

void CDxvkMemoryAllocator::UnmapMemory( const DxvkMemorySuballocation_t& alloc )
{
	AUTO_LOCK( m_mutex );
	for ( int i = 0; i < m_blocks.Count(); ++i )
	{
		if ( m_blocks[ i ]->GetDeviceMemoryHandle() == alloc.pDeviceMemory )
		{
			m_blocks[ i ]->Unmap();
			return;
		}
	}
}

void CDxvkMemoryAllocator::FlushMappedRange( const DxvkMemorySuballocation_t& alloc,
										  VkDeviceSize nOffset, VkDeviceSize nSize )
{
}

void CDxvkMemoryAllocator::InvalidateMappedRange( const DxvkMemorySuballocation_t& alloc,
											   VkDeviceSize nOffset, VkDeviceSize nSize )
{
}

uint32_t CDxvkMemoryAllocator::FindMemoryType( uint32_t nTypeBits,
											 uint32_t nRequiredFlags ) const
{
	AUTO_LOCK( m_mutex );
	for ( uint32_t i = 0; i < m_nMemoryTypeCount && i < m_heapInfos.Count(); ++i )
	{
		if ( ( nTypeBits & ( 1u << i ) )
		{
			if ( ( m_heapInfos[ i ].nPropertyFlags & nRequiredFlags ) == nRequiredFlags )
				return i;
		}
	}
	return ~0u;
}

CDxvkMemoryBlock* CDxvkMemoryAllocator::FindOrCreateBlock(
	uint32_t nMemoryType, uint32_t nPropertyFlags,
	VkDeviceSize nMinSize, const char* pDebugName )
{
	VkDeviceSize nBlockSize = MAX( m_nDefaultBlockSize, nMinSize );
	CDxvkMemoryBlock* pBlock = new CDxvkMemoryBlock();
	char szName[ 256 ];
	if ( pDebugName )
		V_snprintf( szName, sizeof(szName), "MemBlock_%s_%s", pDebugName, m_szDebugName ? m_szDebugName : "" );
	else
		V_snprintf( szName, sizeof(szName), "MemBlock_Type%u", nMemoryType );

	if ( !pBlock->Init( m_pAdapter, nBlockSize, nMemoryType, nPropertyFlags, szName ) )
	{
		delete pBlock;
		return nullptr;
	}
	m_blocks.AddToTail( pBlock );
	return pBlock;
}

bool CDxvkMemoryAllocator::QueryMemoryHeaps()
{
	m_heapInfos.RemoveAll();
	for ( uint32_t i = 0; i < m_nMemoryTypeCount; ++i )
	{
		DxvkMemoryHeapInfo_t info = {};
		info.nMemoryTypeIndex = i;
		info.nPropertyFlags = 0;
		info.nHeapIndex = ( i < 4 ) ? 0 : 1;
		info.nHeapSize = ( i < 4 ) ? ( 4ull * 1024 * 1024 * 1024 ) : ( 16ull * 1024 * 1024 * 1024 );
		info.nAllocatedSize = 0;
		info.nAllocationCount = 0;
		if ( i < 4 ) info.nPropertyFlags = 0x1; // DEVICE_LOCAL
		else info.nPropertyFlags = 0x2 | 0x4;       // HOST_VISIBLE | HOST_COHERENT
		m_heapInfos.AddToTail( info );
	}
	return true;
}

void CDxvkMemoryAllocator::TrimEmptyBlocks()
{
	AUTO_LOCK( m_mutex );
	for ( int i = m_blocks.Count() - 1; i >= 0; --i )
	{
		if ( m_blocks[ i ]->GetAllocCount() == 0 )
		{
			m_blocks[ i ]->Shutdown();
			delete m_blocks[ i ];
			m_blocks.Remove( i );
		}
	}
}

void CDxvkMemoryAllocator::DumpStats() const
{
}
