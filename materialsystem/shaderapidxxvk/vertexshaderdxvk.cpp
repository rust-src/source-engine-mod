//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Vertex Shader - Vertex shader compilation and management
//          Implements Source engine vertex shader support via DXVK Vulkan SPIR-V
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
// Vertex shader metadata entry
//-----------------------------------------------------------------------------
struct DxvkVertexShaderMeta_t
{
	int nShaderId;
	int nRefCount;
	char szName[ 256 ];
	uint32_t nSpirvWordCount;
	uint32_t* pSpirvCode;
	bool bValid;
	bool bIsStatic;
	int nVertexFormatRequirements;
	int nConstantBufferSize[ 8 ];
	int nSamplerCount;
	int nInputSlotMask;
	int nOutputSlotMask;
	void* pVkShaderModule;
	void* pPipelineLayoutRef;
};

static const int kDxVkMaxVertexShaders = 1024;
static DxvkVertexShaderMeta_t s_DxvkVertexShaders[ kDxVkMaxVertexShaders ];
static int s_DxvkNextVertexShaderId = 1;

//-----------------------------------------------------------------------------
// Vertex shader static constant buffer
//-----------------------------------------------------------------------------
struct DxvkVsConstantBuffer_t
{
	int nSlot;
	int nSize;
	int nUsedCount;
	float fData[ 256 ][ 4 ];
	bool bDirty;
};

static const int kDxVkMaxVsCBuffers = 8;
static DxvkVsConstantBuffer_t s_DxVkCurrentVsCBuffers[ kDxVkMaxVsCBuffers ];
static VertexShaderHandle_t s_DxvkBoundVertexShader = VERTEX_SHADER_HANDLE_INVALID;
static int s_DxvkCurrentUserBoneCount = 0;
static int s_DxvkCurrentLightCombo = 0;

//-----------------------------------------------------------------------------
// CVertexShaderDxVk - Manager for vertex shader creation and binding
//-----------------------------------------------------------------------------
class CVertexShaderDxVk
{
public:
	CVertexShaderDxVk();
	~CVertexShaderDxVk();

	bool Init();
	void Shutdown();

	// Create from SPIR-V or bytecode
	VertexShaderHandle_t CreateShader(
		const uint32_t* pSpirvCode, size_t nWordCount,
		const char* pDebugName = nullptr,
		bool bIsStatic = false,
		int nVertexFormatRequirements = 0 );
	VertexShaderHandle_t CreateFromBytecode(
		const void* pBytecode, size_t nBytecodeSize,
		const char* pDebugName = nullptr,
		bool bIsStatic = false,
		int nVertexFormatRequirements = 0 );
	VertexShaderHandle_t CreateFromCompiledFile(
		const char* pFileName, int nFileIndex = 0,
		const char* pProfile = "vs_3_0" );
	void DestroyShader( VertexShaderHandle_t hShader );

	// Binding / state
	void BindShader( VertexShaderHandle_t hShader );
	VertexShaderHandle_t GetBoundShader() const { return s_DxvkBoundVertexShader; }
	const DxvkVertexShaderMeta_t* GetShaderMeta( VertexShaderHandle_t hShader ) const;

	// Constants
	bool SetConstantFloat( int nSlot, int nRegIndex,
						   const float* pValues, int nVec4Count = 1,
						   bool bForce = false );
	bool SetConstantInt( int nSlot, int nRegIndex,
						 const int* pValues, int nVec4Count = 1,
						 bool bForce = false );
	bool SetConstantBool( int nSlot, int nRegIndex,
						  const BOOL* pValues, int nBoolCount = 1,
						  bool bForce = false );

	void FlushConstants( bool bAllSlots = true );
	void InvalidateAllConstants();
	bool IsConstantDirty( int nSlot ) const;

