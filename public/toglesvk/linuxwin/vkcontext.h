//========= Copyright Valve Corporation, All rights reserved. ============//
//                       TOGL CODE LICENSE
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.
//
// vkcontext.h - Vulkan context (replaces GLMContext from togles)
// Manages VkInstance, VkDevice, VkQueue, swapchain, command buffers,
// and all pipeline state for the DX9-to-Vulkan translation layer.
//
#ifndef VKCONTEXT_H
#define VKCONTEXT_H

#ifdef DX_TO_VK_ABSTRACTION

#include <vulkan/vulkan.h>
#include "tier0/dbg.h"
#include "tier0/threadtools.h"
#include "tier0/vprof.h"
#include "toglesvk/linuxwin/vkbase.h"

class CVKBuffer;
class CVKTex;
class CVKFramebuffer;
class CVKProgram;
class CVKQuery;

// Display parameters for creating a Vulkan context
class CVKDisplayParams
{
public:
	CVKDisplayParams()
	{
		m_focusWindow = NULL;
		m_fsEnable = false;
		m_vsyncEnable = false;
		m_backBufferWidth = 1280;
		m_backBufferHeight = 720;
		m_backBufferFormat = 0; // D3DFMT_A8R8G8B8
		m_multiSampleCount = 0;
		m_enableAutoDepthStencil = true;
		m_autoDepthStencilFormat = 0; // D3DFMT_D24S8
		m_fsRefreshHz = 0;
	}

	void *m_focusWindow;
	bool m_fsEnable;
	bool m_vsyncEnable;
	uint m_backBufferWidth;
	uint m_backBufferHeight;
	DWORD m_backBufferFormat;
	uint m_multiSampleCount;
	bool m_enableAutoDepthStencil;
	DWORD m_autoDepthStencilFormat;
	uint m_fsRefreshHz;
};

// Rect structure
struct VKRect
{
	int x, y, width, height;
};

// Shader pair info (for linked VS+PS)
struct VKShaderPairInfo
{
	int m_totalVertexShaderConstants;
	int m_totalPixelShaderConstants;
	bool m_usesVertexTexture;
	bool m_usesPixelTexture;
};

// Sampler state tracking
struct VKTexSamplingParams
{
	VkFilter m_minFilter;
	VkFilter m_magFilter;
	VkSamplerMipmapMode m_mipFilter;
	VkSamplerAddressMode m_addressU;
	VkSamplerAddressMode m_addressV;
	VkSamplerAddressMode m_addressW;
	float m_mipLodBias;
	float m_minLod;
	float m_maxLod;
	float m_maxAnisotropy;
	VkCompareOp m_compareOp;
	bool m_srgb;
	VkBorderColor m_borderColor;
};

// Per-frame synchronization data
struct VKFrameSync
{
	VkFence m_renderFence;
	VkSemaphore m_imageAvailableSemaphore;
	VkSemaphore m_renderFinishedSemaphore;
	VkCommandBuffer m_commandBuffer;
	VkCommandBuffer m_uploadCommandBuffer;
	bool m_commandBufferBegan;
	bool m_uploadBufferBegan;
};

// Pipeline state buckets (cached D3D9 state that maps to Vulkan dynamic state)
struct VKAlphaTestEnable_t { bool value; };
struct VKDepthTestEnable_t { bool value; };
struct VKDepthMask_t { bool value; };
struct VKDepthFunc_t { VkCompareOp value; };
struct VKClipPlaneEnable_t { bool value[kVKUserClipPlanes]; };
struct VKClipPlaneEquation_t { float value[kVKUserClipPlanes][4]; };
struct VKColorMaskSingle_t { uint8 value; };
struct VKColorMaskMultiple_t { uint8 value[MAX_RENDER_TARGETS]; };
struct VKCullFaceEnable_t { bool value; };
struct VKCullFrontFace_t { VkCullModeFlags value; };
struct VKPolygonMode_t { VkPolygonMode value; };
struct VKDepthBias_t { float bias; float biasClamp; float slopeScaledBias; };
struct VKScissorEnable_t { bool value; };
struct VKScissorBox_t { VkRect2D rect; };
struct VKViewportBox_t { VkViewport viewport; };
struct VKViewportDepthRange_t { float minDepth; float maxDepth; };
struct VKBlendEnable_t { bool value[MAX_RENDER_TARGETS]; };
struct VKBlendFactor_t { VkBlendFactor src; VkBlendFactor dst; };
struct VKBlendEquation_t { VkBlendOp op; };
struct VKBlendColor_t { float r, g, b, a; };
struct VKBlendEnableSRGB_t { bool value; };
struct VKStencilTestEnable_t { bool value; };
struct VKStencilFunc_t { VkCompareOp func; uint32 ref; uint32 mask; };
struct VKStencilOp_t { VkStencilOp sfail; VkStencilOp dpfail; VkStencilOp dppass; };
struct VKStencilWriteMask_t { uint32 value; };
struct VKClearColor_t { float r, g, b, a; };
struct VKClearDepth_t { float value; };
struct VKClearStencil_t { uint32 value; };

