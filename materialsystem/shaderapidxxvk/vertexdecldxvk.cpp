//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Vertex Declaration - Vertex format and layout management
//          Implements Source engine vertex declaration support for DXVK Vulkan
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
#include "bitmap/imageformat.h"
#include "tier1/utlbuffer.h"
#include "dxvk_adapter.h"

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

//-----------------------------------------------------------------------------
// Vertex element usage enumeration (matches Source engine definitions)
//-----------------------------------------------------------------------------
enum DxvkVertexElementUsage_t
{
	DXVIK_POSITION = 0,
	DXVIK_BLENDWEIGHT,
	DXVIK_BLENDINDICES,
	DXVIK_NORMAL,
	DXVIK_POINTSIZE,
	DXVIK_TEXCOORD,
	DXVIK_TANGENT,
	DXVIK_BINORMAL,
	DXVIK_TESSFACTOR,
	DXVIK_POSITIONT,
	DXVIK_COLOR,
	DXVIK_FOG,
	DXVIK_DEPTH,
	DXVIK_SAMPLE,
	DXVIK_USAGE_MAX
};

//-----------------------------------------------------------------------------
// Vertex element type enumeration
//-----------------------------------------------------------------------------
enum DxvkVertexElementType_t
{
	DXVIK_TYPE_FLOAT1 = 0,
	DXVIK_TYPE_FLOAT2 = 1,
	DXVIK_TYPE_FLOAT3 = 2,
	DXVIK_TYPE_FLOAT4 = 3,
	DXVIK_TYPE_D3DCOLOR = 4,
	DXVIK_TYPE_UBYTE4 = 5,
	DXVIK_TYPE_SHORT2 = 6,
	DXVIK_TYPE_SHORT4 = 7,
	DXVIK_TYPE_UBYTE4N = 8,
	DXVIK_TYPE_SHORT2N = 9,
	DXVIK_TYPE_SHORT4N = 10,
	DXVIK_TYPE_USHORT2N = 11,
	DXVIK_TYPE_USHORT4N = 12,
	DXVIK_TYPE_HALF2 = 13,
	DXVIK_TYPE_HALF4 = 14,
	DXVIK_TYPE_INVALID = 0x11,
	DXVIK_TYPE_UNUSED = 0x17,
	DXVIK_TYPE_INT = 0x16,
};

//-----------------------------------------------------------------------------
// Single DXVK vertex element definition
//-----------------------------------------------------------------------------
struct DxvkVertexElement_t
{
	uint16_t nStream;
	uint16_t nOffset;
	uint8_t  nType;       // DxvkVertexElementType_t
	uint8_t  nMethod;
	uint8_t  nUsage;      // DxvkVertexElementUsage_t
	uint8_t  nUsageIndex;
};

//-----------------------------------------------------------------------------
// DXVK Vertex declaration - full layout of a vertex stream
//-----------------------------------------------------------------------------
struct DxvkVertexDecl_t
{
	int nDeclId;
	int nElementCount;
	int nStrides[ 16 ];
	DxvkVertexElement_t elements[ 64 ];
	uint32_t nVertexFormatFlags;
	int nUserDataSize;
	bool bUsesSkinning;
	bool bUsesMorphing;
	bool bUsesLighting;
	bool bValid;
};

static const int kDxVkMaxVertexDecls = 4096;
static DxvkVertexDecl_t s_DxvkVertexDecls[ kDxVkMaxVertexDecls ];
static int s_DxvkNextDeclId = 1;

//-----------------------------------------------------------------------------
// Per-stream vertex input binding for Vulkan pipeline creation
//-----------------------------------------------------------------------------
struct DxvkVkVertexBinding_t
{
	uint32_t nBinding;
	uint32_t nStride;
	uint32_t nInputRate;    // 0 = per-vertex, 1 = per-instance
};

//-----------------------------------------------------------------------------
// Per-attribute vertex input attribute for Vulkan pipeline creation
//-----------------------------------------------------------------------------
struct DxvkVkVertexAttribute_t
{
	uint32_t nLocation;
	uint32_t nBinding;
	uint32_t nFormat;     // VK_FORMAT
	uint32_t nOffset;
};

