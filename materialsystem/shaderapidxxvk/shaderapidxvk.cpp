//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Shader API - Main IShaderAPI implementation
//          Implements Source engine -> DXVK Vulkan translation
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

#include <stdarg.h>

// Forward declarations
class CDxvkMesh;

//=============================================================================
// Texture handle pool (simple handle -> index mapping)
//=============================================================================
struct DxvkTexture_t
{
	int				nWidth;
	int				nHeight;
	int				nDepth;
	int				nMipLevels;
	ImageFormat		format;
	int				nFlags;
	bool			bIsRenderTarget;
	bool			bIsDepthBuffer;
	bool			bValid;
	// Actual Vulkan image/view handles would go here in a full implementation
	void*			pVkImage;
	void*			pVkImageView;
	void*			pAlloc;
};

static const int kMaxTextures = 65536;
static DxvkTexture_t s_Textures[ kMaxTextures ];
static int s_nNextTextureHandle = 1; // 0 = invalid

//=============================================================================
// Snapshot state for state caching / transition table
//=============================================================================
struct DxvkSnapshot_t
{
	bool m_IsTranslucent;
	bool m_IsAlphaTested;
	bool m_bUsesVertexAndPixelShaders;
	bool m_bDepthWriteEnabled;
	VertexFormat_t m_VertexFormat;
	MorphFormat_t m_MorphFormat;
};

static const int kMaxSnapshots = 4096;
static DxvkSnapshot_t s_Snapshots[ kMaxSnapshots ];
static int s_nNextSnapshot = 0;

//=============================================================================
// CShaderAPIDxVk - Main IShaderAPI implementation
//=============================================================================
class CShaderAPIDxVk : public IShaderAPI, public IHardwareConfigInternal, public IDebugTextureInfo
{
public:
	CShaderAPIDxVk();
	virtual ~CShaderAPIDxVk();

	// --- IDebugTextureInfo ---
	virtual bool IsDebugTextureListFresh( int numFramesAllowed = 1 ) { return false; }
	virtual bool SetDebugTextureRendering( bool bEnable ) { return false; }
	virtual void EnableDebugTextureList( bool bEnable ) {}
	virtual void EnableGetAllTextures( bool bEnable ) {}
	virtual KeyValues* GetDebugTextureList() { return NULL; }
	virtual int GetTextureMemoryUsed( TextureMemoryType eTextureMemory ) { return 0; }

	// --- IShaderDynamicAPI ---
	virtual void GetBackBufferDimensions( int& width, int& height ) const;
	virtual void GetCurrentColorCorrection( ShaderColorCorrectionInfo_t* pInfo );

	// === IShaderAPI Methods ===

	// Viewport
	virtual void SetViewports( int nCount, const ShaderViewport_t* pViewports );
	virtual int GetViewports( ShaderViewport_t* pViewports, int nMax ) const;