	// Skinning matrices
	void SetSkinningMatrices( int nMatrixCount, const VMatrix* pMatrices );
	void SetModelViewMatrix( const VMatrix& mv );
	void SetProjectionMatrix( const VMatrix& proj );
	void SetModelViewProjection( const VMatrix& mvp );

	// Lighting helpers
	void SetAmbientCube( const Vector4D ambientCube[6] );
	void SetLight( int nLightIndex, const float* pLightPos,
				   const float* pLightColor, float fRange );
	int GetMaxBones() const { return 53; }
	int GetCurrentBoneCount() const { return s_DxvkCurrentUserBoneCount; }
	void SetCurrentBoneCount( int nBones ) { s_DxvkCurrentUserBoneCount = nBones; }

	// Standard shader indices
	void SetStandardVertexShaderIndex( int nIndex ) { m_nStandardIndex = nIndex; }
	int GetStandardVertexShaderIndex() const { return m_nStandardIndex; }

	// Ref counting
	void AddRef( VertexShaderHandle_t hShader );
	void Release( VertexShaderHandle_t hShader );

	// DXVK/Vulkan access
	void* GetShaderModule( VertexShaderHandle_t hShader ) const;
	void* GetPipelineLayout( VertexShaderHandle_t hShader ) const;
	uint32_t GetSpirvWordCount( VertexShaderHandle_t hShader ) const;
	const uint32_t* GetSpirvCode( VertexShaderHandle_t hShader ) const;

	int GetShaderCount() const { return s_DxvkNextVertexShaderId - 1; }
	void PurgeUnusedShaders();
	void DumpStats() const;

private:
	int AllocShaderId();
	void FreeShaderId( int nId );
	void InitDefaultCBuffers();
	bool ValidateHandle( VertexShaderHandle_t hShader ) const;

	int m_nStandardIndex;
	bool m_bInitialized;

	VMatrix m_SkinningMatrices[ 64 ];
	VMatrix m_ModelView;
	VMatrix m_Projection;
	VMatrix m_ModelViewProjection;
	Vector4D m_AmbientCube[ 6 ];
};

static CVertexShaderDxVk s_VertexShaderMgrDxVk;

//-----------------------------------------------------------------------------
// CVertexShaderDxVk Implementation
//-----------------------------------------------------------------------------
CVertexShaderDxVk::CVertexShaderDxVk() :
	m_nStandardIndex( 0 ),
	m_bInitialized( false )
{
	memset( m_AmbientCube, 0, sizeof(m_AmbientCube) );
}

CVertexShaderDxVk::~CVertexShaderDxVk()
{
	Shutdown();
}

bool CVertexShaderDxVk::Init()
{
	if ( m_bInitialized ) return true;
	memset( s_DxvkVertexShaders, 0, sizeof(s_DxvkVertexShaders) );
	s_DxvkNextVertexShaderId = 1;
	s_DxvkBoundVertexShader = VERTEX_SHADER_HANDLE_INVALID;
	s_DxvkCurrentUserBoneCount = 0;
	s_DxvkCurrentLightCombo = 0;
	InitDefaultCBuffers();
	m_bInitialized = true;
	return true;
}

void CVertexShaderDxVk::Shutdown()
{
	for ( int i = 0; i < kDxVkMaxVertexShaders; ++i )
	{
		DxvkVertexShaderMeta_t& meta = s_DxvkVertexShaders[ i ];
		if ( meta.pSpirvCode )
		{
			free( meta.pSpirvCode );
			meta.pSpirvCode = nullptr;
		}
	}
	memset( s_DxvkVertexShaders, 0, sizeof(s_DxvkVertexShaders) );
	s_DxvkNextVertexShaderId = 1;
	s_DxvkBoundVertexShader = VERTEX_SHADER_HANDLE_INVALID;
	s_DxvkCurrentUserBoneCount = 0;
	s_DxvkCurrentLightCombo = 0;
	m_nStandardIndex = 0;
	m_bInitialized = false;
}