//-----------------------------------------------------------------------------
// Vertex Declaration Manager - CVertexDeclDxVk
//-----------------------------------------------------------------------------
class CVertexDeclDxVk
{
public:
	CVertexDeclDxVk();
	~CVertexDeclDxVk();

	bool Init();
	void Shutdown();

	// Allocate/find a vertex declaration
	int CreateVertexDecl( int nElementCount,
						  const DxvkVertexElement_t* pElements,
						  const int* pStreamStrides = nullptr,
						  uint32_t nVertexFormatFlags = 0,
						  int nUserDataSize = 0 );
	void DestroyVertexDecl( int nDeclId );
	const DxvkVertexDecl_t* GetVertexDecl( int nDeclId ) const;

	// Helpers
	int FindDeclByElements( int nElementCount,
							const DxvkVertexElement_t* pElements ) const;
	int GetElementCount( int nDeclId ) const;
	int GetStreamCount( int nDeclId ) const;
	int GetStreamStride( int nDeclId, int nStream ) const;
	uint32_t GetVertexFormatFlags( int nDeclId ) const;
	int GetUserDataSize( int nDeclId ) const;

	// Convert to Vulkan binding/attribute arrays
	uint32_t BuildPipelineVertexInput(
		int nDeclId,
		DxvkVkVertexBinding_t* pOutBindings, uint32_t nMaxBindings,
		DxvkVkVertexAttribute_t* pOutAttributes, uint32_t nMaxAttributes,
		uint32_t nFirstBinding = 0, uint32_t nFirstLocation = 0 ) const;

	// Vertex format compatibility helpers
	bool IsCompatible( int nDeclId0, int nDeclId1 ) const;
	bool IsValidDecl( int nDeclId ) const;

	// From Source engine VertexFormat_t
	int CreateDeclFromVertexFormat( VertexFormat_t vertexFormat, int nUserDataSize = 0 );
	VertexFormat_t ComputeVertexFormat( int nDeclId ) const;

	int GetTotalDeclCount() const { return s_DxvkNextDeclId - 1; }

private:
	bool ValidateElements( int nElementCount, const DxvkVertexElement_t* pElements ) const;
	void ComputeDerivedInfo( DxvkVertexDecl_t* pDecl );
	uint32_t VertexUsageToVkFormat( uint8_t nUsage, uint8_t nType ) const;
	uint32_t TypeSizeBytes( uint8_t nType ) const;
	uint32_t UsageToLocation( uint8_t nUsage, uint8_t nUsageIndex ) const;
};

static CVertexDeclDxVk s_VertexDeclMgrDxVk;

//-----------------------------------------------------------------------------
// CVertexDeclDxVk Implementation
//-----------------------------------------------------------------------------
CVertexDeclDxVk::CVertexDeclDxVk()
{
}

CVertexDeclDxVk::~CVertexDeclDxVk()
{
	Shutdown();
}

bool CVertexDeclDxVk::Init()
{
	memset( s_DxvkVertexDecls, 0, sizeof(s_DxvkVertexDecls) );
	s_DxvkNextDeclId = 1;
	return true;
}

void CVertexDeclDxVk::Shutdown()
{
	memset( s_DxvkVertexDecls, 0, sizeof(s_DxvkVertexDecls) );
	s_DxvkNextDeclId = 1;
}

int CVertexDeclDxVk::CreateVertexDecl( int nElementCount,
									   const DxvkVertexElement_t* pElements,
									   const int* pStreamStrides,
									   uint32_t nVertexFormatFlags,
									   int nUserDataSize )
{
	if ( !ValidateElements( nElementCount, pElements ) )
		return 0;

	int existing = FindDeclByElements( nElementCount, pElements );
	if ( existing > 0 ) return existing;

	int nId = 0;
	for ( int i = 1; i < kDxVkMaxVertexDecls; ++i )
	{
		int idx = ( s_DxvkNextDeclId + i ) % kDxVkMaxVertexDecls;
		if ( idx == 0 ) idx = 1;
		if ( !s_DxvkVertexDecls[ idx ].bValid )
		{
			nId = idx;
			s_DxvkNextDeclId = idx + 1;
			break;
		}
	}
	if ( nId == 0 ) return 0;

	DxvkVertexDecl_t* pDecl = &s_DxvkVertexDecls[ nId ];
	memset( pDecl, 0, sizeof(DxvkVertexDecl_t) );
	pDecl->nDeclId = nId;
	pDecl->nElementCount = nElementCount;
	pDecl->nVertexFormatFlags = nVertexFormatFlags;
	pDecl->nUserDataSize = nUserDataSize;
	pDecl->bValid = true;
	memcpy( pDecl->elements, pElements, sizeof(DxvkVertexElement_t) * nElementCount );

	if ( pStreamStrides )
	{
		for ( int s = 0; s < 16; ++s )
			pDecl->nStrides[ s ] = pStreamStrides[ s ];
	}

	ComputeDerivedInfo( pDecl );
	return nId;
}

