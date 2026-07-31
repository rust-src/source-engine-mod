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
// rendermechanism.h - Vulkan backend switch point
// Replaces togles/rendermechanism.h for DX9-to-Vulkan translation
//
#ifndef RENDERMECHANISM_H
#define RENDERMECHANISM_H

#if defined(DX_TO_VK_ABSTRACTION)

#undef PROTECTED_THINGS_ENABLE

#include <vulkan/vulkan.h>

#include "tier0/basetypes.h"
#include "tier0/platform.h"
#include "tier0/mem.h"

// GLAliasTable is a GL-era type used by shaderapidx9 for the global function
// table (gGL) and ToGLConnectLibraries().  The Vulkan backend doesn't use a GL
// function table, so we provide a dummy typedef so the stubs compile.
typedef void *GLAliasTable;

// V_max/V_min are used in the ported dxabstract.cpp but not defined in the
// engine headers.  Provide simple macros.
#ifndef V_max
#define V_max(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef V_min
#define V_min(a, b) ((a) < (b) ? (a) : (b))
#endif

#include "toglesvk/linuxwin/vkbase.h"
#include "toglesvk/linuxwin/vkentrypoints.h"
#include "toglesvk/linuxwin/vkcontext.h"
#include "toglesvk/linuxwin/vkbuffer.h"
#include "toglesvk/linuxwin/vktex.h"
#include "toglesvk/linuxwin/vkfbo.h"
#include "toglesvk/linuxwin/vkprogram.h"
#include "toglesvk/linuxwin/vkquery.h"
#include "toglesvk/linuxwin/dxabstract_types.h"
#include "toglesvk/linuxwin/dxabstract.h"

#else
	//USE_ACTUAL_DX
	#ifdef WIN32
		#ifdef _X360
			#include "d3d9.h"
			#include "d3dx9.h"
		#else
			#include <windows.h>
			#include "../../dx9sdk/include/d3d9.h"
			#include "../../dx9sdk/include/d3dx9.h"
		#endif
		typedef HWND VD3DHWND;
	#endif

	#define	VKMPRINTF(args)
	#define	VKMPRINTSTR(args)
	#define	VKMPRINTTEXT(args)
#endif // defined(DX_TO_VK_ABSTRACTION)

#endif // RENDERMECHANISM_H
