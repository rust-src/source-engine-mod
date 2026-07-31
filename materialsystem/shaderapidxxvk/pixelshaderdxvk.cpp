//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Pixel Shader - Pixel/fragment shader compilation and management
//          Implements Source engine pixel shader support via DXVK Vulkan SPIR-V
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
// Pixel shader metadata entry
//-----------------------------------------------------------------------------
struct DxvkPixelShaderMeta_t
{
	int nShaderId;
	int nRefCount;
	char szName[ 256 ];
	uint32_t nSpirvWordCount;
	uint32_t* pSpirvCode;
	bool bValid;
	bool bIsStatic;
	bool bUsesDepthOutput;
	bool bUsesFog;
	bool bUsesGrabPass;
	bool bNeedsVertexShaderColor;
	int nSamplerMask;
	int nRenderTargetMask;
	int nConstantBufferSize[ 8 ];
	void* pVkShaderModule;
	void* pPipelineLayoutRef;
};

static const int kDxVkMaxPixelShaders = 1024;
static DxvkPixelShaderMeta_t s_DxvkPixelShaders[ kDxVkMaxPixelShaders ];
static int s_DxvkNextPixelShaderId = 1;

//-----------------------------------------------------------------------------
// Pixel shader static constant buffer state
//-----------------------------------------------------------------------------
struct DxvkPsConstantBuffer_t
{
	int nSlot;
	int nSize;
	int nUsedCount;
	float fData[ 224 ][ 4 ];
	bool bDirty;
};

static const int kDxVkMaxPsCBuffers = 8;
static DxvkPsConstantBuffer_t s_DxVkCurrentPsCBuffers[ kDxVkMaxPsCBuffers ];
static PixelShaderHandle_t s_DxvkBoundPixelShader = PIXEL_SHADER_HANDLE_INVALID;

//-----------------------------------------------------------------------------
// Pixel shader runtime state
//-----------------------------------------------------------------------------
struct DxvkPsFogState_t
{
	float fFogStart;
	float fFogEnd;
	float fFogZ;
	float fFogMaxDensity;
	float fFogColor[ 3 ];
	uint32_t nFogMode;
};

struct DxvkPsToneMapState_t
{
	float fScale[ 4 ];
	float fGamma;
	float fExposure;
	bool bSRGBRead;
	bool bSRGBWrite;
};

static DxvkPsFogState_t s_DxVkPsFogState;
static DxvkPsToneMapState_t s_DxVkPsToneMapState;
static float s_DxVkPsFlashlightMatrix[ 16 ];
static bool s_DxVkPsFlashlightEnabled = false;
static int s_DxVkCurrentPixelCombo = 0;

//-----------------------------------------------------------------------------
// CPixelShaderDxVk - Manager for pixel shader creation and binding
//-----------------------------------------------------------------------------
class CPixelShaderDxVk
{
public:
	CPixelShaderDxVk();
	~CPixelShaderDxVk();

	bool Init();
	void Shutdown();

	// Create from SPIR-V or bytecode
	PixelShaderHandle_t CreateShader(
		const uint32_t* pSpirvCode, size_t nWordCount,
		const char* pDebugName = nullptr,
		bool bIsStatic = false,
		int nSamplerMask = 0 );
	PixelShaderHandle_t CreateFromBytecode(
		const void* pBytecode, size_t nBytecodeSize,
		const char* pDebugName = nullptr,
		bool bIsStatic = false,
		int nSamplerMask = 0 );
	PixelShaderHandle_t CreateFromCompiledFile(
		const char* pFileName, int nFileIndex = 0,
		const char* pProfile = "ps_3_0" );
	void DestroyShader( PixelShaderHandle_t hShader );

	// Binding / state
	void BindShader( PixelShaderHandle_t hShader );
	PixelShaderHandle_t GetBoundShader() const { return s_DxvkBoundPixelShader; }
	const DxvkPixelShaderMeta_t* GetShaderMeta( PixelShaderHandle_t hShader ) const;

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

	// Standard pixel shader combos
	void SetStandardPixelShaderIndex( int nIndex ) { m_nStandardIndex = nIndex; }
	int GetStandardPixelShaderIndex() const { return m_nStandardIndex; }

	// Fog / tone mapping
	void SetFogParams( float fStart, float fEnd, float fZ, float fMaxDensity,
					   const float color[3], uint32_t nFogMode );
	void SetToneMapping( const float scale[4], float fGamma, float fExposure,
						 bool bSRGBRead, bool bSRGBWrite );

