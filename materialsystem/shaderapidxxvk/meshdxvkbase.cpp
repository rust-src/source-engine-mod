//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Mesh Base - Base class and utilities for CDxvkMesh
//          Provides shared mesh helpers, constants, and non-inline methods
//          used by the CDxvkMesh implementation defined in shaderdevicemgrdxvk.cpp
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
// Forward declaration of CDxvkMesh as defined in shaderdevicemgrdxvk.cpp.
// We deliberately do NOT include its inline implementation here; this file
// only provides the base helpers, per-stream validation, and global
// instances that other compilation units can link against.
//-----------------------------------------------------------------------------
class CDxvkMesh;

//-----------------------------------------------------------------------------
// Mesh size / layout constants shared by all DXVK mesh implementations.
//-----------------------------------------------------------------------------
namespace DxvkMeshBase
{
	const int kMaxVertexStreams = 16;
	const int kMaxTexCoords = VERTEX_MAX_TEXTURE_COORDINATES;
	const int kMaxBoneWeights = 4;
	const int kMaxBonesPerDraw = 53;

	const int kDefaultVertexBufferSize = 4 * 1024 * 1024;
	const int kDefaultIndexBufferSize = 4 * 1024 * 1024;

	const int kMaxVerticesPerDraw = 32767;
	const int kMaxIndicesPerDraw = 32767;

	const int kMaxPrimListsPerDraw = 1024;
	const int kFlexMeshBudgetBytes = 2 * 1024 * 1024;

	const int kDefaultVertexAlignment = 4;
	const int kDefaultIndexAlignment = 2;
}

//-----------------------------------------------------------------------------
// Mesh descriptor cache entry - used to avoid recomputing vertex offsets
// for each MeshDesc_t that is requested from a CDxvkMesh.
//-----------------------------------------------------------------------------
struct DxvkMeshDescCacheEntry_t
{
	VertexFormat_t vertexFormat;
	int nUserDataSize;
	bool bValid;
	int nOffsets[ 32 ];
	int nSizes[ 32 ];
	int nTotalVertexSize;
};

static const int kDxVkMeshDescCacheSize = 1024;
static DxvkMeshDescCacheEntry_t s_DxvkMeshDescCache[ kDxVkMeshDescCacheSize ];
static int s_DxvkMeshDescCacheNext = 0;
static bool s_DxvkMeshDescCacheInit = false;

//-----------------------------------------------------------------------------
// Vertex format -> size helper.  Mirrors the inline logic used by
// CShaderAPIDxVk::ComputeVertexDescription, but exposed here as a plain
// C helper so other mesh modules can link it without pulling in the
// full IShaderAPI vtable.
//-----------------------------------------------------------------------------
static int DxvkVertexElementSize( VertexFormat_t fmt, int elem )
{
	switch ( elem )
	{
	case 0: return ( fmt & VERTEX_POSITION ) ? 12 : 0;  // float3 position
	case 1: return ( fmt & VERTEX_NORMAL )   ? 12 : 0;  // float3 normal
	case 2: return ( fmt & VERTEX_COLOR )    ?  4 : 0;  // D3DCOLOR color
	case 3: return ( fmt & VERTEX_SPECULAR ) ?  4 : 0;  // D3DCOLOR specular
	case 4: case 5: case 6: case 7:
	case 8: case 9: case 10: case 11:
		{
			int tc = elem - 4;
			int nNumCoords = (int)( ( fmt >> ( TEX_COORD_SIZE_BIT + 3 * tc ) ) & 0x7 );
			return nNumCoords * 4;
		}
	case 12: return ( fmt & VERTEX_BONE_WEIGHT_MASK ) ? 20 : 0; // 4 floats + 4 ubytes
	case 13: return ( fmt & VERTEX_TANGENT_S ) ? 12 : 0;
	case 14: return ( fmt & VERTEX_TANGENT_T ) ? 12 : 0;
	default: return 0;
	}
}

