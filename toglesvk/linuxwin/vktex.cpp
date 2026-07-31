//========= Copyright Valve Corporation, All rights reserved. ============//
//                       TOGL CODE LICENSE
//
//  Copyright 2011-2014 Valve Corporation
//  All Rights Reserved.
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
// vktex.cpp
//
// CVKTex - wraps a VkImage + its backing memory + the VkImageViews needed for
// shader-resource, render-target and depth-stencil access. LockRect/LockBox
// use a staging buffer when the image's memory is not host-visible.
//
//===============================================================================
#include "toglesvk/rendermechanism.h"

#include "tier0/dbg.h"
#include "tier0/icommandline.h"
#include "tier0/vprof.h"

// memdbgon -must- be the last include file in a .cpp file.
#include "tier0/memdbgon.h"

#if !defined( DX_TO_VK_ABSTRACTION )
#error vktex.cpp must only be compiled under DX_TO_VK_ABSTRACTION
#endif

//=============================================================================
// CVKTex
//=============================================================================

CVKTex::CVKTex()
{
	m_ctx = NULL;
	m_image = VK_NULL_HANDLE;
	m_memory = VK_NULL_HANDLE;
	m_view = VK_NULL_HANDLE;
	m_rtView = VK_NULL_HANDLE;
	m_dsView = VK_NULL_HANDLE;
	m_format = VK_FORMAT_UNDEFINED;
	m_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	m_width = 1;
	m_height = 1;
	m_depth = 1;
	m_mipCount = 1;
	m_faceCount = 1;
	m_texType = kVKTex2D;
	m_isRenderTarget = false;
	m_isDepthStencil = false;
	m_isDynamic = false;
	m_srgb = false;

	m_locked = false;
	m_mappedPtr = NULL;
	m_mappedOffset = 0;
	m_mappedSize = 0;

	m_stagingBuffer = VK_NULL_HANDLE;
	m_stagingMemory = VK_NULL_HANDLE;
	m_stagingMappedPtr = NULL;
	m_stagingSize = 0;
}

CVKTex::~CVKTex()
{
	Destroy();
}

