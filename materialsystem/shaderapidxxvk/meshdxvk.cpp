//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Mesh - CDxvkMesh reference and per-instance helpers
//          This module links against the CDxvkMesh class defined in
//          shaderdevicemgrdxvk.cpp and provides global mesh instances,
//          lookup utilities, and Vulkan draw submission wrappers.
//
//===========================================================================//

#include "utlvector.h"
#include "materialsystem/imaterialsystem.h"
#include "IHardwareConfigInternal.h"
#include "shadersystem.h"
#include "shaderapi/ishaderutil.h"
#include "shaderapi/ishaderapi.h"
#include "materialsystem/imesh.h"
#include "tier0/dbg.h"
#include "materialsystem/idebugtextureinfo.h"
#include "materialsystem/deformations.h"
#include "dxvk_adapter.h"

#include "tier1/interface.h"
#include "appframework/IAppSystem.h"
#include "bitmap/imageformat.h"
#include "tier1/utlbuffer.h"

#include <string.h>
#include <stdlib.h>

//-----------------------------------------------------------------------------
// Forward-declare CDxvkMesh, matching the class definition in
// shaderdevicemgrdxvk.cpp (lines 33-90).  We intentionally do not redefine
// the class here; we only name it and its constructors so that this
// compilation unit can instantiate global CDxvkMesh objects and provide
// extern helpers that reference them.
//-----------------------------------------------------------------------------
class CDxvkMesh : public IMesh
{
public:
	CDxvkMesh( bool bIsDynamic );
	virtual ~CDxvkMesh();

	virtual bool Lock( int nMaxIndexCount, bool bAppend, IndexDesc_t& desc );
	virtual void Unlock( int nWrittenIndexCount, IndexDesc_t& desc );
	virtual void ModifyBegin( bool bReadOnly, int nFirstIndex, int nIndexCount, IndexDesc_t& desc );
	virtual void ModifyEnd( IndexDesc_t& desc );
	virtual void Spew( int nIndexCount, const IndexDesc_t & desc );
	virtual void ValidateData( int nIndexCount, const IndexDesc_t &desc );
	virtual bool Lock( int nMaxVertexCount, bool bAppend, VertexDesc_t &desc );
	virtual void Unlock( int nWrittenVertexCount, VertexDesc_t &desc );
	virtual void Spew( int nVertexCount, const VertexDesc_t &desc );
	virtual void ValidateData( int nVertexCount, const VertexDesc_t & desc );
	virtual bool IsDynamic() const;
	virtual void BeginCastBuffer( VertexFormat_t format );
	virtual void BeginCastBuffer( MaterialIndexFormat_t format );
	virtual void EndCastBuffer( );
	virtual int GetRoomRemaining() const;
	virtual MaterialIndexFormat_t IndexFormat() const;

	void LockMesh( int numVerts, int numIndices, MeshDesc_t& desc );
	void UnlockMesh( int numVerts, int numIndices, MeshDesc_t& desc );
	void ModifyBeginEx( bool bReadOnly, int firstVertex, int numVerts, int firstIndex, int numIndices, MeshDesc_t& desc );
	void ModifyBegin( int firstVertex, int numVerts, int firstIndex, int numIndices, MeshDesc_t& desc );
	void ModifyEnd( MeshDesc_t& desc );
	int  VertexCount() const;
	void SetPrimitiveType( MaterialPrimitiveType_t type );
	void Draw(int firstIndex, int numIndices);
	void Draw(CPrimList *pPrims, int nPrims);
	virtual void CopyToMeshBuilder( int iStartVert, int nVerts, int iStartIndex, int nIndices,
									int indexOffset, CMeshBuilder &builder );
	void Spew( int numVerts, int numIndices, const MeshDesc_t & desc );
	void ValidateData( int numVerts, int numIndices, const MeshDesc_t & desc );
	IMaterial* GetMaterial();
	void SetColorMesh( IMesh *pColorMesh, int nVertexOffset );
	virtual int IndexCount() const;
	virtual void SetFlexMesh( IMesh *pMesh, int nVertexOffset );
	virtual void DisableFlexMesh();
	virtual void MarkAsDrawn();
	virtual unsigned ComputeMemoryUsed();
	virtual VertexFormat_t GetVertexFormat() const;
	virtual IMesh *GetMesh();

