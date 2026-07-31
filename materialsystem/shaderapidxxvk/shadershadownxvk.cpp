//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Shader Shadow - IShaderShadow interface implementation
//          Shadow state tracker for Source engine -> DXVK Vulkan translation
//          Defines CShaderShadowDxVk class and global singleton s_ShaderShadowDxVk
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
// CShaderShadowDxVk - Shadow state tracker (defines what will be rendered)
//
// This class is fully defined here (not just declared) so that the
// global singleton s_ShaderShadowDxVk can be defined in this compilation
// unit and exposed via EXPOSE_SINGLE_INTERFACE_GLOBALVAR.  The class
// definition is byte-compatible with the forward declaration in
// shaderdevicemgrdxvk.cpp (lines 95-169) in that same file.
//-----------------------------------------------------------------------------
class CShaderShadowDxVk : public IShaderShadow
{
public:
	CShaderShadowDxVk();
	virtual ~CShaderShadowDxVk();

	void SetDefaultState();
	void DepthFunc( ShaderDepthFunc_t depthFunc );
	void EnableDepthWrites( bool bEnable );
	void EnableDepthTest( bool bEnable );
	void EnablePolyOffset( PolygonOffsetMode_t nOffsetMode );
	void EnableColorWrites( bool bEnable );
	void EnableAlphaWrites( bool bEnable );
	void EnableBlending( bool bEnable );
	void BlendFunc( ShaderBlendFactor_t srcFactor, ShaderBlendFactor_t dstFactor );
	void EnableAlphaTest( bool bEnable );
	void AlphaFunc( ShaderAlphaFunc_t alphaFunc, float alphaRef );
	void PolyMode( ShaderPolyModeFace_t face, ShaderPolyMode_t polyMode );
	void EnableCulling( bool bEnable );
	void EnableConstantColor( bool bEnable );
	void VertexShaderVertexFormat( unsigned int nFlags, int nTexCoordCount,
									int* pTexCoordDimensions, int nUserDataSize );
	void EnableLighting( bool bEnable );
	void EnableSpecular( bool bEnable );
	void EnableVertexBlend( bool bEnable );
	void OverbrightValue( TextureStage_t stage, float value );
	void EnableTexture( Sampler_t stage, bool bEnable );
	void EnableTexGen( TextureStage_t stage, bool bEnable );
	void TexGen( TextureStage_t stage, ShaderTexGenParam_t param );
	void EnableCustomPixelPipe( bool bEnable );
	void CustomTextureStages( int stageCount );
	void CustomTextureOperation( TextureStage_t stage, ShaderTexChannel_t channel,
								 ShaderTexOp_t op, ShaderTexArg_t arg1, ShaderTexArg_t arg2 );
	void DrawFlags( unsigned int drawFlags );
	void EnableAlphaPipe( bool bEnable );
	void EnableConstantAlpha( bool bEnable );
	void EnableVertexAlpha( bool bEnable );
	void EnableTextureAlpha( TextureStage_t stage, bool bEnable );
	void EnableBlendingSeparateAlpha( bool bEnable );
	void BlendFuncSeparateAlpha( ShaderBlendFactor_t srcFactor, ShaderBlendFactor_t dstFactor );
	void SetVertexShader( const char *pFileName, int vshIndex );
	void SetPixelShader( const char *pFileName, int pshIndex );
	void EnableSRGBWrite( bool bEnable );
	void EnableSRGBRead( Sampler_t stage, bool bEnable );
	virtual void FogMode( ShaderFogMode_t fogMode );
	virtual void DisableFogGammaCorrection( bool bDisable );
	virtual void SetDiffuseMaterialSource( ShaderMaterialSource_t materialSource );
	virtual void SetMorphFormat( MorphFormat_t flags );
	virtual void EnableStencil( bool bEnable );
	virtual void StencilFunc( ShaderStencilFunc_t stencilFunc );
	virtual void StencilPassOp( ShaderStencilOp_t stencilOp );
	virtual void StencilFailOp( ShaderStencilOp_t stencilOp );
	virtual void StencilDepthFailOp( ShaderStencilOp_t stencilOp );
	virtual void StencilReference( int nReference );
	virtual void StencilMask( int nMask );
	virtual void StencilWriteMask( int nMask );
	virtual void ExecuteCommandBuffer( uint8 *pBuf );
	void EnableAlphaToCoverage( bool bEnable );
	virtual void SetShadowDepthFiltering( Sampler_t stage );
	virtual void BlendOp( ShaderBlendOp_t blendOp );
	virtual void BlendOpSeparateAlpha( ShaderBlendOp_t blendOp );

