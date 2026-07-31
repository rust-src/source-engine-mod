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
// vkquery.h - Vulkan query wrapper (replaces CGLMQuery)
//
#ifndef VKQUERY_H
#define VKQUERY_H

#ifdef DX_TO_VK_ABSTRACTION

#include <vulkan/vulkan.h>
#include "tier0/dbg.h"

class CVKQuery
{
public:
	CVKQuery();
	~CVKQuery();

	bool Create( CVKContext *ctx, VkQueryType type );
	void Destroy();

	void Begin( VkCommandBuffer cmd );
	void End( VkCommandBuffer cmd );

	// Returns true if data is available
	bool GetData( void *data, size_t dataSize, bool flush );

	VkQueryPool GetQueryPool() const { return m_queryPool; }
	uint32 GetQueryIndex() const { return m_queryIndex; }

private:
	CVKContext *m_ctx;
	VkQueryPool m_queryPool;
	uint32 m_queryIndex;
	VkQueryType m_type;
	bool m_active;
};

#endif // DX_TO_VK_ABSTRACTION

#endif // VKQUERY_H
