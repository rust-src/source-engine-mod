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
// vkbuffer.h - Vulkan buffer wrapper (replaces CGLMBuffer)
//
#ifndef VKBUFFER_H
#define VKBUFFER_H

#ifdef DX_TO_VK_ABSTRACTION

#include <vulkan/vulkan.h>
#include "tier0/dbg.h"

enum VKBufferType
{
	kVKBufferVertexBuffer = 0,
	kVKBufferIndexBuffer,
	kVKBufferUniformBuffer,
	kVKBufferStagingBuffer,
};

class CVKBuffer
{
public:
	CVKBuffer();
	~CVKBuffer();

	bool Create( CVKContext *ctx, VkBufferUsageFlags usage, VkDeviceSize size, bool dynamic );
	void Destroy();

	// Lock/Unlock for CPU access
	void *Lock( VkDeviceSize offset, VkDeviceSize size, bool readOnly );
	void Unlock();

	// Getters
	VkBuffer GetBuffer() const { return m_buffer; }
	VkDeviceSize GetSize() const { return m_size; }
	VkDeviceSize GetActualSize() const { return m_actualSize; }
	VkDeviceMemory GetMemory() const { return m_memory; }
	bool IsDynamic() const { return m_dynamic; }

	// For persistent mapping
	void *GetMappedPtr() const { return m_mappedPtr; }

private:
	CVKContext *m_ctx;
	VkBuffer m_buffer;
	VkDeviceMemory m_memory;
	VkDeviceSize m_size;
	VkDeviceSize m_actualSize;
	VkDeviceSize m_alignment;
	bool m_dynamic;
	bool m_mapped;
	void *m_mappedPtr;

	// Staging buffer for non-host-visible memory
	VkBuffer m_stagingBuffer;
	VkDeviceMemory m_stagingMemory;
	void *m_stagingMappedPtr;
};

#endif // DX_TO_VK_ABSTRACTION

#endif // VKBUFFER_H