	// Clears
	virtual void ClearBuffers( bool bClearColor, bool bClearDepth, bool bClearStencil,
							   int renderTargetWidth, int renderTargetHeight );
	virtual void ClearColor3ub( unsigned char r, unsigned char g, unsigned char b );
	virtual void ClearColor4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a );

	// Shader binding
	virtual void BindVertexShader( VertexShaderHandle_t hVertexShader ) {}
	virtual void BindGeometryShader( GeometryShaderHandle_t hGeometryShader ) {}
	virtual void BindPixelShader( PixelShaderHandle_t hPixelShader ) {}

	// Raster state
	virtual void SetRasterState( const ShaderRasterState_t& state ) {}
	virtual void MarkUnusedVertexFields( unsigned int nFlags, int nTexCoordCount, bool *pUnusedTexCoords ) {}
	virtual bool OwnGPUResources( bool bEnable ) { return false; }
	virtual bool DoRenderTargetsNeedSeparateDepthBuffer() const;

	// Mode management
	virtual bool SetMode( void* hwnd, int nAdapter, const ShaderDeviceInfo_t &info );
	virtual void ChangeVideoMode( const ShaderDeviceInfo_t &info );
	virtual void DXSupportLevelChanged() {}
	virtual void EnableUserClipTransformOverride( bool bEnable ) {}
	virtual void UserClipTransform( const VMatrix &worldToView ) {}

	// Snapshot
	virtual StateSnapshot_t TakeSnapshot();
	virtual void ClearSnapshots();
	virtual bool IsTranslucent( StateSnapshot_t id ) const;
	virtual bool IsAlphaTested( StateSnapshot_t id ) const;
	virtual bool UsesVertexAndPixelShaders( StateSnapshot_t id ) const;
	virtual bool IsDepthWriteEnabled( StateSnapshot_t id ) const;
	virtual VertexFormat_t ComputeVertexFormat( int numSnapshots, StateSnapshot_t* pIds ) const;
	virtual VertexFormat_t ComputeVertexUsage( int numSnapshots, StateSnapshot_t* pIds ) const;
	virtual MorphFormat_t ComputeMorphFormat( int numSnapshots, StateSnapshot_t* pIds ) const;
	virtual int CompareSnapshots( StateSnapshot_t snapshot0, StateSnapshot_t snapshot1 ) { return 0; }

	// Pass rendering
	virtual void BeginPass( StateSnapshot_t snapshot );
	virtual void RenderPass( int nPass, int nPassCount );
	virtual void FlushBufferedPrimitives();

	// Mesh access
	virtual IMesh* GetDynamicMesh( IMaterial* pMaterial, int nHWSkinBoneCount, bool bBuffered = true,
		IMesh* pVertexOverride = 0, IMesh* pIndexOverride = 0 );
	virtual IMesh* GetDynamicMeshEx( IMaterial* pMaterial, VertexFormat_t vertexFormat, int nHWSkinBoneCount,
		bool bBuffered = true, IMesh* pVertexOverride = 0, IMesh* pIndexOverride = 0 );
	virtual IMesh *GetFlexMesh();

	// Dynamic mesh limits
	virtual void GetMaxToRender( IMesh *pMesh, bool bMaxUntilFlush, int *pMaxVerts, int *pMaxIndices );
	virtual int GetMaxVerticesToRender( IMaterial *pMaterial ) { return 32768; }
	virtual int GetMaxIndicesToRender( ) { return 32768; }

	// Lights
	virtual void SetNumBoneWeights( int numBones );
	virtual void SetLight( int lightNum, const LightDesc_t& desc );
	virtual void SetLightingOrigin( Vector vLightingOrigin );
	virtual void SetAmbientLight( float r, float g, float b );
	virtual void SetAmbientLightCube( Vector4D cube[6] );
	virtual void DisableAllLocalLights() {}

	// Lighting helpers
	void SetDefaultState();
	int GetMaxLights( void ) const { return 4; }
	const LightDesc_t& GetLight( int lightNum ) const { return m_Lights[ lightNum ]; }
	void SetVertexShaderStateAmbientLightCube() {}
	float GetAmbientLightCubeLuminance(void) { return 0.0f; }
	void SetSkinningMatrices();

	// Texture state
	virtual void TexMinFilter( ShaderTexFilterMode_t texFilterMode );
	virtual void TexMagFilter( ShaderTexFilterMode_t texFilterMode );
	virtual void TexWrap( ShaderTexCoordComponent_t coord, ShaderTexWrapMode_t wrapMode );
	virtual void Bind( IMaterial* pMaterial ) { m_pBoundMaterial = pMaterial; }
	virtual void CopyRenderTargetToTexture( ShaderAPITextureHandle_t textureHandle ) {}
	virtual void CopyRenderTargetToTextureEx( ShaderAPITextureHandle_t texID, int nRenderTargetID, Rect_t *pSrcRect = NULL, Rect_t *pDstRect = NULL ) {}
	virtual void CopyTextureToRenderTargetEx( int nRenderTargetID, ShaderAPITextureHandle_t textureHandle, Rect_t *pSrcRect = NULL, Rect_t *pDstRect = NULL ) {}
	virtual void BindLightmap( TextureStage_t stage );
	virtual void BindBumpLightmap( TextureStage_t stage );
	virtual void BindFullbrightLightmap( TextureStage_t stage );
	virtual void BindWhite( TextureStage_t stage );
	virtual void BindBlack( TextureStage_t stage );
	virtual void BindGrey( TextureStage_t stage );
	virtual void BindFBTexture( TextureStage_t stage, int textureIdex );
	virtual void BindFlatNormalMap( TextureStage_t stage );
	virtual void BindNormalizationCubeMap( TextureStage_t stage );
	virtual void BindSignedNormalizationCubeMap( TextureStage_t stage );

	// Texture creation
	virtual ShaderAPITextureHandle_t CreateTexture(
		int width, int height, int depth, ImageFormat dstImageFormat,
		int numMipLevels, int numCopies, int flags,
		const char *pDebugName, const char *pTextureGroupName );
	virtual void CreateTextures(
		ShaderAPITextureHandle_t *pHandles, int count,
		int width, int height, int depth, ImageFormat dstImageFormat,
		int numMipLevels, int numCopies, int flags,
		const char *pDebugName, const char *pTextureGroupName );
	virtual ShaderAPITextureHandle_t CreateDepthTexture(
		ImageFormat renderTargetFormat, int width, int height,
		const char *pDebugName, bool bTexture );
	virtual void DeleteTexture( ShaderAPITextureHandle_t textureHandle );
	virtual bool IsTexture( ShaderAPITextureHandle_t textureHandle );
	virtual bool IsTextureResident( ShaderAPITextureHandle_t textureHandle ) { return true; }

	// Texture modification
	virtual void ModifyTexture( ShaderAPITextureHandle_t textureHandle );
	virtual void TexImage2D( int level, int cubeFaceID, ImageFormat dstFormat,
		int zOffset, int width, int height,
		ImageFormat srcFormat, bool bSrcIsTiled, void *imageData );
	virtual void TexSubImage2D( int level, int cubeFaceID, int xOffset, int yOffset,
		int zOffset, int width, int height,
		ImageFormat srcFormat, int srcStride, bool bSrcIsTiled, void *imageData );
	virtual void TexImageFromVTF( IVTFTexture* pVTF, int iVTFFrame ) {}
	virtual bool TexLock( int level, int cubeFaceID, int xOffset, int yOffset,
		int width, int height, CPixelWriter& writer ) { return false; }
	virtual void TexUnlock( ) {}
	virtual void TexSetPriority( int priority ) {}
	virtual void TexLodClamp( int finest ) {}
	virtual void TexLodBias( float bias ) {}
	virtual void CopyTextureToTexture( ShaderAPITextureHandle_t srcTex, ShaderAPITextureHandle_t dstTex ) {}

	// Texture binding
	virtual void BindTexture( Sampler_t sampler, ShaderAPITextureHandle_t textureHandle );
	virtual void BindVertexTexture( VertexTextureSampler_t nSampler, ShaderAPITextureHandle_t hTexture ) {}

	// Render target
	virtual void SetRenderTarget( ShaderAPITextureHandle_t colorTextureHandle = SHADER_RENDERTARGET_BACKBUFFER,
		ShaderAPITextureHandle_t depthTextureHandle = SHADER_RENDERTARGET_DEPTHBUFFER );
	virtual void SetRenderTargetEx( int nRenderTargetID,
		ShaderAPITextureHandle_t colorTextureHandle = SHADER_RENDERTARGET_BACKBUFFER,
		ShaderAPITextureHandle_t depthTextureHandle = SHADER_RENDERTARGET_DEPTHBUFFER );
	ITexture *GetRenderTargetEx( int nRenderTargetID ) { return NULL; }

	// Read-back / clears
	virtual void ClearBuffersObeyStencil( bool bClearColor, bool bClearDepth );
	virtual void ClearBuffersObeyStencilEx( bool bClearColor, bool bClearAlpha, bool bClearDepth );
	virtual void ReadPixels( int x, int y, int width, int height, unsigned char *data, ImageFormat dstFormat );
	virtual void ReadPixels( Rect_t *pSrcRect, Rect_t *pDstRect, unsigned char *data, ImageFormat dstFormat, int nDstStride );
	virtual void PerformFullScreenStencilOperation( void ) {}
	virtual void ForceHardwareSync( void ) {}
	virtual void FlushHardware();
	virtual void ResetRenderState( bool bFullReset = true );

	// Frame
	virtual void BeginFrame();
	virtual void EndFrame();

	// Selection mode
	virtual int  SelectionMode( bool selectionMode );
	virtual void SelectionBuffer( unsigned int* pBuffer, int size );
	virtual void ClearSelectionNames( );
	virtual void LoadSelectionName( int name );
	virtual void PushSelectionName( int name );
	virtual void PopSelectionName();

	// Shade / cull mode
	virtual void ShadeMode( ShaderShadeMode_t mode );
	virtual void CullMode( MaterialCullMode_t cullMode );
	virtual void ForceDepthFuncEquals( bool bEnable );
	virtual void OverrideDepthEnable( bool bEnable, bool bDepthEnable );
	virtual void OverrideAlphaWriteEnable( bool bEnable, bool bAlphaWriteEnable );
	virtual void OverrideColorWriteEnable( bool bOverrideEnable, bool bColorWriteEnable );

	// Height clip
	virtual void SetHeightClipZ( float z );
	virtual void SetHeightClipMode( enum MaterialHeightClipMode_t heightClipMode );
	virtual void SetClipPlane( int index, const float *pPlane );
	virtual void EnableClipPlane( int index, bool bEnable );
	virtual void SetFastClipPlane( const float *pPlane );
	virtual void EnableFastClip( bool bEnable );

	// Fog
	virtual void FogStart( float fStart );
	virtual void FogEnd( float fEnd );
	virtual void SetFogZ( float fogZ );
	virtual void FogMaxDensity( float flMaxDensity );
	virtual void GetFogDistances( float *fStart, float *fEnd, float *fFogZ );
	virtual void SceneFogColor3ub( unsigned char r, unsigned char g, unsigned char b );
	virtual void SceneFogMode( MaterialFogMode_t fogMode );
	virtual void GetSceneFogColor( unsigned char *rgb );
	virtual MaterialFogMode_t GetSceneFogMode( );
	virtual int GetPixelFogCombo( );

	// Shader constants
	virtual void SetVertexShaderIndex( int vshIndex );
	virtual void SetPixelShaderIndex( int pshIndex );
	virtual void SetVertexShaderConstant( int var, float const* pVec, int numConst = 1, bool bForce = false );
	virtual void SetBooleanVertexShaderConstant( int var, BOOL const* pVec, int numConst = 1, bool bForce = false ) {}
	virtual void SetIntegerVertexShaderConstant( int var, int const* pVec, int numConst = 1, bool bForce = false ) {}
	virtual void SetPixelShaderConstant( int var, float const* pVec, int numConst = 1, bool bForce = false );
	virtual void SetBooleanPixelShaderConstant( int var, BOOL const* pVec, int numBools = 1, bool bForce = false ) {}
	virtual void SetIntegerPixelShaderConstant( int var, int const* pVec, int numIntVecs = 1, bool bForce = false ) {}
	virtual void InvalidateDelayedShaderConstants( void );
	virtual void SetStandardVertexShaderConstants( float fOverbright ) {}

	// Texture transforms
	virtual void SetTextureTransformDimension( TextureStage_t textureStage, int dimension, bool projected ) {}
	virtual void SetBumpEnvMatrix( TextureStage_t textureStage, float m00, float m01, float m10, float m11 ) {}

	// Hardware morph
	virtual void EnableHWMorphing( bool bEnable );
	virtual bool IsHWMorphingEnabled( void ) const { return m_bHWMorphing; }
	virtual MorphFormat_t GetBoundMorphFormat() { return 0; }
	virtual int MaxHWMorphBatchCount() const { return 0; }
	virtual void SetFlexWeights( int nFirstWeight, int nCount, const MorphWeight_t* pWeights ) {}

	// Vertex/index buffer interface
	virtual void BindVertexBuffer( int nStreamID, IVertexBuffer *pVertexBuffer, int nOffsetInBytes,
		int nFirstVertex, int nVertexCount, VertexFormat_t fmt, int nRepetitions = 1 ) {}
	virtual void BindIndexBuffer( IIndexBuffer *pIndexBuffer, int nOffsetInBytes ) {}
	virtual void Draw( MaterialPrimitiveType_t primitiveType, int nFirstIndex, int nIndexCount );

	// Misc config
	virtual ImageFormat GetNearestSupportedFormat( ImageFormat fmt, bool bFilteringRequired = true ) const;
	virtual ImageFormat GetNearestRenderTargetFormat( ImageFormat fmt ) const;
	virtual int  GetCurrentDynamicVBSize( void ) { return 4 * 1024 * 1024; }
	virtual void DestroyVertexBuffers( bool bExitingLevel = false ) {}
	virtual void EvictManagedResources() {}
	virtual void SetAnisotropicLevel( int nAnisotropyLevel ) {}
	virtual void SyncToken( const char *pToken ) {}
	virtual void GetLightmapDimensions( int *w, int *h ) { *w = *h = 0; }
	virtual bool CanDownloadTextures() const { return true; }

	// Stencil
	virtual void SetStencilEnable(bool onoff) {}
	virtual void SetStencilFailOperation(StencilOperation_t op) {}
	virtual void SetStencilZFailOperation(StencilOperation_t op) {}
	virtual void SetStencilPassOperation(StencilOperation_t op) {}
	virtual void SetStencilCompareFunction(StencilComparisonFunction_t cmpfn) {}
	virtual void SetStencilReferenceValue(int ref) {}
	virtual void SetStencilTestMask(uint32 msk) {}
	virtual void SetStencilWriteMask(uint32 msk) {}
	virtual void ClearStencilBufferRectangle(int xmin, int ymin, int xmax, int ymax, int value) {}

	// Occlusion query
	virtual ShaderAPIOcclusionQuery_t CreateOcclusionQueryObject( void ) { return INVALID_SHADERAPI_OCCLUSION_QUERY_HANDLE; }
	virtual void DestroyOcclusionQueryObject( ShaderAPIOcclusionQuery_t ) {}
	virtual void BeginOcclusionQueryDrawing( ShaderAPIOcclusionQuery_t ) {}
	virtual void EndOcclusionQueryDrawing( ShaderAPIOcclusionQuery_t ) {}
	virtual int OcclusionQuery_GetNumPixelsRendered( ShaderAPIOcclusionQuery_t hQuery, bool bFlush = false ) { return 0; }

	// Flashlight
	virtual void SetFlashlightState( const FlashlightState_t &state, const VMatrix &worldToTexture ) {}
	virtual void SetFlashlightStateEx( const FlashlightState_t &state, const VMatrix &worldToTexture, ITexture *pFlashlightDepthTexture ) {}
	virtual const FlashlightState_t &GetFlashlightState( VMatrix &worldToTexture ) const { static FlashlightState_t s; return s; }
	virtual const FlashlightState_t &GetFlashlightStateEx( VMatrix &worldToTexture, ITexture **pFlashlightDepthTexture ) const { static FlashlightState_t s; return s; }
	virtual bool InFlashlightMode() const { return false; }

	// Shader ref counts
	virtual void ClearVertexAndPixelShaderRefCounts() {}
	virtual void PurgeUnusedVertexAndPixelShaders() {}

	// Standard textures
	virtual void BindStandardTexture( Sampler_t stage, StandardTextureId_t id ) {}
	virtual void BindStandardVertexTexture( VertexTextureSampler_t stage, StandardTextureId_t id ) {}
	virtual void GetStandardTextureDimensions( int *pWidth, int *pHeight, StandardTextureId_t id ) { *pWidth = *pHeight = 0; }

	// AA
	virtual bool IsAAEnabled() const { return false; }
	virtual bool SupportsMSAAMode( int nMSAAMode ) { return false; }
	virtual bool SupportsCSAAMode( int nNumSamples, int nQualityLevel ) { return false; }

	// PIX events
	virtual void BeginPIXEvent( unsigned long color, const char *szName ) {}
	virtual void EndPIXEvent() {}
	virtual void SetPIXMarker( unsigned long color, const char *szName ) {}

	// Alpha to coverage
	virtual void EnableAlphaToCoverage() {}
	virtual void DisableAlphaToCoverage() {}

	// Vertex desc
	virtual void ComputeVertexDescription( unsigned char* pBuffer, VertexFormat_t vertexFormat, MeshDesc_t& desc ) const {}

	// Shadows
	virtual bool SupportsShadowDepthTextures( void ) { return false; }
	virtual ImageFormat GetShadowDepthTextureFormat( void ) { return IMAGE_FORMAT_UNKNOWN; }
	virtual ImageFormat GetNullTextureFormat( void ) { return IMAGE_FORMAT_UNKNOWN; }
	virtual bool SupportsFetch4( void ) { return false; }
	virtual void SetShadowDepthBiasFactors( float fShadowSlopeScaleDepthBias, float fShadowDepthBias ) {}

	// Thread ownership
	virtual void AcquireThreadOwnership() {}
	virtual void ReleaseThreadOwnership() {}

	// HDR
	virtual bool SupportsHDR() const { return true; }
	virtual HDRType_t GetHDRType() const { return HDR_TYPE_FLOAT; }
	virtual HDRType_t GetHardwareHDRType() const { return HDR_TYPE_FLOAT; }
	virtual bool SupportsHDRMode( HDRType_t nHDRMode ) const { return true; }
	virtual bool GetHDREnabled( void ) const { return m_bHDREnabled; }
	virtual void SetHDREnabled( bool bEnable ) { m_bHDREnabled = bEnable; }

	// Hardware config queries
	virtual bool HasDestAlphaBuffer() const { return true; }
	virtual bool HasStencilBuffer() const { return true; }
	virtual int  MaxViewports() const { return 1; }
	virtual void OverrideStreamOffsetSupport( bool bOverrideEnabled, bool bEnableSupport ) {}
	virtual int  GetShadowFilterMode() const { return 0; }
	virtual int  StencilBufferBits() const { return 8; }
	virtual int	 GetFrameBufferColorDepth() const { return 32; }
	virtual int  GetSamplerCount() const { return 16; }
	virtual bool HasSetDeviceGammaRamp() const { return true; }
	virtual bool SupportsCompressedTextures() const { return true; }
	virtual VertexCompressionType_t SupportsCompressedVertices() const { return VERTEX_COMPRESSION_NONE; }
	virtual bool SupportsVertexAndPixelShaders() const { return true; }
	virtual bool SupportsPixelShaders_1_4() const { return true; }
	virtual bool SupportsPixelShaders_2_0() const { return true; }
	virtual bool SupportsPixelShaders_2_b() const { return true; }
	virtual bool ActuallySupportsPixelShaders_2_b() const { return true; }
	virtual bool SupportsStaticControlFlow() const { return true; }
	virtual bool SupportsVertexShaders_2_0() const { return true; }
	virtual bool SupportsShaderModel_3_0() const { return true; }
	virtual int  MaximumAnisotropicLevel() const { return 16; }
	virtual int  MaxTextureWidth() const { return 16384; }
	virtual int  MaxTextureHeight() const { return 16384; }
	virtual int  MaxTextureAspectRatio() const { return 16384; }
	virtual int  GetDXSupportLevel() const { return 98; }
	virtual int	 GetMaxDXSupportLevel() const { return 98; }
	virtual const char *GetShaderDLLName() const { return "shaderapidx9_dxvk"; }
	virtual int	 TextureMemorySize() const { return 4096; }
	virtual bool SupportsOverbright() const { return true; }
	virtual bool SupportsCubeMaps() const { return true; }
	virtual bool SupportsMipmappedCubemaps() const { return true; }
	virtual bool SupportsNonPow2Textures() const { return true; }
	virtual int  GetTextureStageCount() const { return 8; }
	virtual int	 NumVertexShaderConstants() const { return 256; }
	virtual int	 NumBooleanVertexShaderConstants() const { return 16; }
	virtual int	 NumIntegerVertexShaderConstants() const { return 16; }
	virtual int	 NumPixelShaderConstants() const { return 224; }
	virtual int  MaxNumLights() const { return 4; }
	virtual bool SupportsHardwareLighting() const { return true; }
	virtual int	 MaxBlendMatrices() const { return 53; }
	virtual int	 MaxBlendMatrixIndices() const { return 53; }
	virtual int	 MaxVertexShaderBlendMatrices() const { return 53; }
	virtual int	 MaxUserClipPlanes() const { return 6; }
	virtual bool UseFastClipping() const { return false; }
	virtual bool SpecifiesFogColorInLinearSpace() const { return false; }
	virtual bool SupportsSRGB() const { return true; }
	virtual bool FakeSRGBWrite() const { return false; }
	virtual bool CanDoSRGBReadFromRTs() const { return true; }
	virtual bool SupportsGLMixedSizeTargets() const { return true; }
	virtual const char *GetHWSpecificShaderDLLName() const { return "shaderapidx9_dxvk"; }
	virtual bool NeedsAAClamp() const { return false; }
	virtual bool SupportsSpheremapping() const { return true; }
	virtual bool ReadPixelsFromFrontBuffer() const { return false; }
	virtual bool PreferDynamicTextures() const { return true; }
	virtual bool PreferReducedFillrate() const { return false; }
	virtual bool HasProjectedBumpEnv() const { return true; }
	virtual int GetCurrentNumBones( void ) const { return m_nCurrentBones; }
	virtual int GetCurrentLightCombo( void ) const { return 0; }
	virtual void GetDX9LightState( LightState_t *state ) const { memset( state, 0, sizeof(*state) ); }
	virtual MaterialFogMode_t GetCurrentFogType( void ) const { return m_SceneFogMode; }
	virtual void RecordString( const char *pStr ) {}
	virtual void GetDXLevelDefaults(uint &max_dxlevel,uint &recommended_dxlevel) { max_dxlevel = recommended_dxlevel = 98; }
	virtual bool NeedsATICentroidHack() const { return false; }
	virtual bool SupportsColorOnSecondStream() const { return false; }
	virtual bool SupportsStaticPlusDynamicLighting() const { return true; }
	virtual bool SupportsStreamOffset() const { return false; }
	virtual void CommitPixelShaderLighting( int pshReg ) {}
	virtual bool InEditorMode() const { return false; }
	virtual bool HasFastVertexTextures() const { return false; }
	virtual int  GetVertexTextureCount() const { return 0; }
	virtual int  GetMaxVertexTextureDimension() const { return 0; }
	virtual int  MaxTextureDepth() const { return 2048; }
	virtual int  NeedsShaderSRGBConversion(void) const { return 0; }
	virtual bool UsesSRGBCorrectBlending() const { return false; }
	virtual bool ShouldWriteDepthToDestAlpha( void ) const { return false; }
	virtual bool IsDX10Card() const { return true; }
	virtual int  GetVertexBufferCompression( void ) const { return 0; }
	virtual bool SupportsBorderColor() const { return false; }
	virtual bool CanStretchRectFromTextures( void ) const { return false; }
	virtual void EnableBuffer2FramesAhead( bool bEnable ) {}
	virtual void SetPSNearAndFarZ( int pshReg ) {}
	virtual void SetDepthFeatheringPixelShaderConstant( int iConstant, float fDepthBlendScale ) {}
	virtual void SetPixelShaderFogParams( int reg ) {}

	// Linear colorspace
	virtual void EnableLinearColorSpaceFrameBuffer( bool bEnable ) {}
	virtual void SetFullScreenTextureHandle( ShaderAPITextureHandle_t h ) {}

	// Rendering parameters
	virtual void SetFloatRenderingParameter(int parm_number, float value);
	virtual void SetIntRenderingParameter(int parm_number, int value);
	virtual void SetVectorRenderingParameter(int parm_number, Vector const &value);
	virtual float GetFloatRenderingParameter(int parm_number) const;
	virtual int GetIntRenderingParameter(int parm_number) const;
	virtual Vector GetVectorRenderingParameter(int parm_number) const;

	// Scissor
	virtual void SetScissorRect( const int nLeft, const int nTop, const int nRight,
		const int nBottom, const bool bEnableScissor ) {}

	// Gamma<->Linear
	virtual float GammaToLinear_HardwareSpecific( float fGamma ) const { return fGamma; }
	virtual float LinearToGamma_HardwareSpecific( float fLinear ) const { return fLinear; }
	virtual void SetLinearToGammaConversionTextures( ShaderAPITextureHandle_t hSRGBWriteEnabledTexture,
		ShaderAPITextureHandle_t hIdentityTexture ) {}

	// Set disallow
	virtual void SetDisallowAccess( bool ) {}
	virtual void EnableShaderShaderMutex( bool ) {}
	virtual void ShaderLock() {}
	virtual void ShaderUnlock() {}

	// Deformations
	virtual void PushDeformation( const DeformationBase_t *pDeformation ) {}
	virtual void PopDeformation( ) {}
	virtual int GetNumActiveDeformations( ) const { return 0; }
	virtual int GetPackedDeformationInformation( int nMaskOfUnderstoodDeformations,
		float *pConstantValuesOut, int nBufferSize,
		int nMaximumDeformations, int *pNumDefsOut ) const { *pNumDefsOut = 0; return 0; }

	// Standard texture handle
	virtual void SetStandardTextureHandle(StandardTextureId_t,ShaderAPITextureHandle_t) {}
	virtual void ExecuteCommandBuffer( uint8 *pData ) {}

	// Misc
	virtual void CopyRenderTargetToScratchTexture( ShaderAPITextureHandle_t srcRt,
		ShaderAPITextureHandle_t dstTex, Rect_t *pSrcRect = NULL, Rect_t *pDstRect = NULL ) {}
	virtual void LockRect( void** pOutBits, int* pOutPitch, ShaderAPITextureHandle_t texHandle,
		int mipmap, int x, int y, int w, int h, bool bWrite, bool bRead ) {}
	virtual void UnlockRect( ShaderAPITextureHandle_t texHandle, int mipmap ) {}

	// Tone mapping
	virtual void SetToneMappingScaleLinear( const Vector &scale ) { m_ToneMapScale = scale; }
	virtual const Vector &GetToneMappingScaleLinear( void ) const { return m_ToneMapScale; }
	virtual float GetLightMapScaleFactor( void ) const { return 1.0f; }

	// Device lost
	virtual void HandleDeviceLost() {}

	// Debug logging
	virtual void PrintfVA( char *fmt, va_list vargs ) { vprintf( fmt, vargs ); }
	virtual void Printf( PRINTF_FORMAT_STRING const char *fmt, ... ) { va_list a; va_start(a,fmt); vprintf(fmt,a); va_end(a); }
	virtual float Knob( char *knobname, float *setvalue = NULL ) { return 0.0f; }

