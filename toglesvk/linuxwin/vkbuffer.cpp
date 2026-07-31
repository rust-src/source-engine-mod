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
// vkbuffer.cpp
//
// CVKBuffer - wraps a VkBuffer + its backing VkDeviceMemory. Dynamic buffers
// are persistently mapped (HOST_VISIBLE | HOST_COHERENT). Static buffers use
// DEVICE_LOCAL memory and a staging buffer for CPU-side uploads.
//
//===============================================================================
#include "toglesvk/rendermechanism.h"

#include "tier0/dbg.h"
#include "tier0/icommandline.h"
#include "tier0/vprof.h"

// memdbgon -must- be the last include file in a .cpp file.
#include "tier0/memdbgon.h"

#if !defined( DX_TO_VK_ABSTRACTION )
#error vkbuffer.cpp must only be compiled under DX_TO_VK_ABSTRACTION
#endif

//=============================================================================
// CVKBuffer
//=============================================================================

CVKBuffer::CVKBuffer()
{
	m_ctx = NULL;
	m_buffer = VK_NULL_HANDLE;
	m_memory = VK_NULL_HANDLE;
	m_size = 0;
	m_actualSize = 0;
	m_alignment = 0;
	m_dynamic = false;
	m_mapped = false;
	m_mappedPtr = NULL;

	m_stagingBuffer = VK_NULL_HANDLE;
	m_stagingMemory = VK_NULL_HANDLE;
	m_stagingMappedPtr = NULL;
}

CVKBuffer::~CVKBuffer()
{
	Destroy();
}