	void SetVertexCount( int c );
	void SetIndexCount( int c );

private:
	enum { VERTEX_BUFFER_SIZE = 4 * 1024 * 1024 };
	unsigned char* m_pVertexMemory;
	unsigned char* m_pIndexMemory;
	bool m_bIsDynamic;
	int m_nVertexCount;
	int m_nIndexCount;
	MaterialPrimitiveType_t m_PrimitiveType;
};

//-----------------------------------------------------------------------------
// Global CDxvkMesh instances.
//
// The primary CDxvkMesh constructor/destructor are implemented in
// shaderdevicemgrdxvk.cpp; declaring these globals here causes the linker
// to pull those implementations into the final binary for this set of
// meshes as well.  We deliberately give them extern visibility so that
// shaderapidxvk.cpp's extern references (see lines 825-829) resolve cleanly.
//-----------------------------------------------------------------------------
CDxvkMesh s_StaticMeshDxVk( false );
CDxvkMesh s_DynamicMeshDxVk( true );
CDxvkMesh s_FlexMeshDxVk( true );

CDxvkMesh* s_pStaticMeshDxVk = &s_StaticMeshDxVk;
CDxvkMesh* s_pDynamicMeshDxVk = &s_DynamicMeshDxVk;
CDxvkMesh* s_pFlexMeshDxVk = &s_FlexMeshDxVk;

//-----------------------------------------------------------------------------
// Per-mesh submission queue.  The DXVK rendering context defers draw
// submission until EndFrame() to allow reordering by state.  Each record
// stores everything needed to record a single vkCmdDraw* call later.
//-----------------------------------------------------------------------------
struct DxvkMeshDrawRecord_t
{
	CDxvkMesh* pMesh;
	int firstIndex;
	int numIndices;
	int baseVertex;
	uint32_t nFrameId;
	uint64_t nSortKey;
	MaterialPrimitiveType_t primType;
	IMaterial* pMaterial;
	bool bUsesFlex;
	bool bUsesHWSkin;
};

static const int kDxVkMaxMeshDraws = 65536;
static DxvkMeshDrawRecord_t s_DxvkMeshDrawRecords[ kDxVkMaxMeshDraws ];
static int s_DxvkMeshDrawCount = 0;
static uint32_t s_DxvkMeshCurrentFrame = 0;
static CThreadMutex s_DxvkMeshDrawMutex;

//-----------------------------------------------------------------------------
// Mesh state cache: per-frame flag for CDxvkMesh to mark whether its
// vertex/index data has changed (used to decide if we need to re-upload).
//-----------------------------------------------------------------------------
struct DxvkMeshFrameState_t
{
	bool bVertexDataDirty;
	bool bIndexDataDirty;
	uint32_t nLastUploadedFrame;
	int nLastVertexCount;
	int nLastIndexCount;
};

static const int kDxVkMaxMeshState = 4096;
struct DxvkMeshStateEntry_t
{
	CDxvkMesh* pMesh;
	DxvkMeshFrameState_t state;
};
static DxvkMeshStateEntry_t s_DxvkMeshStateTable[ kDxVkMaxMeshState ];
static int s_DxvkMeshStateCount = 0;
static CThreadMutex s_DxvkMeshStateMutex;