private:
	// Internal helpers
	int AllocTextureHandle();
	void FreeTextureHandle( int h );
	DxvkTexture_t* GetTexture( int h );
	void InitTexturePool();

	// State
	ShaderViewport_t m_CurrentViewport;
	unsigned char m_ClearColor[4];

	// Lights
	LightDesc_t m_Lights[ 4 ];
	int m_nCurrentBones;

	// Fog
	float m_flFogStart, m_flFogEnd, m_flFogZ, m_flFogMaxDensity;
	unsigned char m_SceneFogColor[3];
	MaterialFogMode_t m_SceneFogMode;

	// Cached state
	VertexFormat_t m_CurrentVertexFormat;
	IMaterial* m_pBoundMaterial;
	int m_nCurrentModifyTexture;

	// Snapshot state (used to compute next snapshot)
	bool m_bSnapshotTranslucent;
	bool m_bSnapshotAlphaTested;
	bool m_bSnapshotVSPS;
	bool m_bSnapshotDepthWrite;

	// Feature toggles
	bool m_bHWMorphing;
	bool m_bHDREnabled;

	// Rendering parameters
	float m_FloatRenderParms[ 64 ];
	int   m_IntRenderParms[ 64 ];
	Vector m_VectorRenderParms[ 64 ];

	// Tone mapping
	Vector m_ToneMapScale;

	// Texture binding table (sampler -> handle)
	ShaderAPITextureHandle_t m_SamplerBindings[ 16 ];
	ShaderAPITextureHandle_t m_RenderTargetColor[ 4 ];
	ShaderAPITextureHandle_t m_RenderTargetDepth;
};