//-----------------------------------------------------------------------------
// Compute total vertex stride for a given VertexFormat_t + user data size.
// This is exported for use by the vertex buffer wrappers in other TUs.
//-----------------------------------------------------------------------------
extern "C" int DxvkComputeVertexStride( VertexFormat_t fmt, int nUserDataSize /* = 0 */ )
{
	int stride = 0;
	for ( int i = 0; i < 16; ++i )
		stride += DxvkVertexElementSize( fmt, i );
	stride += nUserDataSize * 4;
	if ( stride == 0 ) stride = 32;
	return stride;
}

//-----------------------------------------------------------------------------
// Populate the DxvkMeshDescCache for a given format/userdata combo.
//-----------------------------------------------------------------------------
static const DxvkMeshDescCacheEntry_t* DxvkEnsureMeshDescCache( VertexFormat_t fmt, int nUserDataSize )
{
	if ( !s_DxvkMeshDescCacheInit )
	{
		memset( s_DxvkMeshDescCache, 0, sizeof(s_DxvkMeshDescCache) );
		s_DxvkMeshDescCacheInit = true;
	}

	for ( int i = 0; i < kDxVkMeshDescCacheSize; ++i )
	{
		const DxvkMeshDescCacheEntry_t& e = s_DxvkMeshDescCache[ i ];
		if ( e.bValid && e.vertexFormat == fmt && e.nUserDataSize == nUserDataSize )
			return &e;
	}

	int idx = s_DxvkMeshDescCacheNext++ % kDxVkMeshDescCacheSize;
	DxvkMeshDescCacheEntry_t& e = s_DxvkMeshDescCache[ idx ];
	memset( &e, 0, sizeof(e) );
	e.vertexFormat = fmt;
	e.nUserDataSize = nUserDataSize;
	e.bValid = true;

	int offset = 0;
	for ( int i = 0; i < 16; ++i )
	{
		int sz = DxvkVertexElementSize( fmt, i );
		e.nOffsets[ i ] = offset;
		e.nSizes[ i ] = sz;
		offset += sz;
	}
	offset += nUserDataSize * 4;
	e.nTotalVertexSize = offset ? offset : 32;
	return &e;
}