//-----------------------------------------------------------------------------
// Create: allocate the VkBuffer + VkDeviceMemory. For dynamic buffers we
// request HOST_VISIBLE | HOST_COHERENT and persistently map it. For static
// buffers we request DEVICE_LOCAL and also allocate a small staging buffer
// (HOST_VISIBLE) used to feed upload commands.
//-----------------------------------------------------------------------------
bool CVKBuffer::Create( CVKContext *ctx, VkBufferUsageFlags usage, VkDeviceSize size, bool dynamic )
{
	Assert( ctx );
	Assert( size > 0 );
	if ( !ctx || size == 0 || !gVK || gVK->m_device == VK_NULL_HANDLE )
	{
		return false;
	}

	m_ctx = ctx;
	m_size = size;
	m_dynamic = dynamic;

	VkBufferCreateInfo bufInfo;
	memset( &bufInfo, 0, sizeof( bufInfo ) );
	bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufInfo.size = size;
	bufInfo.usage = usage;
	bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if ( gVK->vkCreateBuffer( gVK->m_device, &bufInfo, VK_NULL_HANDLE, &m_buffer ) != VK_SUCCESS )
	{
		Warning( "CVKBuffer::Create: vkCreateBuffer failed (size=%llu)\n", (unsigned long long)size );
		m_buffer = VK_NULL_HANDLE;
		return false;
	}

	VkMemoryRequirements memReq;
	memset( &memReq, 0, sizeof( memReq ) );
	gVK->vkGetBufferMemoryRequirements( gVK->m_device, m_buffer, &memReq );

	m_actualSize = memReq.size;
	m_alignment = memReq.alignment;

	VkMemoryPropertyFlags memFlags;
	if ( dynamic )
	{
		memFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	}
	else
	{
		memFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	}

	m_memory = m_ctx->AllocateMemory( memReq.size, memReq.memoryTypeBits, memFlags );
	if ( m_memory == VK_NULL_HANDLE )
	{
		// Fall back to host-visible memory if the requested type isn't available
		// (common on devices with a single memory heap).
		if ( !dynamic )
		{
			Warning( "CVKBuffer::Create: DEVICE_LOCAL alloc failed, falling back to HOST_VISIBLE\n" );
			memFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
			m_memory = m_ctx->AllocateMemory( memReq.size, memReq.memoryTypeBits, memFlags );
		}
		if ( m_memory == VK_NULL_HANDLE )
		{
			Warning( "CVKBuffer::Create: AllocateMemory failed\n" );
			return false;
		}
	}

	if ( gVK->vkBindBufferMemory( gVK->m_device, m_buffer, m_memory, 0 ) != VK_SUCCESS )
	{
		Warning( "CVKBuffer::Create: vkBindBufferMemory failed\n" );
		return false;
	}

	// Persistently map dynamic buffers so Lock()/Unlock() are trivial.
	if ( dynamic )
	{
		if ( gVK->vkMapMemory( gVK->m_device, m_memory, 0, m_actualSize, 0, &m_mappedPtr ) != VK_SUCCESS )
		{
			Warning( "CVKBuffer::Create: vkMapMemory failed for dynamic buffer\n" );
			m_mappedPtr = NULL;
		}
	}

	// For static buffers, create a staging buffer that is persistently mapped so
	// Lock() can return a CPU pointer the caller can write through.
	if ( !dynamic )
	{
		VkBufferCreateInfo stagingInfo;
		memset( &stagingInfo, 0, sizeof( stagingInfo ) );
		stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		stagingInfo.size = size;
		stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if ( gVK->vkCreateBuffer( gVK->m_device, &stagingInfo, VK_NULL_HANDLE, &m_stagingBuffer ) != VK_SUCCESS )
		{
			Warning( "CVKBuffer::Create: vkCreateBuffer (staging) failed\n" );
			m_stagingBuffer = VK_NULL_HANDLE;
			return true; // buffer itself is still usable for GPU-only access
		}

		VkMemoryRequirements stagingReq;
		memset( &stagingReq, 0, sizeof( stagingReq ) );
		gVK->vkGetBufferMemoryRequirements( gVK->m_device, m_stagingBuffer, &stagingReq );

		VkMemoryPropertyFlags stagingFlags =
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

		m_stagingMemory = m_ctx->AllocateMemory( stagingReq.size, stagingReq.memoryTypeBits, stagingFlags );
		if ( m_stagingMemory == VK_NULL_HANDLE )
		{
			Warning( "CVKBuffer::Create: AllocateMemory (staging) failed\n" );
			return true;
		}

		gVK->vkBindBufferMemory( gVK->m_device, m_stagingBuffer, m_stagingMemory, 0 );

		if ( gVK->vkMapMemory( gVK->m_device, m_stagingMemory, 0, stagingReq.size, 0, &m_stagingMappedPtr ) != VK_SUCCESS )
		{
			Warning( "CVKBuffer::Create: vkMapMemory (staging) failed\n" );
			m_stagingMappedPtr = NULL;
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
// Destroy.
//-----------------------------------------------------------------------------
void CVKBuffer::Destroy()
{
	if ( !gVK || gVK->m_device == VK_NULL_HANDLE )
	{
		m_buffer = VK_NULL_HANDLE;
		m_memory = VK_NULL_HANDLE;
		m_stagingBuffer = VK_NULL_HANDLE;
		m_stagingMemory = VK_NULL_HANDLE;
		return;
	}

	gVK->vkDeviceWaitIdle( gVK->m_device );

	if ( m_mappedPtr )
	{
		gVK->vkUnmapMemory( gVK->m_device, m_memory );
		m_mappedPtr = NULL;
	}

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

	if ( m_buffer != VK_NULL_HANDLE )
	{
		gVK->vkDestroyBuffer( gVK->m_device, m_buffer, VK_NULL_HANDLE );
		m_buffer = VK_NULL_HANDLE;
	}

	if ( m_memory != VK_NULL_HANDLE )
	{
		gVK->vkFreeMemory( gVK->m_device, m_memory, VK_NULL_HANDLE );
		m_memory = VK_NULL_HANDLE;
	}

	m_size = 0;
	m_actualSize = 0;
	m_alignment = 0;
	m_dynamic = false;
	m_mapped = false;
	m_ctx = NULL;
}

//-----------------------------------------------------------------------------
// Lock: return a CPU pointer into the buffer.
//   - Dynamic buffer: offset into the persistent mapping.
//   - Static buffer: offset into the staging buffer; Unlock() issues the
//     vkCmdCopyBuffer to flush the data to the device-local buffer.
//-----------------------------------------------------------------------------
void *CVKBuffer::Lock( VkDeviceSize offset, VkDeviceSize size, bool readOnly )
{
	if ( m_buffer == VK_NULL_HANDLE )
	{
		return NULL;
	}

	if ( m_dynamic )
	{
		if ( !m_mappedPtr )
		{
			return NULL;
		}
		m_mapped = true;
		return (void *)( (uint8 *)m_mappedPtr + offset );
	}

	// Static buffer path: write through the staging buffer.
	if ( m_stagingMappedPtr )
	{
		m_mapped = true;
		VkDeviceSize lockSize = ( size == 0 ) ? m_size : size;
		(void)lockSize;
		(void)readOnly;
		return (void *)( (uint8 *)m_stagingMappedPtr + offset );
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// Unlock: for static buffers, record a vkCmdCopyBuffer from the staging
// buffer into the device-local buffer on the upload command buffer.
//-----------------------------------------------------------------------------
void CVKBuffer::Unlock()
{
	if ( !m_mapped )
	{
		return;
	}
	m_mapped = false;

	if ( m_dynamic )
	{
		// HOST_COHERENT memory: nothing to do, the GPU sees writes automatically.
		return;
	}

	// Static buffer: kick a copy from staging -> device-local.
	if ( m_stagingBuffer == VK_NULL_HANDLE || m_buffer == VK_NULL_HANDLE || !m_ctx )
	{
		return;
	}

	VkCommandBuffer cmd = m_ctx->GetUploadCommandBuffer();
	if ( cmd == VK_NULL_HANDLE )
	{
		return;
	}

	VkBufferCopy region;
	memset( &region, 0, sizeof( region ) );
	region.srcOffset = 0;
	region.dstOffset = 0;
	region.size = m_size;

	gVK->vkCmdCopyBuffer( cmd, m_stagingBuffer, m_buffer, 1, &region );

	VkBufferMemoryBarrier barrier;
	memset( &barrier, 0, sizeof( barrier ) );
	barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT |
							VK_ACCESS_UNIFORM_READ_BIT;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer = m_buffer;
	barrier.offset = 0;
	barrier.size = m_size;

	gVK->vkCmdPipelineBarrier( cmd,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0,
		0, NULL,
		1, &barrier,
		0, NULL );
}