// ---- Singleton ----
CShaderAPIDxVk g_ShaderAPIDxVk;

// Expose single interfaces so material system can find shaderapi_dxvk at startup
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CShaderAPIDxVk, IShaderAPI,
								   SHADERAPI_INTERFACE_VERSION, g_ShaderAPIDxVk )
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CShaderAPIDxVk, IMaterialSystemHardwareConfig,
				MATERIALSYSTEM_HARDWARECONFIG_INTERFACE_VERSION, g_ShaderAPIDxVk )
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CShaderAPIDxVk, IDebugTextureInfo,
				DEBUG_TEXTURE_INFO_VERSION, g_ShaderAPIDxVk )

// (IShaderShadow exposure is handled in shadershadownxvk.cpp via its singleton)

//=============================================================================
// CShaderAPIDxVk Implementation
//=============================================================================
CShaderAPIDxVk::CShaderAPIDxVk()
{
	memset( m_Lights, 0, sizeof(m_Lights) );
	memset( m_ClearColor, 0, sizeof(m_ClearColor) );
	memset( &m_CurrentViewport, 0, sizeof(m_CurrentViewport) );
	memset( m_SceneFogColor, 0, sizeof(m_SceneFogColor) );
	memset( m_FloatRenderParms, 0, sizeof(m_FloatRenderParms) );
	memset( m_IntRenderParms, 0, sizeof(m_IntRenderParms) );
	for ( int i = 0; i < 64; ++i )
		m_VectorRenderParms[ i ].Init();
	memset( m_SamplerBindings, 0, sizeof(m_SamplerBindings) );
	memset( m_RenderTargetColor, 0, sizeof(m_RenderTargetColor) );

	m_CurrentViewport.m_nWidth = 1920;
	m_CurrentViewport.m_nHeight = 1080;

	m_nCurrentBones = 0;
	m_flFogStart = 0.0f;
	m_flFogEnd = 1.0f;
	m_flFogZ = 0.0f;
	m_flFogMaxDensity = 1.0f;
	m_SceneFogMode = MATERIAL_FOG_NONE;

	m_CurrentVertexFormat = 0;
	m_pBoundMaterial = nullptr;
	m_nCurrentModifyTexture = 0;

	m_bSnapshotTranslucent = false;
	m_bSnapshotAlphaTested = false;
	m_bSnapshotVSPS = false;
	m_bSnapshotDepthWrite = true;

	m_bHWMorphing = false;
	m_bHDREnabled = true;
	m_RenderTargetDepth = 0;
	m_ToneMapScale.Init( 1.0f, 1.0f, 1.0f );

	InitTexturePool();
}