void CVertexDeclDxVk::DestroyVertexDecl( int nDeclId )
{
	if ( nDeclId > 0 && nDeclId < kDxVkMaxVertexDecls )
		memset( &s_DxvkVertexDecls[ nDeclId ], 0, sizeof(DxvkVertexDecl_t) );
}

const DxvkVertexDecl_t* CVertexDeclDxVk::GetVertexDecl( int nDeclId ) const
{
	if ( nDeclId <= 0 || nDeclId >= kDxVkMaxVertexDecls ) return nullptr;
	if ( !s_DxvkVertexDecls[ nDeclId ].bValid ) return nullptr;
	return &s_DxvkVertexDecls[ nDeclId ];
}

int CVertexDeclDxVk::FindDeclByElements( int nElementCount,
										  const DxvkVertexElement_t* pElements ) const
{
	for ( int i = 1; i < kDxVkMaxVertexDecls; ++i )
	{
		const DxvkVertexDecl_t* pD = &s_DxvkVertexDecls[ i ];
		if ( !pD->bValid ) continue;
		if ( pD->nElementCount != nElementCount ) continue;
		if ( memcmp( pD->elements, pElements, sizeof(DxvkVertexElement_t) * nElementCount ) == 0 )
			return i;
	}
	return 0;
}

int CVertexDeclDxVk::GetElementCount( int nDeclId ) const
{
	const DxvkVertexDecl_t* pD = GetVertexDecl( nDeclId );
	return pD ? pD->nElementCount : 0;
}

int CVertexDeclDxVk::GetStreamCount( int nDeclId ) const
{
	const DxvkVertexDecl_t* pD = GetVertexDecl( nDeclId );
	if ( !pD ) return 0;
	int nMax = 0;
	for ( int i = 0; i < pD->nElementCount; ++i )
		if ( pD->elements[ i ].nStream + 1 > nMax )
			nMax = pD->elements[ i ].nStream + 1;
	return nMax;
}

int CVertexDeclDxVk::GetStreamStride( int nDeclId, int nStream ) const
{
	const DxvkVertexDecl_t* pD = GetVertexDecl( nDeclId );
	if ( !pD || nStream < 0 || nStream >= 16 ) return 0;
	return pD->nStrides[ nStream ];
}

uint32_t CVertexDeclDxVk::GetVertexFormatFlags( int nDeclId ) const
{
	const DxvkVertexDecl_t* pD = GetVertexDecl( nDeclId );
	return pD ? pD->nVertexFormatFlags : 0;
}

int CVertexDeclDxVk::GetUserDataSize( int nDeclId ) const
{
	const DxvkVertexDecl_t* pD = GetVertexDecl( nDeclId );
	return pD ? pD->nUserDataSize : 0;
}