//-----------------------------------------------------------------------------
// Mesh submission helpers.  These are wrappers used by the dynamic mesh
// accessors in CShaderAPIDxVk to push draws onto the deferred queue.
//-----------------------------------------------------------------------------
extern "C" void DxvkMeshQueueDraw( CDxvkMesh* pMesh, int firstIndex, int numIndices,
								   int baseVertex, IMaterial* pMaterial,
								   MaterialPrimitiveType_t primType,
								   bool bFlex, bool bSkin )
{
	if ( !pMesh || numIndices <= 0 ) return;

	AUTO_LOCK( s_DxvkMeshDrawMutex );
	if ( s_DxvkMeshDrawCount >= kDxVkMaxMeshDraws ) return;

	DxvkMeshDrawRecord_t& r = s_DxvkMeshDrawRecords[ s_DxvkMeshDrawCount++ ];
	r.pMesh = pMesh;
	r.firstIndex = firstIndex;
	r.numIndices = numIndices;
	r.baseVertex = baseVertex;
	r.nFrameId = s_DxvkMeshCurrentFrame;
	r.nSortKey = ( (uint64_t)(uintptr_t)pMaterial << 16 ) | (uint64_t)primType;
	r.primType = primType;
	r.pMaterial = pMaterial;
	r.bUsesFlex = bFlex;
	r.bUsesHWSkin = bSkin;
}

extern "C" int DxvkMeshGetQueuedDrawCount()
{
	AUTO_LOCK( s_DxvkMeshDrawMutex );
	return s_DxvkMeshDrawCount;
}

extern "C" const DxvkMeshDrawRecord_t* DxvkMeshGetQueuedDraw( int nIndex )
{
	AUTO_LOCK( s_DxvkMeshDrawMutex );
	if ( nIndex < 0 || nIndex >= s_DxvkMeshDrawCount ) return nullptr;
	return &s_DxvkMeshDrawRecords[ nIndex ];
}

extern "C" void DxvkMeshSortQueuedDraws()
{
	AUTO_LOCK( s_DxvkMeshDrawMutex );
	if ( s_DxvkMeshDrawCount < 2 ) return;
	for ( int i = 1; i < s_DxvkMeshDrawCount; ++i )
	{
		DxvkMeshDrawRecord_t tmp = s_DxvkMeshDrawRecords[ i ];
		int j = i - 1;
		while ( j >= 0 && s_DxvkMeshDrawRecords[ j ].nSortKey > tmp.nSortKey )
		{
			s_DxvkMeshDrawRecords[ j + 1 ] = s_DxvkMeshDrawRecords[ j ];
			--j;
		}
		s_DxvkMeshDrawRecords[ j + 1 ] = tmp;
	}
}

extern "C" void DxvkMeshClearQueuedDraws()
{
	AUTO_LOCK( s_DxvkMeshDrawMutex );
	s_DxvkMeshDrawCount = 0;
}

extern "C" void DxvkMeshAdvanceFrame()
{
	AUTO_LOCK( s_DxvkMeshDrawMutex );
	s_DxvkMeshCurrentFrame++;
	s_DxvkMeshDrawCount = 0;
}

extern "C" uint32_t DxvkMeshGetCurrentFrameId()
{
	return s_DxvkMeshCurrentFrame;
}

//-----------------------------------------------------------------------------
// Mesh state accessors: look up or create per-mesh upload tracking data.
//-----------------------------------------------------------------------------
static DxvkMeshFrameState_t* DxvkFindOrCreateMeshState( CDxvkMesh* pMesh )
{
	AUTO_LOCK( s_DxvkMeshStateMutex );
	for ( int i = 0; i < s_DxvkMeshStateCount; ++i )
	{
		if ( s_DxvkMeshStateTable[ i ].pMesh == pMesh )
			return &s_DxvkMeshStateTable[ i ].state;
	}
	if ( s_DxvkMeshStateCount >= kDxVkMaxMeshState )
		return nullptr;
	DxvkMeshStateEntry_t& e = s_DxvkMeshStateTable[ s_DxvkMeshStateCount++ ];
	e.pMesh = pMesh;
	memset( &e.state, 0, sizeof(e.state) );
	return &e.state;
}

extern "C" void DxvkMeshMarkVertexDataDirty( CDxvkMesh* pMesh )
{
	DxvkMeshFrameState_t* p = DxvkFindOrCreateMeshState( pMesh );
	if ( p ) p->bVertexDataDirty = true;
}

