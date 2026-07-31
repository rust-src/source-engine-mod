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
// vkquery.cpp
//
// CVKQuery - wraps a VkQueryPool slot used for occlusion / timestamp queries.
// Each CVKQuery owns its own one-query pool (kept simple, matching the
// one-query-per-object semantics of D3D9 IDirect3DQuery9).
//
//===============================================================================
#include "toglesvk/rendermechanism.h"

#include "tier0/dbg.h"
#include "tier0/icommandline.h"
#include "tier0/vprof.h"

// memdbgon -must- be the last include file in a .cpp file.
#include "tier0/memdbgon.h"

#if !defined( DX_TO_VK_ABSTRACTION )
#error vkquery.cpp must only be compiled under DX_TO_VK_ABSTRACTION
#endif

//=============================================================================
// CVKQuery
//=============================================================================

CVKQuery::CVKQuery()
{
	m_ctx = NULL;
	m_queryPool = VK_NULL_HANDLE;
	m_queryIndex = 0;
	m_type = VK_QUERY_TYPE_OCCLUSION;
	m_active = false;
}

CVKQuery::~CVKQuery()
{
	Destroy();
}

//-----------------------------------------------------------------------------
// Create: allocate a one-slot VkQueryPool of the requested type.
//-----------------------------------------------------------------------------
bool CVKQuery::Create( CVKContext *ctx, VkQueryType type )
{
	Assert( ctx );
	if ( !ctx || !gVK || gVK->m_device == VK_NULL_HANDLE )
	{
		return false;
	}

	m_ctx = ctx;
	m_type = type;
	m_queryIndex = 0;
	m_active = false;

	VkQueryPoolCreateInfo poolInfo;
	memset( &poolInfo, 0, sizeof( poolInfo ) );
	poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	poolInfo.queryType = type;
	poolInfo.queryCount = 1;

	// Occlusion queries need the precise bit to return actual sample counts;
	// binary occlusion would use VK_QUERY_CONTROL_NO_WAIT_BIT at begin time,
	// which the callers here don't request.
	if ( type == VK_QUERY_TYPE_OCCLUSION )
	{
		// no per-pool flags in core Vulkan 1.x for occlusion
	}
	else if ( type == VK_QUERY_TYPE_TIMESTAMP )
	{
		// timestamp pools carry no special flags either
	}

	if ( gVK->vkCreateQueryPool( gVK->m_device, &poolInfo, VK_NULL_HANDLE, &m_queryPool ) != VK_SUCCESS )
	{
		Warning( "CVKQuery::Create: vkCreateQueryPool failed (type=%d)\n", (int)type );
		m_queryPool = VK_NULL_HANDLE;
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Destroy.
//-----------------------------------------------------------------------------
void CVKQuery::Destroy()
{
	if ( !gVK || gVK->m_device == VK_NULL_HANDLE )
	{
		m_queryPool = VK_NULL_HANDLE;
		m_ctx = NULL;
		return;
	}

	if ( m_queryPool != VK_NULL_HANDLE )
	{
		gVK->vkDestroyQueryPool( gVK->m_device, m_queryPool, VK_NULL_HANDLE );
		m_queryPool = VK_NULL_HANDLE;
	}

	m_active = false;
	m_ctx = NULL;
}

//-----------------------------------------------------------------------------
// Begin: record vkCmdBeginQuery (no-op for timestamp queries).
//-----------------------------------------------------------------------------
void CVKQuery::Begin( VkCommandBuffer cmd )
{
	if ( cmd == VK_NULL_HANDLE || m_queryPool == VK_NULL_HANDLE )
	{
		return;
	}

	if ( m_type == VK_QUERY_TYPE_TIMESTAMP )
	{
		// Timestamp queries have no begin - they're written once via
		// vkCmdWriteTimestamp; treat Begin as a no-op and record the
		// timestamp at End() instead.
		m_active = true;
		return;
	}

	// Reset the query slot before reusing it.
	if ( gVK->vkCmdResetQueryPool )
	{
		gVK->vkCmdResetQueryPool( cmd, m_queryPool, m_queryIndex, 1 );
	}

	gVK->vkCmdBeginQuery( cmd, m_queryPool, m_queryIndex, 0 );
	m_active = true;
}

//-----------------------------------------------------------------------------
// End: record vkCmdEndQuery (or vkCmdWriteTimestamp for timestamp queries).
//-----------------------------------------------------------------------------
void CVKQuery::End( VkCommandBuffer cmd )
{
	if ( cmd == VK_NULL_HANDLE || m_queryPool == VK_NULL_HANDLE || !m_active )
	{
		return;
	}

	if ( m_type == VK_QUERY_TYPE_TIMESTAMP )
	{
		// Look up vkCmdWriteTimestamp - it's a core entry point not exposed
		// on CVulkanEntryPoints (only vkCmd* set state / draw ones are).
		PFN_vkCmdWriteTimestamp pfnWriteTimestamp =
			(PFN_vkCmdWriteTimestamp) VKGetInstanceProcAddr( "vkCmdWriteTimestamp" );
		if ( pfnWriteTimestamp )
		{
			pfnWriteTimestamp( cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_queryPool, m_queryIndex );
		}
		else
		{
			Warning( "CVKQuery::End: vkCmdWriteTimestamp unavailable\n" );
		}
	}
	else
	{
		gVK->vkCmdEndQuery( cmd, m_queryPool, m_queryIndex );
	}

	m_active = false;
}

//-----------------------------------------------------------------------------
// GetData: poll vkGetQueryPoolResults. Returns true when the result is
// available and copies it into the caller's buffer.
//-----------------------------------------------------------------------------
bool CVKQuery::GetData( void *data, size_t dataSize, bool flush )
{
	if ( m_queryPool == VK_NULL_HANDLE || !data || dataSize == 0 || !gVK->vkGetQueryPoolResults )
	{
		return false;
	}

	VkQueryResultFlags flags = VK_QUERY_RESULT_PARTIAL_BIT;
	if ( flush )
	{
		flags |= VK_QUERY_RESULT_WAIT_BIT;
	}
	else
	{
		flags |= VK_QUERY_RESULT_64_BIT;
	}

	VkResult result = gVK->vkGetQueryPoolResults(
		gVK->m_device, m_queryPool, m_queryIndex, 1,
		dataSize, data, dataSize, flags );

	if ( result == VK_SUCCESS )
	{
		return true;
	}
	if ( result == VK_NOT_READY )
	{
		return false;
	}

	Warning( "CVKQuery::GetData: vkGetQueryPoolResults failed (VkResult=%d)\n", (int)result );
	return false;
}