uint32_t CVertexDeclDxVk::BuildPipelineVertexInput(
	int nDeclId,
	DxvkVkVertexBinding_t* pOutBindings, uint32_t nMaxBindings,
	DxvkVkVertexAttribute_t* pOutAttributes, uint32_t nMaxAttributes,
	uint32_t nFirstBinding, uint32_t nFirstLocation ) const
{
	const DxvkVertexDecl_t* pD = GetVertexDecl( nDeclId );
	if ( !pD || !pOutBindings || !pOutAttributes ) return 0;

	uint32_t nBindingsUsed = 0;
	uint32_t nAttrsUsed = 0;

	bool bBindingSet[ 16 ] = {};
	for ( int i = 0; i < pD->nElementCount && nAttrsUsed < nMaxAttributes; ++i )
	{
		const DxvkVertexElement_t& elem = pD->elements[ i ];

		if ( !bBindingSet[ elem.nStream ] && nBindingsUsed < nMaxBindings )
		{
			pOutBindings[ nBindingsUsed ].nBinding = nFirstBinding + elem.nStream;
			pOutBindings[ nBindingsUsed ].nStride = pD->nStrides[ elem.nStream ];
			pOutBindings[ nBindingsUsed ].nInputRate = 0;
			bBindingSet[ elem.nStream ] = true;
			nBindingsUsed++;
		}

		pOutAttributes[ nAttrsUsed ].nLocation = nFirstLocation + nAttrsUsed;
		pOutAttributes[ nAttrsUsed ].nBinding = nFirstBinding + elem.nStream;
		pOutAttributes[ nAttrsUsed ].nFormat = VertexUsageToVkFormat( elem.nUsage, elem.nType );
		pOutAttributes[ nAttrsUsed ].nOffset = elem.nOffset;
		nAttrsUsed++;
	}

	return nAttrsUsed;
}

bool CVertexDeclDxVk::IsCompatible( int nDeclId0, int nDeclId1 ) const
{
	const DxvkVertexDecl_t* pA = GetVertexDecl( nDeclId0 );
	const DxvkVertexDecl_t* pB = GetVertexDecl( nDeclId1 );
	if ( !pA || !pB ) return false;
	if ( pA->nElementCount != pB->nElementCount ) return false;
	for ( int i = 0; i < pA->nElementCount; ++i )
	{
		if ( pA->elements[ i ].nUsage != pB->elements[ i ].nUsage ) return false;
		if ( pA->elements[ i ].nUsageIndex != pB->elements[ i ].nUsageIndex ) return false;
	}
	return true;
}

bool CVertexDeclDxVk::IsValidDecl( int nDeclId ) const
{
	return GetVertexDecl( nDeclId ) != nullptr;
}