// D3D9 render state value buckets stored on the device
struct VKRenderStateBuckets
{
	VKAlphaTestEnable_t    m_AlphaTestEnable;
	VKDepthTestEnable_t    m_DepthTestEnable;
	VKDepthMask_t          m_DepthMask;
	VKDepthFunc_t          m_DepthFunc;
	VKClipPlaneEnable_t    m_ClipPlaneEnable;
	VKClipPlaneEquation_t  m_ClipPlaneEquation;
	VKColorMaskSingle_t    m_ColorMaskSingle;
	VKColorMaskMultiple_t  m_ColorMaskMultiple;
	VKCullFaceEnable_t     m_CullFaceEnable;
	VKCullFrontFace_t      m_CullFrontFace;
	VKPolygonMode_t        m_PolygonMode;
	VKDepthBias_t          m_DepthBias;
	VKScissorEnable_t      m_ScissorEnable;
	VKScissorBox_t         m_ScissorBox;
	VKViewportBox_t        m_ViewportBox;
	VKViewportDepthRange_t m_ViewportDepthRange;
	VKBlendEnable_t        m_BlendEnable;
	VKBlendFactor_t        m_BlendFactor;
	VKBlendEquation_t      m_BlendEquation;
	VKBlendColor_t         m_BlendColor;
	VKBlendEnableSRGB_t    m_BlendEnableSRGB;
	VKStencilTestEnable_t  m_StencilTestEnable;
	VKStencilFunc_t        m_StencilFunc;
	VKStencilOp_t          m_StencilOp;
	VKStencilWriteMask_t   m_StencilWriteMask;
	VKClearColor_t         m_ClearColor;
	VKClearDepth_t         m_ClearDepth;
	VKClearStencil_t       m_ClearStencil;
};

// Render target state for framebuffer cache key
struct RenderTargetState_t
{
	CVKTex *m_pRenderTargets[MAX_RENDER_TARGETS];
	CVKTex *m_pDepthStencil;

	RenderTargetState_t()
	{
		memset( this, 0, sizeof(*this) );
	}

	void clear() { V_memset( this, 0, sizeof( *this ) ); }

	inline bool RefersTo( CVKTex * pSurf ) const
	{
		for ( uint i = 0; i < MAX_RENDER_TARGETS; i++ )
			if ( m_pRenderTargets[i] == pSurf )
				return true;

		if ( m_pDepthStencil == pSurf )
			return true;

		return false;
	}

	static inline bool LessFunc( const RenderTargetState_t &lhs, const RenderTargetState_t &rhs )
	{
		COMPILE_TIME_ASSERT( sizeof( lhs.m_pRenderTargets[0] ) == sizeof( uintp ) );
		uint64 lhs0 = reinterpret_cast<const uint64 *>(lhs.m_pRenderTargets)[0];
		uint64 rhs0 = reinterpret_cast<const uint64 *>(rhs.m_pRenderTargets)[0];
		if ( lhs0 < rhs0 )
			return true;
		else if ( lhs0 == rhs0 )
		{
			uint64 lhs1 = reinterpret_cast<const uint64 *>(lhs.m_pRenderTargets)[1];
			uint64 rhs1 = reinterpret_cast<const uint64 *>(rhs.m_pRenderTargets)[1];
			if ( lhs1 < rhs1 )
				return true;
			else if ( lhs1 == rhs1 )
			{
				return lhs.m_pDepthStencil < rhs.m_pDepthStencil;
			}
		}
		return false;
	}

	inline bool operator < ( const RenderTargetState_t &rhs ) const
	{
		return LessFunc( *this, rhs );
	}

	bool operator==( const RenderTargetState_t &other ) const
	{
		return memcmp( this, &other, sizeof(*this) ) == 0;
	}
};

// Hash map for framebuffer cache
class CVKFramebufferMap
{
public:
	CVKFramebuffer *FindOrCreate( const RenderTargetState_t &key, CVKContext *ctx );
	void DestroyAll();

private:
	struct Entry
	{
		RenderTargetState_t key;
		CVKFramebuffer *value;
	};
	CUtlVector<Entry> m_entries;
};