//-----------------------------------------------------------------------------
// Create: allocate VkImage + memory + the appropriate VkImageViews.
//-----------------------------------------------------------------------------
bool CVKTex::Create( CVKContext *ctx, const CVKTexParams &params )
{
	Assert( ctx );
	if ( !ctx || !gVK || gVK->m_device == VK_NULL_HANDLE )
	{
		return false;
	}

	m_ctx = ctx;
	m_texType = params.m_texType;
	m_format = params.m_format;
	m_width = params.m_width;
	m_height = params.m_height;
	m_depth = params.m_depth;
	m_mipCount = params.m_mipCount;
	m_faceCount = params.m_faceCount;
	m_isRenderTarget = params.m_isRenderTarget;
	m_isDepthStencil = params.m_isDepthStencil;
	m_isDynamic = params.m_isDynamic;
	m_srgb = params.m_srgb;
	m_layout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkImageCreateInfo imageInfo;
	memset( &imageInfo, 0, sizeof( imageInfo ) );
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.format = m_format;
	imageInfo.extent.width = m_width;
	imageInfo.extent.height = m_height;
	imageInfo.extent.depth = m_depth;
	imageInfo.mipLevels = m_mipCount;
	imageInfo.arrayLayers = m_faceCount;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	if ( m_isRenderTarget )
	{
		usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}
	if ( m_isDepthStencil )
	{
		usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}
	if ( !m_isDepthStencil )
	{
		usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
	}
	imageInfo.usage = usage;

	switch ( m_texType )
	{
		case kVKTex2D:
			imageInfo.imageType = VK_IMAGE_TYPE_2D;
			break;
		case kVKTex3D:
			imageInfo.imageType = VK_IMAGE_TYPE_3D;
			imageInfo.arrayLayers = 1;
			break;
		case kVKTexCube:
			imageInfo.imageType = VK_IMAGE_TYPE_2D;
			imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
			imageInfo.arrayLayers = 6;
			break;
	}

	if ( gVK->vkCreateImage( gVK->m_device, &imageInfo, VK_NULL_HANDLE, &m_image ) != VK_SUCCESS )
	{
		Warning( "CVKTex::Create: vkCreateImage failed\n" );
		m_image = VK_NULL_HANDLE;
		return false;
	}

	VkMemoryRequirements memReq;
	memset( &memReq, 0, sizeof( memReq ) );
	gVK->vkGetImageMemoryRequirements( gVK->m_device, m_image, &memReq );

	VkMemoryPropertyFlags memFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	m_memory = m_ctx->AllocateMemory( memReq.size, memReq.memoryTypeBits, memFlags );
	if ( m_memory == VK_NULL_HANDLE )
	{
		Warning( "CVKTex::Create: AllocateMemory failed\n" );
		return false;
	}

	if ( gVK->vkBindImageMemory( gVK->m_device, m_image, m_memory, 0 ) != VK_SUCCESS )
	{
		Warning( "CVKTex::Create: vkBindImageMemory failed\n" );
		return false;
	}

	// Create the appropriate VkImageViews (shader-resource, render-target,
	// depth-stencil). Done inline rather than via a member helper so the
	// public header stays untouched.
	VkImageViewType viewType;
	switch ( m_texType )
	{
		case kVKTex2D:   viewType = VK_IMAGE_VIEW_TYPE_2D; break;
		case kVKTex3D:   viewType = VK_IMAGE_VIEW_TYPE_3D; break;
		case kVKTexCube: viewType = VK_IMAGE_VIEW_TYPE_CUBE; break;
		default:         viewType = VK_IMAGE_VIEW_TYPE_2D; break;
	}

	VkImageAspectFlags srAspect = m_isDepthStencil
		? ( VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT )
		: VK_IMAGE_ASPECT_COLOR_BIT;

	if ( !m_isDepthStencil )
	{
		VkImageViewCreateInfo viewInfo;
		memset( &viewInfo, 0, sizeof( viewInfo ) );
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_image;
		viewInfo.viewType = viewType;
		viewInfo.format = m_format;
		viewInfo.subresourceRange.aspectMask = srAspect;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = m_mipCount;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = m_faceCount;
		viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

		if ( gVK->vkCreateImageView( gVK->m_device, &viewInfo, VK_NULL_HANDLE, &m_view ) != VK_SUCCESS )
		{
			Warning( "CVKTex::Create: vkCreateImageView (shader resource) failed\n" );
			m_view = VK_NULL_HANDLE;
		}
	}

	if ( m_isRenderTarget )
	{
		VkImageViewCreateInfo viewInfo;
		memset( &viewInfo, 0, sizeof( viewInfo ) );
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = m_format;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;
		viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

		if ( gVK->vkCreateImageView( gVK->m_device, &viewInfo, VK_NULL_HANDLE, &m_rtView ) != VK_SUCCESS )
		{
			Warning( "CVKTex::Create: vkCreateImageView (render target) failed\n" );
			m_rtView = VK_NULL_HANDLE;
		}
	}

	if ( m_isDepthStencil )
	{
		VkImageAspectFlags dsAspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		VkImageViewCreateInfo viewInfo;
		memset( &viewInfo, 0, sizeof( viewInfo ) );
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = m_format;
		viewInfo.subresourceRange.aspectMask = dsAspect;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;
		viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

		if ( gVK->vkCreateImageView( gVK->m_device, &viewInfo, VK_NULL_HANDLE, &m_dsView ) != VK_SUCCESS )
		{
			Warning( "CVKTex::Create: vkCreateImageView (depth-stencil) failed\n" );
			m_dsView = VK_NULL_HANDLE;
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
// Destroy.
//-----------------------------------------------------------------------------
void CVKTex::Destroy()
{
	if ( !gVK || gVK->m_device == VK_NULL_HANDLE )
	{
		m_image = VK_NULL_HANDLE;
		m_memory = VK_NULL_HANDLE;
		m_view = VK_NULL_HANDLE;
		m_rtView = VK_NULL_HANDLE;
		m_dsView = VK_NULL_HANDLE;
		m_stagingBuffer = VK_NULL_HANDLE;
		m_stagingMemory = VK_NULL_HANDLE;
		return;
	}

	gVK->vkDeviceWaitIdle( gVK->m_device );

	if ( m_stagingMappedPtr )
	{
		gVK->vkUnmapMemory( gVK->m_device, m_stagingMemory );
		m_stagingMappedPtr = NULL;
	}

	if ( m_stagingBuffer != VK_NULL_HANDLE )
	{
		gVK->vkDestroyBuffer( gVK->m_device, m_stagingBuffer, VK_NULL_HANDLE );
		m_stagingBuffer = VK_NULL_HANDLE;
	}

	if ( m_stagingMemory != VK_NULL_HANDLE )
	{
		gVK->vkFreeMemory( gVK->m_device, m_stagingMemory, VK_NULL_HANDLE );
		m_stagingMemory = VK_NULL_HANDLE;
	}

	if ( m_view != VK_NULL_HANDLE )
	{
		gVK->vkDestroyImageView( gVK->m_device, m_view, VK_NULL_HANDLE );
		m_view = VK_NULL_HANDLE;
	}

	if ( m_rtView != VK_NULL_HANDLE )
	{
		gVK->vkDestroyImageView( gVK->m_device, m_rtView, VK_NULL_HANDLE );
		m_rtView = VK_NULL_HANDLE;
	}

	if ( m_dsView != VK_NULL_HANDLE )
	{
		gVK->vkDestroyImageView( gVK->m_device, m_dsView, VK_NULL_HANDLE );
		m_dsView = VK_NULL_HANDLE;
	}

	if ( m_image != VK_NULL_HANDLE )
	{
		gVK->vkDestroyImage( gVK->m_device, m_image, VK_NULL_HANDLE );
		m_image = VK_NULL_HANDLE;
	}

	if ( m_memory != VK_NULL_HANDLE )
	{
		gVK->vkFreeMemory( gVK->m_device, m_memory, VK_NULL_HANDLE );
		m_memory = VK_NULL_HANDLE;
	}

	m_locked = false;
	m_mappedPtr = NULL;
	m_mappedOffset = 0;
	m_mappedSize = 0;
	m_stagingSize = 0;
	m_ctx = NULL;
}

//-----------------------------------------------------------------------------
// GetSubresourceLayout: query VkSubresourceLayout (only meaningful for
// LINEAR tiling; reported best-effort for OPTIMAL images on some drivers).
//-----------------------------------------------------------------------------
void CVKTex::GetSubresourceLayout( uint32 mip, uint32 arrayLayer, VkSubresourceLayout *pLayout )
{
	if ( !pLayout || m_image == VK_NULL_HANDLE || !gVK->vkGetImageSubresourceLayout )
	{
		return;
	}

	VkImageSubresource subres;
	memset( &subres, 0, sizeof( subres ) );
	subres.aspectMask = m_isDepthStencil
		? ( VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT )
		: VK_IMAGE_ASPECT_COLOR_BIT;
	subres.mipLevel = mip;
	subres.arrayLayer = arrayLayer;

	gVK->vkGetImageSubresourceLayout( gVK->m_device, m_image, &subres, pLayout );
}

//-----------------------------------------------------------------------------
// EnsureStagingBuffer: allocate (or reuse) a host-visible staging buffer big
// enough for the locked subresource. Returns the mapped CPU pointer.
//-----------------------------------------------------------------------------
void *CVKTex::EnsureStagingBuffer( VkDeviceSize size )
{
	if ( m_stagingBuffer != VK_NULL_HANDLE && m_stagingSize >= size && m_stagingMappedPtr )
	{
		return m_stagingMappedPtr;
	}

	// Drop any existing (smaller) staging allocation.
	if ( m_stagingMappedPtr )
	{
		gVK->vkUnmapMemory( gVK->m_device, m_stagingMemory );
		m_stagingMappedPtr = NULL;
	}
	if ( m_stagingBuffer != VK_NULL_HANDLE )
	{
		gVK->vkDestroyBuffer( gVK->m_device, m_stagingBuffer, VK_NULL_HANDLE );
		m_stagingBuffer = VK_NULL_HANDLE;
	}
	if ( m_stagingMemory != VK_NULL_HANDLE )
	{
		gVK->vkFreeMemory( gVK->m_device, m_stagingMemory, VK_NULL_HANDLE );
		m_stagingMemory = VK_NULL_HANDLE;
	}

	VkBufferCreateInfo bufInfo;
	memset( &bufInfo, 0, sizeof( bufInfo ) );
	bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufInfo.size = size;
	bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if ( gVK->vkCreateBuffer( gVK->m_device, &bufInfo, VK_NULL_HANDLE, &m_stagingBuffer ) != VK_SUCCESS )
	{
		Warning( "CVKTex::EnsureStagingBuffer: vkCreateBuffer failed\n" );
		m_stagingBuffer = VK_NULL_HANDLE;
		return NULL;
	}

	VkMemoryRequirements memReq;
	memset( &memReq, 0, sizeof( memReq ) );
	gVK->vkGetBufferMemoryRequirements( gVK->m_device, m_stagingBuffer, &memReq );

	VkMemoryPropertyFlags flags =
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	m_stagingMemory = m_ctx->AllocateMemory( memReq.size, memReq.memoryTypeBits, flags );
	if ( m_stagingMemory == VK_NULL_HANDLE )
	{
		Warning( "CVKTex::EnsureStagingBuffer: AllocateMemory failed\n" );
		return NULL;
	}

	gVK->vkBindBufferMemory( gVK->m_device, m_stagingBuffer, m_stagingMemory, 0 );

	if ( gVK->vkMapMemory( gVK->m_device, m_stagingMemory, 0, memReq.size, 0, &m_stagingMappedPtr ) != VK_SUCCESS )
	{
		Warning( "CVKTex::EnsureStagingBuffer: vkMapMemory failed\n" );
		m_stagingMappedPtr = NULL;
		return NULL;
	}

	m_stagingSize = memReq.size;
	return m_stagingMappedPtr;
}

//-----------------------------------------------------------------------------
// LockRect: lock a 2D / cube face / mip region. For OPTIMAL-tiling images we
// hand back a staging buffer pointer and record the copy-back on UnlockRect.
//-----------------------------------------------------------------------------
bool CVKTex::LockRect( uint32 face, uint32 mip, VKLockedRect *pLockedRect, const VkRect2D *pRect, bool readOnly )
{
	if ( !pLockedRect || m_image == VK_NULL_HANDLE || !m_ctx )
	{
		return false;
	}

	if ( m_locked )
	{
		Warning( "CVKTex::LockRect: already locked\n" );
		return false;
	}

	// Compute the locked region dimensions.
	uint32 regionWidth = m_width;
	uint32 regionHeight = m_height;
	if ( pRect )
	{
		regionWidth = pRect->extent.width;
		regionHeight = pRect->extent.height;
	}
	else
	{
		regionWidth = MAX( 1u, m_width >> mip );
		regionHeight = MAX( 1u, m_height >> mip );
	}

	// Estimate bytes-per-pixel from the format (good enough for the common
	// uncompressed color / depth-stencil formats we care about).
	uint32 bytesPerPixel = 4;
	if ( m_isDepthStencil )
	{
		bytesPerPixel = 4; // D24S8
	}

	VkDeviceSize lockSize = (VkDeviceSize)regionWidth * regionHeight * bytesPerPixel;
	VkDeviceSize rowPitch = (VkDeviceSize)regionWidth * bytesPerPixel;

	void *pBits = EnsureStagingBuffer( lockSize );
	if ( !pBits )
	{
		return false;
	}

	if ( readOnly )
	{
		// Issue a copy image -> staging so the caller sees current contents.
		VkCommandBuffer cmd = m_ctx->GetUploadCommandBuffer();
		if ( cmd != VK_NULL_HANDLE && gVK->vkCmdCopyImageToBuffer )
		{
			VkImageLayout oldLayout = m_layout;
			if ( oldLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL )
			{
				TransitionLayout( cmd, oldLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
			}

			VkBufferImageCopy region;
			memset( &region, 0, sizeof( region ) );
			region.bufferOffset = 0;
			region.bufferRowLength = regionWidth;
			region.bufferImageHeight = regionHeight;
			region.imageSubresource.aspectMask = m_isDepthStencil
				? ( VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT )
				: VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.mipLevel = mip;
			region.imageSubresource.baseArrayLayer = face;
			region.imageSubresource.layerCount = 1;
			region.imageOffset.x = pRect ? pRect->offset.x : 0;
			region.imageOffset.y = pRect ? pRect->offset.y : 0;
			region.imageOffset.z = 0;
			region.imageExtent.width = regionWidth;
			region.imageExtent.height = regionHeight;
			region.imageExtent.depth = 1;

			gVK->vkCmdCopyImageToBuffer( cmd, m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				m_stagingBuffer, 1, &region );

			TransitionLayout( cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, oldLayout );
		}
	}

	pLockedRect->pBits = pBits;
	pLockedRect->Pitch = (int)rowPitch;

	m_locked = true;
	m_mappedOffset = 0;
	m_mappedSize = lockSize;

	// Remember the lock params for UnlockRect.
	// (Re-using member fields; the face/mip/rect are stashed implicitly via
	// the next UnlockRect call matching this LockRect.)
	(void)face;
	(void)mip;

	return true;
}

//-----------------------------------------------------------------------------
// UnlockRect: for write locks, record staging -> image copy and transition
// the image back into a shader-readable / render-target layout.
//-----------------------------------------------------------------------------
void CVKTex::UnlockRect( uint32 face, uint32 mip )
{
	if ( !m_locked || !m_ctx )
	{
		m_locked = false;
		return;
	}
	m_locked = false;

	VkCommandBuffer cmd = m_ctx->GetUploadCommandBuffer();
	if ( cmd == VK_NULL_HANDLE || m_stagingBuffer == VK_NULL_HANDLE )
	{
		return;
	}

	VkImageLayout oldLayout = m_layout;
	if ( oldLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL )
	{
		TransitionLayout( cmd, oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
	}

	VkBufferImageCopy region;
	memset( &region, 0, sizeof( region ) );
	region.bufferOffset = 0;
	region.bufferRowLength = m_width;
	region.bufferImageHeight = m_height;
	region.imageSubresource.aspectMask = m_isDepthStencil
		? ( VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT )
		: VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = mip;
	region.imageSubresource.baseArrayLayer = face;
	region.imageSubresource.layerCount = 1;
	region.imageOffset.x = 0;
	region.imageOffset.y = 0;
	region.imageOffset.z = 0;
	region.imageExtent.width = m_width;
	region.imageExtent.height = m_height;
	region.imageExtent.depth = 1;

	gVK->vkCmdCopyBufferToImage( cmd, m_stagingBuffer, m_image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );

	VkImageLayout finalLayout = m_isRenderTarget ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		: ( m_isDepthStencil ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
			: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	TransitionLayout( cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, finalLayout );
}

//-----------------------------------------------------------------------------
// LockBox / UnlockBox: 3D texture equivalent of LockRect / UnlockRect.
//-----------------------------------------------------------------------------
bool CVKTex::LockBox( uint32 mip, VKLockedBox *pLockedBox, bool readOnly )
{
	if ( !pLockedBox || m_image == VK_NULL_HANDLE || !m_ctx )
	{
		return false;
	}

	if ( m_locked )
	{
		Warning( "CVKTex::LockBox: already locked\n" );
		return false;
	}

	uint32 mipWidth = MAX( 1u, m_width >> mip );
	uint32 mipHeight = MAX( 1u, m_height >> mip );
	uint32 mipDepth = MAX( 1u, m_depth >> mip );
	uint32 bytesPerPixel = 4;

	VkDeviceSize lockSize = (VkDeviceSize)mipWidth * mipHeight * mipDepth * bytesPerPixel;
	VkDeviceSize rowPitch = (VkDeviceSize)mipWidth * bytesPerPixel;
	VkDeviceSize slicePitch = rowPitch * mipHeight;

	void *pBits = EnsureStagingBuffer( lockSize );
	if ( !pBits )
	{
		return false;
	}

	if ( readOnly )
	{
		VkCommandBuffer cmd = m_ctx->GetUploadCommandBuffer();
		if ( cmd != VK_NULL_HANDLE && gVK->vkCmdCopyImageToBuffer )
		{
			VkImageLayout oldLayout = m_layout;
			if ( oldLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL )
			{
				TransitionLayout( cmd, oldLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL );
			}

			VkBufferImageCopy region;
			memset( &region, 0, sizeof( region ) );
			region.bufferOffset = 0;
			region.bufferRowLength = mipWidth;
			region.bufferImageHeight = mipHeight;
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.mipLevel = mip;
			region.imageSubresource.baseArrayLayer = 0;
			region.imageSubresource.layerCount = 1;
			region.imageOffset.x = 0;
			region.imageOffset.y = 0;
			region.imageOffset.z = 0;
			region.imageExtent.width = mipWidth;
			region.imageExtent.height = mipHeight;
			region.imageExtent.depth = mipDepth;

			gVK->vkCmdCopyImageToBuffer( cmd, m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				m_stagingBuffer, 1, &region );

			TransitionLayout( cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, oldLayout );
		}
	}

	pLockedBox->pBits = pBits;
	pLockedBox->RowPitch = (int)rowPitch;
	pLockedBox->SlicePitch = (int)slicePitch;

	m_locked = true;
	m_mappedOffset = 0;
	m_mappedSize = lockSize;
	return true;
}

void CVKTex::UnlockBox( uint32 mip )
{
	if ( !m_locked || !m_ctx )
	{
		m_locked = false;
		return;
	}
	m_locked = false;

	VkCommandBuffer cmd = m_ctx->GetUploadCommandBuffer();
	if ( cmd == VK_NULL_HANDLE || m_stagingBuffer == VK_NULL_HANDLE )
	{
		return;
	}

	uint32 mipWidth = MAX( 1u, m_width >> mip );
	uint32 mipHeight = MAX( 1u, m_height >> mip );
	uint32 mipDepth = MAX( 1u, m_depth >> mip );

	VkImageLayout oldLayout = m_layout;
	if ( oldLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL )
	{
		TransitionLayout( cmd, oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
	}

	VkBufferImageCopy region;
	memset( &region, 0, sizeof( region ) );
	region.bufferOffset = 0;
	region.bufferRowLength = mipWidth;
	region.bufferImageHeight = mipHeight;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = mip;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageOffset.x = 0;
	region.imageOffset.y = 0;
	region.imageOffset.z = 0;
	region.imageExtent.width = mipWidth;
	region.imageExtent.height = mipHeight;
	region.imageExtent.depth = mipDepth;

	gVK->vkCmdCopyBufferToImage( cmd, m_stagingBuffer, m_image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );

	VkImageLayout finalLayout = m_isRenderTarget ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	TransitionLayout( cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, finalLayout );
}

//-----------------------------------------------------------------------------
// TransitionLayout: record a vkCmdPipelineBarrier that transitions this
// image between two layouts. Tracks m_layout so the next transition starts
// from the right state.
//-----------------------------------------------------------------------------
void CVKTex::TransitionLayout( VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout )
{
	if ( cmd == VK_NULL_HANDLE || m_image == VK_NULL_HANDLE )
	{
		return;
	}

	if ( oldLayout == newLayout )
	{
		m_layout = newLayout;
		return;
	}

	VkImageMemoryBarrier barrier;
	memset( &barrier, 0, sizeof( barrier ) );
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = m_image;

	VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	if ( m_isDepthStencil )
	{
		aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	}
	barrier.subresourceRange.aspectMask = aspectMask;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = m_mipCount;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = m_faceCount;

	VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

	// Source access masks
	switch ( oldLayout )
	{
		case VK_IMAGE_LAYOUT_UNDEFINED:
			barrier.srcAccessMask = 0;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
			break;
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			break;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			break;
		default:
			barrier.srcAccessMask = 0;
			break;
	}

	// Destination access masks
	switch ( newLayout )
	{
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
			break;
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			break;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			break;
		default:
			barrier.dstAccessMask = 0;
			break;
	}

	gVK->vkCmdPipelineBarrier( cmd, srcStageMask, dstStageMask, 0,
		0, NULL,
		0, NULL,
		1, &barrier );

	m_layout = newLayout;
}