int CVertexDeclDxVk::CreateDeclFromVertexFormat( VertexFormat_t vertexFormat, int nUserDataSize )
{
	DxvkVertexElement_t elems[ 64 ];
	int strides[ 16 ] = {};
	int nCount = 0;
	uint32_t nOffset = 0;
	uint16_t nStream = 0;

	(void)nUserDataSize;

	if ( vertexFormat & VERTEX_POSITION )
	{
		elems[ nCount ].nStream = nStream;
		elems[ nCount ].nOffset = nOffset;
		elems[ nCount ].nType = DXVIK_TYPE_FLOAT3;
		elems[ nCount ].nMethod = 0;
		elems[ nCount ].nUsage = DXVIK_POSITION;
		elems[ nCount ].nUsageIndex = 0;
		nOffset += 12; nCount++;
	}

	if ( vertexFormat & VERTEX_NORMAL )
	{
		elems[ nCount ].nStream = nStream;
		elems[ nCount ].nOffset = nOffset;
		elems[ nCount ].nType = DXVIK_TYPE_FLOAT3;
		elems[ nCount ].nMethod = 0;
		elems[ nCount ].nUsage = DXVIK_NORMAL;
		elems[ nCount ].nUsageIndex = 0;
		nOffset += 12; nCount++;
	}

	if ( vertexFormat & VERTEX_COLOR )
	{
		elems[ nCount ].nStream = nStream;
		elems[ nCount ].nOffset = nOffset;
		elems[ nCount ].nType = DXVIK_TYPE_D3DCOLOR;
		elems[ nCount ].nMethod = 0;
		elems[ nCount ].nUsage = DXVIK_COLOR;
		elems[ nCount ].nUsageIndex = 0;
		nOffset += 4; nCount++;
	}

	if ( vertexFormat & VERTEX_SPECULAR )
	{
		elems[ nCount ].nStream = nStream;
		elems[ nCount ].nOffset = nOffset;
		elems[ nCount ].nType = DXVIK_TYPE_D3DCOLOR;
		elems[ nCount ].nMethod = 0;
		elems[ nCount ].nUsage = DXVIK_COLOR;
		elems[ nCount ].nUsageIndex = 1;
		nOffset += 4; nCount++;
	}

	for ( int tc = 0; tc < VERTEX_MAX_TEXTURE_COORDINATES; ++tc )
	{
		int mask = ( VERTEX_TEXCOORD_MASK( VERTEX_TEXCOORD_SIZE_2 ) ) << ( tc * VERTEX_TEXCOORD_BITS );
		if ( vertexFormat & mask )
		{
			elems[ nCount ].nStream = nStream;
			elems[ nCount ].nOffset = nOffset;
			elems[ nCount ].nType = DXVIK_TYPE_FLOAT2;
			elems[ nCount ].nMethod = 0;
			elems[ nCount ].nUsage = DXVIK_TEXCOORD;
			elems[ nCount ].nUsageIndex = tc;
			nOffset += 8; nCount++;
		}
	}

	if ( vertexFormat & VERTEX_BONE_WEIGHT_MASK )
	{
		elems[ nCount ].nStream = nStream;
		elems[ nCount ].nOffset = nOffset;
		elems[ nCount ].nType = DXVIK_TYPE_FLOAT4;
		elems[ nCount ].nMethod = 0;
		elems[ nCount ].nUsage = DXVIK_BLENDWEIGHT;
		elems[ nCount ].nUsageIndex = 0;
		nOffset += 16; nCount++;

		elems[ nCount ].nStream = nStream;
		elems[ nCount ].nOffset = nOffset;
		elems[ nCount ].nType = DXVIK_TYPE_UBYTE4;
		elems[ nCount ].nMethod = 0;
		elems[ nCount ].nUsage = DXVIK_BLENDINDICES;
		elems[ nCount ].nUsageIndex = 0;
		nOffset += 4; nCount++;
	}

	if ( vertexFormat & VERTEX_TANGENT_S )
	{
		elems[ nCount ].nStream = nStream;
		elems[ nCount ].nOffset = nOffset;
		elems[ nCount ].nType = DXVIK_TYPE_FLOAT3;
		elems[ nCount ].nMethod = 0;
		elems[ nCount ].nUsage = DXVIK_TANGENT;
		elems[ nCount ].nUsageIndex = 0;
		nOffset += 12; nCount++;
	}

	if ( vertexFormat & VERTEX_TANGENT_T )
	{
		elems[ nCount ].nStream = nStream;
		elems[ nCount ].nOffset = nOffset;
		elems[ nCount ].nType = DXVIK_TYPE_FLOAT3;
		elems[ nCount ].nMethod = 0;
		elems[ nCount ].nUsage = DXVIK_BINORMAL;
		elems[ nCount ].nUsageIndex = 0;
		nOffset += 12; nCount++;
	}

	strides[ nStream ] = nOffset;

	return CreateVertexDecl( nCount, elems, strides, vertexFormat, 0 );
}

VertexFormat_t CVertexDeclDxVk::ComputeVertexFormat( int nDeclId ) const
{
	const DxvkVertexDecl_t* pD = GetVertexDecl( nDeclId );
	return pD ? (VertexFormat_t)pD->nVertexFormatFlags : (VertexFormat_t)0;
}

bool CVertexDeclDxVk::ValidateElements( int nElementCount,
										 const DxvkVertexElement_t* pElements ) const
{
	if ( nElementCount <= 0 || !pElements ) return false;
	if ( nElementCount > 64 ) return false;
	for ( int i = 0; i < nElementCount; ++i )
	{
		if ( pElements[ i ].nType >= DXVIK_TYPE_INVALID &&
			 pElements[ i ].nType != DXVIK_TYPE_INT )
			return false;
		if ( pElements[ i ].nStream >= 16 )
			return false;
	}
	return true;
}