// Main Vulkan context class - replaces GLMContext
class CVKContext
{
public:
	CVKContext();
	~CVKContext();

	// Initialization
	bool Create( CVKDisplayParams *params );
	void Destroy();

	// Display
	void GetDisplaySize( uint &width, uint &height );
	void SetDisplaySize( uint width, uint height );

	// Swapchain
	bool CreateSwapchain();
	void DestroySwapchain();
	bool Present();

	// Command buffer management
	VkCommandBuffer GetCommandBuffer();
	VkCommandBuffer GetUploadCommandBuffer();
	void FlushCommandBuffers();
	void BeginRenderPass();
	void EndRenderPass();

	// Memory management
	int32 FindMemoryType( uint32 typeBits, VkMemoryPropertyFlags properties );
	VkDeviceMemory AllocateMemory( VkDeviceSize size, uint32 typeBits, VkMemoryPropertyFlags properties );

	// Texture management
	CVKTex *CreateTex( const struct CVKTexParams &params );
	void DestroyTex( CVKTex *tex );
	void SetSamplerTex( int sampler, CVKTex *tex );
	void SetSamplerStates( int sampler, VkFilter minFilter, VkFilter magFilter, VkSamplerMipmapMode mipFilter,
		VkSamplerAddressMode addrU, VkSamplerAddressMode addrV, VkSamplerAddressMode addrW,
		int minLod, float lodBias );
	void SetSamplerMaxAnisotropy( int sampler, float value );
	void SetSamplerSRGB( int sampler, bool value );

	// Buffer management
	CVKBuffer *CreateBuffer( VkBufferUsageFlags usage, VkDeviceSize size, bool dynamic );
	void DestroyBuffer( CVKBuffer *buffer );

	// Shader management
	CVKProgram *CreateProgram( enum VKProgramType type, const void *data, size_t size );
	void DestroyProgram( CVKProgram *program );

	// Query management
	CVKQuery *CreateQuery( VkQueryType type );
	void DestroyQuery( CVKQuery *query );

	// Framebuffer management
	CVKFramebuffer *GetFBO( const RenderTargetState_t &key );

	// Render state - mirrors D3D9 render states
	void WriteDepthTestEnable( bool enable );
	void WriteDepthMask( bool enable );
	void WriteDepthFunc( VkCompareOp func );
	void WriteDepthBias( float bias, float biasClamp, float slopeScaledBias );
	void WriteCullFaceEnable( bool enable );
	void WriteCullFrontFace( VkCullModeFlags mode );
	void WriteBlendEnable( int target, bool enable );
	void WriteBlendFactor( int target, VkBlendFactor src, VkBlendFactor dst );
	void WriteBlendEquation( int target, VkBlendOp op );
	void WriteBlendColor( float r, float g, float b, float a );
	void WriteBlendEnableSRGB( bool enable );
	void WriteStencilTestEnable( bool enable );
	void WriteStencilFunc( VkCompareOp func, uint32 ref, uint32 mask );
	void WriteStencilOp( VkStencilOp sfail, VkStencilOp dpfail, VkStencilOp dppass );
	void WriteStencilWriteMask( uint32 mask );
	void WriteColorMask( int target, uint8 mask );
	void WriteViewport( const VkViewport &vp );
	void WriteScissor( const VkRect2D &rect );
	void WriteScissorEnable( bool enable );
	void WriteClipPlaneEnable( int idx, bool enable );
	void WriteClipPlaneEquation( int idx, const float eq[4] );

	// Draw calls
	void FlushDrawStates( uint nStartIndex, uint nEndIndex, uint nBaseVertex );
	void DrawPrimitive( VkPrimitiveTopology topology, uint startVertex, uint vertexCount );
	void DrawIndexedPrimitive( VkPrimitiveTopology topology, uint startIndex, uint indexCount, int baseVertex, uint baseIndex );

	// Clear
	void Clear( uint32 mask, const float *color, float depth, uint32 stencil );
	void ClearColorImage( CVKTex *tex, const float color[4] );
	void ClearDepthStencilImage( CVKTex *tex, float depth, uint32 stencil );

	// Blit
	void BlitTex( CVKTex *src, CVKTex *dst, const VkImageBlit *blit );
	void CopyTex( CVKTex *src, CVKTex *dst, const VkImageCopy *copy );

	// Gamma ramp
	void SetGammaRamp( const void *ramp );