	// Flashlight
	void SetFlashlightState( bool bEnabled, const float* pMatrix4x4 = nullptr );
	bool IsFlashlightEnabled() const { return s_DxVkPsFlashlightEnabled; }
	const float* GetFlashlightMatrix() const { return s_DxVkPsFlashlightMatrix; }

	// Fog state accessors
	float GetFogStart() const { return s_DxVkPsFogState.fFogStart; }
	float GetFogEnd() const { return s_DxVkPsFogState.fFogEnd; }
	float GetFogZ() const { return s_DxVkPsFogState.fFogZ; }
	uint32_t GetFogMode() const { return s_DxVkPsFogState.nFogMode; }
	const float* GetFogColor() const { return s_DxVkPsFogState.fFogColor; }

	// Ref counting
	void AddRef( PixelShaderHandle_t hShader );
	void Release( PixelShaderHandle_t hShader );

	// DXVK/Vulkan access
	void* GetShaderModule( PixelShaderHandle_t hShader ) const;
	void* GetPipelineLayout( PixelShaderHandle_t hShader ) const;
	uint32_t GetSpirvWordCount( PixelShaderHandle_t hShader ) const;
	const uint32_t* GetSpirvCode( PixelShaderHandle_t hShader ) const;

	int GetShaderCount() const { return s_DxvkNextPixelShaderId - 1; }
	void PurgeUnusedShaders();
	void DumpStats() const;

	// Pixel combo
	int GetCurrentPixelCombo() const { return s_DxVkCurrentPixelCombo; }
	void SetCurrentPixelCombo( int nCombo ) { s_DxVkCurrentPixelCombo = nCombo; }

private:
	int AllocShaderId();
	void FreeShaderId( int nId );
	void InitDefaultCBuffers();
	void InitDefaultPixelState();
	bool ValidateHandle( PixelShaderHandle_t hShader ) const;

	int m_nStandardIndex;
	bool m_bInitialized;
};

static CPixelShaderDxVk s_PixelShaderMgrDxVk;

//-----------------------------------------------------------------------------
// CPixelShaderDxVk Implementation
//-----------------------------------------------------------------------------
CPixelShaderDxVk::CPixelShaderDxVk() :
	m_nStandardIndex( 0 ),
	m_bInitialized( false )
{
}

CPixelShaderDxVk::~CPixelShaderDxVk()
{
	Shutdown();
}

bool CPixelShaderDxVk::Init()
{
	if ( m_bInitialized ) return true;
	memset( s_DxvkPixelShaders, 0, sizeof(s_DxvkPixelShaders) );
	s_DxvkNextPixelShaderId = 1;
	s_DxvkBoundPixelShader = PIXEL_SHADER_HANDLE_INVALID;
	s_DxVkCurrentPixelCombo = 0;
	InitDefaultCBuffers();
	InitDefaultPixelState();
	m_bInitialized = true;
	return true;
}

void CPixelShaderDxVk::Shutdown()
{
	for ( int i = 0; i < kDxVkMaxPixelShaders; ++i )
	{
		DxvkPixelShaderMeta_t& meta = s_DxvkPixelShaders[ i ];
		if ( meta.pSpirvCode )
		{
			free( meta.pSpirvCode );
			meta.pSpirvCode = nullptr;
		}
	}
	memset( s_DxvkPixelShaders, 0, sizeof(s_DxvkPixelShaders) );
	s_DxvkNextPixelShaderId = 1;
	s_DxvkBoundPixelShader = PIXEL_SHADER_HANDLE_INVALID;
	s_DxVkCurrentPixelCombo = 0;
	memset( &s_DxVkPsFogState, 0, sizeof(s_DxVkPsFogState) );
	memset( &s_DxVkPsToneMapState, 0, sizeof(s_DxVkPsToneMapState) );
	memset( s_DxVkPsFlashlightMatrix, 0, sizeof(s_DxVkPsFlashlightMatrix) );
	s_DxVkPsFlashlightEnabled = false;
	m_nStandardIndex = 0;
	m_bInitialized = false;
}