void CVertexDeclDxVk::ComputeDerivedInfo( DxvkVertexDecl_t* pDecl )
{
	if ( !pDecl ) return;
	pDecl->bUsesSkinning = false;
	pDecl->bUsesMorphing = false;
	pDecl->bUsesLighting = false;

	for ( int i = 0; i < pDecl->nElementCount; ++i )
	{
		switch ( pDecl->elements[ i ].nUsage )
		{
		case DXVIK_BLENDWEIGHT:
		case DXVIK_BLENDINDICES:
			pDecl->bUsesSkinning = true;
			break;
		case DXVIK_POSITION:
			break;
		case DXVIK_NORMAL:
			pDecl->bUsesLighting = true;
			break;
		case DXVIK_COLOR:
			break;
		default:
			break;
		}
	}

	for ( int s = 0; s < 16; ++s )
	{
		if ( pDecl->nStrides[ s ] == 0 )
		{
			uint32_t nMaxOffset = 0;
			for ( int i = 0; i < pDecl->nElementCount; ++i )
			{
				if ( pDecl->elements[ i ].nStream == s )
				{
					uint32_t nEnd = pDecl->elements[ i ].nOffset +
						TypeSizeBytes( pDecl->elements[ i ].nType );
					if ( nEnd > nMaxOffset ) nMaxOffset = nEnd;
				}
			}
			pDecl->nStrides[ s ] = (int)nMaxOffset;
		}
	}
}

uint32_t CVertexDeclDxVk::VertexUsageToVkFormat( uint8_t nUsage, uint8_t nType ) const
{
	(void)nUsage;
	switch ( nType )
	{
	case DXVIK_TYPE_FLOAT1:  return 131;  // VK_FORMAT_R32_SFLOAT
	case DXVIK_TYPE_FLOAT2:  return 103;  // VK_FORMAT_R32G32_SFLOAT
	case DXVIK_TYPE_FLOAT3:  return 106;  // VK_FORMAT_R32G32B32_SFLOAT
	case DXVIK_TYPE_FLOAT4:  return 109;  // VK_FORMAT_R32G32B32A32_SFLOAT
	case DXVIK_TYPE_D3DCOLOR:return 37;   // VK_FORMAT_B8G8R8A8_UNORM
	case DXVIK_TYPE_UBYTE4:  return 30;   // VK_FORMAT_R8G8B8A8_UINT
	case DXVIK_TYPE_UBYTE4N: return 31;   // VK_FORMAT_R8G8B8A8_UNORM
	case DXVIK_TYPE_SHORT2:  return 76;   // VK_FORMAT_R16G16_SINT
	case DXVIK_TYPE_SHORT4:  return 78;   // VK_FORMAT_R16G16B16A16_SINT
	case DXVIK_TYPE_SHORT2N: return 77;   // VK_FORMAT_R16G16_SNORM
	case DXVIK_TYPE_SHORT4N: return 79;   // VK_FORMAT_R16G16B16A16_SNORM
	default:                 return 109;  // default FLOAT4
	}
}

uint32_t CVertexDeclDxVk::TypeSizeBytes( uint8_t nType ) const
{
	switch ( nType )
	{
	case DXVIK_TYPE_FLOAT1: return 4;
	case DXVIK_TYPE_FLOAT2: return 8;
	case DXVIK_TYPE_FLOAT3: return 12;
	case DXVIK_TYPE_FLOAT4: return 16;
	case DXVIK_TYPE_D3DCOLOR: return 4;
	case DXVIK_TYPE_UBYTE4: return 4;
	case DXVIK_TYPE_UBYTE4N: return 4;
	case DXVIK_TYPE_SHORT2: return 4;
	case DXVIK_TYPE_SHORT4: return 8;
	case DXVIK_TYPE_SHORT2N: return 4;
	case DXVIK_TYPE_SHORT4N: return 8;
	case DXVIK_TYPE_USHORT2N: return 4;
	case DXVIK_TYPE_USHORT4N: return 8;
	case DXVIK_TYPE_HALF2: return 4;
	case DXVIK_TYPE_HALF4: return 8;
	case DXVIK_TYPE_INT: return 4;
	default: return 0;
	}
}

uint32_t CVertexDeclDxVk::UsageToLocation( uint8_t nUsage, uint8_t nUsageIndex ) const
{
	switch ( nUsage )
	{
	case DXVIK_POSITION:     return 0;
	case DXVIK_BLENDWEIGHT:  return 1;
	case DXVIK_BLENDINDICES: return 2;
	case DXVIK_NORMAL:       return 3;
	case DXVIK_COLOR:        return 4 + nUsageIndex;
	case DXVIK_TEXCOORD:     return 8 + nUsageIndex;
	case DXVIK_TANGENT:      return 16;
	case DXVIK_BINORMAL:     return 17;
	default:                 return 31;
	}
}