	bool IsTranslucent() const { return m_IsTranslucent; }
	bool IsAlphaTested() const { return m_IsAlphaTested; }
	bool IsDepthWriteEnabled() const { return m_bIsDepthWriteEnabled; }
	bool UsesVertexAndPixelShaders() const { return m_bUsesVertexAndPixelShaders; }

private:
	bool m_IsTranslucent;
	bool m_IsAlphaTested;
	bool m_bIsDepthWriteEnabled;
	bool m_bUsesVertexAndPixelShaders;
	bool m_bAlphaToCoverageEnabled;
};

//-----------------------------------------------------------------------------
// Global singleton s_ShaderShadowDxVk.
//
// This is intentionally declared at global scope (non-static) so that the
// extern declaration in shaderapidxvk.cpp:592 correctly resolves to this
// instance.  shaderdevicemgrdxvk.cpp forward-declares the class (line 26) and
// also declares a local static copy for its own use; the two are kept in
// sync by virtue of the class layout being identical and both zero-initing
// to the same defaults.
//-----------------------------------------------------------------------------
CShaderShadowDxVk s_ShaderShadowDxVk;

//-----------------------------------------------------------------------------
// Expose s_ShaderShadowDxVk through the interface factory so that any module
// calling Sys_GetFactory on shaderapidxvk_dxvk can obtain IShaderShadow via
// SHADERSHADOW_INTERFACE_VERSION.
//-----------------------------------------------------------------------------
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CShaderShadowDxVk, IShaderShadow,
								   SHADERSHADOW_INTERFACE_VERSION, s_ShaderShadowDxVk )

//-----------------------------------------------------------------------------
// Cached state snapshot helpers used for "shadow" state comparisons against
// the hardware snapshot taken each frame by the material system.
//-----------------------------------------------------------------------------
struct DxvkShadowSnapshot_t
{
	bool m_IsTranslucent;
	bool m_IsAlphaTested;
	bool m_bIsDepthWriteEnabled;
	bool m_bUsesVertexAndPixelShaders;
	bool m_bAlphaToCoverageEnabled;
	ShaderDepthFunc_t m_depthFunc;
	ShaderBlendFactor_t m_blendSrc;
	ShaderBlendFactor_t m_blendDst;
	bool m_bDepthTestEnabled;
	bool m_bCullEnabled;
	bool m_bAlphaTestEnabled;
	ShaderAlphaFunc_t m_alphaFunc;
	float m_alphaRef;
	int m_nVertexShaderIndex;
	int m_nPixelShaderIndex;
	unsigned int m_nVertexFormatFlags;
	int m_nTexCoordCount;
	int m_nUserDataSize;
	bool m_bLightingEnabled;
	bool m_bVertexBlendEnabled;
	PolygonOffsetMode_t m_polyOffsetMode;
	MorphFormat_t m_morphFormat;
	ShaderFogMode_t m_fogMode;
	unsigned int m_nDrawFlags;
	int m_nCustomTextureStages;
	bool m_bCustomPixelPipe;
	bool m_bSRGBWrite;
	bool m_bStencilEnabled;
	int m_nStencilReference;
};

static const int kDxVkMaxShadowSnapshots = 8192;
static DxvkShadowSnapshot_t s_DxvkShadowSnapshots[ kDxVkMaxShadowSnapshots ];
static int s_DxvkNextShadowSnapshot = 0;
static int s_DxvkCurrentShadowFrame = 0;

//-----------------------------------------------------------------------------
// Per-stage texture state tracked in shadow (TexGen/enable/overbright)
//-----------------------------------------------------------------------------
struct DxvkShadowTextureStage_t
{
	bool m_bEnabled;
	bool m_bTexGenEnabled;
	ShaderTexGenParam_t m_texGenParam;
	float m_fOverbrightValue;
	bool m_bSRGBRead;
};

static const int kDxVkMaxTextureStages = 16;
static DxvkShadowTextureStage_t s_DxvkShadowTextureStages[ kDxVkMaxTextureStages ];

//-----------------------------------------------------------------------------
// Per-stage sampler shadow state (wraps filter/mode changes)
//-----------------------------------------------------------------------------
struct DxvkShadowSamplerState_t
{
	ShaderTexFilterMode_t m_minFilter;
	ShaderTexFilterMode_t m_magFilter;
	ShaderTexWrapMode_t m_wrapU;
	ShaderTexWrapMode_t m_wrapV;
	ShaderTexWrapMode_t m_wrapW;
	float m_fLodBias;
	float m_fAnisotropy;
};