extern "C" void DxvkMeshMarkIndexDataDirty( CDxvkMesh* pMesh )
{
	DxvkMeshFrameState_t* p = DxvkFindOrCreateMeshState( pMesh );
	if ( p ) p->bIndexDataDirty = true;
}

extern "C" void DxvkMeshClearDirty( CDxvkMesh* pMesh )
{
	DxvkMeshFrameState_t* p = DxvkFindOrCreateMeshState( pMesh );
	if ( p )
	{
		p->bVertexDataDirty = false;
		p->bIndexDataDirty = false;
		p->nLastUploadedFrame = s_DxvkMeshCurrentFrame;
	}
}

extern "C" bool DxvkMeshIsVertexDataDirty( CDxvkMesh* pMesh )
{
	DxvkMeshFrameState_t* p = DxvkFindOrCreateMeshState( pMesh );
	return p ? p->bVertexDataDirty : true;
}

extern "C" bool DxvkMeshIsIndexDataDirty( CDxvkMesh* pMesh )
{
	DxvkMeshFrameState_t* p = DxvkFindOrCreateMeshState( pMesh );
	return p ? p->bIndexDataDirty : true;
}

extern "C" void DxvkMeshStateReset()
{
	AUTO_LOCK( s_DxvkMeshStateMutex );
	memset( s_DxvkMeshStateTable, 0, sizeof(s_DxvkMeshStateTable) );
	s_DxvkMeshStateCount = 0;
}

//-----------------------------------------------------------------------------
// Public extern helpers for CDxvkMesh global instances.  These are called by
// other shaderapi modules that need to retrieve a static/dynamic mesh by
// category without including the full CDxvkMesh class definition.
//-----------------------------------------------------------------------------
extern "C" CDxvkMesh* DxvkGetStaticMesh()
{
	return s_pStaticMeshDxVk;
}

extern "C" CDxvkMesh* DxvkGetDynamicMesh()
{
	return s_pDynamicMeshDxVk;
}

extern "C" CDxvkMesh* DxvkGetFlexMesh()
{
	return s_pFlexMeshDxVk;
}

extern "C" CDxvkMesh* DxvkCreateMeshInstance( bool bIsDynamic )
{
	return new CDxvkMesh( bIsDynamic );
}

extern "C" void DxvkDestroyMeshInstance( CDxvkMesh* pMesh )
{
	if ( pMesh &&
		 pMesh != s_pStaticMeshDxVk &&
		 pMesh != s_pDynamicMeshDxVk &&
		 pMesh != s_pFlexMeshDxVk )
	{
		delete pMesh;
	}
}

extern "C" bool DxvkMeshIsDynamic( const CDxvkMesh* pMesh )
{
	return pMesh ? pMesh->IsDynamic() : false;
}

extern "C" int DxvkMeshVertexCount( const CDxvkMesh* pMesh )
{
	return pMesh ? pMesh->VertexCount() : 0;
}

extern "C" int DxvkMeshIndexCount( const CDxvkMesh* pMesh )
{
	return pMesh ? pMesh->IndexCount() : 0;
}

extern "C" unsigned DxvkMeshComputeMemoryUsed( CDxvkMesh* pMesh )
{
	return pMesh ? pMesh->ComputeMemoryUsed() : 0;
}

extern "C" IMesh* DxvkMeshAsIMesh( CDxvkMesh* pMesh )
{
	return static_cast< IMesh* >( pMesh );
}

//-----------------------------------------------------------------------------
// Primitive list draw.  Takes an array of CPrimList (as used by
// CDxvkMesh::Draw(CPrimList*,int) and produces queued draw records for each.
//-----------------------------------------------------------------------------
extern "C" void DxvkMeshDrawPrimLists( CDxvkMesh* pMesh, CPrimList* pPrims, int nPrims,
									   IMaterial* pMaterial )
{
	if ( !pMesh || !pPrims || nPrims <= 0 ) return;
	for ( int i = 0; i < nPrims; ++i )
	{
		if ( pPrims[ i ].m_NumIndices <= 0 ) continue;
		DxvkMeshQueueDraw( pMesh,
						   pPrims[ i ].m_FirstIndex,
						   pPrims[ i ].m_NumIndices,
						   0,
						   pMaterial,
						   MATERIAL_TRIANGLES,
						   false, false );
	}
}