CShaderAPIDxVk::~CShaderAPIDxVk() {}

void CShaderAPIDxVk::InitTexturePool()
{
	memset( s_Textures, 0, sizeof(s_Textures) );
	s_nNextTextureHandle = 1;
	s_nNextSnapshot = 0;
	memset( s_Snapshots, 0, sizeof(s_Snapshots) );
}

int CShaderAPIDxVk::AllocTextureHandle()
{
	for ( int i = 1; i < kMaxTextures; ++i )
	{
		int idx = ( s_nNextTextureHandle + i ) % kMaxTextures;
		if ( idx == 0 ) idx = 1;
		if ( !s_Textures[ idx ].bValid )
		{
			s_nNextTextureHandle = idx + 1;
			memset( &s_Textures[ idx ], 0, sizeof(DxvkTexture_t) );
			s_Textures[ idx ].bValid = true;
			return idx;
		}
	}
	return INVALID_SHADERAPI_TEXTURE_HANDLE;
}

void CShaderAPIDxVk::FreeTextureHandle( int h )
{
	if ( h > 0 && h < kMaxTextures )
		memset( &s_Textures[ h ], 0, sizeof(DxvkTexture_t) );
}

DxvkTexture_t* CShaderAPIDxVk::GetTexture( int h )
{
	if ( h <= 0 || h >= kMaxTextures || !s_Textures[h].bValid )
		return nullptr;
	return &s_Textures[ h ];
}

// --- IShaderDynamicAPI ---
void CShaderAPIDxVk::GetBackBufferDimensions( int& width, int& height ) const
{
	width  = m_CurrentViewport.m_nWidth  ? m_CurrentViewport.m_nWidth  : 1920;
	height = m_CurrentViewport.m_nHeight ? m_CurrentViewport.m_nHeight : 1080;
}

void CShaderAPIDxVk::GetCurrentColorCorrection( ShaderColorCorrectionInfo_t* pInfo )
{
	pInfo->m_bIsEnabled = false;
	pInfo->m_nLookupCount = 0;
	pInfo->m_flDefaultWeight = 0.0f;
}