VertexShaderHandle_t CVertexShaderDxVk::CreateShader(
	const uint32_t* pSpirvCode, size_t nWordCount,
	const char* pDebugName, bool bIsStatic,
	int nVertexFormatRequirements )
{
	if ( !m_bInitialized ) Init();
	if ( !pSpirvCode || nWordCount == 0 ) return VERTEX_SHADER_HANDLE_INVALID;

	int nId = AllocShaderId();
	if ( nId == 0 ) return VERTEX_SHADER_HANDLE_INVALID;

	DxvkVertexShaderMeta_t& meta = s_DxvkVertexShaders[ nId ];
	memset( &meta, 0, sizeof(meta) );
	meta.nShaderId = nId;
	meta.nRefCount = 1;
	meta.bValid = true;
	meta.bIsStatic = bIsStatic;
	meta.nVertexFormatRequirements = nVertexFormatRequirements;
	meta.nSpirvWordCount = (uint32_t)nWordCount;
	meta.nSamplerCount = 0;
	meta.nInputSlotMask = 0;
	meta.nOutputSlotMask = 0;
	if ( pDebugName )
		Q_strncpy( meta.szName, pDebugName, sizeof(meta.szName) - 1 );

	meta.pSpirvCode = (uint32_t*)malloc( nWordCount * sizeof(uint32_t) );
	if ( meta.pSpirvCode )
		memcpy( meta.pSpirvCode, pSpirvCode, nWordCount * sizeof(uint32_t) );

	for ( int s = 0; s < 8; ++s )
		meta.nConstantBufferSize[ s ] = 0;

	return (VertexShaderHandle_t)nId;
}

VertexShaderHandle_t CVertexShaderDxVk::CreateFromBytecode(
	const void* pBytecode, size_t nBytecodeSize,
	const char* pDebugName, bool bIsStatic,
	int nVertexFormatRequirements )
{
	if ( !pBytecode || nBytecodeSize < 4 ) return VERTEX_SHADER_HANDLE_INVALID;
	if ( nBytecodeSize % 4 != 0 ) return VERTEX_SHADER_HANDLE_INVALID;
	return CreateShader( (const uint32_t*)pBytecode, nBytecodeSize / 4,
						 pDebugName, bIsStatic, nVertexFormatRequirements );
}

VertexShaderHandle_t CVertexShaderDxVk::CreateFromCompiledFile(
	const char* pFileName, int nFileIndex, const char* pProfile )
{
	(void)pProfile; (void)nFileIndex;
	return VERTEX_SHADER_HANDLE_INVALID;
}

void CVertexShaderDxVk::DestroyShader( VertexShaderHandle_t hShader )
{
	if ( !ValidateHandle( hShader ) ) return;
	int nId = (int)hShader;
	DxvkVertexShaderMeta_t& meta = s_DxvkVertexShaders[ nId ];
	meta.nRefCount = ( meta.nRefCount > 0 ) ? meta.nRefCount - 1 : 0;
	if ( meta.nRefCount == 0 && !meta.bIsStatic )
		FreeShaderId( nId );
}

void CVertexShaderDxVk::BindShader( VertexShaderHandle_t hShader )
{
	if ( ValidateHandle( hShader ) || hShader == VERTEX_SHADER_HANDLE_INVALID )
		s_DxvkBoundVertexShader = hShader;
}

const DxvkVertexShaderMeta_t* CVertexShaderDxVk::GetShaderMeta( VertexShaderHandle_t hShader ) const
{
	if ( !ValidateHandle( hShader ) ) return nullptr;
	return &s_DxvkVertexShaders[ (int)hShader ];
}