	// Stats
	uint m_nTotalDrawsOrClears;
	uint m_nTotalVBLockBytes;
	uint m_nTotalIBLockBytes;

	// Accessors
	VkDevice GetDevice() { return gVK ? gVK->m_device : VK_NULL_HANDLE; }
	VkPhysicalDevice GetPhysicalDevice() { return gVK ? gVK->m_physicalDevice : VK_NULL_HANDLE; }
	VkQueue GetQueue() { return m_queue; }
	VkCommandBuffer GetActiveCommandBuffer() { return m_activeCommandBuffer; }
	VkRenderPass GetRenderPass() { return m_renderPass; }

	// Pipeline cache (for graphics pipelines)
	VkPipelineCache GetPipelineCache() { return m_pipelineCache; }

	// Descriptor set layout for samplers
	VkDescriptorSetLayout GetSamplerDescriptorSetLayout() { return m_samplerDescriptorSetLayout; }
	VkPipelineLayout GetPipelineLayout() { return m_pipelineLayout; }

	// Current bound state
	CVKTex *m_boundRenderTargets[MAX_RENDER_TARGETS];
	CVKTex *m_boundDepthStencil;
	CVKProgram *m_boundVertexShader;
	CVKProgram *m_boundPixelShader;

	// Render state buckets
	VKRenderStateBuckets m_state;
	bool m_stateDirty;

private:
	// Vulkan handles
	VkSurfaceKHR m_surface;
	VkSwapchainKHR m_swapchain;
	VkQueue m_queue;
	uint32 m_queueFamilyIndex;

	// Swapchain images
	VkFormat m_swapchainFormat;
	VkExtent2D m_swapchainExtent;
	CVKTex **m_swapchainTextures;
	uint32 m_swapchainImageCount;
	uint32 m_currentSwapchainImage;

	// Depth buffer for swapchain
	CVKTex *m_depthBuffer;

	// Render pass
	VkRenderPass m_renderPass;

	// Per-frame sync
	VKFrameSync m_frames[MAX_FRAMES_IN_FLIGHT];
	uint32 m_currentFrame;

	// Command pool
	VkCommandPool m_commandPool;
	VkCommandPool m_uploadCommandPool;
	VkCommandBuffer m_activeCommandBuffer;
	VkCommandBuffer m_activeUploadBuffer;
	bool m_inRenderPass;

	// Pipeline cache
	VkPipelineCache m_pipelineCache;

	// Descriptor set layout and pipeline layout for samplers
	VkDescriptorSetLayout m_samplerDescriptorSetLayout;
	VkPipelineLayout m_pipelineLayout;
	VkDescriptorPool m_descriptorPool;

	// Sampler cache
	VkSampler m_samplers[VK_SAMPLER_COUNT];
	VKTexSamplingParams m_samplerParams[VK_SAMPLER_COUNT];
	CVKTex *m_samplerTexs[VK_SAMPLER_COUNT];
	bool m_samplerDirty[VK_SAMPLER_COUNT];

	// Framebuffer cache
	CVKFramebufferMap m_fboMap;

	// Display params
	CVKDisplayParams m_params;

	// Memory type indices
	int32 m_deviceLocalMemoryIndex;
	int32 m_hostVisibleMemoryIndex;
	int32 m_hostCoherentMemoryIndex;

	// Thread ownership
	uint64 m_ownerThreadId;

	void CreateRenderPass();
	void CreateDescriptorSetLayouts();
	void CreatePipelineLayout();
	void CreateDescriptorPool();
	void CreateCommandPools();
	void CreateFrameSync();
	void DestroyFrameSync();
	void CreateSamplers();
	void DestroySamplers();
};

// Helper conversion functions (D3D9 → Vulkan)
VkCompareOp D3DCompareFuncToVK( DWORD func );
VkBlendOp D3DBlendOperationToVK( DWORD op );
VkBlendFactor D3DBlendFactorToVK( DWORD factor );
VkStencilOp D3DStencilOpToVK( DWORD op );
VkPrimitiveTopology D3DPrimitiveTypeToVK( DWORD type );
VkCullModeFlags D3DCullModeToVK( DWORD mode );
VkFormat D3DFormatToVKFormat( DWORD format );
VkPolygonMode D3DFillModeToVK( DWORD mode );
VkFilter D3DTextureFilterToVK( DWORD filter );
VkSamplerAddressMode D3DTextureAddressToVK( DWORD address );

#endif // DX_TO_VK_ABSTRACTION

#endif // VKCONTEXT_H