// --- Viewport ---
void CShaderAPIDxVk::SetViewports( int nCount, const ShaderViewport_t* pViewports )
{
	if ( nCount > 0 && pViewports )
		m_CurrentViewport = pViewports[ 0 ];
}

int CShaderAPIDxVk::GetViewports( ShaderViewport_t* pViewports, int nMax ) const
{
	if ( nMax > 0 && pViewports )
		pViewports[ 0 ] = m_CurrentViewport;
	return 1;
}

// --- Clears ---
void CShaderAPIDxVk::ClearBuffers( bool bClearColor, bool bClearDepth, bool bClearStencil,
									int renderTargetWidth, int renderTargetHeight )
{
	// DXVK: record clear attachment ops via render pass
}

void CShaderAPIDxVk::ClearColor3ub( unsigned char r, unsigned char g, unsigned char b )
{
	m_ClearColor[0] = r; m_ClearColor[1] = g; m_ClearColor[2] = b; m_ClearColor[3] = 0xFF;
}

void CShaderAPIDxVk::ClearColor4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a )
{
	m_ClearColor[0] = r; m_ClearColor[1] = g; m_ClearColor[2] = b; m_ClearColor[3] = a;
}

void CShaderAPIDxVk::ClearBuffersObeyStencil( bool bClearColor, bool bClearDepth )
{
	ClearBuffers( bClearColor, bClearDepth, false, m_CurrentViewport.m_nWidth, m_CurrentViewport.m_nHeight );
}

void CShaderAPIDxVk::ClearBuffersObeyStencilEx( bool bClearColor, bool bClearAlpha, bool bClearDepth )
{
	ClearBuffersObeyStencil( bClearColor, bClearDepth );
}

// --- Mode ---
bool CShaderAPIDxVk::SetMode( void* hwnd, int nAdapter, const ShaderDeviceInfo_t &info )
{
	if ( info.m_DisplayMode.m_nWidth > 0 )
		m_CurrentViewport.m_nWidth  = info.m_DisplayMode.m_nWidth;
	if ( info.m_DisplayMode.m_nHeight > 0 )
		m_CurrentViewport.m_nHeight = info.m_DisplayMode.m_nHeight;
	return true;
}

void CShaderAPIDxVk::ChangeVideoMode( const ShaderDeviceInfo_t &info )
{
	if ( info.m_DisplayMode.m_nWidth > 0 )
		m_CurrentViewport.m_nWidth  = info.m_DisplayMode.m_nWidth;
	if ( info.m_DisplayMode.m_nHeight > 0 )
		m_CurrentViewport.m_nHeight = info.m_DisplayMode.m_nHeight;
}

// --- Snapshots ---
StateSnapshot_t CShaderAPIDxVk::TakeSnapshot()
{
	int id = s_nNextSnapshot++;
	if ( s_nNextSnapshot >= kMaxSnapshots ) s_nNextSnapshot = 0;
	s_Snapshots[ id ].m_IsTranslucent = m_bSnapshotTranslucent;
	s_Snapshots[ id ].m_IsAlphaTested = m_bSnapshotAlphaTested;
	s_Snapshots[ id ].m_bUsesVertexAndPixelShaders = m_bSnapshotVSPS;
	s_Snapshots[ id ].m_bDepthWriteEnabled = m_bSnapshotDepthWrite;
	s_Snapshots[ id ].m_VertexFormat = m_CurrentVertexFormat;
	s_Snapshots[ id ].m_MorphFormat = 0;
	return (StateSnapshot_t)id;
}

void CShaderAPIDxVk::ClearSnapshots()
{
	s_nNextSnapshot = 0;
}

bool CShaderAPIDxVk::IsTranslucent( StateSnapshot_t id ) const
{
	return ( id >= 0 && id < kMaxSnapshots ) ? s_Snapshots[id].m_IsTranslucent : false;
}
bool CShaderAPIDxVk::IsAlphaTested( StateSnapshot_t id ) const
{
	return ( id >= 0 && id < kMaxSnapshots ) ? s_Snapshots[id].m_IsAlphaTested : false;
}
bool CShaderAPIDxVk::UsesVertexAndPixelShaders( StateSnapshot_t id ) const
{
	return ( id >= 0 && id < kMaxSnapshots ) ? s_Snapshots[id].m_bUsesVertexAndPixelShaders : false;
}
bool CShaderAPIDxVk::IsDepthWriteEnabled( StateSnapshot_t id ) const
{
	return ( id >= 0 && id < kMaxSnapshots ) ? s_Snapshots[id].m_bDepthWriteEnabled : true;
}

VertexFormat_t CShaderAPIDxVk::ComputeVertexFormat( int numSnapshots, StateSnapshot_t* pIds ) const
{
	VertexFormat_t f = 0;
	for ( int i = 0; i < numSnapshots; ++i )
		if ( pIds[i] >= 0 && pIds[i] < kMaxSnapshots )
			f |= s_Snapshots[ pIds[i] ].m_VertexFormat;
	return f;
}

VertexFormat_t CShaderAPIDxVk::ComputeVertexUsage( int numSnapshots, StateSnapshot_t* pIds ) const
{
	return ComputeVertexFormat( numSnapshots, pIds );
}

MorphFormat_t CShaderAPIDxVk::ComputeMorphFormat( int numSnapshots, StateSnapshot_t* pIds ) const
{
	MorphFormat_t f = 0;
	for ( int i = 0; i < numSnapshots; ++i )
		if ( pIds[i] >= 0 && pIds[i] < kMaxSnapshots )
			f |= s_Snapshots[ pIds[i] ].m_MorphFormat;
	return f;
}

// --- Pass rendering ---
void CShaderAPIDxVk::BeginPass( StateSnapshot_t snapshot )
{
}

void CShaderAPIDxVk::RenderPass( int nPass, int nPassCount )
{
}

void CShaderAPIDxVk::FlushBufferedPrimitives()
{
}

// --- Dynamic meshes ---
extern CDxvkMesh s_StaticMeshDxVk;
extern CDxvkMesh* s_pStaticMeshDxVk;

static CDxvkMesh s_DynamicMeshDxVk( true );
static CDxvkMesh s_FlexMeshDxVk( true );

IMesh* CShaderAPIDxVk::GetDynamicMesh( IMaterial* pMaterial, int nHWSkinBoneCount, bool bBuffered,
	IMesh* pVertexOverride, IMesh* pIndexOverride )
{
	return &s_DynamicMeshDxVk;
}

IMesh* CShaderAPIDxVk::GetDynamicMeshEx( IMaterial* pMaterial, VertexFormat_t vertexFormat, int nHWSkinBoneCount,
	bool bBuffered, IMesh* pVertexOverride, IMesh* pIndexOverride )
{
	m_CurrentVertexFormat |= vertexFormat;
	return &s_DynamicMeshDxVk;
}

IMesh* CShaderAPIDxVk::GetFlexMesh()
{
	return &s_FlexMeshDxVk;
}

void CShaderAPIDxVk::GetMaxToRender( IMesh *pMesh, bool bMaxUntilFlush, int *pMaxVerts, int *pMaxIndices )
{
	*pMaxVerts = 32768;
	*pMaxIndices = 32768;
}

// --- Lights / bones ---
void CShaderAPIDxVk::SetNumBoneWeights( int numBones ) { m_nCurrentBones = numBones; }

void CShaderAPIDxVk::SetLight( int lightNum, const LightDesc_t& desc )
{
	if ( lightNum >= 0 && lightNum < 4 )
		m_Lights[ lightNum ] = desc;
}

