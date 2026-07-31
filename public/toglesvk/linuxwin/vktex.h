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
// vktex.h - Vulkan texture wrapper (replaces CGLMTex)
//
#ifndef VKTEX_H
#define VKTEX_H

#ifdef DX_TO_VK_ABSTRACTION

#include <vulkan/vulkan.h>
#include "tier0/dbg.h"

// Texture type
enum VKTexType
{
	kVKTex2D = 0,
	kVKTex3D,
	kVKTexCube,
};

// Texture creation params
struct CVKTexParams
{
	CVKTexParams()
	{
		memset( this, 0, sizeof(*this) );
		m_texType = kVKTex2D;
		m_format = VK_FORMAT_UNDEFINED;
		m_width = 1;
		m_height = 1;
		m_depth = 1;
		m_mipCount = 1;
		m_faceCount = 1;
		m_isRenderTarget = false;
		m_isDepthStencil = false;
		m_isDynamic = false;
		m_srgb = false;
	}

	VKTexType m_texType;
	VkFormat m_format;
	uint32 m_width;
	uint32 m_height;
	uint32 m_depth;
	uint32 m_mipCount;
	uint32 m_faceCount;
	bool m_isRenderTarget;
	bool m_isDepthStencil;
	bool m_isDynamic;
	bool m_srgb;
};

// Locked rect (for LockRect)
struct VKLockedRect
{
	void *pBits;
	int Pitch;
};

// Locked box (for LockBox)
struct VKLockedBox
{
	void *pBits;
	int RowPitch;
	int SlicePitch;
};

class CVKTex
{
public:
	CVKTex();
	~CVKTex();

	bool Create( CVKContext *ctx, const CVKTexParams &params );
	void Destroy();

	// Locking
	bool LockRect( uint32 face, uint32 mip, VKLockedRect *pLockedRect, const VkRect2D *pRect, bool readOnly );
	void UnlockRect( uint32 face, uint32 mip );
	bool LockBox( uint32 mip, VKLockedBox *pLockedBox, bool readOnly );
	void UnlockBox( uint32 mip );

	// Subresource layout
	void GetSubresourceLayout( uint32 mip, uint32 arrayLayer, VkSubresourceLayout *pLayout );

	// Getters
	VkImage GetImage() const { return m_image; }
	VkImageView GetView() const { return m_view; }
	VkImageView GetRenderTargetView() const { return m_rtView; }
	VkImageView GetDepthStencilView() const { return m_dsView; }
	VkFormat GetFormat() const { return m_format; }
	uint32 GetWidth() const { return m_width; }
	uint32 GetHeight() const { return m_height; }
	uint32 GetDepth() const { return m_depth; }
	uint32 GetMipCount() const { return m_mipCount; }
	uint32 GetFaceCount() const { return m_faceCount; }
	VKTexType GetTexType() const { return m_texType; }
	bool IsRenderTarget() const { return m_isRenderTarget; }
	bool IsDepthStencil() const { return m_isDepthStencil; }

	// Layout transitions
	void TransitionLayout( VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout );

	// Current layout
	VkImageLayout GetLayout() const { return m_layout; }
	void SetLayout( VkImageLayout layout ) { m_layout = layout; }

private:
	CVKContext *m_ctx;
	VkImage m_image;
	VkDeviceMemory m_memory;
	VkImageView m_view;          // shader resource view
	VkImageView m_rtView;        // render target view
	VkImageView m_dsView;        // depth-stencil view
	VkFormat m_format;
	VkImageLayout m_layout;
	uint32 m_width;
	uint32 m_height;
	uint32 m_depth;
	uint32 m_mipCount;
	uint32 m_faceCount;
	VKTexType m_texType;
	bool m_isRenderTarget;
	bool m_isDepthStencil;
	bool m_isDynamic;
	bool m_srgb;

	// Lock state
	bool m_locked;
	void *m_mappedPtr;
	VkDeviceSize m_mappedOffset;
	VkDeviceSize m_mappedSize;

	// Staging buffer for uploads
	VkBuffer m_stagingBuffer;
	VkDeviceMemory m_stagingMemory;
	void *m_stagingMappedPtr;
	VkDeviceSize m_stagingSize;
};

#endif // DX_TO_VK_ABSTRACTION

#endif // VKTEX_H
