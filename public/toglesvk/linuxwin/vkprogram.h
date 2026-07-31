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
// vkprogram.h - Vulkan shader program wrapper (replaces CGLMProgram)
// Manages SPIR-V shader modules translated from DX9 bytecode
//
#ifndef VKPROGRAM_H
#define VKPROGRAM_H

#ifdef DX_TO_VK_ABSTRACTION

#include <vulkan/vulkan.h>
#include "tier0/dbg.h"

enum VKProgramType
{
	kVKProgramVertex = 0,
	kVKProgramPixel,
	kVKProgramGeometry, // reserved
};

class CVKProgram
{
public:
	CVKProgram();
	~CVKProgram();

	bool Create( CVKContext *ctx, VKProgramType type, const void *spirvData, size_t spirvSize );
	void Destroy();

	VkShaderModule GetShaderModule() const { return m_shaderModule; }
	VkShaderStageFlagBits GetStage() const;
	VKProgramType GetType() const { return m_type; }

	// DX9 shader info (set during translation)
	uint m_samplerMask;      // (1<<n) mask of active samplers
	uint m_samplerTypes;     // SAMPLER_2D, etc. per slot
	uint m_highWater;        // max constant register used

	// Vertex shader specific
	uint m_vertexAttribMap[MAX_VERTEX_ATTRIBUTES];
	uint m_maxVertexAttrs;

	// Constant buffer info
	uint m_totalConstants;
	bool m_usesTexture;

private:
	CVKContext *m_ctx;
	VkShaderModule m_shaderModule;
	VKProgramType m_type;
};

#endif // DX_TO_VK_ABSTRACTION

#endif // VKPROGRAM_H