PixelShaderHandle_t CPixelShaderDxVk::CreateShader(
	const uint32_t* pSpirvCode, size_t nWordCount,
	const char* pDebugName, bool bIsStatic,
	int nSamplerMask )
{
	if ( !m_bInitialized ) Init();
	if ( !pSpirvCode || nWordCount == 0 ) return PIXEL_SHADER_HANDLE_INVALID;

	int nId = AllocShaderId();
	if ( nId == 0 ) return PIXEL_SHADER_HANDLE_INVALID;

	DxvkPixelShaderMeta_t& meta = s_DxvkPixelShaders[ nId ];
	memset( &meta, 0, sizeof(meta) );
	meta.nShaderId = nId;
	meta.nRefCount = 1;
	meta.bValid = true;
	meta.bIsStatic = bIsStatic;
	meta.bUsesDepthOutput = false;
	meta.bUsesFog = false;
	meta.bUsesGrabPass = false;
	meta.bNeedsVertexShaderColor = false;
	meta.nSamplerMask = nSamplerMask;
	meta.nRenderTargetMask = 1;
	meta.nSpirvWordCount = (uint32_t)nWordCount;
	if ( pDebugName )
		Q_strncpy( meta.szName, pDebugName, sizeof(meta.szName) - 1 );

	meta.pSpirvCode = (uint32_t*)malloc( nWordCount * sizeof(uint32_t) );
	if ( meta.pSpirvCode )
		memcpy( meta.pSpirvCode, pSpirvCode, nWordCount * sizeof(uint32_t) );

	for ( int s = 0; s < 8; ++s )
		meta.nConstantBufferSize[ s ] = 0;

	return (PixelShaderHandle_t)nId;
}

PixelShaderHandle_t CPixelShaderDxVk::CreateFromBytecode(
	const void* pBytecode, size_t nBytecodeSize,
	const char* pDebugName, bool bIsStatic,
	int nSamplerMask )
{
	if ( !pBytecode || nBytecodeSize < 4 ) return PIXEL_SHADER_HANDLE_INVALID;
	if ( nBytecodeSize % 4 != 0 ) return PIXEL_SHADER_HANDLE_INVALID;
	return CreateShader( (const uint32_t*)pBytecode, nBytecodeSize / 4,
						 pDebugName, bIsStatic, nSamplerMask );
}

PixelShaderHandle_t CPixelShaderDxVk::CreateFromCompiledFile(
	const char* pFileName, int nFileIndex, const char* pProfile )
{
	(void)pProfile; (void)nFileIndex;
	return PIXEL_SHADER_HANDLE_INVALID;
}

void CPixelShaderDxVk::DestroyShader( PixelShaderHandle_t hShader )
{
	if ( !ValidateHandle( hShader ) ) return;
	int nId = (int)(intptr_t)hShader;
	DxvkPixelShaderMeta_t& meta = s_DxvkPixelShaders[ nId ];
	meta.nRefCount = ( meta.nRefCount > 0 ) ? meta.nRefCount - 1 : 0;
	if ( meta.nRefCount == 0 && !meta.bIsStatic )
		FreeShaderId( nId );
}

void CPixelShaderDxVk::BindShader( PixelShaderHandle_t hShader )
{
	if ( ValidateHandle( hShader ) || hShader == PIXEL_SHADER_HANDLE_INVALID )
		s_DxvkBoundPixelShader = hShader;
}

const DxvkPixelShaderMeta_t* CPixelShaderDxVk::GetShaderMeta( PixelShaderHandle_t hShader ) const
{
	if ( !ValidateHandle( hShader ) ) return nullptr;
	return &s_DxvkPixelShaders[ (int)(intptr_t)hShader ];
}

bool CPixelShaderDxVk::SetConstantFloat( int nSlot, int nRegIndex,
										 const float* pValues, int nVec4Count,
										 bool bForce )
{
	if ( nSlot < 0 || nSlot >= kDxVkMaxPsCBuffers || !pValues ) return false;
	DxvkPsConstantBuffer_t& cb = s_DxVkCurrentPsCBuffers[ nSlot ];
	int nLast = nRegIndex + nVec4Count;
	if ( nLast > 224 ) return false;
	for ( int i = 0; i < nVec4Count; ++i )
	{
		for ( int j = 0; j < 4; ++j )
			cb.fData[ nRegIndex + i ][ j ] = pValues[ i * 4 + j ];
	}
	cb.bDirty = true;
	return true;
}

bool CPixelShaderDxVk::SetConstantInt( int nSlot, int nRegIndex,
										const int* pValues, int nVec4Count,
										bool bForce )
{
	if ( nSlot < 0 || nSlot >= kDxVkMaxPsCBuffers || !pValues ) return false;
	DxvkPsConstantBuffer_t& cb = s_DxVkCurrentPsCBuffers[ nSlot ];
	int nLast = nRegIndex + nVec4Count;
	if ( nLast > 224 ) return false;
	for ( int i = 0; i < nVec4Count; ++i )
	{
		for ( int j = 0; j < 4; ++j )
			cb.fData[ nRegIndex + i ][ j ] = (float)pValues[ i * 4 + j ];
	}
	cb.bDirty = true;
	return true;
}