//-----------------------------------------------------------------------------
// Mesh descriptor builder: fills a MeshDesc_t for a given vertex format.
// Works without a concrete CDxvkMesh instance (used for preview/validation).
//-----------------------------------------------------------------------------
extern "C" void DxvkBuildMeshDescriptor( void* pOutBuffer, VertexFormat_t fmt,
										 int nUserDataSize,
										 int nMaxVertexCount,
										 int nMaxIndexCount,
										 bool bIs32BitIndex )
{
	(void)nMaxVertexCount;
	(void)nMaxIndexCount;
	(void)bIs32BitIndex;
	if ( !pOutBuffer ) return;

	const DxvkMeshDescCacheEntry_t* e = DxvkEnsureMeshDescCache( fmt, nUserDataSize );
	if ( !e ) return;

	MeshDesc_t* pDesc = (MeshDesc_t*)pOutBuffer;
	memset( pDesc, 0, sizeof(MeshDesc_t) );

	uint8_t* pBase = (uint8_t*)pDesc;
	ptrdiff_t offPos = (uint8_t*)&pDesc->m_pPosition - pBase;
	ptrdiff_t offNormal = (uint8_t*)&pDesc->m_pNormal - pBase;
	ptrdiff_t offColor  = (uint8_t*)&pDesc->m_pColor - pBase;
	ptrdiff_t offBoneWeight = (uint8_t*)&pDesc->m_pBoneWeight - pBase;
	ptrdiff_t offBoneIdx = (uint8_t*)&pDesc->m_pBoneMatrixIndex - pBase;
	ptrdiff_t offTangentS = (uint8_t*)&pDesc->m_pTangentS - pBase;
	ptrdiff_t offTangentT = (uint8_t*)&pDesc->m_pTangentT - pBase;
	ptrdiff_t offUserData = (uint8_t*)&pDesc->m_pUserData - pBase;
	ptrdiff_t offTexCoord = (uint8_t*)pDesc->m_pTexCoord - pBase;

	ptrdiff_t offSize_Pos  = (uint8_t*)&pDesc->m_VertexSize_Position - pBase;
	ptrdiff_t offSize_Nor  = (uint8_t*)&pDesc->m_VertexSize_Normal - pBase;
	ptrdiff_t offSize_Col  = (uint8_t*)&pDesc->m_VertexSize_Color - pBase;
	ptrdiff_t offSize_BW   = (uint8_t*)&pDesc->m_VertexSize_BoneWeight - pBase;
	ptrdiff_t offSize_BM   = (uint8_t*)&pDesc->m_VertexSize_BoneMatrixIndex - pBase;
	ptrdiff_t offSize_TS   = (uint8_t*)&pDesc->m_VertexSize_TangentS - pBase;
	ptrdiff_t offSize_TT   = (uint8_t*)&pDesc->m_VertexSize_TangentT - pBase;
	ptrdiff_t offSize_UD   = (uint8_t*)&pDesc->m_VertexSize_UserData - pBase;
	ptrdiff_t offSize_TC   = (uint8_t*)pDesc->m_VertexSize_TexCoord - pBase;

	ptrdiff_t offStride = (uint8_t*)&pDesc->m_ActualVertexSize - pBase;
	ptrdiff_t offNumBW  = (uint8_t*)&pDesc->m_NumBoneWeights - pBase;

	for ( int i = 0; i < 16; ++i )
	{
		int sz = e->nSizes[ i ];
		if ( sz == 0 ) continue;
		switch ( i )
		{
		case 0:  // Position
			*(int*)( pBase + offSize_Pos ) = sz;
			break;
		case 1:  // Normal
			*(int*)( pBase + offSize_Nor ) = sz;
			break;
		case 2:  // Color
			*(int*)( pBase + offSize_Col ) = sz;
			break;
		case 3:  // Specular
			break;
		case 12: // Bone weights + indices
			*(int*)( pBase + offSize_BW ) = 16;
			*(int*)( pBase + offSize_BM ) = 4;
			*(int*)( pBase + offNumBW ) = 2;
			break;
		case 13:
			*(int*)( pBase + offSize_TS ) = sz;
			break;
		case 14:
			*(int*)( pBase + offSize_TT ) = sz;
			break;
		default:
			if ( i >= 4 && i <= 11 )
			{
				int tc = i - 4;
				if ( tc < 16 )
					*(int*)( pBase + offSize_TC + tc * sizeof(int) ) = sz;
			}
			break;
		}
	}

	*(int*)( pBase + offSize_UD ) = nUserDataSize * 4;
	*(int*)( pBase + offStride ) = e->nTotalVertexSize;
}

//-----------------------------------------------------------------------------
// Primitive type helpers.  Convert Source engine MaterialPrimitiveType_t
// to counts for validation / draw routing.
//-----------------------------------------------------------------------------
extern "C" int DxvkPrimitiveIndexCount( MaterialPrimitiveType_t type, int nNumPrimitives )
{
	switch ( type )
	{
	case MATERIAL_POINTS:          return nNumPrimitives * 1;
	case MATERIAL_LINES:           return nNumPrimitives * 2;
	case MATERIAL_TRIANGLES:       return nNumPrimitives * 3;
	case MATERIAL_TRIANGLE_STRIP:  return nNumPrimitives + 2;
	case MATERIAL_LINE_STRIP:      return nNumPrimitives + 1;
	case MATERIAL_POLYGON:         return nNumPrimitives;
	case MATERIAL_QUADS:           return nNumPrimitives * 4;
	default:                       return nNumPrimitives * 3;
	}
}