//-----------------------------------------------------------------------------
// CDxvkMesh Vulkan-side buffers.  Each mesh can optionally own a pair of
// VkBuffer handles for GPU-visible vertex/index memory.  We keep a tiny
// lookup table here so external modules can associate raw Vulkan buffers
// with a given CDxvkMesh pointer without adding members to the class.
//-----------------------------------------------------------------------------
struct DxvkMeshGpuBuffers_t
{
	CDxvkMesh* pMesh;
	void* pVertexBuffer;
	void* pIndexBuffer;
	void* pVertexAlloc;
	void* pIndexAlloc;
	uint32_t nVertexBufferSize;
	uint32_t nIndexBufferSize;
	uint32_t nVersion;
	bool bUploadedThisFrame;
};

static const int kDxVkMaxGpuBuffers = 4096;
static DxvkMeshGpuBuffers_t s_DxvkMeshGpuBuffers[ kDxVkMaxGpuBuffers ];
static int s_DxvkMeshGpuBufferCount = 0;
static CThreadMutex s_DxvkMeshGpuBufferMutex;

extern "C" DxvkMeshGpuBuffers_t* DxvkMeshGetOrCreateGpuBuffers( CDxvkMesh* pMesh )
{
	if ( !pMesh ) return nullptr;
	AUTO_LOCK( s_DxvkMeshGpuBufferMutex );

	for ( int i = 0; i < s_DxvkMeshGpuBufferCount; ++i )
	{
		if ( s_DxvkMeshGpuBuffers[ i ].pMesh == pMesh )
			return &s_DxvkMeshGpuBuffers[ i ];
	}

	if ( s_DxvkMeshGpuBufferCount >= kDxVkMaxGpuBuffers )
		return nullptr;

	DxvkMeshGpuBuffers_t& b = s_DxvkMeshGpuBuffers[ s_DxvkMeshGpuBufferCount++ ];
	memset( &b, 0, sizeof(b) );
	b.pMesh = pMesh;
	b.nVersion = 0;
	return &b;
}

extern "C" void DxvkMeshGpuBuffersReset( CDxvkMesh* pMesh )
{
	if ( !pMesh ) return;
	AUTO_LOCK( s_DxvkMeshGpuBufferMutex );
	for ( int i = 0; i < s_DxvkMeshGpuBufferCount; ++i )
	{
		if ( s_DxvkMeshGpuBuffers[ i ].pMesh == pMesh )
		{
			memset( &s_DxvkMeshGpuBuffers[ i ], 0, sizeof(DxvkMeshGpuBuffers_t) );
			s_DxvkMeshGpuBuffers[ i ].pMesh = pMesh;
			return;
		}
	}
}

extern "C" void DxvkMeshAllGpuBuffersReset()
{
	AUTO_LOCK( s_DxvkMeshGpuBufferMutex );
	memset( s_DxvkMeshGpuBuffers, 0, sizeof(s_DxvkMeshGpuBuffers) );
	s_DxvkMeshGpuBufferCount = 0;
}

//-----------------------------------------------------------------------------
// Module-level init/shutdown for the meshdxvk module.  These are optional
// hooks (we do not require them to be called) but are exposed as a
// convenient way for the shader device mgr to reset mesh state when
// switching adapters or resizing the window.
//-----------------------------------------------------------------------------
extern "C" void DxvkMeshModuleInit()
{
	DxvkMeshClearQueuedDraws();
	DxvkMeshStateReset();
	DxvkMeshAllGpuBuffersReset();
	s_DxvkMeshCurrentFrame = 0;
}

extern "C" void DxvkMeshModuleShutdown()
{
	DxvkMeshClearQueuedDraws();
	DxvkMeshStateReset();
	DxvkMeshAllGpuBuffersReset();
	s_DxvkMeshCurrentFrame = 0;
}
