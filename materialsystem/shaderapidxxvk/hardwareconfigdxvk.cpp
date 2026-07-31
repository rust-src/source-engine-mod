//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Hardware config - reports capabilities to the material system
//
//===========================================================================//

#include "IHardwareConfigInternal.h"
#include "tier0/platform.h"
#include "tier0/dbg.h"
#include "dxvk_adapter.h"

//-----------------------------------------------------------------------------
// CDxvkHardwareConfig
//
// Encapsulates GPU feature reporting for the DXVK backend.
// The material system queries this via IMaterialSystemHardwareConfig.
//-----------------------------------------------------------------------------
class CDxvkHardwareConfig
{
public:
	CDxvkHardwareConfig() { SetDefaults(); }

	void SetDefaults()
	{
		m_nMaxTextureWidth  = 16384;
		m_nMaxTextureHeight = 16384;
		m_nMaxTextureDepth  = 2048;
		m_nMaxTextureAnisotropy = 16;
		m_nTextureMemoryMB = 4096;
		m_nDXSupportLevel = 98;   // DX9.0c equivalent feature set
		m_nMaxDXSupportLevel = 98;
		m_nNumSamplers = 16;
		m_nNumVertexSamplers = 4;
		m_nNumVertexShaderConstants = 256;
		m_nNumPixelShaderConstants  = 224;
		m_nStencilBufferBits = 8;
		m_nFramebufferColorDepth = 32;
		m_nMaxViewports = 16;
		m_nMaxHWMorphBatchCount = 0;
		m_nTextureStageCount = 8;
		m_nMaxLights = 4;
		m_nMaxBlendMatrices = 53;
		m_nMaxUserClipPlanes = 6;
		m_bSupportsCompressedTextures = true;
		m_bSupportsNonPow2Textures = true;
		m_bSupportsCubeMaps = true;
		m_bSupportsMipmappedCubemaps = true;
		m_bSupportsOverbright = true;
		m_bSupportsHardwareLighting = true;
		m_bSupportsSRGB = true;
		m_bSupportsHDR = true;
		m_bSupportsFetch4 = false;
		m_bSupportsShadowDepthTextures = true;
		m_bSupportsStreamOffset = false;
		m_bSupportsMSAA = true;
		m_bHasDestAlpha = true;
		m_bHasStencil = true;
		m_bPreferDynamicTextures = true;
		m_bSupportsVertexAndPixelShaders = true;
		m_bSupportsSM20b = true;
		m_bSupportsSM30 = true;
		m_eHDRType = HDR_TYPE_FLOAT;
	}

	// Would populate from actual Vulkan physical device properties
	bool InitFromPhysicalDevice()
	{
		// Real DXVK impl would query VkPhysicalDeviceLimits and VkPhysicalDeviceFeatures here.
		return true;
	}

	// Accessors
	int MaxTextureWidth()  const { return m_nMaxTextureWidth; }
	int MaxTextureHeight() const { return m_nMaxTextureHeight; }
	int MaxTextureDepth()  const { return m_nMaxTextureDepth; }
	int MaxAnisotropy()    const { return m_nMaxTextureAnisotropy; }
	int TextureMemoryMB()  const { return m_nTextureMemoryMB; }
	int DXSupportLevel()   const { return m_nDXSupportLevel; }
	int NumSamplers()      const { return m_nNumSamplers; }
	int StencilBits()      const { return m_nStencilBufferBits; }
	bool SupportsCompressedTextures() const { return m_bSupportsCompressedTextures; }
	bool SupportsSRGB()               const { return m_bSupportsSRGB; }
	bool SupportsHDR()                const { return m_bSupportsHDR; }
	HDRType_t HDRType()               const { return m_eHDRType; }

private:
	int m_nMaxTextureWidth;
	int m_nMaxTextureHeight;
	int m_nMaxTextureDepth;
	int m_nMaxTextureAnisotropy;
	int m_nTextureMemoryMB;
	int m_nDXSupportLevel;
	int m_nMaxDXSupportLevel;
	int m_nNumSamplers;
	int m_nNumVertexSamplers;
	int m_nNumVertexShaderConstants;
	int m_nNumPixelShaderConstants;
	int m_nStencilBufferBits;
	int m_nFramebufferColorDepth;
	int m_nMaxViewports;
	int m_nMaxHWMorphBatchCount;
	int m_nTextureStageCount;
	int m_nMaxLights;
	int m_nMaxBlendMatrices;
	int m_nMaxUserClipPlanes;

	bool m_bSupportsCompressedTextures;
	bool m_bSupportsNonPow2Textures;
	bool m_bSupportsCubeMaps;
	bool m_bSupportsMipmappedCubemaps;
	bool m_bSupportsOverbright;
	bool m_bSupportsHardwareLighting;
	bool m_bSupportsSRGB;
	bool m_bSupportsHDR;
	bool m_bSupportsFetch4;
	bool m_bSupportsShadowDepthTextures;
	bool m_bSupportsStreamOffset;
	bool m_bSupportsMSAA;
	bool m_bHasDestAlpha;
	bool m_bHasStencil;
	bool m_bPreferDynamicTextures;
	bool m_bSupportsVertexAndPixelShaders;
	bool m_bSupportsSM20b;
	bool m_bSupportsSM30;

	HDRType_t m_eHDRType;
};

static CDxvkHardwareConfig s_HardwareConfig;

//-----------------------------------------------------------------------------
// Public accessors
//-----------------------------------------------------------------------------
bool DxvkHardwareConfig_Init()
{
	s_HardwareConfig.SetDefaults();
	if ( g_pDxvkAdapter && g_pDxvkAdapter->IsInitialized() )
		s_HardwareConfig.InitFromPhysicalDevice();
	return true;
}

const CDxvkHardwareConfig* DxvkHardwareConfig_Get()
{
	return &s_HardwareConfig;
}

// IMaterialSystemHardwareConfig::GetShadowFilterMode uses this path
int  DxvkHardwareConfig_GetShadowFilterMode() { return 0; }
bool DxvkHardwareConfig_SupportsCSAAMode( int nSamples, int nQuality ) { return false; }
int  DxvkHardwareConfig_GetVertexTextureCount() { return s_HardwareConfig.NumSamplers() / 4; }

//-----------------------------------------------------------------------------
// Debug dump
//-----------------------------------------------------------------------------
void DxvkHardwareConfig_DumpSpew()
{
	Msg( "DXVK Hardware Config:\n" );
	Msg( "  Max texture: %dx%dx%d\n",
		 s_HardwareConfig.MaxTextureWidth(),
		 s_HardwareConfig.MaxTextureHeight(),
		 s_HardwareConfig.MaxTextureDepth() );
	Msg( "  DX support level: %d\n", s_HardwareConfig.DXSupportLevel() );
	Msg( "  Texture memory: %d MB\n", s_HardwareConfig.TextureMemoryMB() );
	Msg( "  HDR supported: %d (type %d)\n",
		 s_HardwareConfig.SupportsHDR() ? 1 : 0, (int)s_HardwareConfig.HDRType() );
}
