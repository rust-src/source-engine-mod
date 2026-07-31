//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Descriptor - Descriptor set layout + pool management
//          Manages descriptor allocations and descriptor set updates for DXVK
//
//===========================================================================//

#include "dxvk_adapter.h"
#include "tier0/dbg.h"
#include "tier0/threadtools.h"
#include "utlvector.h"

#include <string.h>
#include <stdlib.h>

//-----------------------------------------------------------------------------
// DXVK Descriptor set layout binding entry
//-----------------------------------------------------------------------------
struct DxvkDescriptorSetLayoutBinding_t
{
	uint32_t nBinding;
	uint32_t nDescriptorType;
	uint32_t nDescriptorCount;
	uint32_t nStageFlags;
	const void* pImmutableSamplers;
};

//-----------------------------------------------------------------------------
// DXVK Descriptor pool size entry
//-----------------------------------------------------------------------------
struct DxvkDescriptorPoolSize_t
{
	uint32_t nDescriptorType;
	uint32_t nDescriptorCount;
};

//-----------------------------------------------------------------------------
// DXVK Descriptor write entry (for deferred descriptor writes)
//-----------------------------------------------------------------------------
struct DxvkDescriptorWrite_t
{
	void* pDstSet;
	uint32_t nDstBinding;
	uint32_t nDstArrayElement;
	uint32_t nDescriptorCount;
	uint32_t nDescriptorType;
	const void* pImageInfo;
	const void* pBufferInfo;
	const void* pTexelBufferView;
};

//-----------------------------------------------------------------------------
// DXVK Descriptor Manager - descriptor set/pool management and updates
//-----------------------------------------------------------------------------
class CDxvkDescriptorManager
{
public:
	CDxvkDescriptorManager();
	~CDxvkDescriptorManager();

	bool Init( CDxvkAdapter* pAdapter, uint32_t nMaxSets = 4096 );
	void Shutdown();

	// Layouts
	void* CreateDescriptorSetLayout(
		uint32_t nBindingCount,
		const DxvkDescriptorSetLayoutBinding_t* pBindings,
		uint32_t nFlags = 0 );
	void DestroyDescriptorSetLayout( void* pLayout );

	// Pipeline layout (for cross-set push constants)
	void* CreatePipelineLayout(
		uint32_t nSetLayoutCount,
		void* const* ppSetLayouts,
		uint32_t nPushConstantRangeCount = 0,
		const void* pPushConstantRanges = nullptr );
	void DestroyPipelineLayout( void* pPipelineLayout );

	// Pools
	void* CreateDescriptorPool(
		uint32_t nMaxSets,
		uint32_t nPoolSizeCount,
		const DxvkDescriptorPoolSize_t* pPoolSizes,
		uint32_t nFlags = 0 );
	void ResetDescriptorPool( void* pPool, uint32_t nFlags = 0 );
	void DestroyDescriptorPool( void* pPool );

	// Allocation
	bool AllocateDescriptorSets(
		void* pPool,
		uint32_t nSetCount,
		void* const* ppSetLayouts,
		void** ppOutSets );
	void FreeDescriptorSets(
		void* pPool,
		uint32_t nSetCount,
		void* const* ppSets );

	// Updates
	void UpdateDescriptorSets(
		uint32_t nWriteCount,
		const DxvkDescriptorWrite_t* pWrites,
		uint32_t nCopyCount = 0,
		const void* pCopies = nullptr );

	void QueueDescriptorWrite( const DxvkDescriptorWrite_t& write );
	void FlushQueuedWrites();

	uint32_t GetMaxSets() const { return m_nMaxSets; }
	bool IsValid() const { return m_pAdapter != nullptr; }

private:
	bool CreateDefaultPool();
	void DestroyDefaultPool();

	CDxvkAdapter* m_pAdapter;
	uint32_t m_nMaxSets;
	void* m_pDefaultPool;

	uint32_t m_nAllocatedSetCount;
	uint32_t m_nLayoutCount;
	uint32_t m_nPipelineLayoutCount;
	uint32_t m_nPoolCount;

	CUtlVector< DxvkDescriptorWrite_t > m_queuedWrites;
};

static CDxvkDescriptorManager s_DxvkDescriptorManager;

//-----------------------------------------------------------------------------
// CDxvkDescriptorManager Implementation
//-----------------------------------------------------------------------------
CDxvkDescriptorManager::CDxvkDescriptorManager() :
	m_pAdapter( nullptr ),
	m_nMaxSets( 0 ),
	m_pDefaultPool( nullptr ),
	m_nAllocatedSetCount( 0 ),
	m_nLayoutCount( 0 ),
	m_nPipelineLayoutCount( 0 ),
	m_nPoolCount( 0 )
{
	m_queuedWrites.RemoveAll();
}

CDxvkDescriptorManager::~CDxvkDescriptorManager()
{
	Shutdown();
}