extern "C" int DxvkPrimitiveCount( MaterialPrimitiveType_t type, int nNumIndices )
{
	if ( nNumIndices <= 0 ) return 0;
	switch ( type )
	{
	case MATERIAL_POINTS:          return nNumIndices;
	case MATERIAL_LINES:           return nNumIndices / 2;
	case MATERIAL_TRIANGLES:       return nNumIndices / 3;
	case MATERIAL_TRIANGLE_STRIP:  return ( nNumIndices >= 2 ) ? ( nNumIndices - 2 ) : 0;
	case MATERIAL_LINE_STRIP:      return ( nNumIndices >= 1 ) ? ( nNumIndices - 1 ) : 0;
	case MATERIAL_QUADS:           return nNumIndices / 4;
	case MATERIAL_POLYGON:         return nNumIndices;
	default:                       return nNumIndices / 3;
	}
}

extern "C" bool DxvkIsValidPrimitiveType( MaterialPrimitiveType_t type )
{
	switch ( type )
	{
	case MATERIAL_POINTS:
	case MATERIAL_LINES:
	case MATERIAL_TRIANGLES:
	case MATERIAL_TRIANGLE_STRIP:
	case MATERIAL_LINE_STRIP:
	case MATERIAL_POLYGON:
	case MATERIAL_QUADS:
	case MATERIAL_LINE_LOOP:
		return true;
	default:
		return false;
	}
}

//-----------------------------------------------------------------------------
// Mesh budget accounting: track total bytes allocated for static/dynamic meshes
// across all DXVK mesh instances.  This is read back by the material system's
// memory budget panel via IMaterialSystemHardwareConfig.
//-----------------------------------------------------------------------------
struct DxvkMeshBudget_t
{
	int64_t nStaticVertexBytes;
	int64_t nStaticIndexBytes;
	int64_t nDynamicVertexBytes;
	int64_t nDynamicIndexBytes;
	int64_t nFlexVertexBytes;
	int     nStaticMeshCount;
	int     nDynamicMeshCount;
	int     nFlexMeshCount;
	bool    bLimitReached;
};

static DxvkMeshBudget_t s_DxvkMeshBudget = {};
static CThreadMutex s_DxvkMeshBudgetMutex;

extern "C" void DxvkMeshBudgetAddStatic( int64_t vertBytes, int64_t idxBytes )
{
	AUTO_LOCK( s_DxvkMeshBudgetMutex );
	s_DxvkMeshBudget.nStaticVertexBytes += vertBytes;
	s_DxvkMeshBudget.nStaticIndexBytes += idxBytes;
	s_DxvkMeshBudget.nStaticMeshCount++;
	int64_t nTotal = s_DxvkMeshBudget.nStaticVertexBytes + s_DxvkMeshBudget.nStaticIndexBytes
				   + s_DxvkMeshBudget.nDynamicVertexBytes + s_DxvkMeshBudget.nDynamicIndexBytes;
	s_DxvkMeshBudget.bLimitReached = nTotal > ( 512ll * 1024 * 1024 );
}

extern "C" void DxvkMeshBudgetAddDynamic( int64_t vertBytes, int64_t idxBytes )
{
	AUTO_LOCK( s_DxvkMeshBudgetMutex );
	s_DxvkMeshBudget.nDynamicVertexBytes += vertBytes;
	s_DxvkMeshBudget.nDynamicIndexBytes += idxBytes;
	s_DxvkMeshBudget.nDynamicMeshCount++;
}

extern "C" void DxvkMeshBudgetAddFlex( int64_t vertBytes )
{
	AUTO_LOCK( s_DxvkMeshBudgetMutex );
	s_DxvkMeshBudget.nFlexVertexBytes += vertBytes;
	s_DxvkMeshBudget.nFlexMeshCount++;
}