bool CPixelShaderDxVk::SetConstantBool( int nSlot, int nRegIndex,
										 const BOOL* pValues, int nBoolCount,
										 bool bForce )
{
	if ( nSlot < 0 || nSlot >= kDxVkMaxPsCBuffers || !pValues ) return false;
	DxvkPsConstantBuffer_t& cb = s_DxVkCurrentPsCBuffers[ nSlot ];
	for ( int i = 0; i < nBoolCount; ++i )
	{
		int idx = nRegIndex + ( i / 4 );
		if ( idx >= 224 ) break;
		int comp = i % 4;
		cb.fData[ idx ][ comp ] = pValues[ i ] ? -1.0f : 0.0f;
	}
	cb.bDirty = true;
	return true;
}

void CPixelShaderDxVk::FlushConstants( bool bAllSlots )
{
	for ( int s = 0; s < kDxVkMaxPsCBuffers; ++s )
		s_DxVkCurrentPsCBuffers[ s ].bDirty = false;
}

void CPixelShaderDxVk::InvalidateAllConstants()
{
	for ( int s = 0; s < kDxVkMaxPsCBuffers; ++s )
		s_DxVkCurrentPsCBuffers[ s ].bDirty = true;
}

bool CPixelShaderDxVk::IsConstantDirty( int nSlot ) const
{
	if ( nSlot < 0 || nSlot >= kDxVkMaxPsCBuffers ) return false;
	return s_DxVkCurrentPsCBuffers[ nSlot ].bDirty;
}

void CPixelShaderDxVk::SetFogParams( float fStart, float fEnd, float fZ, float fMaxDensity,
									  const float color[3], uint32_t nFogMode )
{
	s_DxVkPsFogState.fFogStart = fStart;
	s_DxVkPsFogState.fFogEnd = fEnd;
	s_DxVkPsFogState.fFogZ = fZ;
	s_DxVkPsFogState.fFogMaxDensity = fMaxDensity;
	if ( color )
	{
		s_DxVkPsFogState.fFogColor[ 0 ] = color[ 0 ];
		s_DxVkPsFogState.fFogColor[ 1 ] = color[ 1 ];
		s_DxVkPsFogState.fFogColor[ 2 ] = color[ 2 ];
	}
	s_DxVkPsFogState.nFogMode = nFogMode;
}

void CPixelShaderDxVk::SetToneMapping( const float scale[4], float fGamma, float fExposure,
									   bool bSRGBRead, bool bSRGBWrite )
{
	if ( scale )
	{
		s_DxVkPsToneMapState.fScale[ 0 ] = scale[ 0 ];
		s_DxVkPsToneMapState.fScale[ 1 ] = scale[ 1 ];
		s_DxVkPsToneMapState.fScale[ 2 ] = scale[ 2 ];
		s_DxVkPsToneMapState.fScale[ 3 ] = scale[ 3 ];
	}
	s_DxVkPsToneMapState.fGamma = fGamma;
	s_DxVkPsToneMapState.fExposure = fExposure;
	s_DxVkPsToneMapState.bSRGBRead = bSRGBRead;
	s_DxVkPsToneMapState.bSRGBWrite = bSRGBWrite;
}

void CPixelShaderDxVk::SetFlashlightState( bool bEnabled, const float* pMatrix4x4 )
{
	s_DxVkPsFlashlightEnabled = bEnabled;
	if ( pMatrix4x4 )
		memcpy( s_DxVkPsFlashlightMatrix, pMatrix4x4, 16 * sizeof(float) );
}

void CPixelShaderDxVk::AddRef( PixelShaderHandle_t hShader )
{
	if ( ValidateHandle( hShader ) )
		s_DxvkPixelShaders[ (int)(intptr_t)hShader ].nRefCount++;
}

void CPixelShaderDxVk::Release( PixelShaderHandle_t hShader )
{
	DestroyShader( hShader );
}

void* CPixelShaderDxVk::GetShaderModule( PixelShaderHandle_t hShader ) const
{
	if ( !ValidateHandle( hShader ) ) return nullptr;
	return s_DxvkPixelShaders[ (int)(intptr_t)hShader ].pVkShaderModule;
}

