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
// vkprogram.cpp
//
// CVKProgram - wraps a single SPIR-V VkShaderModule translated from a DX9
// vertex or pixel shader. The translation itself lives in dx9asmvtospv.cpp;
// this class just owns the resulting module and exposes its stage flag.
//
//===============================================================================
#include "toglesvk/rendermechanism.h"

#include "tier0/dbg.h"
#include "tier0/icommandline.h"
#include "tier0/vprof.h"
#include "tier0/fasttimer.h"

#include <string.h>

// memdbgon -must- be the last include file in a .cpp file.
#include "tier0/memdbgon.h"

#if !defined( DX_TO_VK_ABSTRACTION )
#error vkprogram.cpp must only be compiled under DX_TO_VK_ABSTRACTION
#endif

//=============================================================================
// CVKProgram
//=============================================================================

CVKProgram::CVKProgram()
{
	m_ctx = NULL;
	m_shaderModule = VK_NULL_HANDLE;
	m_type = kVKProgramVertex;

	m_samplerMask = 0;
	m_samplerTypes = 0;
	m_highWater = 0;

	memset( m_vertexAttribMap, 0, sizeof( m_vertexAttribMap ) );
	m_maxVertexAttrs = 0;

	m_totalConstants = 0;
	m_usesTexture = false;
}

CVKProgram::~CVKProgram()
{
	Destroy();
}

//-----------------------------------------------------------------------------
// Create: build a VkShaderModule from a SPIR-V binary. The data pointer must
// be 4-byte aligned and the size a multiple of 4 (SPIR-V is a stream of
// uint32 words); SPIR-V modules produced by dx9asmvtospv satisfy this.
//-----------------------------------------------------------------------------
bool CVKProgram::Create( CVKContext *ctx, VKProgramType type, const void *spirvData, size_t spirvSize )
{
	Assert( ctx );
	Assert( spirvData );
	if ( !ctx || !spirvData || spirvSize == 0 || !gVK || gVK->m_device == VK_NULL_HANDLE )
	{
		return false;
	}

	if ( ( spirvSize & 0x3 ) != 0 )
	{
		Warning( "CVKProgram::Create: SPIR-V size %zu is not a multiple of 4\n", spirvSize );
		return false;
	}

	// Sanity check the SPIR-V magic number.
	const uint32 *pWords = (const uint32 *)spirvData;
	if ( pWords[0] != 0x07230203 )
	{
		Warning( "CVKProgram::Create: invalid SPIR-V magic (0x%08x)\n", pWords[0] );
		return false;
	}

	m_ctx = ctx;
	m_type = type;

	VkShaderModuleCreateInfo moduleInfo;
	memset( &moduleInfo, 0, sizeof( moduleInfo ) );
	moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	moduleInfo.codeSize = spirvSize;
	moduleInfo.pCode = pWords;

	if ( gVK->vkCreateShaderModule( gVK->m_device, &moduleInfo, VK_NULL_HANDLE, &m_shaderModule ) != VK_SUCCESS )
	{
		Warning( "CVKProgram::Create: vkCreateShaderModule failed (size=%zu)\n", spirvSize );
		m_shaderModule = VK_NULL_HANDLE;
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Destroy.
//-----------------------------------------------------------------------------
void CVKProgram::Destroy()
{
	if ( !gVK || gVK->m_device == VK_NULL_HANDLE )
	{
		m_shaderModule = VK_NULL_HANDLE;
		m_ctx = NULL;
		return;
	}

	if ( m_shaderModule != VK_NULL_HANDLE )
	{
		gVK->vkDestroyShaderModule( gVK->m_device, m_shaderModule, VK_NULL_HANDLE );
		m_shaderModule = VK_NULL_HANDLE;
	}

	m_ctx = NULL;
}

//-----------------------------------------------------------------------------
// GetStage: map the program type to its VkShaderStageFlagBits.
//-----------------------------------------------------------------------------
VkShaderStageFlagBits CVKProgram::GetStage() const
{
	switch ( m_type )
	{
		case kVKProgramVertex:   return VK_SHADER_STAGE_VERTEX_BIT;
		case kVKProgramPixel:    return VK_SHADER_STAGE_FRAGMENT_BIT;
		case kVKProgramGeometry: return VK_SHADER_STAGE_GEOMETRY_BIT;
		default:
			Assert( !"CVKProgram::GetStage: unknown program type" );
			return VK_SHADER_STAGE_VERTEX_BIT;
	}
}
