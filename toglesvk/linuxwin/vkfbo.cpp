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
// vkfbo.cpp
//
// CVKFramebuffer - thin wrapper around a VkFramebuffer built from a set of
// render target textures and (optionally) a depth-stencil texture, bound
// against a VkRenderPass that matches their formats.
//
//===============================================================================
#include "toglesvk/rendermechanism.h"

#include "tier0/dbg.h"
#include "tier0/icommandline.h"
#include "tier0/vprof.h"

// memdbgon -must- be the last include file in a .cpp file.
#include "tier0/memdbgon.h"

#if !defined( DX_TO_VK_ABSTRACTION )
#error vkfbo.cpp must only be compiled under DX_TO_VK_ABSTRACTION
#endif

//=============================================================================
// CVKFramebuffer
//=============================================================================

CVKFramebuffer::CVKFramebuffer()
{
	m_ctx = NULL;
	m_framebuffer = VK_NULL_HANDLE;
	m_width = 0;
	m_height = 0;
	m_rtCount = 0;
}

CVKFramebuffer::~CVKFramebuffer()
{
	Destroy();
}

//-----------------------------------------------------------------------------
// Create: assemble a VkFramebuffer from up to MAX_RENDER_TARGETS color
// attachments and an optional depth-stencil attachment. The render pass
// passed in must have been created with matching attachment descriptions.
//-----------------------------------------------------------------------------
bool CVKFramebuffer::Create( CVKContext *ctx, CVKTex **renderTargets, int rtCount, CVKTex *depthStencil, VkRenderPass renderPass )
{
	Assert( ctx );
	if ( !ctx || !gVK || gVK->m_device == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE )
	{
		return false;
	}

	m_ctx = ctx;
	m_rtCount = 0;

	if ( rtCount < 0 )
	{
		rtCount = 0;
	}
	if ( rtCount > MAX_RENDER_TARGETS )
	{
		Warning( "CVKFramebuffer::Create: rtCount=%d clamped to MAX_RENDER_TARGETS=%d\n", rtCount, MAX_RENDER_TARGETS );
		rtCount = MAX_RENDER_TARGETS;
	}

	// Collect the attachment views and derive the framebuffer dimensions from
	// the first non-null attachment.
	VkImageView attachments[MAX_RENDER_TARGETS + 1];
	memset( attachments, 0, sizeof( attachments ) );

	uint32 attachmentCount = 0;
	uint32 fbWidth = 0;
	uint32 fbHeight = 0;

	for ( int i = 0; i < rtCount; ++i )
	{
		CVKTex *tex = renderTargets ? renderTargets[i] : NULL;
		if ( !tex )
		{
			continue;
		}

		VkImageView view = tex->GetRenderTargetView();
		if ( view == VK_NULL_HANDLE )
		{
			// Render-target view wasn't created - fall back to the shader
			// resource view; the caller is expected to have created the
			// texture with m_isRenderTarget set.
			view = tex->GetView();
		}
		if ( view == VK_NULL_HANDLE )
		{
			Warning( "CVKFramebuffer::Create: render target %d has no view\n", i );
			continue;
		}

		attachments[attachmentCount++] = view;
		m_rtCount = i + 1;

		if ( fbWidth == 0 )
		{
			fbWidth = tex->GetWidth();
			fbHeight = tex->GetHeight();
		}
	}

	if ( depthStencil )
	{
		VkImageView dsView = depthStencil->GetDepthStencilView();
		if ( dsView == VK_NULL_HANDLE )
		{
			Warning( "CVKFramebuffer::Create: depth-stencil has no view\n" );
		}
		else
		{
			attachments[attachmentCount++] = dsView;
			if ( fbWidth == 0 )
			{
				fbWidth = depthStencil->GetWidth();
				fbHeight = depthStencil->GetHeight();
			}
		}
	}

	if ( attachmentCount == 0 )
	{
		Warning( "CVKFramebuffer::Create: no attachments\n" );
		return false;
	}

	m_width = fbWidth;
	m_height = fbHeight;

	VkFramebufferCreateInfo fbInfo;
	memset( &fbInfo, 0, sizeof( fbInfo ) );
	fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fbInfo.renderPass = renderPass;
	fbInfo.attachmentCount = attachmentCount;
	fbInfo.pAttachments = attachments;
	fbInfo.width = fbWidth;
	fbInfo.height = fbHeight;
	fbInfo.layers = 1;

	if ( gVK->vkCreateFramebuffer( gVK->m_device, &fbInfo, VK_NULL_HANDLE, &m_framebuffer ) != VK_SUCCESS )
	{
		Warning( "CVKFramebuffer::Create: vkCreateFramebuffer failed\n" );
		m_framebuffer = VK_NULL_HANDLE;
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Destroy.
//-----------------------------------------------------------------------------
void CVKFramebuffer::Destroy()
{
	if ( !gVK || gVK->m_device == VK_NULL_HANDLE )
	{
		m_framebuffer = VK_NULL_HANDLE;
		m_ctx = NULL;
		return;
	}

	if ( m_framebuffer != VK_NULL_HANDLE )
	{
		gVK->vkDestroyFramebuffer( gVK->m_device, m_framebuffer, VK_NULL_HANDLE );
		m_framebuffer = VK_NULL_HANDLE;
	}

	m_width = 0;
	m_height = 0;
	m_rtCount = 0;
	m_ctx = NULL;
}