void* CPixelShaderDxVk::GetPipelineLayout( PixelShaderHandle_t hShader ) const
{
	if ( !ValidateHandle( hShader ) ) return nullptr;
	return s_DxvkPixelShaders[ (int)(intptr_t)hShader ].pPipelineLayoutRef;
}

uint32_t CPixelShaderDxVk::GetSpirvWordCount( PixelShaderHandle_t hShader ) const
{
	if ( !ValidateHandle( hShader ) ) return 0;
	return s_DxvkPixelShaders[ (int)(intptr_t)hShader ].nSpirvWordCount;
}

const uint32_t* CPixelShaderDxVk::GetSpirvCode( PixelShaderHandle_t hShader ) const
{
	if ( !ValidateHandle( hShader ) ) return nullptr;
	return s_DxvkPixelShaders[ (int)(intptr_t)hShader ].pSpirvCode;
}

void CPixelShaderDxVk::PurgeUnusedShaders()
{
	for ( int i = 1; i < kDxVkMaxPixelShaders; ++i )
	{
		DxvkPixelShaderMeta_t& meta = s_DxvkPixelShaders[ i ];
		if ( meta.bValid && meta.nRefCount == 0 && !meta.bIsStatic )
			FreeShaderId( i );
	}
}

void CPixelShaderDxVk::DumpStats() const
{
}

int CPixelShaderDxVk::AllocShaderId()
{
	for ( int i = 1; i < kDxVkMaxPixelShaders; ++i )
	{
		int idx = ( s_DxvkNextPixelShaderId + i ) % kDxVkMaxPixelShaders;
		if ( idx == 0 ) idx = 1;
		if ( !s_DxvkPixelShaders[ idx ].bValid )
		{
			s_DxvkNextPixelShaderId = idx + 1;
			return idx;
		}
	}
	return 0;
}

void CPixelShaderDxVk::FreeShaderId( int nId )
{
	if ( nId <= 0 || nId >= kDxVkMaxPixelShaders ) return;
	DxvkPixelShaderMeta_t& meta = s_DxvkPixelShaders[ nId ];
	if ( meta.pSpirvCode )
	{
		free( meta.pSpirvCode );
		meta.pSpirvCode = nullptr;
	}
	memset( &meta, 0, sizeof(meta) );
}

void CPixelShaderDxVk::InitDefaultCBuffers()
{
	for ( int s = 0; s < kDxVkMaxPsCBuffers; ++s )
	{
		memset( &s_DxVkCurrentPsCBuffers[ s ], 0, sizeof(DxvkPsConstantBuffer_t) );
		s_DxVkCurrentPsCBuffers[ s ].nSlot = s;
		s_DxVkCurrentPsCBuffers[ s ].nSize = 224 * 4 * sizeof(float);
		s_DxVkCurrentPsCBuffers[ s ].bDirty = true;
	}
}

void CPixelShaderDxVk::InitDefaultPixelState()
{
	memset( &s_DxVkPsFogState, 0, sizeof(s_DxVkPsFogState) );
	s_DxVkPsFogState.fFogEnd = 1.0f;
	s_DxVkPsFogState.fFogMaxDensity = 1.0f;

	memset( &s_DxVkPsToneMapState, 0, sizeof(s_DxVkPsToneMapState) );
	s_DxVkPsToneMapState.fScale[ 0 ] = 1.0f;
	s_DxVkPsToneMapState.fScale[ 1 ] = 1.0f;
	s_DxVkPsToneMapState.fScale[ 2 ] = 1.0f;
	s_DxVkPsToneMapState.fScale[ 3 ] = 1.0f;
	s_DxVkPsToneMapState.fGamma = 1.0f;
	s_DxVkPsToneMapState.fExposure = 1.0f;

	memset( s_DxVkPsFlashlightMatrix, 0, sizeof(s_DxVkPsFlashlightMatrix) );
	s_DxVkPsFlashlightMatrix[ 0 ]  = 1.0f;
	s_DxVkPsFlashlightMatrix[ 5 ]  = 1.0f;
	s_DxVkPsFlashlightMatrix[ 10 ] = 1.0f;
	s_DxVkPsFlashlightMatrix[ 15 ] = 1.0f;
	s_DxVkPsFlashlightEnabled = false;
}

bool CPixelShaderDxVk::ValidateHandle( PixelShaderHandle_t hShader ) const
{
	int nId = (int)(intptr_t)hShader;
	if ( nId <= 0 || nId >= kDxVkMaxPixelShaders ) return false;
	return s_DxvkPixelShaders[ nId ].bValid;
}