bool CVertexShaderDxVk::SetConstantFloat( int nSlot, int nRegIndex,
										  const float* pValues, int nVec4Count,
										  bool bForce )
{
	if ( nSlot < 0 || nSlot >= kDxVkMaxVsCBuffers || !pValues ) return false;
	DxvkVsConstantBuffer_t& cb = s_DxVkCurrentVsCBuffers[ nSlot ];
	int nLast = nRegIndex + nVec4Count;
	if ( nLast > 256 ) return false;
	for ( int i = 0; i < nVec4Count; ++i )
	{
		for ( int j = 0; j < 4; ++j )
			cb.fData[ nRegIndex + i ][ j ] = pValues[ i * 4 + j ];
	}
	cb.bDirty = true;
	return true;
}

bool CVertexShaderDxVk::SetConstantInt( int nSlot, int nRegIndex,
										const int* pValues, int nVec4Count,
										bool bForce )
{
	if ( nSlot < 0 || nSlot >= kDxVkMaxVsCBuffers || !pValues ) return false;
	DxvkVsConstantBuffer_t& cb = s_DxVkCurrentVsCBuffers[ nSlot ];
	int nLast = nRegIndex + nVec4Count;
	if ( nLast > 256 ) return false;
	for ( int i = 0; i < nVec4Count; ++i )
	{
		for ( int j = 0; j < 4; ++j )
			cb.fData[ nRegIndex + i ][ j ] = (float)pValues[ i * 4 + j ];
	}
	cb.bDirty = true;
	return true;
}

bool CVertexShaderDxVk::SetConstantBool( int nSlot, int nRegIndex,
										 const BOOL* pValues, int nBoolCount,
										 bool bForce )
{
	if ( nSlot < 0 || nSlot >= kDxVkMaxVsCBuffers || !pValues ) return false;
	DxvkVsConstantBuffer_t& cb = s_DxVkCurrentVsCBuffers[ nSlot ];
	for ( int i = 0; i < nBoolCount; ++i )
	{
		int idx = nRegIndex + ( i / 4 );
		if ( idx >= 256 ) break;
		int comp = i % 4;
		cb.fData[ idx ][ comp ] = pValues[ i ] ? -1.0f : 0.0f;
	}
	cb.bDirty = true;
	return true;
}

void CVertexShaderDxVk::FlushConstants( bool bAllSlots )
{
	int nStart = bAllSlots ? 0 : 0;
	int nEnd = bAllSlots ? kDxVkMaxVsCBuffers : kDxVkMaxVsCBuffers;
	for ( int s = nStart; s < nEnd; ++s )
		s_DxVkCurrentVsCBuffers[ s ].bDirty = false;
}

void CVertexShaderDxVk::InvalidateAllConstants()
{
	for ( int s = 0; s < kDxVkMaxVsCBuffers; ++s )
		s_DxVkCurrentVsCBuffers[ s ].bDirty = true;
}

bool CVertexShaderDxVk::IsConstantDirty( int nSlot ) const
{
	if ( nSlot < 0 || nSlot >= kDxVkMaxVsCBuffers ) return false;
	return s_DxVkCurrentVsCBuffers[ nSlot ].bDirty;
}

void CVertexShaderDxVk::SetSkinningMatrices( int nMatrixCount, const VMatrix* pMatrices )
{
	if ( !pMatrices ) return;
	int nLimit = MIN( nMatrixCount, 64 );
	for ( int i = 0; i < nLimit; ++i )
		m_SkinningMatrices[ i ] = pMatrices[ i ];
}

void CVertexShaderDxVk::SetModelViewMatrix( const VMatrix& mv )
{
	m_ModelView = mv;
}

void CVertexShaderDxVk::SetProjectionMatrix( const VMatrix& proj )
{
	m_Projection = proj;
}

void CVertexShaderDxVk::SetModelViewProjection( const VMatrix& mvp )
{
	m_ModelViewProjection = mvp;
}

void CVertexShaderDxVk::SetAmbientCube( const Vector4D ambientCube[6] )
{
	if ( !ambientCube ) return;
	for ( int i = 0; i < 6; ++i )
		m_AmbientCube[ i ] = ambientCube[ i ];
}