void CShaderAPIDxVk::SetLightingOrigin( Vector vLightingOrigin ) {}
void CShaderAPIDxVk::SetAmbientLight( float r, float g, float b ) {}
void CShaderAPIDxVk::SetAmbientLightCube( Vector4D cube[6] ) {}
void CShaderAPIDxVk::SetSkinningMatrices() {}

// --- Texture state ---
void CShaderAPIDxVk::TexMinFilter( ShaderTexFilterMode_t texFilterMode ) {}
void CShaderAPIDxVk::TexMagFilter( ShaderTexFilterMode_t texFilterMode ) {}
void CShaderAPIDxVk::TexWrap( ShaderTexCoordComponent_t coord, ShaderTexWrapMode_t wrapMode ) {}

void CShaderAPIDxVk::BindLightmap( TextureStage_t stage ) {}
void CShaderAPIDxVk::BindBumpLightmap( TextureStage_t stage ) {}
void CShaderAPIDxVk::BindFullbrightLightmap( TextureStage_t stage ) {}
void CShaderAPIDxVk::BindWhite( TextureStage_t stage ) {}
void CShaderAPIDxVk::BindBlack( TextureStage_t stage ) {}
void CShaderAPIDxVk::BindGrey( TextureStage_t stage ) {}
void CShaderAPIDxVk::BindFBTexture( TextureStage_t stage, int textureIdex ) {}
void CShaderAPIDxVk::BindFlatNormalMap( TextureStage_t stage ) {}
void CShaderAPIDxVk::BindNormalizationCubeMap( TextureStage_t stage ) {}
void CShaderAPIDxVk::BindSignedNormalizationCubeMap( TextureStage_t stage ) {}

// --- Texture creation ---
ShaderAPITextureHandle_t CShaderAPIDxVk::CreateTexture(
	int width, int height, int depth, ImageFormat dstImageFormat,
	int numMipLevels, int numCopies, int flags,
	const char *pDebugName, const char *pTextureGroupName )
{
	int h = AllocTextureHandle();
	DxvkTexture_t* pTex = GetTexture( h );
	if ( pTex )
	{
		pTex->nWidth = width;
		pTex->nHeight = height;
		pTex->nDepth = depth;
		pTex->nMipLevels = numMipLevels;
		pTex->format = dstImageFormat;
		pTex->nFlags = flags;
		pTex->bIsRenderTarget = ( flags & TEXTURE_CREATE_RENDERTARGET ) != 0;
		pTex->bIsDepthBuffer  = ( flags & TEXTURE_CREATE_DEPTHBUFFER )  != 0;
	}
	return h;
}

void CShaderAPIDxVk::CreateTextures(
	ShaderAPITextureHandle_t *pHandles, int count,
	int width, int height, int depth, ImageFormat dstImageFormat,
	int numMipLevels, int numCopies, int flags,
	const char *pDebugName, const char *pTextureGroupName )
{
	for ( int i = 0; i < count; ++i )
	{
		pHandles[i] = CreateTexture( width, height, depth, dstImageFormat,
			numMipLevels, numCopies, flags, pDebugName, pTextureGroupName );
	}
}

ShaderAPITextureHandle_t CShaderAPIDxVk::CreateDepthTexture(
	ImageFormat renderTargetFormat, int width, int height,
	const char *pDebugName, bool bTexture )
{
	return CreateTexture( width, height, 1, renderTargetFormat, 1, 1,
		TEXTURE_CREATE_DEPTHBUFFER | ( bTexture ? TEXTURE_CREATE_RENDERTARGET : 0 ),
		pDebugName, "Depth" );
}

void CShaderAPIDxVk::DeleteTexture( ShaderAPITextureHandle_t textureHandle )
{
	FreeTextureHandle( (int)textureHandle );
}

bool CShaderAPIDxVk::IsTexture( ShaderAPITextureHandle_t textureHandle )
{
	return GetTexture( (int)textureHandle ) != nullptr;
}

void CShaderAPIDxVk::ModifyTexture( ShaderAPITextureHandle_t textureHandle )
{
	m_nCurrentModifyTexture = (int)textureHandle;
}

void CShaderAPIDxVk::TexImage2D( int level, int cubeFaceID, ImageFormat dstFormat,
	int zOffset, int width, int height,
	ImageFormat srcFormat, bool bSrcIsTiled, void *imageData )
{
	// DXVK: stage image data and record a copy command to the Vulkan image
}

void CShaderAPIDxVk::TexSubImage2D( int level, int cubeFaceID, int xOffset, int yOffset,
	int zOffset, int width, int height,
	ImageFormat srcFormat, int srcStride, bool bSrcIsTiled, void *imageData )
{
}

// --- Texture binding ---
void CShaderAPIDxVk::BindTexture( Sampler_t sampler, ShaderAPITextureHandle_t textureHandle )
{
	if ( sampler >= 0 && sampler < 16 )
		m_SamplerBindings[ sampler ] = textureHandle;
}

// --- Render target ---
void CShaderAPIDxVk::SetRenderTarget( ShaderAPITextureHandle_t colorTextureHandle,
									  ShaderAPITextureHandle_t depthTextureHandle )
{
	SetRenderTargetEx( 0, colorTextureHandle, depthTextureHandle );
}

void CShaderAPIDxVk::SetRenderTargetEx( int nRenderTargetID,
	ShaderAPITextureHandle_t colorTextureHandle,
	ShaderAPITextureHandle_t depthTextureHandle )
{
	if ( nRenderTargetID >= 0 && nRenderTargetID < 4 )
		m_RenderTargetColor[ nRenderTargetID ] = colorTextureHandle;
	if ( depthTextureHandle != SHADER_RENDERTARGET_DEPTHBUFFER )
		m_RenderTargetDepth = depthTextureHandle;
	else
		m_RenderTargetDepth = 0;
}

// --- Read pixels ---
void CShaderAPIDxVk::ReadPixels( int x, int y, int width, int height, unsigned char *data, ImageFormat dstFormat )
{
	memset( data, 0, width * height * 4 );
}

void CShaderAPIDxVk::ReadPixels( Rect_t *pSrcRect, Rect_t *pDstRect, unsigned char *data, ImageFormat dstFormat, int nDstStride )
{
	if ( !data || !pSrcRect ) return;
	int w = pSrcRect->right - pSrcRect->left;
	int h = pSrcRect->bottom - pSrcRect->top;
	int stride = ( nDstStride > 0 ) ? nDstStride : w * 4;
	for ( int y = 0; y < h; ++y )
		memset( data + y * stride, 0, w * 4 );
}

// --- Frame ---
void CShaderAPIDxVk::BeginFrame()
{
}

void CShaderAPIDxVk::EndFrame()
{
}

void CShaderAPIDxVk::FlushHardware()
{
}

void CShaderAPIDxVk::ResetRenderState( bool bFullReset )
{
	SetDefaultState();
}

void CShaderAPIDxVk::SetDefaultState()
{
	memset( m_SamplerBindings, 0, sizeof(m_SamplerBindings) );
	memset( m_RenderTargetColor, 0, sizeof(m_RenderTargetColor) );
	m_RenderTargetDepth = 0;
	m_CurrentVertexFormat = 0;
	m_pBoundMaterial = nullptr;
	m_nCurrentModifyTexture = 0;
}

// --- Selection mode ---
int  CShaderAPIDxVk::SelectionMode( bool selectionMode ) { return 0; }
void CShaderAPIDxVk::SelectionBuffer( unsigned int* pBuffer, int size ) {}
void CShaderAPIDxVk::ClearSelectionNames( ) {}
void CShaderAPIDxVk::LoadSelectionName( int name ) {}
void CShaderAPIDxVk::PushSelectionName( int name ) {}
void CShaderAPIDxVk::PopSelectionName() {}

