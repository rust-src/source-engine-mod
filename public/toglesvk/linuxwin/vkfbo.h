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
// vkfbo.h - Vulkan framebuffer wrapper (replaces CGLMFBO)
//
#ifndef VKFBO_H
#define VKFBO_H

#ifdef DX_TO_VK_ABSTRACTION

#include <vulkan/vulkan.h>
#include "tier0/dbg.h"

class CVKTex;

class CVKFramebuffer
{
public:
	CVKFramebuffer();
	~CVKFramebuffer();

	bool Create( CVKContext *ctx, CVKTex **renderTargets, int rtCount, CVKTex *depthStencil, VkRenderPass renderPass );
	void Destroy();

	VkFramebuffer GetFramebuffer() const { return m_framebuffer; }
	uint32 GetWidth() const { return m_width; }
	uint32 GetHeight() const { return m_height; }
	int GetRenderTargetCount() const { return m_rtCount; }

private:
	CVKContext *m_ctx;
	VkFramebuffer m_framebuffer;
	uint32 m_width;
	uint32 m_height;
	int m_rtCount;
};

#endif // DX_TO_VK_ABSTRACTION

#endif // VKFBO_H