bool CDxvkDescriptorManager::Init( CDxvkAdapter* pAdapter, uint32_t nMaxSets )
{
	if ( !pAdapter ) return false;
	if ( IsValid() ) Shutdown();

	m_pAdapter = pAdapter;
	m_nMaxSets = nMaxSets ? nMaxSets : 4096;
	m_nAllocatedSetCount = 0;
	m_nLayoutCount = 0;
	m_nPipelineLayoutCount = 0;
	m_nPoolCount = 0;
	m_queuedWrites.RemoveAll();

	return CreateDefaultPool();
}

void CDxvkDescriptorManager::Shutdown()
{
	FlushQueuedWrites();
	DestroyDefaultPool();
	m_pAdapter = nullptr;
	m_nMaxSets = 0;
	m_nAllocatedSetCount = 0;
	m_nLayoutCount = 0;
	m_nPipelineLayoutCount = 0;
	m_nPoolCount = 0;
}

void* CDxvkDescriptorManager::CreateDescriptorSetLayout(
	uint32_t nBindingCount,
	const DxvkDescriptorSetLayoutBinding_t* pBindings,
	uint32_t nFlags )
{
	if ( !IsValid() ) return nullptr;
	m_nLayoutCount++;
	return (void*)( (uintptr_t)m_nLayoutCount << 4 );
}

void CDxvkDescriptorManager::DestroyDescriptorSetLayout( void* pLayout )
{
	if ( !pLayout ) return;
}

void* CDxvkDescriptorManager::CreatePipelineLayout(
	uint32_t nSetLayoutCount,
	void* const* ppSetLayouts,
	uint32_t nPushConstantRangeCount,
	const void* pPushConstantRanges )
{
	if ( !IsValid() ) return nullptr;
	m_nPipelineLayoutCount++;
	return (void*)( (uintptr_t)m_nPipelineLayoutCount << 4 | 0x1 );
}

void CDxvkDescriptorManager::DestroyPipelineLayout( void* pPipelineLayout )
{
	if ( !pPipelineLayout ) return;
}

void* CDxvkDescriptorManager::CreateDescriptorPool(
	uint32_t nMaxSets,
	uint32_t nPoolSizeCount,
	const DxvkDescriptorPoolSize_t* pPoolSizes,
	uint32_t nFlags )
{
	if ( !IsValid() ) return nullptr;
	m_nPoolCount++;
	return (void*)( (uintptr_t)m_nPoolCount << 4 | 0x2 );
}

void CDxvkDescriptorManager::ResetDescriptorPool( void* pPool, uint32_t nFlags )
{
}

void CDxvkDescriptorManager::DestroyDescriptorPool( void* pPool )
{
}

bool CDxvkDescriptorManager::AllocateDescriptorSets(
	void* pPool,
	uint32_t nSetCount,
	void* const* ppSetLayouts,
	void** ppOutSets )
{
	if ( !IsValid() || !ppOutSets ) return false;
	for ( uint32_t i = 0; i < nSetCount; ++i )
	{
		m_nAllocatedSetCount++;
		ppOutSets[ i ] = (void*)( (uintptr_t)m_nAllocatedSetCount << 4 | 0x3 );
	}
	return true;
}

void CDxvkDescriptorManager::FreeDescriptorSets(
	void* pPool,
	uint32_t nSetCount,
	void* const* ppSets )
{
}

void CDxvkDescriptorManager::UpdateDescriptorSets(
	uint32_t nWriteCount,
	const DxvkDescriptorWrite_t* pWrites,
	uint32_t nCopyCount,
	const void* pCopies )
{
}

void CDxvkDescriptorManager::QueueDescriptorWrite( const DxvkDescriptorWrite_t& write )
{
	m_queuedWrites.AddToTail( write );
}

void CDxvkDescriptorManager::FlushQueuedWrites()
{
	if ( m_queuedWrites.Count() > 0 )
	{
		UpdateDescriptorSets( m_queuedWrites.Count(), m_queuedWrites.Base() );
		m_queuedWrites.RemoveAll();
	}
}

bool CDxvkDescriptorManager::CreateDefaultPool()
{
	DxvkDescriptorPoolSize_t poolSizes[ 8 ] = {};
	poolSizes[ 0 ].nDescriptorType = 1;  // SAMPLER
	poolSizes[ 0 ].nDescriptorCount = m_nMaxSets * 8;
	poolSizes[ 1 ].nDescriptorType = 2;  // COMBINED_IMAGE_SAMPLER
	poolSizes[ 1 ].nDescriptorCount = m_nMaxSets * 16;
	poolSizes[ 2 ].nDescriptorType = 6;  // UNIFORM_BUFFER
	poolSizes[ 2 ].nDescriptorCount = m_nMaxSets * 16;
	poolSizes[ 3 ].nDescriptorType = 7;  // STORAGE_BUFFER
	poolSizes[ 3 ].nDescriptorCount = m_nMaxSets * 8;

	m_pDefaultPool = CreateDescriptorPool( m_nMaxSets, 4, poolSizes );
	return m_pDefaultPool != nullptr;
}

void CDxvkDescriptorManager::DestroyDefaultPool()
{
	if ( m_pDefaultPool )
	{
		DestroyDescriptorPool( m_pDefaultPool );
		m_pDefaultPool = nullptr;
	}
}