// --- Shade / cull ---
void CShaderAPIDxVk::ShadeMode( ShaderShadeMode_t mode ) {}
void CShaderAPIDxVk::CullMode( MaterialCullMode_t cullMode ) {}
void CShaderAPIDxVk::ForceDepthFuncEquals( bool bEnable ) {}
void CShaderAPIDxVk::OverrideDepthEnable( bool bEnable, bool bDepthEnable ) {}
void CShaderAPIDxVk::OverrideAlphaWriteEnable( bool bEnable, bool bAlphaWriteEnable ) {}
void CShaderAPIDxVk::OverrideColorWriteEnable( bool bOverrideEnable, bool bColorWriteEnable ) {}

// --- Height clip / user clip ---
void CShaderAPIDxVk::SetHeightClipZ( float z ) {}
void CShaderAPIDxVk::SetHeightClipMode( enum MaterialHeightClipMode_t heightClipMode ) {}
void CShaderAPIDxVk::SetClipPlane( int index, const float *pPlane ) {}
void CShaderAPIDxVk::EnableClipPlane( int index, bool bEnable ) {}
void CShaderAPIDxVk::SetFastClipPlane( const float *pPlane ) {}
void CShaderAPIDxVk::EnableFastClip( bool bEnable ) {}

// --- Fog ---
void CShaderAPIDxVk::FogStart( float fStart ) { m_flFogStart = fStart; }
void CShaderAPIDxVk::FogEnd( float fEnd ) { m_flFogEnd = fEnd; }
void CShaderAPIDxVk::SetFogZ( float fogZ ) { m_flFogZ = fogZ; }
void CShaderAPIDxVk::FogMaxDensity( float flMaxDensity ) { m_flFogMaxDensity = flMaxDensity; }
void CShaderAPIDxVk::GetFogDistances( float *fStart, float *fEnd, float *fFogZ ) { *fStart = m_flFogStart; *fEnd = m_flFogEnd; *fFogZ = m_flFogZ; }
void CShaderAPIDxVk::SceneFogColor3ub( unsigned char r, unsigned char g, unsigned char b ) { m_SceneFogColor[0]=r; m_SceneFogColor[1]=g; m_SceneFogColor[2]=b; }
void CShaderAPIDxVk::SceneFogMode( MaterialFogMode_t fogMode ) { m_SceneFogMode = fogMode; }
void CShaderAPIDxVk::GetSceneFogColor( unsigned char *rgb ) { if ( rgb ) memcpy( rgb, m_SceneFogColor, 3 ); }
MaterialFogMode_t CShaderAPIDxVk::GetSceneFogMode( ) { return m_SceneFogMode; }
int CShaderAPIDxVk::GetPixelFogCombo( ) { return 0; }

// --- Shader indices/constants ---
void CShaderAPIDxVk::SetVertexShaderIndex( int vshIndex ) { m_bSnapshotVSPS = ( vshIndex > 0 ); }
void CShaderAPIDxVk::SetPixelShaderIndex( int pshIndex ) { m_bSnapshotVSPS = ( pshIndex > 0 ); }
void CShaderAPIDxVk::SetVertexShaderConstant( int var, float const* pVec, int numConst, bool bForce ) {}
void CShaderAPIDxVk::SetPixelShaderConstant( int var, float const* pVec, int numConst, bool bForce ) {}
void CShaderAPIDxVk::InvalidateDelayedShaderConstants( void ) {}

// --- Morph ---
void CShaderAPIDxVk::EnableHWMorphing( bool bEnable ) { m_bHWMorphing = bEnable; }

// --- New VB/IB interface ---
void CShaderAPIDxVk::Draw( MaterialPrimitiveType_t primitiveType, int nFirstIndex, int nIndexCount )
{
}

// --- Nearest formats ---
ImageFormat CShaderAPIDxVk::GetNearestSupportedFormat( ImageFormat fmt, bool bFilteringRequired ) const
{
	// Vulkan supports a wide range; return requested if known, else BGRA8888
	switch ( fmt )
	{
	case IMAGE_FORMAT_RGBA8888:
	case IMAGE_FORMAT_ABGR8888:
	case IMAGE_FORMAT_BGRA8888:
	case IMAGE_FORMAT_ARGB8888:
	case IMAGE_FORMAT_RGB888:
	case IMAGE_FORMAT_BGR888:
	case IMAGE_FORMAT_RGB565:
	case IMAGE_FORMAT_DXT1:
	case IMAGE_FORMAT_DXT1_ONEBITALPHA:
	case IMAGE_FORMAT_DXT3:
	case IMAGE_FORMAT_DXT5:
	case IMAGE_FORMAT_BGRX8888:
	case IMAGE_FORMAT_RGBA16161616:
	case IMAGE_FORMAT_RGBA16161616F:
	case IMAGE_FORMAT_RGBA32323232F:
	case IMAGE_FORMAT_R32F:
	case IMAGE_FORMAT_RG1616F:
	case IMAGE_FORMAT_RG3232F:
	case IMAGE_FORMAT_G16R16:
		return fmt;
	default:
		return IMAGE_FORMAT_BGRA8888;
	}
}

ImageFormat CShaderAPIDxVk::GetNearestRenderTargetFormat( ImageFormat fmt ) const
{
	switch ( fmt )
	{
	case IMAGE_FORMAT_RGBA8888:
	case IMAGE_FORMAT_BGRA8888:
	case IMAGE_FORMAT_ARGB8888:
	case IMAGE_FORMAT_ABGR8888:
	case IMAGE_FORMAT_RGBA16161616F:
	case IMAGE_FORMAT_RGBA32323232F:
	case IMAGE_FORMAT_R32F:
		return fmt;
	default:
		return IMAGE_FORMAT_BGRA8888;
	}
}

// --- Rendering parameters ---
void CShaderAPIDxVk::SetFloatRenderingParameter(int parm_number, float value)
{
	if ( parm_number >= 0 && parm_number < 64 ) m_FloatRenderParms[ parm_number ] = value;
}
void CShaderAPIDxVk::SetIntRenderingParameter(int parm_number, int value)
{
	if ( parm_number >= 0 && parm_number < 64 ) m_IntRenderParms[ parm_number ] = value;
}
void CShaderAPIDxVk::SetVectorRenderingParameter(int parm_number, Vector const &value)
{
	if ( parm_number >= 0 && parm_number < 64 ) m_VectorRenderParms[ parm_number ] = value;
}
float CShaderAPIDxVk::GetFloatRenderingParameter(int parm_number) const
{
	return ( parm_number >= 0 && parm_number < 64 ) ? m_FloatRenderParms[ parm_number ] : 0.0f;
}
int CShaderAPIDxVk::GetIntRenderingParameter(int parm_number) const
{
	return ( parm_number >= 0 && parm_number < 64 ) ? m_IntRenderParms[ parm_number ] : 0;
}
Vector CShaderAPIDxVk::GetVectorRenderingParameter(int parm_number) const
{
	return ( parm_number >= 0 && parm_number < 64 ) ? m_VectorRenderParms[ parm_number ] : Vector(0,0,0);
}

// Render targets need separate depth? Only for MSAA, report false for our baseline stub
bool CShaderAPIDxVk::DoRenderTargetsNeedSeparateDepthBuffer() const { return false; }