void CVertexShaderDxVk::SetLight( int nLightIndex, const float* pLightPos,
								  const float* pLightColor, float fRange )
{
	(void)fRange;
	if ( nLightIndex < 0 || nLightIndex >= 4 ) return;
}

void CVertexShaderDxVk::AddRef( VertexShaderHandle_t hShader )
{
	if ( ValidateHandle( hShader ) )
		s_DxvkVertexShaders[ (int)hShader ].nRefCount++;
}

void CVertexShaderDxVk::Release( VertexShaderHandle_t hShader )
{
	DestroyShader( hShader );
}

void* CVertexShaderDxVk::GetShaderModule( VertexShaderHandle_t hShader ) const
{
	if ( !ValidateHandle( hShader ) ) return nullptr;
	return s_DxvkVertexShaders[ (int)hShader ].pVkShaderModule;
}

void* CVertexShaderDxVk::GetPipelineLayout( VertexShaderHandle_t hShader ) const
{
	if ( !ValidateHandle( hShader ) ) return nullptr;
	return s_DxvkVertexShaders[ (int)hShader ].pPipelineLayoutRef;
}

uint32_t CVertexShaderDxVk::GetSpirvWordCount( VertexShaderHandle_t hShader ) const
{
	if ( !ValidateHandle( hShader ) ) return 0;
	return s_DxvkVertexShaders[ (int)hShader ].nSpirvWordCount;
}

const uint32_t* CVertexShaderDxVk::GetSpirvCode( VertexShaderHandle_t hShader ) const
{
	if ( !ValidateHandle( hShader ) ) return nullptr;
	return s_DxvkVertexShaders[ (int)hShader ].pSpirvCode;
}

void CVertexShaderDxVk::PurgeUnusedShaders()
{
	for ( int i = 1; i < kDxVkMaxVertexShaders; ++i )
	{
		DxvkVertexShaderMeta_t& meta = s_DxvkVertexShaders[ i ];
		if ( meta.bValid && meta.nRefCount == 0 && !meta.bIsStatic )
			FreeShaderId( i );
	}
}

void CVertexShaderDxVk::DumpStats() const
{
}

int CVertexShaderDxVk::AllocShaderId()
{
	for ( int i = 1; i < kDxVkMaxVertexShaders; ++i )
	{
		int idx = ( s_DxvkNextVertexShaderId + i ) % kDxVkMaxVertexShaders;
		if ( idx == 0 ) idx = 1;
		if ( !s_DxvkVertexShaders[ idx ].bValid )
		{
			s_DxvkNextVertexShaderId = idx + 1;
			return idx;
		}
	}
	return 0;
}

void CVertexShaderDxVk::FreeShaderId( int nId )
{
	if ( nId <= 0 || nId >= kDxVkMaxVertexShaders ) return;
	DxvkVertexShaderMeta_t& meta = s_DxvkVertexShaders[ nId ];
	if ( meta.pSpirvCode )
	{
		free( meta.pSpirvCode );
		meta.pSpirvCode = nullptr;
	}
	memset( &meta, 0, sizeof(meta) );
}

void CVertexShaderDxVk::InitDefaultCBuffers()
{
	for ( int s = 0; s < kDxVkMaxVsCBuffers; ++s )
	{
		memset( &s_DxVkCurrentVsCBuffers[ s ], 0, sizeof(DxvkVsConstantBuffer_t) );
		s_DxVkCurrentVsCBuffers[ s ].nSlot = s;
		s_DxVkCurrentVsCBuffers[ s ].nSize = 256 * 4 * sizeof(float);
		s_DxVkCurrentVsCBuffers[ s ].bDirty = true;
	}
}

bool CVertexShaderDxVk::ValidateHandle( VertexShaderHandle_t hShader ) const
{
	int nId = (int)hShader;
	if ( nId <= 0 || nId >= kDxVkMaxVertexShaders ) return false;
	return s_DxvkVertexShaders[ nId ].bValid;
}