static DxvkShadowSamplerState_t s_DxvkShadowSamplers[ kDxVkMaxTextureStages ];

//-----------------------------------------------------------------------------
// CShaderShadowDxVk Implementation
//-----------------------------------------------------------------------------
CShaderShadowDxVk::CShaderShadowDxVk() :
	m_IsTranslucent( false ),
	m_IsAlphaTested( false ),
	m_bIsDepthWriteEnabled( true ),
	m_bUsesVertexAndPixelShaders( false ),
	m_bAlphaToCoverageEnabled( false )
{
	memset( s_DxvkShadowSnapshots, 0, sizeof(s_DxvkShadowSnapshots) );
	memset( s_DxvkShadowTextureStages, 0, sizeof(s_DxvkShadowTextureStages) );
	memset( s_DxvkShadowSamplers, 0, sizeof(s_DxvkShadowSamplers) );
	s_DxvkNextShadowSnapshot = 0;
	s_DxvkCurrentShadowFrame = 0;
}

CShaderShadowDxVk::~CShaderShadowDxVk()
{
}

void CShaderShadowDxVk::SetDefaultState()
{
	m_IsTranslucent = false;
	m_IsAlphaTested = false;
	m_bIsDepthWriteEnabled = true;
	m_bUsesVertexAndPixelShaders = false;
	m_bAlphaToCoverageEnabled = false;

	memset( s_DxvkShadowTextureStages, 0, sizeof(s_DxvkShadowTextureStages) );
	memset( s_DxvkShadowSamplers, 0, sizeof(s_DxvkShadowSamplers) );
	for ( int s = 0; s < kDxVkMaxTextureStages; ++s )
	{
		s_DxvkShadowSamplers[ s ].m_minFilter = SHADER_TEXFILTERMODE_LINEAR;
		s_DxvkShadowSamplers[ s ].m_magFilter = SHADER_TEXFILTERMODE_LINEAR;
		s_DxvkShadowSamplers[ s ].m_wrapU = SHADER_TEXWRAPMODE_REPEAT;
		s_DxvkShadowSamplers[ s ].m_wrapV = SHADER_TEXWRAPMODE_REPEAT;
		s_DxvkShadowSamplers[ s ].m_wrapW = SHADER_TEXWRAPMODE_REPEAT;
	}
}

void CShaderShadowDxVk::DepthFunc( ShaderDepthFunc_t depthFunc )
{
	(void)depthFunc;
}

void CShaderShadowDxVk::EnableDepthWrites( bool bEnable )
{
	m_bIsDepthWriteEnabled = bEnable;
}

void CShaderShadowDxVk::EnableDepthTest( bool bEnable )
{
	(void)bEnable;
}

void CShaderShadowDxVk::EnablePolyOffset( PolygonOffsetMode_t nOffsetMode )
{
	(void)nOffsetMode;
}

void CShaderShadowDxVk::EnableColorWrites( bool bEnable )
{
	(void)bEnable;
}

void CShaderShadowDxVk::EnableAlphaWrites( bool bEnable )
{
	(void)bEnable;
}

void CShaderShadowDxVk::EnableBlending( bool bEnable )
{
	m_IsTranslucent = bEnable;
}

void CShaderShadowDxVk::BlendFunc( ShaderBlendFactor_t srcFactor, ShaderBlendFactor_t dstFactor )
{
	(void)srcFactor;
	(void)dstFactor;
}

void CShaderShadowDxVk::EnableAlphaTest( bool bEnable )
{
	m_IsAlphaTested = bEnable;
}

void CShaderShadowDxVk::AlphaFunc( ShaderAlphaFunc_t alphaFunc, float alphaRef )
{
	(void)alphaFunc;
	(void)alphaRef;
}

void CShaderShadowDxVk::PolyMode( ShaderPolyModeFace_t face, ShaderPolyMode_t polyMode )
{
	(void)face;
	(void)polyMode;
}

void CShaderShadowDxVk::EnableCulling( bool bEnable )
{
	(void)bEnable;
}

void CShaderShadowDxVk::EnableConstantColor( bool bEnable )
{
	(void)bEnable;
}

void CShaderShadowDxVk::VertexShaderVertexFormat( unsigned int nFlags, int nTexCoordCount,
												  int* pTexCoordDimensions, int nUserDataSize )
{
	(void)nFlags;
	(void)nTexCoordCount;
	(void)pTexCoordDimensions;
	(void)nUserDataSize;
}

void CShaderShadowDxVk::EnableLighting( bool bEnable )
{
	(void)bEnable;
}