extern "C" void DxvkMeshBudgetGet( int64_t* pStaticV, int64_t* pStaticI,
								  int64_t* pDynamicV, int64_t* pDynamicI,
								  int64_t* pFlexV, int* pMeshCount )
{
	AUTO_LOCK( s_DxvkMeshBudgetMutex );
	if ( pStaticV ) *pStaticV = s_DxvkMeshBudget.nStaticVertexBytes;
	if ( pStaticI ) *pStaticI = s_DxvkMeshBudget.nStaticIndexBytes;
	if ( pDynamicV ) *pDynamicV = s_DxvkMeshBudget.nDynamicVertexBytes;
	if ( pDynamicI ) *pDynamicI = s_DxvkMeshBudget.nDynamicIndexBytes;
	if ( pFlexV ) *pFlexV = s_DxvkMeshBudget.nFlexVertexBytes;
	if ( pMeshCount )
		*pMeshCount = s_DxvkMeshBudget.nStaticMeshCount +
					  s_DxvkMeshBudget.nDynamicMeshCount +
					  s_DxvkMeshBudget.nFlexMeshCount;
}

extern "C" void DxvkMeshBudgetReset()
{
	AUTO_LOCK( s_DxvkMeshBudgetMutex );
	memset( &s_DxvkMeshBudget, 0, sizeof(s_DxvkMeshBudget) );
}

extern "C" bool DxvkMeshBudgetIsLimitReached()
{
	AUTO_LOCK( s_DxvkMeshBudgetMutex );
	return s_DxvkMeshBudget.bLimitReached;
}

//-----------------------------------------------------------------------------
// IndexDesc_t / VertexDesc_t initialiser helpers.  These take an empty
// descriptor struct and fill it with sane default values so that callers
// don't accidentally dereference stale pointers.
//-----------------------------------------------------------------------------
extern "C" void DxvkInitVertexDesc( VertexDesc_t* pDesc, void* pBackingMemory, int nStride )
{
	if ( !pDesc ) return;
	memset( pDesc, 0, sizeof(VertexDesc_t) );
	if ( !pBackingMemory ) return;

	pDesc->m_pPosition       = (float*)pBackingMemory;
	pDesc->m_pNormal         = (float*)pBackingMemory;
	pDesc->m_pColor          = (unsigned char*)pBackingMemory;
	pDesc->m_pBoneWeight     = (float*)pBackingMemory;
	pDesc->m_pBoneMatrixIndex= (unsigned char*)pBackingMemory;
	pDesc->m_pTangentS       = (float*)pBackingMemory;
	pDesc->m_pTangentT       = (float*)pBackingMemory;
	pDesc->m_pUserData       = (float*)pBackingMemory;
	pDesc->m_NumBoneWeights  = 2;

	for ( int i = 0; i < VERTEX_MAX_TEXTURE_COORDINATES; ++i )
		pDesc->m_pTexCoord[ i ] = (float*)pBackingMemory;

	pDesc->m_VertexSize_Position       = (unsigned short)nStride;
	pDesc->m_VertexSize_Normal         = (unsigned short)nStride;
	pDesc->m_VertexSize_Color          = (unsigned short)nStride;
	pDesc->m_VertexSize_BoneWeight     = (unsigned short)nStride;
	pDesc->m_VertexSize_BoneMatrixIndex= (unsigned short)nStride;
	pDesc->m_VertexSize_TangentS       = (unsigned short)nStride;
	pDesc->m_VertexSize_TangentT       = (unsigned short)nStride;
	pDesc->m_VertexSize_UserData       = (unsigned short)nStride;
	for ( int i = 0; i < VERTEX_MAX_TEXTURE_COORDINATES; ++i )
		pDesc->m_VertexSize_TexCoord[ i ] = (unsigned short)nStride;
	pDesc->m_ActualVertexSize = (unsigned short)( nStride ? nStride : 32 );
}

extern "C" void DxvkInitIndexDesc( IndexDesc_t* pDesc, void* pBackingMemory, int nIndexSize )
{
	if ( !pDesc ) return;
	memset( pDesc, 0, sizeof(IndexDesc_t) );
	if ( !pBackingMemory ) return;

	pDesc->m_pIndices = (unsigned short*)pBackingMemory;
	pDesc->m_nIndexSize = nIndexSize ? nIndexSize : sizeof(unsigned short);
}

//-----------------------------------------------------------------------------
// Vertex format compatibility mask: returns true if the left format is a
// subset of the right format.  Used when deciding if a mesh built for one
// vertex shader can be used with another.
//-----------------------------------------------------------------------------
extern "C" bool DxvkIsVertexFormatSubset( VertexFormat_t required, VertexFormat_t available )
{
	VertexFormat_t reqPosMask = VERTEX_POSITION | VERTEX_NORMAL | VERTEX_COLOR | VERTEX_SPECULAR
							  | VERTEX_BONE_WEIGHT_MASK | VERTEX_TANGENT_S | VERTEX_TANGENT_T;
	if ( ( required & reqPosMask & ~available ) != 0 )
		return false;

	for ( int tc = 0; tc < VERTEX_MAX_TEXTURE_COORDINATES; ++tc )
	{
		VertexFormat_t mask = VERTEX_TEXCOORD_MASK( tc );
		VertexFormat_t reqTC = required & mask;
		VertexFormat_t avlTC = available & mask;
		if ( reqTC != 0 && avlTC < reqTC )
			return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// CDxvkMesh "Base" module namespace helpers.  These methods are intentionally
// named with the "Base" suffix so they don't collide with the inline
// implementations in shaderdevicemgrdxvk.cpp when the linker resolves symbols.
//-----------------------------------------------------------------------------
namespace DxvkMeshBase
{
	int ComputeMemoryUsed( int nVertexCount, int nIndexCount,
						   VertexFormat_t fmt, int nUserDataSize,
						   bool b32BitIndices )
	{
		int stride = DxvkComputeVertexStride( fmt, nUserDataSize );
		int idxSize = b32BitIndices ? 4 : 2;
		return nVertexCount * stride + nIndexCount * idxSize;
	}

	bool ValidateDrawRange( MaterialPrimitiveType_t primType,
							int firstIndex, int numIndices,
							int vertexCount, int indexCount )
	{
		if ( !DxvkIsValidPrimitiveType( primType ) ) return false;
		if ( firstIndex < 0 || numIndices < 0 ) return false;
		if ( firstIndex + numIndices > indexCount ) return false;
		return true;
	}

	void SpewMeshInfo( const char* pName, bool bIsDynamic,
					   int vertexCount, int indexCount,
					   MaterialPrimitiveType_t primType,
					   VertexFormat_t fmt )
	{
		(void)pName; (void)bIsDynamic; (void)vertexCount; (void)indexCount;
		(void)primType; (void)fmt;
	}

	bool IsDynamicCompatible( VertexFormat_t fmt, int nUserDataSize,
							  int nDesiredVerts, int nDesiredIndices )
	{
		int stride = DxvkComputeVertexStride( fmt, nUserDataSize );
		int bytes = nDesiredVerts * stride + nDesiredIndices * 2;
		return bytes <= kDefaultVertexBufferSize + kDefaultIndexBufferSize;
	}

	int SafeIndexCount( MaterialPrimitiveType_t primType, int nRequested )
	{
		int maxCount = kMaxIndicesPerDraw;
		switch ( primType )
		{
		case MATERIAL_TRIANGLES:
			nRequested = ( nRequested / 3 ) * 3;
			break;
		case MATERIAL_LINES:
			nRequested = ( nRequested / 2 ) * 2;
			break;
		default:
			break;
		}
		return MIN( nRequested, maxCount );
	}
}