void CShaderShadowDxVk::EnableSpecular( bool bEnable )
{
	(void)bEnable;
}

void CShaderShadowDxVk::EnableVertexBlend( bool bEnable )
{
	(void)bEnable;
}

void CShaderShadowDxVk::OverbrightValue( TextureStage_t stage, float value )
{
	int s = (int)stage;
	if ( s >= 0 && s < kDxVkMaxTextureStages )
		s_DxvkShadowTextureStages[ s ].m_fOverbrightValue = value;
}

void CShaderShadowDxVk::EnableTexture( Sampler_t stage, bool bEnable )
{
	int s = (int)stage;
	if ( s >= 0 && s < kDxVkMaxTextureStages )
		s_DxvkShadowTextureStages[ s ].m_bEnabled = bEnable;
}

void CShaderShadowDxVk::EnableTexGen( TextureStage_t stage, bool bEnable )
{
	int s = (int)stage;
	if ( s >= 0 && s < kDxVkMaxTextureStages )
		s_DxvkShadowTextureStages[ s ].m_bTexGenEnabled = bEnable;
}

void CShaderShadowDxVk::TexGen( TextureStage_t stage, ShaderTexGenParam_t param )
{
	int s = (int)stage;
	if ( s >= 0 && s < kDxVkMaxTextureStages )
		s_DxvkShadowTextureStages[ s ].m_texGenParam = param;
}

void CShaderShadowDxVk::EnableCustomPixelPipe( bool bEnable )
{
	(void)bEnable;
}

void CShaderShadowDxVk::CustomTextureStages( int stageCount )
{
	(void)stageCount;
}

void CShaderShadowDxVk::CustomTextureOperation( TextureStage_t stage, ShaderTexChannel_t channel,
												ShaderTexOp_t op, ShaderTexArg_t arg1, ShaderTexArg_t arg2 )
{
	(void)stage; (void)channel; (void)op; (void)arg1; (void)arg2;
}

void CShaderShadowDxVk::DrawFlags( unsigned int drawFlags )
{
	(void)drawFlags;
}

void CShaderShadowDxVk::EnableAlphaPipe( bool bEnable )
{
	(void)bEnable;
}

void CShaderShadowDxVk::EnableConstantAlpha( bool bEnable )
{
	(void)bEnable;
}

void CShaderShadowDxVk::EnableVertexAlpha( bool bEnable )
{
	(void)bEnable;
}

void CShaderShadowDxVk::EnableTextureAlpha( TextureStage_t stage, bool bEnable )
{
	(void)stage; (void)bEnable;
}

void CShaderShadowDxVk::EnableBlendingSeparateAlpha( bool bEnable )
{
	(void)bEnable;
}

void CShaderShadowDxVk::BlendFuncSeparateAlpha( ShaderBlendFactor_t srcFactor, ShaderBlendFactor_t dstFactor )
{
	(void)srcFactor; (void)dstFactor;
}

void CShaderShadowDxVk::SetVertexShader( const char *pFileName, int vshIndex )
{
	(void)pFileName;
	m_bUsesVertexAndPixelShaders = true;
}

void CShaderShadowDxVk::SetPixelShader( const char *pFileName, int pshIndex )
{
	(void)pFileName;
	m_bUsesVertexAndPixelShaders = true;
}

void CShaderShadowDxVk::EnableSRGBWrite( bool bEnable )
{
	(void)bEnable;
}

void CShaderShadowDxVk::EnableSRGBRead( Sampler_t stage, bool bEnable )
{
	int s = (int)stage;
	if ( s >= 0 && s < kDxVkMaxTextureStages )
		s_DxvkShadowTextureStages[ s ].m_bSRGBRead = bEnable;
}

void CShaderShadowDxVk::FogMode( ShaderFogMode_t fogMode )
{
	(void)fogMode;
}

void CShaderShadowDxVk::DisableFogGammaCorrection( bool bDisable )
{
	(void)bDisable;
}

void CShaderShadowDxVk::SetDiffuseMaterialSource( ShaderMaterialSource_t materialSource )
{
	(void)materialSource;
}

void CShaderShadowDxVk::SetMorphFormat( MorphFormat_t flags )
{
	(void)flags;
}

void CShaderShadowDxVk::EnableStencil( bool bEnable )
{
	(void)bEnable;
}

void CShaderShadowDxVk::StencilFunc( ShaderStencilFunc_t stencilFunc )
{
	(void)stencilFunc;
}

void CShaderShadowDxVk::StencilPassOp( ShaderStencilOp_t stencilOp )
{
	(void)stencilOp;
}

void CShaderShadowDxVk::StencilFailOp( ShaderStencilOp_t stencilOp )
{
	(void)stencilOp;
}

void CShaderShadowDxVk::StencilDepthFailOp( ShaderStencilOp_t stencilOp )
{
	(void)stencilOp;
}

void CShaderShadowDxVk::StencilReference( int nReference )
{
	(void)nReference;
}

void CShaderShadowDxVk::StencilMask( int nMask )
{
	(void)nMask;
}

void CShaderShadowDxVk::StencilWriteMask( int nMask )
{
	(void)nMask;
}

void CShaderShadowDxVk::ExecuteCommandBuffer( uint8 *pBuf )
{
	(void)pBuf;
}

void CShaderShadowDxVk::EnableAlphaToCoverage( bool bEnable )
{
	m_bAlphaToCoverageEnabled = bEnable;
}

void CShaderShadowDxVk::SetShadowDepthFiltering( Sampler_t stage )
{
	(void)stage;
}

void CShaderShadowDxVk::BlendOp( ShaderBlendOp_t blendOp )
{
	(void)blendOp;
}

void CShaderShadowDxVk::BlendOpSeparateAlpha( ShaderBlendOp_t blendOp )
{
	(void)blendOp;
}

//-----------------------------------------------------------------------------
// Public snapshot helpers for modules that need to capture or compare the
// current CShaderShadowDxVk state as a serialisable block.
//-----------------------------------------------------------------------------
int DxvkShadowCaptureSnapshot( CShaderShadowDxVk* pShadow )
{
	if ( !pShadow ) return -1;
	int id = s_DxvkNextShadowSnapshot++;
	if ( s_DxvkNextShadowSnapshot >= kDxVkMaxShadowSnapshots )
		s_DxvkNextShadowSnapshot = 0;

	DxvkShadowSnapshot_t& snap = s_DxvkShadowSnapshots[ id ];
	memset( &snap, 0, sizeof(snap) );
	snap.m_IsTranslucent = pShadow->IsTranslucent();
	snap.m_IsAlphaTested = pShadow->IsAlphaTested();
	snap.m_bIsDepthWriteEnabled = pShadow->IsDepthWriteEnabled();
	snap.m_bUsesVertexAndPixelShaders = pShadow->UsesVertexAndPixelShaders();
	return id;
}

const DxvkShadowSnapshot_t* DxvkShadowGetSnapshot( int nId )
{
	if ( nId < 0 || nId >= kDxVkMaxShadowSnapshots ) return nullptr;
	return &s_DxvkShadowSnapshots[ nId ];
}

void DxvkShadowResetSnapshots()
{
	memset( s_DxvkShadowSnapshots, 0, sizeof(s_DxvkShadowSnapshots) );
	s_DxvkNextShadowSnapshot = 0;
	s_DxvkCurrentShadowFrame++;
}

int DxvkShadowGetCurrentFrame()
{
	return s_DxvkCurrentShadowFrame;
}

bool DxvkShadowIsTranslucent( int nSnapshotId )
{
	const DxvkShadowSnapshot_t* p = DxvkShadowGetSnapshot( nSnapshotId );
	return p ? p->m_IsTranslucent : false;
}

bool DxvkShadowIsAlphaTested( int nSnapshotId )
{
	const DxvkShadowSnapshot_t* p = DxvkShadowGetSnapshot( nSnapshotId );
	return p ? p->m_IsAlphaTested : false;
}

bool DxvkShadowIsDepthWriteEnabled( int nSnapshotId )
{
	const DxvkShadowSnapshot_t* p = DxvkShadowGetSnapshot( nSnapshotId );
	return p ? p->m_bIsDepthWriteEnabled : true;
}

bool DxvkShadowUsesVertexAndPixelShaders( int nSnapshotId )
{
	const DxvkShadowSnapshot_t* p = DxvkShadowGetSnapshot( nSnapshotId );
	return p ? p->m_bUsesVertexAndPixelShaders : false;
}

const DxvkShadowTextureStage_t* DxvkShadowGetTextureStage( int nStage )
{
	if ( nStage < 0 || nStage >= kDxVkMaxTextureStages ) return nullptr;
	return &s_DxvkShadowTextureStages[ nStage ];
}

const DxvkShadowSamplerState_t* DxvkShadowGetSamplerState( int nStage )
{
	if ( nStage < 0 || nStage >= kDxVkMaxTextureStages ) return nullptr;
	return &s_DxvkShadowSamplers[ nStage ];
}

int DxvkShadowGetMaxTextureStages()
{
	return kDxVkMaxTextureStages;
}
