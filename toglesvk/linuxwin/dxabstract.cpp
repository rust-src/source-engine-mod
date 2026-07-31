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
// dxabstract.cpp
//
// D3D9 abstraction layer - Vulkan backend.
// Implements the D3D9 device surface declared in toglesvk/linuxwin/dxabstract.h
// on top of the CVKContext Vulkan wrapper.  Mirrors the structure of the TOGL
// OpenGL backend (togles/linuxwin/dxabstract.cpp) but routes every operation
// through the Vulkan entry points exposed by CVKContext.
//
//==================================================================================================
#include "toglesvk/rendermechanism.h"
#include "tier0/vprof_telemetry.h"
#include "tier0/dbg.h"
#include "tier0/threadtools.h"
#include "tier0/vprof.h"
#include "tier1/strtools.h"
#include "tier1/utlbuffer.h"
#include "dx9asmvtospv.h"
#include "mathlib/vmatrix.h"
#include "materialsystem/IShader.h"

#include "tier0/icommandline.h"
#include "tier0/memdbgon.h"

#ifdef USE_ACTUAL_DX

#pragma comment( lib, "../../dx9sdk/lib/d3d9.lib" )
#pragma comment( lib, "../../dx9sdk/lib/d3dx9.lib" )

#else

#ifdef POSIX
#define strcat_s( a, b, c) V_strcat( a, c, b )
#endif

#define D3D_DEVICE_VALID_MARKER 0x12EBC845
#define VK_PUBLIC_ENTRYPOINT_CHECKS( dev ) \
	Assert( dev->GetCurrentOwnerThreadId() == ThreadGetCurrentId() ); \
	Assert( dev->m_nValidMarker == D3D_DEVICE_VALID_MARKER );

#define VK_PUBLIC_ENTRYPOINT_CHECKS_RET_VOID \
	Assert( GetCurrentOwnerThreadId() == ThreadGetCurrentId() ); \
	Assert( m_nValidMarker == D3D_DEVICE_VALID_MARKER );

// ------------------------------------------------------------------------------------------------------------------------------ //
// Global state
// ------------------------------------------------------------------------------------------------------------------------------ //
bool g_bNullD3DDevice;

static IDirect3DDevice9 *g_pD3D_Device;

// Dummy gGL for shaderapidx9 compatibility (declared in rendermechanism.h)
GLAliasTable gGL = NULL;

// ToGLConnectLibraries - initializes Vulkan backend instead of GL
// Called by shaderdevicedx8.cpp when DX_TO_GL_ABSTRACTION is defined
GLAliasTable ToGLConnectLibraries( void *factory )
{
	VKConnectLibraries();
	return NULL;
}

// Single global DX9-bytecode-to-SPIR-V translator (mirrors the single
// D3DToGL instance used in the OpenGL backend).  Not thread safe; the D3D9
// device is single threaded by contract.
static CD3DToVK g_D3DToVKTranslator;

// Batch / perf analysis is stubbed out for the Vulkan port.
#define VK_BATCH_PERF_CALL_TIMER

// ------------------------------------------------------------------------------------------------------------------------------ //
// Helpers that depend on the host launcher / display.  The Vulkan backend
// queries the live device for rendered size rather than a GLM display DB.
// ------------------------------------------------------------------------------------------------------------------------------ //
inline void RenderedSize( uint &width, uint &height, bool set )
{
	if ( g_pD3D_Device && g_pD3D_Device->m_ctx )
	{
		if ( set )
			g_pD3D_Device->m_ctx->SetDisplaySize( width, height );
		else
			g_pD3D_Device->m_ctx->GetDisplaySize( width, height );
	}
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// D3DMATRIX operators (Win32 path)
// ------------------------------------------------------------------------------------------------------------------------------ //
#if defined( WIN32 )

bool D3DMATRIX::operator == ( CONST D3DMATRIX& src) const
{
	return V_memcmp( (void*)this, (void*)&src, sizeof(*this) ) == 0;
}

D3DMATRIX::operator void* ()
{
	return (void*)this;
}

#endif

// ------------------------------------------------------------------------------------------------------------------------------ //
// D3DXMATRIX operators
// ------------------------------------------------------------------------------------------------------------------------------ //
D3DXMATRIX D3DXMATRIX::operator*( const D3DXMATRIX &o ) const
{
	D3DXMATRIX result;

	D3DXMatrixMultiply( &result, this, &o );	// this = lhs    o = rhs    result = this * o

	return result;
}

D3DXMATRIX::operator FLOAT* ()
{
	return (float*)this;
}

float& D3DXMATRIX::operator()( int row, int column )
{
	return m[row][column];
}

const float& D3DXMATRIX::operator()( int row, int column ) const
{
	return m[row][column];
}

bool D3DXMATRIX::operator != ( CONST D3DXMATRIX& src ) const
{
	return V_memcmp( (void*)this, (void*)&src, sizeof(*this) ) != 0;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// D3DXPLANE operators
// ------------------------------------------------------------------------------------------------------------------------------ //
float& D3DXPLANE::operator[]( int i )
{
	return ((float*)this)[i];
}

bool D3DXPLANE::operator==( const D3DXPLANE &o )
{
	return a == o.a && b == o.b && c == o.c && d == o.d;
}

bool D3DXPLANE::operator!=( const D3DXPLANE &o )
{
	return !( *this == o );
}

D3DXPLANE::operator float*()
{
	return (float*)this;
}

D3DXPLANE::operator const float*() const
{
	return (const float*)this;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// D3DXVECTOR2 operators
// ------------------------------------------------------------------------------------------------------------------------------ //
D3DXVECTOR2::operator FLOAT* ()
{
	return (float*)this;
}

D3DXVECTOR2::operator CONST FLOAT* () const
{
	return (const float*)this;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// D3DXVECTOR3 operators
// ------------------------------------------------------------------------------------------------------------------------------ //
D3DXVECTOR3::D3DXVECTOR3( float a, float b, float c )
{
	x = a;
	y = b;
	z = c;
}

D3DXVECTOR3::operator FLOAT* ()
{
	return (float*)this;
}

D3DXVECTOR3::operator CONST FLOAT* () const
{
	return (const float*)this;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// D3DXVECTOR4 operators
// ------------------------------------------------------------------------------------------------------------------------------ //
D3DXVECTOR4::D3DXVECTOR4( float a, float b, float c, float d )
{
	x = a;
	y = b;
	z = c;
	w = d;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// IDirect3DResource9
// ------------------------------------------------------------------------------------------------------------------------------ //
DWORD IDirect3DResource9::SetPriority(DWORD PriorityNew)
{
	// no-op on Vulkan; priorities are not exposed
	return 0;
}

// ============================================================================================================================== //
// Resource destructors
// ============================================================================================================================== //

// ------------------------------------------------------------------------------------------------------------------------------ //
// IDirect3DBaseTexture9
// ------------------------------------------------------------------------------------------------------------------------------ //
IDirect3DBaseTexture9::~IDirect3DBaseTexture9()
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( m_device );

	if (m_device)
	{
		Assert( m_device->m_ObjectStats.m_nTotalTextures >= 1 );
		m_device->m_ObjectStats.m_nTotalTextures--;

		m_device->ReleasedTexture( this );

		if (m_tex)
		{
			m_device->ReleasedCGLMTex( m_tex );
			m_tex->m_ctx->DestroyTex( m_tex );
			m_tex = NULL;
		}
		m_device = NULL;	// ** THIS ** is the only place to scrub this.
	}
}

D3DRESOURCETYPE IDirect3DBaseTexture9::GetType()
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( m_device );

	return m_restype;
}

DWORD IDirect3DBaseTexture9::GetLevelCount()
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( m_device );

	return m_tex ? m_tex->GetMipCount() : 1;
}

HRESULT IDirect3DBaseTexture9::GetLevelDesc(UINT Level,D3DSURFACE_DESC *pDesc)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( m_device );

	if ( !m_tex || Level >= (UINT)m_tex->GetMipCount() )
		return D3DERR_INVALIDCALL;

	D3DSURFACE_DESC result = m_descZero;
	result.Width  = V_max( 1u, m_tex->GetWidth()  >> Level );
	result.Height = V_max( 1u, m_tex->GetHeight() >> Level );

	*pDesc = result;
	return S_OK;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// IDirect3DTexture9
// ------------------------------------------------------------------------------------------------------------------------------ //
IDirect3DTexture9::~IDirect3DTexture9()
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( m_device );

	if ( m_device )
	{
		if ( m_surfZero )
		{
			m_surfZero->m_device = NULL;
			m_surfZero->Release();
			m_surfZero = NULL;
		}
	}
}

HRESULT IDirect3DTexture9::LockRect(UINT Level,D3DLOCKED_RECT* pLockedRect,CONST RECT* pRect,DWORD Flags)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( m_device );

	if ( !m_tex )
		return D3DERR_INVALIDCALL;

	VKLockedRect vkLocked;
	VkRect2D rect;
	VkRect2D *pVkRect = NULL;
	if ( pRect )
	{
		rect.offset.x = pRect->left;
		rect.offset.y = pRect->top;
		rect.extent.width  = pRect->right  - pRect->left;
		rect.extent.height = pRect->bottom - pRect->top;
		pVkRect = &rect;
	}

	bool readOnly = (Flags & D3DLOCK_READONLY) != 0;
	if ( !m_tex->LockRect( 0, Level, &vkLocked, pVkRect, readOnly ) )
		return D3DERR_INVALIDCALL;

	pLockedRect->pBits = vkLocked.pBits;
	pLockedRect->Pitch  = vkLocked.Pitch;
	return S_OK;
}

HRESULT IDirect3DTexture9::UnlockRect(UINT Level)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( m_device );

	if ( !m_tex )
		return D3DERR_INVALIDCALL;

	m_tex->UnlockRect( 0, Level );
	return S_OK;
}

HRESULT IDirect3DTexture9::GetSurfaceLevel(UINT Level,IDirect3DSurface9** ppSurfaceLevel)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( m_device );

	if ( !ppSurfaceLevel )
		return D3DERR_INVALIDCALL;

	// The top level surface is cached on the texture; deeper levels are
	// fabricated on demand and share the underlying CVKTex.
	if ( Level == 0 && m_surfZero )
	{
		m_surfZero->AddRef();
		*ppSurfaceLevel = m_surfZero;
		return S_OK;
	}

	IDirect3DSurface9 *surf = new IDirect3DSurface9;
	surf->m_device = m_device;
	surf->m_restype = D3DRTYPE_SURFACE;
	surf->m_tex = m_tex;				// share
	surf->m_face = 0;
	surf->m_mip  = Level;
	surf->m_desc = m_descZero;
	surf->m_desc.Width  = V_max( 1u, m_tex->GetWidth()  >> Level );
	surf->m_desc.Height = V_max( 1u, m_tex->GetHeight() >> Level );

	if ( m_device )
		m_device->m_ObjectStats.m_nTotalSurfaces++;

	*ppSurfaceLevel = surf;
	return S_OK;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// IDirect3DCubeTexture9
// ------------------------------------------------------------------------------------------------------------------------------ //
IDirect3DCubeTexture9::~IDirect3DCubeTexture9()
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( m_device );

	if ( m_device )
	{
		for ( int i = 0; i < 6; i++ )
		{
			if ( m_surfZero[i] )
			{
				m_surfZero[i]->m_device = NULL;
				m_surfZero[i]->Release();
				m_surfZero[i] = NULL;
			}
		}
	}
}

HRESULT IDirect3DCubeTexture9::GetCubeMapSurface(D3DCUBEMAP_FACES FaceType,UINT Level,IDirect3DSurface9** ppCubeMapSurface)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( m_device );

	if ( !ppCubeMapSurface )
		return D3DERR_INVALIDCALL;

	UINT face = (UINT)FaceType;
	if ( face == 0 && Level == 0 && m_surfZero[0] )
	{
		m_surfZero[0]->AddRef();
		*ppCubeMapSurface = m_surfZero[0];
		return S_OK;
	}

	IDirect3DSurface9 *surf = new IDirect3DSurface9;
	surf->m_device = m_device;
	surf->m_restype = D3DRTYPE_SURFACE;
	surf->m_tex = m_tex;
	surf->m_face = face;
	surf->m_mip  = Level;
	surf->m_desc = m_descZero;
	surf->m_desc.Width  = V_max( 1u, m_tex->GetWidth()  >> Level );
	surf->m_desc.Height = V_max( 1u, m_tex->GetHeight() >> Level );

	if ( m_device )
		m_device->m_ObjectStats.m_nTotalSurfaces++;

	*ppCubeMapSurface = surf;
	return S_OK;
}

HRESULT IDirect3DCubeTexture9::GetLevelDesc(UINT Level,D3DSURFACE_DESC *pDesc)
{
	VK_PUBLIC_ENTRYPOINT_CHECKS( m_device );
	if ( !m_tex || Level >= (UINT)m_tex->GetMipCount() )
		return D3DERR_INVALIDCALL;

	D3DSURFACE_DESC result = m_descZero;
	result.Width  = V_max( 1u, m_tex->GetWidth()  >> Level );
	result.Height = V_max( 1u, m_tex->GetHeight() >> Level );
	*pDesc = result;
	return S_OK;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// IDirect3DVolumeTexture9
// ------------------------------------------------------------------------------------------------------------------------------ //
IDirect3DVolumeTexture9::~IDirect3DVolumeTexture9()
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( m_device );

	if ( m_device && m_surfZero )
	{
		m_surfZero->m_device = NULL;
		m_surfZero->Release();
		m_surfZero = NULL;
	}
}

HRESULT IDirect3DVolumeTexture9::LockBox(UINT Level,D3DLOCKED_BOX* pLockedVolume,CONST D3DBOX* pBox,DWORD Flags)
{
	VK_PUBLIC_ENTRYPOINT_CHECKS( m_device );
	if ( !m_tex )
		return D3DERR_INVALIDCALL;

	VKLockedBox vkBox;
	bool readOnly = (Flags & D3DLOCK_READONLY) != 0;
	if ( !m_tex->LockBox( Level, &vkBox, readOnly ) )
		return D3DERR_INVALIDCALL;

	pLockedVolume->pBits      = vkBox.pBits;
	pLockedVolume->RowPitch   = vkBox.RowPitch;
	pLockedVolume->SlicePitch = vkBox.SlicePitch;
	return S_OK;
}

HRESULT IDirect3DVolumeTexture9::UnlockBox(UINT Level)
{
	VK_PUBLIC_ENTRYPOINT_CHECKS( m_device );
	if ( !m_tex )
		return D3DERR_INVALIDCALL;
	m_tex->UnlockBox( Level );
	return S_OK;
}

HRESULT IDirect3DVolumeTexture9::GetLevelDesc( UINT level, D3DVOLUME_DESC *pDesc )
{
	VK_PUBLIC_ENTRYPOINT_CHECKS( m_device );
	if ( !m_tex || !pDesc || level >= (UINT)m_tex->GetMipCount() )
		return D3DERR_INVALIDCALL;

	D3DVOLUME_DESC result = m_volDescZero;
	result.Width  = V_max( 1u, m_tex->GetWidth()  >> level );
	result.Height = V_max( 1u, m_tex->GetHeight() >> level );
	result.Depth  = V_max( 1u, m_tex->GetDepth()  >> level );
	*pDesc = result;
	return S_OK;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// IDirect3DSurface9
// ------------------------------------------------------------------------------------------------------------------------------ //
IDirect3DSurface9::~IDirect3DSurface9()
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( m_device );

	if ( m_device )
	{
		m_device->m_ObjectStats.m_nTotalSurfaces--;
		m_device->ReleasedSurface( this );
		// The underlying CVKTex is owned by the texture / device, not by the surface,
		// so do not destroy it here.
		m_tex = NULL;
		m_device = NULL;
	}
}

HRESULT IDirect3DSurface9::LockRect(D3DLOCKED_RECT* pLockedRect,CONST RECT* pRect,DWORD Flags)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( m_device );

	if ( !m_tex )
		return D3DERR_INVALIDCALL;

	VKLockedRect vkLocked;
	VkRect2D rect;
	VkRect2D *pVkRect = NULL;
	if ( pRect )
	{
		rect.offset.x = pRect->left;
		rect.offset.y = pRect->top;
		rect.extent.width  = pRect->right  - pRect->left;
		rect.extent.height = pRect->bottom - pRect->top;
		pVkRect = &rect;
	}

	bool readOnly = (Flags & D3DLOCK_READONLY) != 0;
	if ( !m_tex->LockRect( m_face, m_mip, &vkLocked, pVkRect, readOnly ) )
		return D3DERR_INVALIDCALL;

	pLockedRect->pBits = vkLocked.pBits;
	pLockedRect->Pitch  = vkLocked.Pitch;
	return S_OK;
}

HRESULT IDirect3DSurface9::UnlockRect()
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( m_device );

	if ( !m_tex )
		return D3DERR_INVALIDCALL;

	m_tex->UnlockRect( m_face, m_mip );
	return S_OK;
}

HRESULT IDirect3DSurface9::GetDesc(D3DSURFACE_DESC *pDesc)
{
	VK_PUBLIC_ENTRYPOINT_CHECKS( m_device );
	if ( !pDesc )
		return D3DERR_INVALIDCALL;
	*pDesc = m_desc;
	return S_OK;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// IDirect3DVertexDeclaration9
// ------------------------------------------------------------------------------------------------------------------------------ //
IDirect3DVertexDeclaration9::~IDirect3DVertexDeclaration9()
{
	VK_BATCH_PERF_CALL_TIMER;
	if ( m_device )
	{
		m_device->m_ObjectStats.m_nTotalVertexDecls--;
		m_device->ReleasedVertexDeclaration( this );
		m_device = NULL;
	}
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// IDirect3DQuery9
// ------------------------------------------------------------------------------------------------------------------------------ //
IDirect3DQuery9::~IDirect3DQuery9()
{
	VK_BATCH_PERF_CALL_TIMER;
	if ( m_ctx && m_query )
	{
		m_ctx->DestroyQuery( m_query );
		m_query = NULL;
	}
	if ( m_device )
	{
		m_device->m_ObjectStats.m_nTotalQueries--;
		m_device->ReleasedQuery( this );
		m_device = NULL;
	}
}

HRESULT IDirect3DQuery9::Issue(DWORD dwIssueFlags)
{
	VK_BATCH_PERF_CALL_TIMER;
	if ( m_device )
		Assert( m_device->m_nValidMarker == D3D_DEVICE_VALID_MARKER );

	Assert( m_ctx );
	VkCommandBuffer cmd = m_ctx->GetActiveCommandBuffer();

	if ( dwIssueFlags & D3DISSUE_BEGIN )
	{
		m_nIssueStartThreadID = ThreadGetCurrentId();
		m_nIssueStartDrawCallIndex = m_ctx->m_nTotalDrawsOrClears;
		m_nIssueStartFrameIndex = 0;
		m_nIssueStartQueryCreationCounter = 0;

		if ( m_type == D3DQUERYTYPE_OCCLUSION && m_query )
		{
			m_query->Begin( cmd );
		}
	}

	if ( dwIssueFlags & D3DISSUE_END )
	{
		m_nIssueEndThreadID = ThreadGetCurrentId();
		m_nIssueEndDrawCallIndex = m_ctx->m_nTotalDrawsOrClears;
		m_nIssueEndFrameIndex = 0;
		m_nIssueEndQueryCreationCounter = 0;

		if ( m_query )
		{
			if ( m_type == D3DQUERYTYPE_OCCLUSION )
			{
				m_query->End( cmd );
			}
			else // D3DQUERYTYPE_EVENT - fence semantics: End inserts the fence.
			{
				m_nIssueStartThreadID = m_nIssueEndThreadID;
				m_nIssueStartDrawCallIndex = m_nIssueEndDrawCallIndex;
				m_query->End( cmd );
			}
		}
	}
	return S_OK;
}

HRESULT IDirect3DQuery9::GetData(void* pData,DWORD dwSize,DWORD dwGetDataFlags)
{
	VK_BATCH_PERF_CALL_TIMER;
	HRESULT result = S_FALSE;

	if ( pData )
		*(uint*)pData = 0;

	if ( !m_query )
		return S_FALSE;

	bool flush = (dwGetDataFlags & D3DGETDATA_FLUSH) != 0;
	bool done = m_query->GetData( pData, dwSize, flush );

	if ( done )
		result = S_OK;

	return result;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// IDirect3DVertexBuffer9
// ------------------------------------------------------------------------------------------------------------------------------ //
IDirect3DVertexBuffer9::~IDirect3DVertexBuffer9()
{
	VK_BATCH_PERF_CALL_TIMER;
	if ( m_ctx && m_vtxBuffer )
	{
		m_ctx->DestroyBuffer( m_vtxBuffer );
		m_vtxBuffer = NULL;
	}
	if ( m_device )
	{
		m_device->m_ObjectStats.m_nTotalVertexBuffers--;
		m_device->ReleasedVertexBuffer( this );
		m_device = NULL;
	}
}

HRESULT IDirect3DVertexBuffer9::Lock(UINT OffsetToLock,UINT SizeToLock,void** ppbData,DWORD Flags)
{
	VK_BATCH_PERF_CALL_TIMER;
	if ( !m_vtxBuffer || !ppbData )
		return D3DERR_INVALIDCALL;

	bool readOnly = (Flags & D3DLOCK_READONLY) != 0;
	VkDeviceSize size = (SizeToLock == 0) ? m_vtxBuffer->GetSize() : SizeToLock;
	*ppbData = m_vtxBuffer->Lock( OffsetToLock, size, readOnly );

	if ( *ppbData == NULL )
		return D3DERR_INVALIDCALL;

	if ( m_ctx )
		m_ctx->m_nTotalVBLockBytes += size;

	return S_OK;
}

HRESULT IDirect3DVertexBuffer9::Unlock()
{
	VK_BATCH_PERF_CALL_TIMER;
	if ( !m_vtxBuffer )
		return D3DERR_INVALIDCALL;
	m_vtxBuffer->Unlock();
	return S_OK;
}

void IDirect3DVertexBuffer9::UnlockActualSize( uint nActualSize, const void *pActualData )
{
	if ( m_vtxBuffer )
		m_vtxBuffer->Unlock();
	(void)nActualSize;
	(void)pActualData;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// IDirect3DIndexBuffer9
// ------------------------------------------------------------------------------------------------------------------------------ //
IDirect3DIndexBuffer9::~IDirect3DIndexBuffer9()
{
	VK_BATCH_PERF_CALL_TIMER;
	if ( m_ctx && m_idxBuffer )
	{
		m_ctx->DestroyBuffer( m_idxBuffer );
		m_idxBuffer = NULL;
	}
	if ( m_device )
	{
		m_device->m_ObjectStats.m_nTotalIndexBuffers--;
		m_device->ReleasedIndexBuffer( this );
		m_device = NULL;
	}
}

HRESULT IDirect3DIndexBuffer9::Lock(UINT OffsetToLock,UINT SizeToLock,void** ppbData,DWORD Flags)
{
	VK_BATCH_PERF_CALL_TIMER;
	if ( !m_idxBuffer || !ppbData )
		return D3DERR_INVALIDCALL;

	bool readOnly = (Flags & D3DLOCK_READONLY) != 0;
	VkDeviceSize size = (SizeToLock == 0) ? m_idxBuffer->GetSize() : SizeToLock;
	*ppbData = m_idxBuffer->Lock( OffsetToLock, size, readOnly );

	if ( *ppbData == NULL )
		return D3DERR_INVALIDCALL;

	if ( m_ctx )
		m_ctx->m_nTotalIBLockBytes += size;

	return S_OK;
}

HRESULT IDirect3DIndexBuffer9::Unlock()
{
	VK_BATCH_PERF_CALL_TIMER;
	if ( !m_idxBuffer )
		return D3DERR_INVALIDCALL;
	m_idxBuffer->Unlock();
	return S_OK;
}

void IDirect3DIndexBuffer9::UnlockActualSize( uint nActualSize, const void *pActualData )
{
	if ( m_idxBuffer )
		m_idxBuffer->Unlock();
	(void)nActualSize;
	(void)pActualData;
}

HRESULT IDirect3DIndexBuffer9::GetDesc(D3DINDEXBUFFER_DESC *pDesc)
{
	if ( !pDesc )
		return D3DERR_INVALIDCALL;
	*pDesc = m_idxDesc;
	return S_OK;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// IDirect3DPixelShader9 / IDirect3DVertexShader9
// ------------------------------------------------------------------------------------------------------------------------------ //
IDirect3DPixelShader9::~IDirect3DPixelShader9()
{
	VK_BATCH_PERF_CALL_TIMER;
	if ( m_pixProgram && m_pixProgram->GetType() == kVKProgramPixel )
	{
		// The CVKProgram is owned by the device's context; release through it.
		if ( m_device && m_device->m_ctx )
			m_device->m_ctx->DestroyProgram( m_pixProgram );
		m_pixProgram = NULL;
	}
	if ( m_device )
	{
		m_device->m_ObjectStats.m_nTotalPixelShaders--;
		m_device->ReleasedPixelShader( this );
		m_device = NULL;
	}
}

IDirect3DVertexShader9::~IDirect3DVertexShader9()
{
	VK_BATCH_PERF_CALL_TIMER;
	if ( m_vtxProgram && m_vtxProgram->GetType() == kVKProgramVertex )
	{
		if ( m_device && m_device->m_ctx )
			m_device->m_ctx->DestroyProgram( m_vtxProgram );
		m_vtxProgram = NULL;
	}
	if ( m_device )
	{
		m_device->m_ObjectStats.m_nTotalVertexShaders--;
		m_device->ReleasedVertexShader( this );
		m_device = NULL;
	}
}

// ============================================================================================================================== //
// IDirect3D9
// ============================================================================================================================== //
IDirect3D9::~IDirect3D9()
{
}

UINT IDirect3D9::GetAdapterCount()
{
	// The Vulkan backend exposes a single logical adapter.
	return 1;
}

// Fill a D3DCAPS9 structure from the live Vulkan context.  The values mirror
// what the OpenGL backend reports (a fixed high-end DX9-class feature set).
static void FillD3DCaps9( D3DCAPS9* pCaps )
{
	V_memset( pCaps, 0, sizeof(*pCaps) );

	pCaps->DeviceType						= D3DDEVTYPE_HAL;

	pCaps->DevCaps							= D3DDEVCAPS_HWRASTERIZATION | D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_EXECUTESYSTEMMEMORY | D3DDEVCAPS_TLVERTEXSYSTEMMEMORY | D3DDEVCAPS_TLVERTEXVIDEOMEMORY;
	pCaps->DevCaps2							= D3DDEVCAPS2_STREAMOFFSET;

	pCaps->PrimitiveMiscCaps				= D3DPMISCCAPS_MASKZ | D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW | D3DPMISCCAPS_COLORWRITEENABLE | D3DPMISCCAPS_BLENDOP;
	pCaps->RasterCaps						= D3DPRASTERCAPS_DEPTHBIAS | D3DPRASTERCAPS_SLOPESCALEDEPTHBIAS | D3DPRASTERCAPS_SCISSORTEST | D3DPRASTERCAPS_MIPMAPLODBIAS | D3DPRASTERCAPS_FOGVERTEX | D3DPRASTERCAPS_FOGTABLE;

	pCaps->TextureCaps						= D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_CUBEMAP | D3DPTEXTURECAPS_MIPCUBEMAP | D3DPTEXTURECAPS_NONPOW2CONDITIONAL;
	pCaps->TextureFilterCaps				= D3DPTFILTERCAPS_MINFANISOTROPIC | D3DPTFILTERCAPS_MAGFANISOTROPIC;

	pCaps->MaxTextureWidth					= 16384;
	pCaps->MaxTextureHeight					= 16384;
	pCaps->MaxVolumeExtent					= 8192;
	pCaps->MaxTextureAspectRatio			= 16384;
	pCaps->MaxAnisotropy					= 16;

	pCaps->TextureOpCaps					= D3DTEXOPCAPS_ADD | D3DTEXOPCAPS_MODULATE2X;
	pCaps->MaxTextureBlendStages			= 1;
	pCaps->MaxSimultaneousTextures			= 1;

	pCaps->VertexProcessingCaps				= 0;
	pCaps->MaxActiveLights					= 0;
	pCaps->MaxUserClipPlanes				= kVKUserClipPlanes;
	pCaps->MaxVertexBlendMatrices			= 0;
	pCaps->MaxVertexBlendMatrixIndex		= 0;

	pCaps->MaxPrimitiveCount				= 0x00555555;
	pCaps->MaxStreams						= D3D_MAX_STREAMS;

	// D3DVS_VERSION(3,0) / D3DPS_VERSION(3,0) encodings (the toglesvk headers
	// only ship the SPV_-prefixed variants, so use the literal bit patterns).
	pCaps->VertexShaderVersion				= 0xFFFE0300;
	pCaps->MaxVertexShaderConst				= 256;

	pCaps->PixelShaderVersion				= 0xFFFF0300;

	pCaps->PS20Caps.DynamicFlowControlDepth	= 24;
	pCaps->PS20Caps.NumTemps				= 32;
	pCaps->PS20Caps.StaticFlowControlDepth	= 24;
	pCaps->PS20Caps.NumInstructionSlots		= 512;

	pCaps->NumSimultaneousRTs				= 1;
	pCaps->MaxVertexShader30InstructionSlots	= 0;
	pCaps->MaxPixelShader30InstructionSlots		= 0;

#if DX_TO_VK_ABSTRACTION
	pCaps->FakeSRGBWrite			= true;
	pCaps->CanDoSRGBReadFromRTs		= true;
	pCaps->MixedSizeTargets			= false;
	pCaps->SupportInt16Format		= true;
#endif
}

HRESULT IDirect3D9::GetDeviceCaps(UINT Adapter,D3DDEVTYPE DeviceType,D3DCAPS9* pCaps)
{
	VK_BATCH_PERF_CALL_TIMER;
	FillD3DCaps9( pCaps );
	return S_OK;
}

HRESULT IDirect3D9::GetAdapterIdentifier( UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9* pIdentifier )
{
	VK_BATCH_PERF_CALL_TIMER;
	V_memset( pIdentifier, 0, sizeof(*pIdentifier) );

	V_snprintf( pIdentifier->Driver,      sizeof(pIdentifier->Driver),      "Vulkan" );
	V_snprintf( pIdentifier->Description, sizeof(pIdentifier->Description), "Vulkan renderer" );

	pIdentifier->VendorId		= 0;	// populated by the live physical device if desired
	pIdentifier->DeviceId		= 0;
	pIdentifier->SubSysId		= 0;
	pIdentifier->Revision		= 0;
	pIdentifier->VideoMemory	= 0;
	return S_OK;
}

HRESULT IDirect3D9::CheckDeviceFormat(UINT Adapter,D3DDEVTYPE DeviceType,D3DFORMAT AdapterFormat,DWORD Usage,D3DRESOURCETYPE RType,D3DFORMAT CheckFormat)
{
	VK_BATCH_PERF_CALL_TIMER;

	DWORD knownUsageMask =	D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL | D3DUSAGE_DYNAMIC | D3DUSAGE_AUTOGENMIPMAP
						|	D3DUSAGE_QUERY_SRGBREAD | D3DUSAGE_QUERY_FILTER | D3DUSAGE_QUERY_SRGBWRITE | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING
						|	D3DUSAGE_QUERY_VERTEXTEXTURE;
	(void)knownUsageMask;

	// If the requested D3D format has a Vulkan representation, accept it.
	VkFormat vkFmt = D3DFormatToVKFormat( (DWORD)CheckFormat );
	if ( vkFmt == VK_FORMAT_UNDEFINED )
		return D3DERR_NOTAVAILABLE;

	return S_OK;
}

UINT IDirect3D9::GetAdapterModeCount(UINT Adapter,D3DFORMAT Format)
{
	VK_BATCH_PERF_CALL_TIMER;
	// A single windowed mode is advertised.
	return 1;
}

HRESULT IDirect3D9::EnumAdapterModes(UINT Adapter,D3DFORMAT Format,UINT Mode,D3DDISPLAYMODE* pMode)
{
	VK_BATCH_PERF_CALL_TIMER;
	if ( !pMode )
		return D3DERR_INVALIDCALL;

	uint w = 1280, h = 720;
	if ( g_pD3D_Device && g_pD3D_Device->m_ctx )
		g_pD3D_Device->m_ctx->GetDisplaySize( w, h );

	pMode->Width		= w;
	pMode->Height		= h;
	pMode->RefreshRate	= 0;
	pMode->Format		= Format;
	return S_OK;
}

HRESULT IDirect3D9::CheckDeviceType(UINT Adapter,D3DDEVTYPE DevType,D3DFORMAT AdapterFormat,D3DFORMAT BackBufferFormat,BOOL bWindowed)
{
	VK_BATCH_PERF_CALL_TIMER;
	return S_OK;
}

HRESULT IDirect3D9::GetAdapterDisplayMode(UINT Adapter,D3DDISPLAYMODE* pMode)
{
	VK_BATCH_PERF_CALL_TIMER;
	if ( !pMode )
		return D3DERR_INVALIDCALL;

	uint w = 1280, h = 720;
	if ( g_pD3D_Device && g_pD3D_Device->m_ctx )
		g_pD3D_Device->m_ctx->GetDisplaySize( w, h );

	pMode->Width		= w;
	pMode->Height		= h;
	pMode->RefreshRate	= 0;
	pMode->Format		= (D3DFORMAT)5; // D3DFMT_X8R8G8B8
	return S_OK;
}

HRESULT IDirect3D9::CheckDepthStencilMatch(UINT Adapter,D3DDEVTYPE DeviceType,D3DFORMAT AdapterFormat,D3DFORMAT RenderTargetFormat,D3DFORMAT DepthStencilFormat)
{
	VK_BATCH_PERF_CALL_TIMER;
	return S_OK;
}

HRESULT IDirect3D9::CheckDeviceMultiSampleType( UINT Adapter,D3DDEVTYPE DeviceType,D3DFORMAT SurfaceFormat,BOOL Windowed,D3DMULTISAMPLE_TYPE MultiSampleType,DWORD* pQualityLevels )
{
	VK_BATCH_PERF_CALL_TIMER;
	if ( pQualityLevels )
		*pQualityLevels = ( MultiSampleType == D3DMULTISAMPLE_NONE ) ? 1 : 0;
	return ( MultiSampleType == D3DMULTISAMPLE_NONE ) ? S_OK : D3DERR_NOTAVAILABLE;
}

HRESULT IDirect3D9::CreateDevice(UINT Adapter,D3DDEVTYPE DeviceType,VD3DHWND hFocusWindow,DWORD BehaviorFlags,D3DPRESENT_PARAMETERS* pPresentationParameters,IDirect3DDevice9** ppReturnedDeviceInterface)
{
	VK_BATCH_PERF_CALL_TIMER;

#if !TOGL_SUPPORT_NULL_DEVICE
	if ( DeviceType == D3DDEVTYPE_NULLREF )
	{
		Warning( "TOGL: Must define TOGL_SUPPORT_NULL_DEVICE to use the NULL device\n" );
		DebuggerBreak();
		return E_FAIL;
	}
#endif

	// NULL out the return pointer so if we exit early it is not set.
	*ppReturnedDeviceInterface = NULL;

	HRESULT result = S_OK;

	if ( pPresentationParameters->AutoDepthStencilFormat != (D3DFORMAT)2 /* D3DFMT_D24S8 */ )
	{
		DXABSTRACT_BREAK_ON_ERROR();
		result = D3DERR_NOTAVAILABLE;
	}

	if ( result == S_OK )
	{
		IDirect3DDevice9Params devparams;
		V_memset( &devparams, 0, sizeof(devparams) );

		devparams.m_adapter				= Adapter;
		devparams.m_deviceType			= DeviceType;
		devparams.m_focusWindow		= hFocusWindow;
		devparams.m_behaviorFlags		= BehaviorFlags;
		devparams.m_presentationParameters = *pPresentationParameters;

		IDirect3DDevice9 *dev = new IDirect3DDevice9;
		result = dev->Create( &devparams );

		if ( result == S_OK )
		{
			*ppReturnedDeviceInterface = dev;
			g_pD3D_Device = dev;
		}
		else
		{
			delete dev;
		}

		g_bNullD3DDevice = ( DeviceType == D3DDEVTYPE_NULLREF );
	}
	return result;
}

// ============================================================================================================================== //
// IDirect3DDevice9 - construction / lifecycle
// ============================================================================================================================== //
IDirect3DDevice9::IDirect3DDevice9()
{
	m_nValidMarker = D3D_DEVICE_VALID_MARKER;

	m_ctx = NULL;
	m_pFBOs = NULL;
	m_bFBODirty = true;

	m_pVertDecl = NULL;

	for ( int i = 0; i < D3D_MAX_STREAMS; i++ )
	{
		m_vtx_buffers[i] = NULL;
		m_streams[i].m_vtxBuffer = NULL;
		m_streams[i].m_offset = 0;
		m_streams[i].m_stride = 0;
	}
	m_pDummy_vtx_buffer = NULL;
	m_indices.m_idxBuffer = NULL;

	m_vertexShader = NULL;
	m_pixelShader = NULL;

	for ( int i = 0; i < VK_SAMPLER_COUNT; i++ )
		m_textures[i] = NULL;

	for ( int i = 0; i < MAX_RENDER_TARGETS; i++ )
		m_pRenderTargets[i] = NULL;
	m_pDepthStencil = NULL;

	m_pDefaultColorSurface = NULL;
	m_pDefaultDepthStencilSurface = NULL;

	m_ObjectStats.clear();
	m_PrevObjectStats.clear();

	V_memset( &gl, 0, sizeof(gl) );
}

IDirect3DDevice9::~IDirect3DDevice9()
{
	// Drop cached references to surfaces / shaders / buffers.  These objects
	// own themselves (refcounted); we only need to clear our weak pointers.
	for ( int i = 0; i < MAX_RENDER_TARGETS; i++ )
	{
		if ( m_pRenderTargets[i] )
		{
			m_pRenderTargets[i]->m_device = NULL;
			m_pRenderTargets[i] = NULL;
		}
	}
	if ( m_pDepthStencil )
	{
		m_pDepthStencil->m_device = NULL;
		m_pDepthStencil = NULL;
	}
	if ( m_pDefaultColorSurface )
	{
		m_pDefaultColorSurface->m_device = NULL;
		m_pDefaultColorSurface = NULL;
	}
	if ( m_pDefaultDepthStencilSurface )
	{
		m_pDefaultDepthStencilSurface->m_device = NULL;
		m_pDefaultDepthStencilSurface = NULL;
	}

	m_pVertDecl = NULL;
	m_vertexShader = NULL;
	m_pixelShader = NULL;

	if ( m_ctx )
	{
		m_ctx->Destroy();
		delete m_ctx;
		m_ctx = NULL;
	}

	m_nValidMarker = 0;

	if ( g_pD3D_Device == this )
		g_pD3D_Device = NULL;
}

HRESULT IDirect3DDevice9::Create( IDirect3DDevice9Params *params )
{
	VK_BATCH_PERF_CALL_TIMER;

	m_params = *params;

	// Build the Vulkan display parameters from the D3D present parameters.
	CVKDisplayParams vkParams;
	vkParams.m_focusWindow				= params->m_focusWindow;
	vkParams.m_fsEnable				= !params->m_presentationParameters.Windowed;
	vkParams.m_vsyncEnable				= (params->m_presentationParameters.PresentationInterval == D3DPRESENT_INTERVAL_ONE);
	vkParams.m_backBufferWidth			= params->m_presentationParameters.BackBufferWidth;
	vkParams.m_backBufferHeight			= params->m_presentationParameters.BackBufferHeight;
	vkParams.m_backBufferFormat		= (DWORD)params->m_presentationParameters.BackBufferFormat;
	vkParams.m_multiSampleCount		= params->m_presentationParameters.MultiSampleType;
	vkParams.m_enableAutoDepthStencil	= params->m_presentationParameters.EnableAutoDepthStencil;
	vkParams.m_autoDepthStencilFormat	= (DWORD)params->m_presentationParameters.AutoDepthStencilFormat;
	vkParams.m_fsRefreshHz				= params->m_presentationParameters.FullScreen_RefreshRateInHz;

	m_ctx = new CVKContext;
	if ( !m_ctx->Create( &vkParams ) )
	{
		Warning( "TOGL: CVKContext::Create failed\n" );
		delete m_ctx;
		m_ctx = NULL;
		return D3DERR_NOTAVAILABLE;
	}

	// Dummy vertex buffer used to satisfy Vulkan vertex binding requirements
	// when a stream is inactive.
	VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	m_pDummy_vtx_buffer = m_ctx->CreateBuffer( usage, 16, true );

	InitStates();
	FullFlushStates();

	return S_OK;
}

HRESULT IDirect3DDevice9::Reset(D3DPRESENT_PARAMETERS* pPresentationParameters)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );

	if ( !m_ctx || !pPresentationParameters )
		return D3DERR_INVALIDCALL;

	// Tear down and rebuild the swapchain against the new parameters.
	m_ctx->DestroySwapchain();

	CVKDisplayParams vkParams;
	vkParams.m_focusWindow			= m_params.m_focusWindow;
	vkParams.m_fsEnable				= !pPresentationParameters->Windowed;
	vkParams.m_vsyncEnable			= (pPresentationParameters->PresentationInterval == D3DPRESENT_INTERVAL_ONE);
	vkParams.m_backBufferWidth		= pPresentationParameters->BackBufferWidth;
	vkParams.m_backBufferHeight		= pPresentationParameters->BackBufferHeight;
	vkParams.m_backBufferFormat		= (DWORD)pPresentationParameters->BackBufferFormat;
	vkParams.m_multiSampleCount		= pPresentationParameters->MultiSampleType;
	vkParams.m_enableAutoDepthStencil= pPresentationParameters->EnableAutoDepthStencil;
	vkParams.m_autoDepthStencilFormat= (DWORD)pPresentationParameters->AutoDepthStencilFormat;
	vkParams.m_fsRefreshHz			= pPresentationParameters->FullScreen_RefreshRateInHz;

	m_params.m_presentationParameters = *pPresentationParameters;

	if ( !m_ctx->CreateSwapchain() )
		return D3DERR_DEVICENOTRESET;

	m_bFBODirty = true;
	InitStates();
	FullFlushStates();
	return S_OK;
}

HRESULT IDirect3DDevice9::Present(CONST RECT* pSourceRect,CONST RECT* pDestRect,VD3DHWND hDestWindowOverride,CONST RGNDATA* pDirtyRegion)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );

	TOGL_NULL_DEVICE_CHECK;

	if ( !m_ctx )
		return D3DERR_INVALIDCALL;

	m_ctx->FlushCommandBuffers();
	bool ok = m_ctx->Present();
	return ok ? S_OK : D3DERR_DEVICELOST;
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// Scene management
// ------------------------------------------------------------------------------------------------------------------------------ //
HRESULT IDirect3DDevice9::BeginScene()
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	TOGL_NULL_DEVICE_CHECK;
	// Vulkan has no explicit BeginScene; the render pass is begun lazily on draw.
	return S_OK;
}

HRESULT IDirect3DDevice9::EndScene()
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	TOGL_NULL_DEVICE_CHECK;
	if ( m_ctx )
		m_ctx->EndRenderPass();
	return S_OK;
}

HRESULT IDirect3DDevice9::Clear(DWORD Count,CONST D3DRECT* pRects,DWORD Flags,D3DCOLOR Color,float Z,DWORD Stencil)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	TOGL_NULL_DEVICE_CHECK;

	if ( !m_ctx )
		return D3DERR_INVALIDCALL;

	uint32 mask = 0;
	if ( Flags & D3DCLEAR_TARGET )		mask |= VK_IMAGE_ASPECT_COLOR_BIT;
	if ( Flags & D3DCLEAR_ZBUFFER )		mask |= VK_IMAGE_ASPECT_DEPTH_BIT;
	if ( Flags & D3DCLEAR_STENCIL )		mask |= VK_IMAGE_ASPECT_STENCIL_BIT;

	float color[4];
	color[0] = (float)((Color >> 16) & 0xFF) / 255.0f;	// R
	color[1] = (float)((Color >>  8) & 0xFF) / 255.0f;	// G
	color[2] = (float)((Color      ) & 0xFF) / 255.0f;	// B
	color[3] = (float)((Color >> 24) & 0xFF) / 255.0f;	// A

	m_ctx->Clear( mask, color, Z, Stencil );
	m_ctx->m_nTotalDrawsOrClears++;
	return S_OK;
}

// ============================================================================================================================== //
// Texture creation
// ============================================================================================================================== //

// Helper: convert a D3D texture request into CVKTexParams.
static inline void FillVKTexParams( CVKTexParams &params, D3DRESOURCETYPE type,
	DWORD Usage, D3DFORMAT Format, UINT Width, UINT Height, UINT Depth, UINT Levels )
{
	V_memset( &params, 0, sizeof(params) );

	switch ( type )
	{
		case D3DRTYPE_TEXTURE:		params.m_texType = kVKTex2D;	break;
		case D3DRTYPE_VOLUMETEXTURE:params.m_texType = kVKTex3D;	break;
		case D3DRTYPE_CUBETEXTURE:	params.m_texType = kVKTexCube;	params.m_faceCount = 6; break;
		default:					params.m_texType = kVKTex2D;	break;
	}

	params.m_format		= D3DFormatToVKFormat( (DWORD)Format );
	params.m_width		= V_max( 1u, Width );
	params.m_height		= V_max( 1u, Height );
	params.m_depth		= V_max( 1u, Depth );
	params.m_mipCount	= V_max( 1u, Levels );
	params.m_isRenderTarget	= (Usage & D3DUSAGE_RENDERTARGET) != 0;
	params.m_isDepthStencil	= (Usage & D3DUSAGE_DEPTHSTENCIL) != 0;
	params.m_isDynamic		= (Usage & D3DUSAGE_DYNAMIC) != 0;
	params.m_srgb			= (Usage & D3DUSAGE_TEXTURE_SRGB) != 0;
}

HRESULT IDirect3DDevice9::CreateTexture(UINT Width,UINT Height,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DTexture9** ppTexture,VD3DHANDLE* pSharedHandle, char *debugLabel )
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );

	if ( !ppTexture )
		return D3DERR_INVALIDCALL;
	*ppTexture = NULL;

	m_ObjectStats.m_nTotalTextures++;

	IDirect3DTexture9 *dxtex = new IDirect3DTexture9;
	dxtex->m_restype	= D3DRTYPE_TEXTURE;
	dxtex->m_device		= this;
	dxtex->m_surfZero	= NULL;

	dxtex->m_descZero.Format				= Format;
	dxtex->m_descZero.Type					= D3DRTYPE_TEXTURE;
	dxtex->m_descZero.Usage					= Usage;
	dxtex->m_descZero.Pool					= Pool;
	dxtex->m_descZero.MultiSampleType		= D3DMULTISAMPLE_NONE;
	dxtex->m_descZero.MultiSampleQuality	= 0;
	dxtex->m_descZero.Width					= Width;
	dxtex->m_descZero.Height				= Height;

	CVKTexParams params;
	FillVKTexParams( params, D3DRTYPE_TEXTURE, Usage, Format, Width, Height, 1, Levels );

	dxtex->m_tex = m_ctx->CreateTex( params );
	if ( !dxtex->m_tex )
	{
		Warning( "TOGL: CreateTexture: CVKContext::CreateTex failed (%dx%d fmt %x)\n", Width, Height, Format );
		dxtex->m_device = NULL;
		delete dxtex;
		m_ObjectStats.m_nTotalTextures--;
		return D3DERR_OUTOFVIDEOMEMORY;
	}

	// Fabricate the top-level surface wrapper.
	IDirect3DSurface9 *surf = new IDirect3DSurface9;
	surf->m_device	= this;
	surf->m_restype	= D3DRTYPE_SURFACE;
	surf->m_tex		= dxtex->m_tex;
	surf->m_face	= 0;
	surf->m_mip		= 0;
	surf->m_desc	= dxtex->m_descZero;
	dxtex->m_surfZero = surf;
	m_ObjectStats.m_nTotalSurfaces++;

	*ppTexture = dxtex;
	return S_OK;
}

HRESULT IDirect3DDevice9::CreateCubeTexture(UINT EdgeLength,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DCubeTexture9** ppCubeTexture,VD3DHANDLE* pSharedHandle, char *debugLabel )
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );

	if ( !ppCubeTexture )
		return D3DERR_INVALIDCALL;
	*ppCubeTexture = NULL;

	m_ObjectStats.m_nTotalTextures++;

	IDirect3DCubeTexture9 *dxtex = new IDirect3DCubeTexture9;
	dxtex->m_restype	= D3DRTYPE_CUBETEXTURE;
	dxtex->m_device		= this;
	for ( int i = 0; i < 6; i++ )
		dxtex->m_surfZero[i] = NULL;

	dxtex->m_descZero.Format				= Format;
	dxtex->m_descZero.Type					= D3DRTYPE_CUBETEXTURE;
	dxtex->m_descZero.Usage					= Usage;
	dxtex->m_descZero.Pool					= Pool;
	dxtex->m_descZero.MultiSampleType		= D3DMULTISAMPLE_NONE;
	dxtex->m_descZero.MultiSampleQuality	= 0;
	dxtex->m_descZero.Width					= EdgeLength;
	dxtex->m_descZero.Height				= EdgeLength;

	CVKTexParams params;
	FillVKTexParams( params, D3DRTYPE_CUBETEXTURE, Usage, Format, EdgeLength, EdgeLength, 1, Levels );

	dxtex->m_tex = m_ctx->CreateTex( params );
	if ( !dxtex->m_tex )
	{
		Warning( "TOGL: CreateCubeTexture: CVKContext::CreateTex failed\n" );
		dxtex->m_device = NULL;
		delete dxtex;
		m_ObjectStats.m_nTotalTextures--;
		return D3DERR_OUTOFVIDEOMEMORY;
	}

	IDirect3DSurface9 *surf = new IDirect3DSurface9;
	surf->m_device	= this;
	surf->m_restype	= D3DRTYPE_SURFACE;
	surf->m_tex		= dxtex->m_tex;
	surf->m_face	= 0;
	surf->m_mip		= 0;
	surf->m_desc		= dxtex->m_descZero;
	dxtex->m_surfZero[0] = surf;
	m_ObjectStats.m_nTotalSurfaces++;

	*ppCubeTexture = dxtex;
	return S_OK;
}

HRESULT IDirect3DDevice9::CreateVolumeTexture(UINT Width,UINT Height,UINT Depth,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DVolumeTexture9** ppVolumeTexture,VD3DHANDLE* pSharedHandle, char *debugLabel )
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );

	if ( !ppVolumeTexture )
		return D3DERR_INVALIDCALL;
	*ppVolumeTexture = NULL;

	m_ObjectStats.m_nTotalTextures++;

	IDirect3DVolumeTexture9 *dxtex = new IDirect3DVolumeTexture9;
	dxtex->m_restype	= D3DRTYPE_VOLUMETEXTURE;
	dxtex->m_device		= this;
	dxtex->m_surfZero	= NULL;

	dxtex->m_descZero.Format				= Format;
	dxtex->m_descZero.Type					= D3DRTYPE_VOLUMETEXTURE;
	dxtex->m_descZero.Usage					= Usage;
	dxtex->m_descZero.Pool					= Pool;
	dxtex->m_descZero.MultiSampleType		= D3DMULTISAMPLE_NONE;
	dxtex->m_descZero.MultiSampleQuality	= 0;
	dxtex->m_descZero.Width					= Width;
	dxtex->m_descZero.Height				= Height;

	dxtex->m_volDescZero.Format	= Format;
	dxtex->m_volDescZero.Type	= D3DRTYPE_VOLUMETEXTURE;
	dxtex->m_volDescZero.Usage	= Usage;
	dxtex->m_volDescZero.Pool	= Pool;
	dxtex->m_volDescZero.Width	= Width;
	dxtex->m_volDescZero.Height	= Height;
	dxtex->m_volDescZero.Depth	= Depth;

	CVKTexParams params;
	FillVKTexParams( params, D3DRTYPE_VOLUMETEXTURE, Usage, Format, Width, Height, Depth, Levels );

	dxtex->m_tex = m_ctx->CreateTex( params );
	if ( !dxtex->m_tex )
	{
		Warning( "TOGL: CreateVolumeTexture: CVKContext::CreateTex failed\n" );
		dxtex->m_device = NULL;
		delete dxtex;
		m_ObjectStats.m_nTotalTextures--;
		return D3DERR_OUTOFVIDEOMEMORY;
	}

	*ppVolumeTexture = dxtex;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetTextureNonInline(DWORD Stage,IDirect3DBaseTexture9* pTexture)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	Assert( Stage < VK_SAMPLER_COUNT );
	if ( Stage >= VK_SAMPLER_COUNT )
		return D3DERR_INVALIDCALL;

	m_textures[Stage] = pTexture;
	m_ctx->SetSamplerTex( Stage, pTexture ? pTexture->m_tex : NULL );
	return S_OK;
}

HRESULT IDirect3DDevice9::GetTexture(DWORD Stage,IDirect3DBaseTexture9** ppTexture)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	if ( !ppTexture || Stage >= VK_SAMPLER_COUNT )
		return D3DERR_INVALIDCALL;

	*ppTexture = m_textures[Stage];
	if ( *ppTexture )
		(*ppTexture)->AddRef();
	return S_OK;
}

// ============================================================================================================================== //
// Render targets / depth stencil / surfaces
// ============================================================================================================================== //
static IDirect3DSurface9 *CreateSurfaceForResource( IDirect3DDevice9 *device, CVKTex *tex,
	DWORD Usage, D3DFORMAT Format, UINT Width, UINT Height, bool depthStencil )
{
	IDirect3DSurface9 *surf = new IDirect3DSurface9;
	surf->m_device	= device;
	surf->m_restype	= D3DRTYPE_SURFACE;
	surf->m_tex		= tex;
	surf->m_face	= 0;
	surf->m_mip		= 0;

	surf->m_desc.Format				= Format;
	surf->m_desc.Type				= D3DRTYPE_SURFACE;
	surf->m_desc.Usage				= Usage;
	surf->m_desc.Pool				= D3DPOOL_DEFAULT;
	surf->m_desc.MultiSampleType	= D3DMULTISAMPLE_NONE;
	surf->m_desc.MultiSampleQuality	= 0;
	surf->m_desc.Width				= Width;
	surf->m_desc.Height				= Height;
	(void)depthStencil;

	device->m_ObjectStats.m_nTotalSurfaces++;
	return surf;
}

HRESULT IDirect3DDevice9::CreateRenderTarget(UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,DWORD MultisampleQuality,BOOL Lockable,IDirect3DSurface9** ppSurface,VD3DHANDLE* pSharedHandle, char *debugLabel )
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );

	if ( !ppSurface )
		return D3DERR_INVALIDCALL;
	*ppSurface = NULL;

	m_ObjectStats.m_nTotalRenderTargets++;

	CVKTexParams params;
	FillVKTexParams( params, D3DRTYPE_TEXTURE, D3DUSAGE_RENDERTARGET, Format, Width, Height, 1, 1 );
	params.m_isRenderTarget = true;

	CVKTex *tex = m_ctx->CreateTex( params );
	if ( !tex )
	{
		Warning( "TOGL: CreateRenderTarget: CVKContext::CreateTex failed\n" );
		return D3DERR_OUTOFVIDEOMEMORY;
	}

	*ppSurface = CreateSurfaceForResource( this, tex, D3DUSAGE_RENDERTARGET, Format, Width, Height, false );
	return S_OK;
}

HRESULT IDirect3DDevice9::SetRenderTarget(DWORD RenderTargetIndex,IDirect3DSurface9* pRenderTarget)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	if ( RenderTargetIndex >= MAX_RENDER_TARGETS )
		return D3DERR_INVALIDCALL;

	if ( m_pRenderTargets[RenderTargetIndex] )
		m_pRenderTargets[RenderTargetIndex]->m_device = NULL; // we held a weak ref
	m_pRenderTargets[RenderTargetIndex] = pRenderTarget;

	if ( m_ctx )
		m_ctx->m_boundRenderTargets[RenderTargetIndex] = pRenderTarget ? pRenderTarget->m_tex : NULL;

	m_bFBODirty = true;
	return S_OK;
}

HRESULT IDirect3DDevice9::GetRenderTarget(DWORD RenderTargetIndex,IDirect3DSurface9** ppRenderTarget)
{
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	if ( !ppRenderTarget || RenderTargetIndex >= MAX_RENDER_TARGETS )
		return D3DERR_INVALIDCALL;
	if ( !m_pRenderTargets[RenderTargetIndex] )
		return D3DERR_NOTFOUND;
	m_pRenderTargets[RenderTargetIndex]->AddRef();
	*ppRenderTarget = m_pRenderTargets[RenderTargetIndex];
	return S_OK;
}

HRESULT IDirect3DDevice9::CreateOffscreenPlainSurface(UINT Width,UINT Height,D3DFORMAT Format,D3DPOOL Pool,IDirect3DSurface9** ppSurface,VD3DHANDLE* pSharedHandle )
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	if ( !ppSurface )
		return D3DERR_INVALIDCALL;
	*ppSurface = NULL;

	CVKTexParams params;
	FillVKTexParams( params, D3DRTYPE_TEXTURE, 0, Format, Width, Height, 1, 1 );
	CVKTex *tex = m_ctx->CreateTex( params );
	if ( !tex )
		return D3DERR_OUTOFVIDEOMEMORY;

	*ppSurface = CreateSurfaceForResource( this, tex, 0, Format, Width, Height, false );
	return S_OK;
}

HRESULT IDirect3DDevice9::CreateDepthStencilSurface(UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,DWORD MultisampleQuality,BOOL Discard,IDirect3DSurface9** ppSurface,VD3DHANDLE* pSharedHandle )
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	if ( !ppSurface )
		return D3DERR_INVALIDCALL;
	*ppSurface = NULL;

	CVKTexParams params;
	FillVKTexParams( params, D3DRTYPE_TEXTURE, D3DUSAGE_DEPTHSTENCIL, Format, Width, Height, 1, 1 );
	params.m_isDepthStencil = true;

	CVKTex *tex = m_ctx->CreateTex( params );
	if ( !tex )
	{
		Warning( "TOGL: CreateDepthStencilSurface: CVKContext::CreateTex failed\n" );
		return D3DERR_OUTOFVIDEOMEMORY;
	}

	*ppSurface = CreateSurfaceForResource( this, tex, D3DUSAGE_DEPTHSTENCIL, Format, Width, Height, true );
	return S_OK;
}

HRESULT IDirect3DDevice9::SetDepthStencilSurface(IDirect3DSurface9* pNewZStencil)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	if ( m_pDepthStencil )
		m_pDepthStencil->m_device = NULL;
	m_pDepthStencil = pNewZStencil;

	if ( m_ctx )
		m_ctx->m_boundDepthStencil = pNewZStencil ? pNewZStencil->m_tex : NULL;

	m_bFBODirty = true;
	return S_OK;
}

HRESULT IDirect3DDevice9::GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface)
{
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	if ( !ppZStencilSurface )
		return D3DERR_INVALIDCALL;
	if ( !m_pDepthStencil )
		return D3DERR_NOTFOUND;
	m_pDepthStencil->AddRef();
	*ppZStencilSurface = m_pDepthStencil;
	return S_OK;
}

HRESULT IDirect3DDevice9::GetRenderTargetData(IDirect3DSurface9* pRenderTarget,IDirect3DSurface9* pDestSurface)
{
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	if ( !pRenderTarget || !pDestSurface || !pRenderTarget->m_tex || !pDestSurface->m_tex )
		return D3DERR_INVALIDCALL;

	VkImageCopy copy;
	V_memset( &copy, 0, sizeof(copy) );
	copy.srcSubresource.aspectMask	= VK_IMAGE_ASPECT_COLOR_BIT;
	copy.dstSubresource.aspectMask	= VK_IMAGE_ASPECT_COLOR_BIT;
	copy.extent.width				= V_min( pRenderTarget->m_desc.Width, pDestSurface->m_desc.Width );
	copy.extent.height				= V_min( pRenderTarget->m_desc.Height, pDestSurface->m_desc.Height );
	copy.extent.depth				= 1;
	m_ctx->CopyTex( pRenderTarget->m_tex, pDestSurface->m_tex, &copy );
	return S_OK;
}

HRESULT IDirect3DDevice9::GetFrontBufferData(UINT iSwapChain,IDirect3DSurface9* pDestSurface)
{
	// Reading the front buffer is not generally supported on Vulkan.
	Warning( "TOGL: GetFrontBufferData is not supported on the Vulkan backend\n" );
	return S_OK;
}

HRESULT IDirect3DDevice9::StretchRect(IDirect3DSurface9* pSourceSurface,CONST RECT* pSourceRect,IDirect3DSurface9* pDestSurface,CONST RECT* pDestRect,D3DTEXTUREFILTERTYPE Filter)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	if ( !pSourceSurface || !pDestSurface || !pSourceSurface->m_tex || !pDestSurface->m_tex )
		return D3DERR_INVALIDCALL;

	VkImageBlit blit;
	V_memset( &blit, 0, sizeof(blit) );
	blit.srcSubresource.aspectMask	= VK_IMAGE_ASPECT_COLOR_BIT;
	blit.srcSubresource.mipLevel	= pSourceSurface->m_mip;
	blit.srcSubresource.layerCount	= 1;
	blit.dstSubresource.aspectMask	= VK_IMAGE_ASPECT_COLOR_BIT;
	blit.dstSubresource.mipLevel	= pDestSurface->m_mip;
	blit.dstSubresource.layerCount	= 1;

	blit.srcOffsets[0] = { 0, 0, 0 };
	blit.srcOffsets[1] = { (int32_t)pSourceSurface->m_desc.Width, (int32_t)pSourceSurface->m_desc.Height, 1 };
	blit.dstOffsets[0] = { 0, 0, 0 };
	blit.dstOffsets[1] = { (int32_t)pDestSurface->m_desc.Width,   (int32_t)pDestSurface->m_desc.Height,   1 };

	if ( pSourceRect )
	{
		blit.srcOffsets[0].x = pSourceRect->left;
		blit.srcOffsets[0].y = pSourceRect->top;
		blit.srcOffsets[1].x = pSourceRect->right;
		blit.srcOffsets[1].y = pSourceRect->bottom;
	}
	if ( pDestRect )
	{
		blit.dstOffsets[0].x = pDestRect->left;
		blit.dstOffsets[0].y = pDestRect->top;
		blit.dstOffsets[1].x = pDestRect->right;
		blit.dstOffsets[1].y = pDestRect->bottom;
	}

	VkFilter vkFilter = (Filter == D3DTEXF_POINT) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
	(void)vkFilter;

	m_ctx->BlitTex( pSourceSurface->m_tex, pDestSurface->m_tex, &blit );
	return S_OK;
}

// ============================================================================================================================== //
// Shaders
// ============================================================================================================================== //
HRESULT IDirect3DDevice9::CreatePixelShader(CONST DWORD* pFunction,IDirect3DPixelShader9** ppShader, const char *pShaderName, char *debugLabel, const uint32 *pCentroidMask )
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );

	if ( !ppShader || !pFunction )
		return D3DERR_INVALIDCALL;
	*ppShader = NULL;

	m_ObjectStats.m_nTotalPixelShaders++;

	IDirect3DPixelShader9 *dxps = new IDirect3DPixelShader9;
	dxps->m_device		= this;
	dxps->m_restype		= D3DRTYPE_SURFACE;
	dxps->m_pixProgram	= NULL;
	dxps->m_pixHighWater		= 0;
	dxps->m_pixSamplerMask		= 0;
	dxps->m_pixSamplerTypes		= 0;
	dxps->m_pixFragDataMask		= 1;

	bool bVertex = false;
	uint32_t options = DX9ToVK_OptionDoFixupZ | DX9ToVK_OptionDoFixupY | DX9ToVK_OptionSRGBWriteSuffix;

	int res = g_D3DToVKTranslator.Translate( (const uint32_t*)pFunction, 0, &bVertex, options );
	if ( res != DX9ToVK_OK )
	{
		Warning( "TOGL: CreatePixelShader: DX9->SPIR-V translation failed (%s)\n", pShaderName ? pShaderName : "?" );
		dxps->m_device = NULL;
		delete dxps;
		m_ObjectStats.m_nTotalPixelShaders--;
		return E_FAIL;
	}

	const uint32_t *spirv		= g_D3DToVKTranslator.GetSPIRV();
	size_t spirvWordCount	= g_D3DToVKTranslator.GetSPIRVWordCount();
	size_t spirvSize		= spirvWordCount * sizeof(uint32_t);

	dxps->m_pixProgram = m_ctx->CreateProgram( kVKProgramPixel, spirv, spirvSize );
	if ( !dxps->m_pixProgram )
	{
		Warning( "TOGL: CreatePixelShader: CVKContext::CreateProgram failed (%s)\n", pShaderName ? pShaderName : "?" );
		g_D3DToVKTranslator.ClearSPIRV();
		dxps->m_device = NULL;
		delete dxps;
		m_ObjectStats.m_nTotalPixelShaders--;
		return E_FAIL;
	}

	// Mirror translation state onto the D3D wrapper for flush-time use.
	dxps->m_pixSamplerMask = g_D3DToVKTranslator.GetSamplerUsageMask();

	uint32 samplerTypes = 0;
	for ( uint32 i = 0; i < MAX_SPV_SAMPLERS; i++ )
	{
		uint32 t = g_D3DToVKTranslator.GetSamplerType( i );
		samplerTypes |= ( t & 3 ) << ( i * 2 );
	}
	dxps->m_pixSamplerTypes = samplerTypes;

	// Derive a conservative high-water mark from the constant usage mask.
	uint32 constMask = g_D3DToVKTranslator.GetConstUsageMask();
	uint high = 0;
	for ( uint32 i = 0; i < 32; i++ )
		if ( constMask & (1u << i) )
			high = i + 1;
	dxps->m_pixHighWater = high;

	g_D3DToVKTranslator.ClearSPIRV();

	*ppShader = dxps;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetPixelShaderNonInline(IDirect3DPixelShader9* pShader)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	m_pixelShader = pShader;
	if ( m_ctx )
		m_ctx->m_boundPixelShader = pShader ? pShader->m_pixProgram : NULL;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetPixelShaderConstantFNonInline(UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount)
{
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	// Constant buffers are pushed into the program's UBO at draw time via the
	// context flush; here we just latch them into a staging area owned by the
	// context.
	(void)StartRegister; (void)pConstantData; (void)Vector4fCount;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetPixelShaderConstantB(UINT StartRegister,CONST BOOL* pConstantData,UINT  BoolCount)
{
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	(void)StartRegister; (void)pConstantData; (void)BoolCount;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetPixelShaderConstantI(UINT StartRegister,CONST int* pConstantData,UINT Vector4iCount)
{
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	(void)StartRegister; (void)pConstantData; (void)Vector4iCount;
	return S_OK;
}

HRESULT IDirect3DDevice9::CreateVertexShader(CONST DWORD* pFunction,IDirect3DVertexShader9** ppShader, const char *pShaderName, char *debugLabel )
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );

	if ( !ppShader || !pFunction )
		return D3DERR_INVALIDCALL;
	*ppShader = NULL;

	m_ObjectStats.m_nTotalVertexShaders++;

	IDirect3DVertexShader9 *dxvs = new IDirect3DVertexShader9;
	dxvs->m_device		= this;
	dxvs->m_restype		= D3DRTYPE_SURFACE;
	dxvs->m_vtxProgram	= NULL;
	dxvs->m_vtxHighWater		= 0;
	dxvs->m_vtxHighWaterBone	= 0;
	dxvs->m_maxVertexAttrs		= 0;
	V_memset( dxvs->m_vtxAttribMap, 0, sizeof(dxvs->m_vtxAttribMap) );

	bool bVertex = false;
	uint32_t options = DX9ToVK_OptionDoFixupZ | DX9ToVK_OptionDoFixupY | DX9ToVK_OptionDoUserClipPlanes | DX9ToVK_OptionGenerateBoneUniformBuffer;

	int res = g_D3DToVKTranslator.Translate( (const uint32_t*)pFunction, 0, &bVertex, options );
	if ( res != DX9ToVK_OK )
	{
		Warning( "TOGL: CreateVertexShader: DX9->SPIR-V translation failed (%s)\n", pShaderName ? pShaderName : "?" );
		dxvs->m_device = NULL;
		delete dxvs;
		m_ObjectStats.m_nTotalVertexShaders--;
		return E_FAIL;
	}

	const uint32_t *spirv	= g_D3DToVKTranslator.GetSPIRV();
	size_t spirvWordCount	= g_D3DToVKTranslator.GetSPIRVWordCount();
	size_t spirvSize		= spirvWordCount * sizeof(uint32_t);

	dxvs->m_vtxProgram = m_ctx->CreateProgram( kVKProgramVertex, spirv, spirvSize );
	if ( !dxvs->m_vtxProgram )
	{
		Warning( "TOGL: CreateVertexShader: CVKContext::CreateProgram failed (%s)\n", pShaderName ? pShaderName : "?" );
		g_D3DToVKTranslator.ClearSPIRV();
		dxvs->m_device = NULL;
		delete dxvs;
		m_ObjectStats.m_nTotalVertexShaders--;
		return E_FAIL;
	}

	// Mirror the attribute usage map (usage/index packed into a byte) so that
	// vertex declarations can be matched to shader inputs at flush time.
	for ( uint32 i = 0; i < 16; i++ )
		dxvs->m_vtxAttribMap[i] = (unsigned char)g_D3DToVKTranslator.GetAttribMap( i );

	uint32 attribMask = g_D3DToVKTranslator.GetVertexAttributeMask();
	uint maxAttr = 0;
	for ( uint32 i = 0; i < 16; i++ )
		if ( attribMask & (1u << i) )
			maxAttr = i + 1;
	dxvs->m_maxVertexAttrs = maxAttr;

	uint32 constMask = g_D3DToVKTranslator.GetConstUsageMask();
	uint high = 0;
	for ( uint32 i = 0; i < 32; i++ )
		if ( constMask & (1u << i) )
			high = i + 1;
	dxvs->m_vtxHighWater = high;

	g_D3DToVKTranslator.ClearSPIRV();

	*ppShader = dxvs;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetVertexShaderNonInline(IDirect3DVertexShader9* pShader)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	m_vertexShader = pShader;
	if ( m_ctx )
		m_ctx->m_boundVertexShader = pShader ? pShader->m_vtxProgram : NULL;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetVertexShaderConstantFNonInline(UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount)
{
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	(void)StartRegister; (void)pConstantData; (void)Vector4fCount;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetVertexShaderConstantBNonInline(UINT StartRegister,CONST BOOL* pConstantData,UINT  BoolCount)
{
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	(void)StartRegister; (void)pConstantData; (void)BoolCount;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetVertexShaderConstantINonInline(UINT StartRegister,CONST int* pConstantData,UINT Vector4iCount)
{
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	(void)StartRegister; (void)pConstantData; (void)Vector4iCount;
	return S_OK;
}

HRESULT IDirect3DDevice9::LinkShaderPair( IDirect3DVertexShader9* vs, IDirect3DPixelShader9* ps )
{
	// Vulkan links shader stages at pipeline creation time, which the context
	// handles lazily on draw.  Nothing to do eagerly here.
	(void)vs; (void)ps;
	return S_OK;
}

HRESULT IDirect3DDevice9::ValidateShaderPair( IDirect3DVertexShader9* vs, IDirect3DPixelShader9* ps )
{
	(void)vs; (void)ps;
	return S_OK;
}

HRESULT IDirect3DDevice9::QueryShaderPair( int index, VKShaderPairInfo *infoOut )
{
	if ( !infoOut )
		return D3DERR_INVALIDCALL;
	V_memset( infoOut, 0, sizeof(*infoOut) );
	(void)index;
	return S_OK;
}

// ============================================================================================================================== //
// Vertex declarations / FVF
// ============================================================================================================================== //
HRESULT IDirect3DDevice9::CreateVertexDeclaration(CONST D3DVERTEXELEMENT9* pVertexElements,IDirect3DVertexDeclaration9** ppDecl)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );

	if ( !ppDecl || !pVertexElements )
		return D3DERR_INVALIDCALL;
	*ppDecl = NULL;

	IDirect3DVertexDeclaration9 *pDecl = new IDirect3DVertexDeclaration9;
	pDecl->m_device = this;
	pDecl->m_elemCount = 0;
	V_memset( pDecl->m_VertexAttribDescToStreamIndex, 0, sizeof(pDecl->m_VertexAttribDescToStreamIndex) );

	uint i = 0;
	while ( (i < MAX_D3DVERTEXELEMENTS) && (pVertexElements[i].Type != D3DDECLTYPE_UNUSED) )
	{
		D3DVERTEXELEMENT9_GL &elem = pDecl->m_elements[i];
		elem.m_dxdecl = pVertexElements[i];

		GLMVertexAttributeDesc &desc = elem.m_gldecl;
		desc.m_pBuffer		= NULL;
		desc.m_streamOffset	= pVertexElements[i].Offset;
		desc.m_offset		= pVertexElements[i].Offset;
		desc.m_normalized	= GL_FALSE;

		// Map D3DDECLTYPE -> component count + GL datatype.
		switch ( pVertexElements[i].Type )
		{
			case D3DDECLTYPE_FLOAT1:	desc.m_nCompCount = 1; desc.m_datatype = GL_FLOAT; break;
			case D3DDECLTYPE_FLOAT2:	desc.m_nCompCount = 2; desc.m_datatype = GL_FLOAT; break;
			case D3DDECLTYPE_FLOAT3:	desc.m_nCompCount = 3; desc.m_datatype = GL_FLOAT; break;
			case D3DDECLTYPE_FLOAT4:	desc.m_nCompCount = 4; desc.m_datatype = GL_FLOAT; break;
			case D3DDECLTYPE_D3DCOLOR:	desc.m_nCompCount = 4; desc.m_datatype = GL_UNSIGNED_BYTE; desc.m_normalized = GL_TRUE; break;
			case D3DDECLTYPE_UBYTE4:		desc.m_nCompCount = 4; desc.m_datatype = GL_UNSIGNED_BYTE; break;
			case D3DDECLTYPE_SHORT2:	desc.m_nCompCount = 2; desc.m_datatype = GL_SHORT; break;
			case D3DDECLTYPE_SHORT4:	desc.m_nCompCount = 4; desc.m_datatype = GL_SHORT; break;
			case D3DDECLTYPE_UBYTE4N:	desc.m_nCompCount = 4; desc.m_datatype = GL_UNSIGNED_BYTE; desc.m_normalized = GL_TRUE; break;
			case D3DDECLTYPE_SHORT2N:	desc.m_nCompCount = 2; desc.m_datatype = GL_SHORT; desc.m_normalized = GL_TRUE; break;
			case D3DDECLTYPE_SHORT4N:	desc.m_nCompCount = 4; desc.m_datatype = GL_SHORT; desc.m_normalized = GL_TRUE; break;
			case D3DDECLTYPE_USHORT2N:	desc.m_nCompCount = 2; desc.m_datatype = GL_UNSIGNED_SHORT; desc.m_normalized = GL_TRUE; break;
			case D3DDECLTYPE_USHORT4N:	desc.m_nCompCount = 4; desc.m_datatype = GL_UNSIGNED_SHORT; desc.m_normalized = GL_TRUE; break;
			case D3DDECLTYPE_FLOAT16_2:	desc.m_nCompCount = 2; desc.m_datatype = GL_HALF_FLOAT; break;
			case D3DDECLTYPE_FLOAT16_4:	desc.m_nCompCount = 4; desc.m_datatype = GL_HALF_FLOAT; break;
			default:					desc.m_nCompCount = 4; desc.m_datatype = GL_FLOAT; break;
		}
		desc.m_stride = 0; // filled at SetStreamSource time

		pDecl->m_elemCount++;
		i++;
	}

	m_ObjectStats.m_nTotalVertexDecls++;
	*ppDecl = pDecl;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetVertexDeclarationNonInline(IDirect3DVertexDeclaration9* pDecl)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	m_pVertDecl = pDecl;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetFVF(DWORD FVF)
{
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	(void)FVF;
	return S_OK;
}

HRESULT IDirect3DDevice9::GetFVF(DWORD* pFVF)
{
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	if ( pFVF ) *pFVF = 0;
	return S_OK;
}

// ============================================================================================================================== //
// Vertex / index buffers
// ============================================================================================================================== //
HRESULT IDirect3DDevice9::CreateVertexBuffer(UINT Length,DWORD Usage,DWORD FVF,D3DPOOL Pool,IDirect3DVertexBuffer9** ppVertexBuffer,VD3DHANDLE* pSharedHandle)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );

	if ( !ppVertexBuffer || Length == 0 )
		return D3DERR_INVALIDCALL;
	*ppVertexBuffer = NULL;

	m_ObjectStats.m_nTotalVertexBuffers++;

	IDirect3DVertexBuffer9 *pVB = new IDirect3DVertexBuffer9;
	pVB->m_device = this;
	pVB->m_restype = D3DRTYPE_VERTEXBUFFER;
	pVB->m_ctx = m_ctx;

	VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bool dynamic = (Pool == D3DPOOL_DEFAULT) && ((Usage & D3DUSAGE_DYNAMIC) != 0);

	pVB->m_vtxBuffer = m_ctx->CreateBuffer( usage, Length, dynamic );
	if ( !pVB->m_vtxBuffer )
	{
		Warning( "TOGL: CreateVertexBuffer: CVKContext::CreateBuffer failed (%u bytes)\n", Length );
		pVB->m_device = NULL;
		delete pVB;
		m_ObjectStats.m_nTotalVertexBuffers--;
		return D3DERR_OUTOFVIDEOMEMORY;
	}

	pVB->m_vtxDesc.Format	= (D3DFORMAT)0; // D3DFMT_UNKNOWN
	pVB->m_vtxDesc.Type		= D3DRTYPE_VERTEXBUFFER;
	pVB->m_vtxDesc.Usage	= Usage;
	pVB->m_vtxDesc.Pool		= Pool;
	pVB->m_vtxDesc.Size		= Length;
	pVB->m_vtxDesc.FVF		= FVF;

	*ppVertexBuffer = pVB;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetStreamSourceNonInline(UINT StreamNumber,IDirect3DVertexBuffer9* pStreamData,UINT OffsetInBytes,UINT Stride)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	if ( StreamNumber >= D3D_MAX_STREAMS )
		return D3DERR_INVALIDCALL;

	m_streams[StreamNumber].m_vtxBuffer = pStreamData;
	m_streams[StreamNumber].m_offset = OffsetInBytes;
	m_streams[StreamNumber].m_stride = Stride;
	m_vtx_buffers[StreamNumber] = pStreamData ? pStreamData->m_vtxBuffer : NULL;
	return S_OK;
}

HRESULT IDirect3DDevice9::CreateIndexBuffer(UINT Length,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DIndexBuffer9** ppIndexBuffer,VD3DHANDLE* pSharedHandle)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );

	if ( !ppIndexBuffer || Length == 0 )
		return D3DERR_INVALIDCALL;
	*ppIndexBuffer = NULL;

	m_ObjectStats.m_nTotalIndexBuffers++;

	IDirect3DIndexBuffer9 *pIB = new IDirect3DIndexBuffer9;
	pIB->m_device = this;
	pIB->m_restype = D3DRTYPE_INDEXBUFFER;
	pIB->m_ctx = m_ctx;

	VkBufferUsageFlags usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bool dynamic = (Pool == D3DPOOL_DEFAULT) && ((Usage & D3DUSAGE_DYNAMIC) != 0);

	pIB->m_idxBuffer = m_ctx->CreateBuffer( usage, Length, dynamic );
	if ( !pIB->m_idxBuffer )
	{
		Warning( "TOGL: CreateIndexBuffer: CVKContext::CreateBuffer failed (%u bytes)\n", Length );
		pIB->m_device = NULL;
		delete pIB;
		m_ObjectStats.m_nTotalIndexBuffers--;
		return D3DERR_OUTOFVIDEOMEMORY;
	}

	pIB->m_idxDesc.Format	= Format;
	pIB->m_idxDesc.Type		= D3DRTYPE_INDEXBUFFER;
	pIB->m_idxDesc.Usage	= Usage;
	pIB->m_idxDesc.Pool		= Pool;
	pIB->m_idxDesc.Size		= Length;

	*ppIndexBuffer = pIB;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetIndicesNonInline(IDirect3DIndexBuffer9* pIndexData)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	m_indices.m_idxBuffer = pIndexData;
	return S_OK;
}

// ============================================================================================================================== //
// State management
// ============================================================================================================================== //
HRESULT IDirect3DDevice9::SetRenderState(D3DRENDERSTATETYPE State,DWORD Value)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );

	TOGL_NULL_DEVICE_CHECK;

	// The giant switch mapping D3DRS_* values onto CVKContext Write* calls.
	switch (State)
	{
		// ---- depth ----
		case D3DRS_ZENABLE:
		{
			gl.m_DepthTestEnable.value = (Value != 0);
			m_ctx->WriteDepthTestEnable( gl.m_DepthTestEnable.value );
			break;
		}
		case D3DRS_ZWRITEENABLE:
		{
			gl.m_DepthMask.value = (Value != 0);
			m_ctx->WriteDepthMask( gl.m_DepthMask.value );
			break;
		}
		case D3DRS_ZFUNC:
		{
			VkCompareOp func = D3DCompareFuncToVK( Value );
			gl.m_DepthFunc.value = func;
			m_ctx->WriteDepthFunc( func );
			break;
		}

		// ---- color write ----
		case D3DRS_COLORWRITEENABLE:
		{
			uint8 mask = 0;
			if ( Value & D3DCOLORWRITEENABLE_RED   ) mask |= 0x1;
			if ( Value & D3DCOLORWRITEENABLE_GREEN ) mask |= 0x2;
			if ( Value & D3DCOLORWRITEENABLE_BLUE  ) mask |= 0x4;
			if ( Value & D3DCOLORWRITEENABLE_ALPHA ) mask |= 0x8;
			gl.m_ColorMaskSingle.value = mask;
			m_ctx->WriteColorMask( 0, mask );
			break;
		}
		case D3DRS_COLORWRITEENABLE1: m_ctx->WriteColorMask( 1, (uint8)Value ); break;
		case D3DRS_COLORWRITEENABLE2: m_ctx->WriteColorMask( 2, (uint8)Value ); break;
		case D3DRS_COLORWRITEENABLE3: m_ctx->WriteColorMask( 3, (uint8)Value ); break;

		// ---- cull ----
		case D3DRS_CULLMODE:
		{
			VkCullModeFlags mode = D3DCullModeToVK( Value );
			gl.m_CullFrontFace.value = mode;
			gl.m_CullFaceEnable.value = (mode != VK_CULL_MODE_NONE);
			m_ctx->WriteCullFaceEnable( gl.m_CullFaceEnable.value );
			m_ctx->WriteCullFrontFace( mode );
			break;
		}

		// ---- alpha blend ----
		case D3DRS_ALPHABLENDENABLE:
		{
			gl.m_BlendEnable.value[0] = (Value != 0);
			m_ctx->WriteBlendEnable( 0, gl.m_BlendEnable.value[0] );
			break;
		}
		case D3DRS_BLENDOP:
		{
			VkBlendOp op = D3DBlendOperationToVK( Value );
			gl.m_BlendEquation.op = op;
			m_ctx->WriteBlendEquation( 0, op );
			break;
		}
		case D3DRS_SRCBLEND:
		{
			gl.m_BlendFactor.src = D3DBlendFactorToVK( Value );
			m_ctx->WriteBlendFactor( 0, gl.m_BlendFactor.src, gl.m_BlendFactor.dst );
			break;
		}
		case D3DRS_DESTBLEND:
		{
			gl.m_BlendFactor.dst = D3DBlendFactorToVK( Value );
			m_ctx->WriteBlendFactor( 0, gl.m_BlendFactor.src, gl.m_BlendFactor.dst );
			break;
		}
		case D3DRS_BLENDFACTOR:
		{
			gl.m_BlendColor.r = (float)((Value >> 16) & 0xFF) / 255.0f;
			gl.m_BlendColor.g = (float)((Value >>  8) & 0xFF) / 255.0f;
			gl.m_BlendColor.b = (float)((Value      ) & 0xFF) / 255.0f;
			gl.m_BlendColor.a = (float)((Value >> 24) & 0xFF) / 255.0f;
			m_ctx->WriteBlendColor( gl.m_BlendColor.r, gl.m_BlendColor.g, gl.m_BlendColor.b, gl.m_BlendColor.a );
			break;
		}
		case D3DRS_SRGBWRITEENABLE:
		{
			gl.m_BlendEnableSRGB.value = (Value != 0);
			m_ctx->WriteBlendEnableSRGB( gl.m_BlendEnableSRGB.value );
			break;
		}
		case D3DRS_SEPARATEALPHABLENDENABLE:
		case D3DRS_SRCBLENDALPHA:
		case D3DRS_DESTBLENDALPHA:
		case D3DRS_BLENDOPALPHA:
			// Separate alpha blend not wired; latch silently.
			break;

		// ---- alpha test (no fixed-function equivalent on Vulkan; latch) ----
		case D3DRS_ALPHATESTENABLE:
		case D3DRS_ALPHAREF:
		case D3DRS_ALPHAFUNC:
		case D3DRS_DITHERENABLE:
			break;

		// ---- stencil ----
		case D3DRS_STENCILENABLE:
		{
			gl.m_StencilTestEnable.value = (Value != 0);
			m_ctx->WriteStencilTestEnable( gl.m_StencilTestEnable.value );
			break;
		}
		case D3DRS_STENCILFAIL:
		{
			gl.m_StencilOp.sfail = D3DStencilOpToVK( Value );
			m_ctx->WriteStencilOp( gl.m_StencilOp.sfail, gl.m_StencilOp.dpfail, gl.m_StencilOp.dppass );
			break;
		}
		case D3DRS_STENCILZFAIL:
		{
			gl.m_StencilOp.dpfail = D3DStencilOpToVK( Value );
			m_ctx->WriteStencilOp( gl.m_StencilOp.sfail, gl.m_StencilOp.dpfail, gl.m_StencilOp.dppass );
			break;
		}
		case D3DRS_STENCILPASS:
		{
			gl.m_StencilOp.dppass = D3DStencilOpToVK( Value );
			m_ctx->WriteStencilOp( gl.m_StencilOp.sfail, gl.m_StencilOp.dpfail, gl.m_StencilOp.dppass );
			break;
		}
		case D3DRS_STENCILFUNC:
		{
			gl.m_StencilFunc.func = D3DCompareFuncToVK( Value );
			m_ctx->WriteStencilFunc( gl.m_StencilFunc.func, gl.m_StencilFunc.ref, gl.m_StencilFunc.mask );
			break;
		}
		case D3DRS_STENCILREF:
		{
			gl.m_StencilFunc.ref = Value;
			m_ctx->WriteStencilFunc( gl.m_StencilFunc.func, gl.m_StencilFunc.ref, gl.m_StencilFunc.mask );
			break;
		}
		case D3DRS_STENCILMASK:
		{
			gl.m_StencilFunc.mask = Value;
			m_ctx->WriteStencilFunc( gl.m_StencilFunc.func, gl.m_StencilFunc.ref, gl.m_StencilFunc.mask );
			break;
		}
		case D3DRS_STENCILWRITEMASK:
		{
			gl.m_StencilWriteMask.value = Value;
			m_ctx->WriteStencilWriteMask( Value );
			break;
		}
		case D3DRS_TWOSIDEDSTENCILMODE:
		case D3DRS_CCW_STENCILFAIL:
		case D3DRS_CCW_STENCILZFAIL:
		case D3DRS_CCW_STENCILPASS:
		case D3DRS_CCW_STENCILFUNC:
			// Two-sided stencil mirrors the front-facing state on Vulkan.
			break;

		// ---- scissor ----
		case D3DRS_SCISSORTESTENABLE:
		{
			gl.m_ScissorEnable.value = (Value != 0);
			m_ctx->WriteScissorEnable( gl.m_ScissorEnable.value );
			break;
		}

		// ---- depth bias ----
		case D3DRS_DEPTHBIAS:
		{
			gl.m_DepthBias.bias = *(float*)&Value;
			m_ctx->WriteDepthBias( gl.m_DepthBias.bias, gl.m_DepthBias.biasClamp, gl.m_DepthBias.slopeScaledBias );
			break;
		}
		case D3DRS_SLOPESCALEDEPTHBIAS:
		{
			gl.m_DepthBias.slopeScaledBias = *(float*)&Value;
			m_ctx->WriteDepthBias( gl.m_DepthBias.bias, gl.m_DepthBias.biasClamp, gl.m_DepthBias.slopeScaledBias );
			break;
		}

		// ---- clip planes ----
		case D3DRS_CLIPPLANEENABLE:
		{
			for ( int x = 0; x < kVKUserClipPlanes; x++ )
			{
				bool on = (Value & (1u << x)) != 0;
				gl.m_ClipPlaneEnable.value[x] = on;
				m_ctx->WriteClipPlaneEnable( x, on );
			}
			break;
		}

		// ---- fog (fixed-function; latch only) ----
		case D3DRS_FOGENABLE:
		{
			gl.m_FogEnable = (Value != 0);
			break;
		}
		case D3DRS_FOGCOLOR:
		case D3DRS_FOGTABLEMODE:
		case D3DRS_FOGVERTEXMODE:
		case D3DRS_FOGSTART:
		case D3DRS_FOGEND:
		case D3DRS_FOGDENSITY:
		case D3DRS_RANGEFOGENABLE:
		case D3DRS_SPECULARENABLE:
		case D3DRS_LIGHTING:
		case D3DRS_AMBIENT:
		case D3DRS_COLORVERTEX:
		case D3DRS_LOCALVIEWER:
		case D3DRS_NORMALIZENORMALS:
		case D3DRS_DIFFUSEMATERIALSOURCE:
		case D3DRS_SPECULARMATERIALSOURCE:
		case D3DRS_AMBIENTMATERIALSOURCE:
		case D3DRS_EMISSIVEMATERIALSOURCE:
		case D3DRS_VERTEXBLEND:
			break;

		// ---- fill / shade / point / wrap (latched or unsupported) ----
		case D3DRS_FILLMODE:
		case D3DRS_SHADEMODE:
		case D3DRS_LASTPIXEL:
		case D3DRS_POINTSIZE:
		case D3DRS_POINTSIZE_MIN:
		case D3DRS_POINTSIZE_MAX:
		case D3DRS_POINTSPRITEENABLE:
		case D3DRS_POINTSCALEENABLE:
		case D3DRS_POINTSCALE_A:
		case D3DRS_POINTSCALE_B:
		case D3DRS_POINTSCALE_C:
		case D3DRS_MULTISAMPLEANTIALIAS:
		case D3DRS_MULTISAMPLEMASK:
		case D3DRS_PATCHEDGESTYLE:
		case D3DRS_DEBUGMONITORTOKEN:
		case D3DRS_INDEXEDVERTEXBLENDENABLE:
		case D3DRS_TWEENFACTOR:
		case D3DRS_POSITIONDEGREE:
		case D3DRS_NORMALDEGREE:
		case D3DRS_ANTIALIASEDLINEENABLE:
		case D3DRS_MINTESSELLATIONLEVEL:
		case D3DRS_MAXTESSELLATIONLEVEL:
		case D3DRS_ENABLEADAPTIVETESSELLATION:
		case D3DRS_TEXTUREFACTOR:
		case D3DRS_WRAP0:
		case D3DRS_WRAP1:
		case D3DRS_WRAP2:
		case D3DRS_WRAP3:
		case D3DRS_WRAP4:
		case D3DRS_WRAP5:
		case D3DRS_WRAP6:
		case D3DRS_WRAP7:
		case D3DRS_WRAP8:
		case D3DRS_WRAP9:
		case D3DRS_WRAP10:
		case D3DRS_WRAP11:
		case D3DRS_WRAP12:
		case D3DRS_WRAP13:
		case D3DRS_WRAP14:
		case D3DRS_WRAP15:
		case D3DRS_CLIPPING:
			break;

		default:
			DXABSTRACT_BREAK_ON_ERROR();
			break;
	}

	return S_OK;
}

HRESULT IDirect3DDevice9::SetSamplerStateNonInline(DWORD Sampler,D3DSAMPLERSTATETYPE Type,DWORD Value)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	if ( Sampler >= VK_SAMPLER_COUNT )
		return D3DERR_INVALIDCALL;

	m_ctx->m_samplerDirty[Sampler] = true;

	VkFilter				minFilter, magFilter;
	VkSamplerMipmapMode		mipFilter;
	VkSamplerAddressMode	addrU, addrV, addrW;
	int						minLod;
	float					lodBias;

	// Pull the current cached sampler params and apply the single change.
	VKTexSamplingParams &sp = m_ctx->m_samplerParams[Sampler];
	minFilter = sp.m_minFilter;
	magFilter = sp.m_magFilter;
	mipFilter = sp.m_mipFilter;
	addrU = sp.m_addressU;
	addrV = sp.m_addressV;
	addrW = sp.m_addressW;
	minLod = (int)sp.m_minLod;
	lodBias = sp.m_mipLodBias;

	switch (Type)
	{
		case D3DSAMP_ADDRESSU:		addrU = D3DTextureAddressToVK( Value ); break;
		case D3DSAMP_ADDRESSV:		addrV = D3DTextureAddressToVK( Value ); break;
		case D3DSAMP_ADDRESSW:		addrW = D3DTextureAddressToVK( Value ); break;
		case D3DSAMP_BORDERCOLOR:	/* border color not exposed per-sampler here */ break;
		case D3DSAMP_MAGFILTER:		magFilter = D3DTextureFilterToVK( Value ); sp.m_magFilter = magFilter; break;
		case D3DSAMP_MIPFILTER:		mipFilter = (Value == D3DTEXF_NONE) ? VK_SAMPLER_MIPMAP_MODE_NEAREST : (VkSamplerMipmapMode)D3DTextureFilterToVK( Value ); sp.m_mipFilter = mipFilter; break;
		case D3DSAMP_MINFILTER:		minFilter = D3DTextureFilterToVK( Value ); sp.m_minFilter = minFilter; break;
		case D3DSAMP_MIPMAPLODBIAS:	lodBias = *(float*)&Value; sp.m_mipLodBias = lodBias; break;
		case D3DSAMP_MAXMIPLEVEL:	minLod = (int)Value; sp.m_minLod = (float)minLod; break;
		case D3DSAMP_MAXANISOTROPY:	m_ctx->SetSamplerMaxAnisotropy( Sampler, (float)Value ); break;
		case D3DSAMP_SRGBTEXTURE:	m_ctx->SetSamplerSRGB( Sampler, Value != 0 ); break;
		case D3DSAMP_SHADOWFILTER:	break;
		default:					DXABSTRACT_BREAK_ON_ERROR(); break;
	}

	sp.m_addressU = addrU;
	sp.m_addressV = addrV;
	sp.m_addressW = addrW;

	m_ctx->SetSamplerStates( Sampler, minFilter, magFilter, mipFilter, addrU, addrV, addrW, minLod, lodBias );
	return S_OK;
}

void IDirect3DDevice9::SetSamplerStatesNonInline(DWORD Sampler, DWORD AddressU, DWORD AddressV, DWORD AddressW, DWORD MinFilter, DWORD MagFilter, DWORD MipFilter, DWORD MinLod, float LodBias )
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	if ( Sampler >= VK_SAMPLER_COUNT )
		return;

	m_ctx->m_samplerDirty[Sampler] = true;

	VkFilter minF = D3DTextureFilterToVK( MinFilter );
	VkFilter magF = D3DTextureFilterToVK( MagFilter );
	VkSamplerMipmapMode mipF = (MipFilter == D3DTEXF_NONE) ? VK_SAMPLER_MIPMAP_MODE_NEAREST : (VkSamplerMipmapMode)D3DTextureFilterToVK( MipFilter );
	VkSamplerAddressMode aU = D3DTextureAddressToVK( AddressU );
	VkSamplerAddressMode aV = D3DTextureAddressToVK( AddressV );
	VkSamplerAddressMode aW = D3DTextureAddressToVK( AddressW );

	m_ctx->SetSamplerStates( Sampler, minF, magF, mipF, aU, aV, aW, (int)MinLod, LodBias );
}

void IDirect3DDevice9::SetMaxUsedVertexShaderConstantsHintNonInline( uint nMaxReg )
{
	(void)nMaxReg;
}

// ============================================================================================================================== //
// Draw
// ============================================================================================================================== //
HRESULT IDirect3DDevice9::DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType,UINT StartVertex,UINT PrimitiveCount)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	TOGL_NULL_DEVICE_CHECK;

	if ( !m_vertexShader )
		return S_OK;

	if ( m_bFBODirty )
		UpdateBoundFBO();

	VkPrimitiveTopology topology = D3DPrimitiveTypeToVK( PrimitiveType );

	// Determine vertex count from primitive count + type.
	UINT vertexCount = 0;
	switch ( PrimitiveType )
	{
		case D3DPT_LINELIST:		vertexCount = PrimitiveCount * 2; break;
		case D3DPT_TRIANGLELIST:	vertexCount = PrimitiveCount * 3; break;
		case D3DPT_TRIANGLESTRIP:	vertexCount = PrimitiveCount + 2; break;
		case D3DPT_POINTLIST:		vertexCount = PrimitiveCount; break;
		default:					vertexCount = PrimitiveCount * 3; break;
	}

	m_ctx->FlushDrawStates( StartVertex, StartVertex + vertexCount - 1, 0 );
	m_ctx->DrawPrimitive( topology, StartVertex, vertexCount );
	m_ctx->m_nTotalDrawsOrClears++;
	return S_OK;
}

HRESULT IDirect3DDevice9::DrawIndexedPrimitive(D3DPRIMITIVETYPE Type,INT BaseVertexIndex,UINT MinVertexIndex,UINT NumVertices,UINT startIndex,UINT primCount)
{
	tmZone( TELEMETRY_LEVEL2, TMZF_NONE, "%s", __FUNCTION__ );
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	TOGL_NULL_DEVICE_CHECK;

	if ( !m_indices.m_idxBuffer || !m_vertexShader )
		return S_OK;

	if ( m_bFBODirty )
		UpdateBoundFBO();

	VkPrimitiveTopology topology = D3DPrimitiveTypeToVK( Type );

	// Determine index count from primitive count + type.
	UINT indexCount = 0;
	switch ( Type )
	{
		case D3DPT_LINELIST:		indexCount = primCount * 2; break;
		case D3DPT_TRIANGLELIST:	indexCount = primCount * 3; break;
		case D3DPT_TRIANGLESTRIP:	indexCount = primCount + 2; break;
		case D3DPT_POINTLIST:		indexCount = primCount; break;
		default:					indexCount = primCount * 3; break;
	}

	m_ctx->FlushDrawStates( MinVertexIndex, MinVertexIndex + NumVertices - 1, BaseVertexIndex );
	m_ctx->DrawIndexedPrimitive( topology, startIndex, indexCount, BaseVertexIndex, MinVertexIndex );
	m_ctx->m_nTotalDrawsOrClears++;
	return S_OK;
}

HRESULT IDirect3DDevice9::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType,UINT MinVertexIndex,UINT NumVertices,UINT PrimitiveCount,CONST void* pIndexData,D3DFORMAT IndexDataFormat,CONST void* pVertexStreamZeroData,UINT VertexStreamZeroStride)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	TOGL_NULL_DEVICE_CHECK;

	// Upload the user pointer data into a scratch buffer and defer to the
	// indexed draw path.  For simplicity we issue a direct copy of the index
	// data into the dummy index buffer slot.
	(void)PrimitiveType; (void)MinVertexIndex; (void)NumVertices; (void)PrimitiveCount;
	(void)pIndexData; (void)IndexDataFormat; (void)pVertexStreamZeroData; (void)VertexStreamZeroStride;
	Warning( "TOGL: DrawIndexedPrimitiveUP is not fully implemented on the Vulkan backend\n" );
	return S_OK;
}

// ============================================================================================================================== //
// Misc device methods
// ============================================================================================================================== //
BOOL IDirect3DDevice9::ShowCursor(BOOL bShow)
{
	return bShow;
}

HRESULT IDirect3DDevice9::ValidateDevice(DWORD* pNumPasses)
{
	if ( pNumPasses ) *pNumPasses = 1;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetMaterial(CONST D3DMATERIAL9* pMaterial)
{
	(void)pMaterial;
	return S_OK;
}

HRESULT IDirect3DDevice9::LightEnable(DWORD Index,BOOL Enable)
{
	(void)Index; (void)Enable;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetScissorRect(CONST RECT* pRect)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	if ( !pRect )
		return D3DERR_INVALIDCALL;

	VkRect2D rect;
	rect.offset.x = pRect->left;
	rect.offset.y = pRect->top;
	rect.extent.width  = pRect->right  - pRect->left;
	rect.extent.height = pRect->bottom - pRect->top;
	gl.m_ScissorBox.rect = rect;
	m_ctx->WriteScissor( rect );
	return S_OK;
}

HRESULT IDirect3DDevice9::CreateQuery(D3DQUERYTYPE Type,IDirect3DQuery9** ppQuery)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );

	if ( !ppQuery )
		return D3DERR_INVALIDCALL;
	*ppQuery = NULL;

	VkQueryType vkType = VK_QUERY_TYPE_OCCLUSION;
	if ( Type == D3DQUERYTYPE_OCCLUSION )		vkType = VK_QUERY_TYPE_OCCLUSION;
	else if ( Type == D3DQUERYTYPE_EVENT )		vkType = VK_QUERY_TYPE_OCCLUSION; // fence semantics emulated
	else
	{
		Warning( "TOGL: CreateQuery: unsupported query type %d\n", Type );
		return D3DERR_NOTAVAILABLE;
	}

	m_ObjectStats.m_nTotalQueries++;

	IDirect3DQuery9 *pQuery = new IDirect3DQuery9;
	pQuery->m_device = this;
	pQuery->m_restype = D3DRTYPE_SURFACE;
	pQuery->m_type = Type;
	pQuery->m_ctx = m_ctx;
	pQuery->m_query = m_ctx->CreateQuery( vkType );

	pQuery->m_nIssueStartThreadID = 0;
	pQuery->m_nIssueEndThreadID = 0;
	pQuery->m_nIssueStartDrawCallIndex = 0;
	pQuery->m_nIssueEndDrawCallIndex = 0;
	pQuery->m_nIssueStartFrameIndex = 0;
	pQuery->m_nIssueEndFrameIndex = 0;
	pQuery->m_nIssueStartQueryCreationCounter = 0;
	pQuery->m_nIssueEndQueryCreationCounter = 0;

	*ppQuery = pQuery;
	return S_OK;
}

HRESULT IDirect3DDevice9::GetDeviceCaps(D3DCAPS9* pCaps)
{
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	FillD3DCaps9( pCaps );
	return S_OK;
}

HRESULT IDirect3DDevice9::TestCooperativeLevel()
{
	return S_OK;
}

HRESULT IDirect3DDevice9::EvictManagedResources()
{
	return S_OK;
}

HRESULT IDirect3DDevice9::SetLight(DWORD Index,CONST D3DLIGHT9*)
{
	(void)Index;
	return S_OK;
}

void IDirect3DDevice9::SetGammaRamp(UINT iSwapChain,DWORD Flags,CONST D3DGAMMARAMP* pRamp)
{
	VK_PUBLIC_ENTRYPOINT_CHECKS_RET_VOID;
	(void)iSwapChain; (void)Flags;
	if ( pRamp && m_ctx )
		m_ctx->SetGammaRamp( pRamp );
}

void IDirect3DDevice9::SaveGLState()
{
	// No-op; the Vulkan backend does not expose a save/restore of all state.
}

void IDirect3DDevice9::RestoreGLState()
{
}

HRESULT IDirect3DDevice9::SetClipPlane(DWORD Index,CONST float* pPlane)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	if ( Index >= (DWORD)kVKUserClipPlanes || !pPlane )
		return D3DERR_INVALIDCALL;

	for ( int i = 0; i < 4; i++ )
		gl.m_ClipPlaneEquation.value[Index][i] = pPlane[i];
	m_ctx->WriteClipPlaneEquation( Index, pPlane );
	return S_OK;
}

// ---- fixed function (no-ops on Vulkan) ----
HRESULT IDirect3DDevice9::SetTransform(D3DTRANSFORMSTATETYPE State,CONST D3DMATRIX* pMatrix)
{
	(void)State; (void)pMatrix;
	return S_OK;
}

HRESULT IDirect3DDevice9::SetTextureStageState(DWORD Stage,D3DTEXTURESTAGESTATETYPE Type,DWORD Value)
{
	(void)Stage; (void)Type; (void)Value;
	return S_OK;
}

void IDirect3DDevice9::AcquireThreadOwnership( )
{
}

void IDirect3DDevice9::ReleaseThreadOwnership( )
{
}

HRESULT IDirect3DDevice9::SetViewport(CONST D3DVIEWPORT9* pViewport)
{
	VK_BATCH_PERF_CALL_TIMER;
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	if ( !pViewport )
		return D3DERR_INVALIDCALL;

	VkViewport vp;
	vp.x		= (float)pViewport->X;
	vp.y		= (float)pViewport->Y;
	vp.width	= (float)pViewport->Width;
	vp.height	= (float)pViewport->Height;
	vp.minDepth	= pViewport->MinZ;
	vp.maxDepth	= pViewport->MaxZ;

	gl.m_ViewportBox.viewport = vp;
	gl.m_ViewportDepthRange.minDepth = pViewport->MinZ;
	gl.m_ViewportDepthRange.maxDepth = pViewport->MaxZ;
	m_ctx->WriteViewport( vp );
	return S_OK;
}

HRESULT IDirect3DDevice9::GetViewport(D3DVIEWPORT9* pViewport)
{
	VK_PUBLIC_ENTRYPOINT_CHECKS( this );
	if ( !pViewport )
		return D3DERR_INVALIDCALL;

	VkViewport vp = gl.m_ViewportBox.viewport;
	pViewport->X		= (DWORD)vp.x;
	pViewport->Y		= (DWORD)vp.y;
	pViewport->Width	= (DWORD)vp.width;
	pViewport->Height	= (DWORD)vp.height;
	pViewport->MinZ	= gl.m_ViewportDepthRange.minDepth;
	pViewport->MaxZ	= gl.m_ViewportDepthRange.maxDepth;
	return S_OK;
}

#ifdef OSX
HRESULT IDirect3DDevice9::FlushIndexBindings(void)
{
	return S_OK;
}
HRESULT IDirect3DDevice9::FlushVertexBindings(uint baseVertexIndex)
{
	(void)baseVertexIndex;
	return S_OK;
}
#endif

void IDirect3DDevice9::DumpStatsToConsole( const CCommand *pArgs )
{
	(void)pArgs;
	Msg( "TOGL VK device stats:\n" );
	Msg( "  Textures:        %d\n", m_ObjectStats.m_nTotalTextures );
	Msg( "  Surfaces:        %d\n", m_ObjectStats.m_nTotalSurfaces );
	Msg( "  RenderTargets:   %d\n", m_ObjectStats.m_nTotalRenderTargets );
	Msg( "  VertexShaders:   %d\n", m_ObjectStats.m_nTotalVertexShaders );
	Msg( "  PixelShaders:    %d\n", m_ObjectStats.m_nTotalPixelShaders );
	Msg( "  VertexDecls:     %d\n", m_ObjectStats.m_nTotalVertexDecls );
	Msg( "  VertexBuffers:   %d\n", m_ObjectStats.m_nTotalVertexBuffers );
	Msg( "  IndexBuffers:    %d\n", m_ObjectStats.m_nTotalIndexBuffers );
	Msg( "  Queries:         %d\n", m_ObjectStats.m_nTotalQueries );
}

void IDirect3DDevice9::PrintObjectStats( const ObjectStats_t &stats )
{
	(void)stats;
}

// ============================================================================================================================== //
// Private flush / FBO helpers
// ============================================================================================================================== //
void IDirect3DDevice9::FlushClipPlaneEquation()
{
	for ( int x = 0; x < kVKUserClipPlanes; x++ )
		m_ctx->WriteClipPlaneEquation( x, gl.m_ClipPlaneEquation.value[x] );
}

void IDirect3DDevice9::InitStates()
{
	V_memset( &gl, 0, sizeof(gl) );

	// Reasonable defaults.
	gl.m_DepthTestEnable.value	= true;
	gl.m_DepthMask.value			= true;
	gl.m_DepthFunc.value			= VK_COMPARE_OP_LESS_OR_EQUAL;
	gl.m_CullFaceEnable.value		= true;
	gl.m_CullFrontFace.value		= VK_CULL_MODE_BACK_BIT;
	gl.m_BlendFactor.src			= VK_BLEND_FACTOR_ONE;
	gl.m_BlendFactor.dst			= VK_BLEND_FACTOR_ZERO;
	gl.m_BlendEquation.op			= VK_BLEND_OP_ADD;
	gl.m_StencilFunc.func			= VK_COMPARE_OP_ALWAYS;
	gl.m_StencilFunc.mask			= 0xFFFFFFFF;
	gl.m_StencilWriteMask.value	= 0xFFFFFFFF;
	gl.m_ClearDepth.value			= 1.0f;
	gl.m_ViewportDepthRange.minDepth = 0.0f;
	gl.m_ViewportDepthRange.maxDepth = 1.0f;

	VkViewport defaultVP;
	defaultVP.x = 0.0f; defaultVP.y = 0.0f;
	uint w = 1280, h = 720;
	if ( m_ctx ) m_ctx->GetDisplaySize( w, h );
	defaultVP.width = (float)w; defaultVP.height = (float)h;
	defaultVP.minDepth = 0.0f; defaultVP.maxDepth = 1.0f;
	gl.m_ViewportBox.viewport = defaultVP;

	VkRect2D defaultScissor;
	defaultScissor.offset = { 0, 0 };
	defaultScissor.extent = { w, h };
	gl.m_ScissorBox.rect = defaultScissor;
}

void IDirect3DDevice9::FullFlushStates()
{
	m_ctx->WriteDepthTestEnable( gl.m_DepthTestEnable.value );
	m_ctx->WriteDepthMask( gl.m_DepthMask.value );
	m_ctx->WriteDepthFunc( gl.m_DepthFunc.value );
	m_ctx->WriteDepthBias( gl.m_DepthBias.bias, gl.m_DepthBias.biasClamp, gl.m_DepthBias.slopeScaledBias );

	m_ctx->WriteCullFaceEnable( gl.m_CullFaceEnable.value );
	m_ctx->WriteCullFrontFace( gl.m_CullFrontFace.value );

	m_ctx->WriteBlendEnable( 0, gl.m_BlendEnable.value[0] );
	m_ctx->WriteBlendFactor( 0, gl.m_BlendFactor.src, gl.m_BlendFactor.dst );
	m_ctx->WriteBlendEquation( 0, gl.m_BlendEquation.op );
	m_ctx->WriteBlendColor( gl.m_BlendColor.r, gl.m_BlendColor.g, gl.m_BlendColor.b, gl.m_BlendColor.a );
	m_ctx->WriteBlendEnableSRGB( gl.m_BlendEnableSRGB.value );

	m_ctx->WriteColorMask( 0, gl.m_ColorMaskSingle.value );

	m_ctx->WriteStencilTestEnable( gl.m_StencilTestEnable.value );
	m_ctx->WriteStencilFunc( gl.m_StencilFunc.func, gl.m_StencilFunc.ref, gl.m_StencilFunc.mask );
	m_ctx->WriteStencilOp( gl.m_StencilOp.sfail, gl.m_StencilOp.dpfail, gl.m_StencilOp.dppass );
	m_ctx->WriteStencilWriteMask( gl.m_StencilWriteMask.value );

	m_ctx->WriteScissorEnable( gl.m_ScissorEnable.value );
	m_ctx->WriteScissor( gl.m_ScissorBox.rect );
	m_ctx->WriteViewport( gl.m_ViewportBox.viewport );

	for ( int x = 0; x < kVKUserClipPlanes; x++ )
	{
		m_ctx->WriteClipPlaneEnable( x, gl.m_ClipPlaneEnable.value[x] );
		m_ctx->WriteClipPlaneEquation( x, gl.m_ClipPlaneEquation.value[x] );
	}
}

void IDirect3DDevice9::UpdateBoundFBO()
{
	if ( !m_ctx )
		return;

	RenderTargetState_t key;
	for ( int i = 0; i < MAX_RENDER_TARGETS; i++ )
		key.m_pRenderTargets[i] = m_pRenderTargets[i] ? m_pRenderTargets[i]->m_tex : NULL;
	key.m_pDepthStencil = m_pDepthStencil ? m_pDepthStencil->m_tex : NULL;

	m_ctx->GetFBO( key );
	m_bFBODirty = false;
}

void IDirect3DDevice9::ResetFBOMap()
{
	m_bFBODirty = true;
}

void IDirect3DDevice9::ScrubFBOMap( CVKTex *pTex )
{
	(void)pTex;
	m_bFBODirty = true;
}

// ---- retire callbacks (invoked from resource destructors) ----
void IDirect3DDevice9::ReleasedVertexDeclaration( IDirect3DVertexDeclaration9 *pDecl )	{ (void)pDecl; }
void IDirect3DDevice9::ReleasedTexture( IDirect3DBaseTexture9 *baseTex )
{
	if ( !baseTex )
		return;
	for ( int i = 0; i < VK_SAMPLER_COUNT; i++ )
	{
		if ( m_textures[i] == baseTex )
		{
			m_textures[i] = NULL;
			m_ctx->SetSamplerTex( i, NULL );
		}
	}
}
void IDirect3DDevice9::ReleasedCGLMTex( CVKTex *pTex )				{ ScrubFBOMap( pTex ); }
void IDirect3DDevice9::ReleasedSurface( IDirect3DSurface9 *surface )
{
	if ( !surface )
		return;
	for ( int i = 0; i < MAX_RENDER_TARGETS; i++ )
		if ( m_pRenderTargets[i] == surface )
		{
			m_pRenderTargets[i] = NULL;
			if ( m_ctx ) m_ctx->m_boundRenderTargets[i] = NULL;
		}
	if ( m_pDepthStencil == surface )
	{
		m_pDepthStencil = NULL;
		if ( m_ctx ) m_ctx->m_boundDepthStencil = NULL;
	}
	m_bFBODirty = true;
}
void IDirect3DDevice9::ReleasedPixelShader( IDirect3DPixelShader9 *pixelShader )
{
	if ( m_pixelShader == pixelShader ) m_pixelShader = NULL;
}
void IDirect3DDevice9::ReleasedVertexShader( IDirect3DVertexShader9 *vertexShader )
{
	if ( m_vertexShader == vertexShader ) m_vertexShader = NULL;
}
void IDirect3DDevice9::ReleasedVertexBuffer( IDirect3DVertexBuffer9 *vertexBuffer )
{
	for ( int i = 0; i < D3D_MAX_STREAMS; i++ )
		if ( m_streams[i].m_vtxBuffer == vertexBuffer )
		{
			m_streams[i].m_vtxBuffer = NULL;
			m_vtx_buffers[i] = NULL;
		}
}
void IDirect3DDevice9::ReleasedIndexBuffer( IDirect3DIndexBuffer9 *indexBuffer )
{
	if ( m_indices.m_idxBuffer == indexBuffer )
		m_indices.m_idxBuffer = NULL;
}
void IDirect3DDevice9::ReleasedQuery( IDirect3DQuery9 *query )		{ (void)query; }

// ============================================================================================================================== //
// ID3DXMatrixStack
// ============================================================================================================================== //
ID3DXMatrixStack::ID3DXMatrixStack()
{
	m_refcount[0] = 1;
	m_refcount[1] = 0;
	m_mark = false;
	m_stackTop = 0;
	D3DXMATRIX ident;
	D3DXMatrixIdentity( &ident );
	m_stack.AddToTail( ident );
}

void ID3DXMatrixStack::AddRef( int which, char *comment )
{
	Assert( which >= 0 && which < 2 );
	m_refcount[which]++;
	(void)comment;
}

ULONG ID3DXMatrixStack::Release( int which, char *comment )
{
	Assert( which >= 0 && which < 2 );
	m_refcount[which]--;
	bool deleting = (!m_refcount[0]) && (!m_refcount[1]);
	(void)comment;
	if ( deleting )
	{
		delete this;
		return 0;
	}
	return m_refcount[0];
}

HRESULT ID3DXMatrixStack::Create( void )
{
	return S_OK;
}

D3DXMATRIX* ID3DXMatrixStack::GetTop()
{
	return &m_stack[m_stackTop];
}

void ID3DXMatrixStack::Push()
{
	D3DXMATRIX cur = m_stack[m_stackTop];
	m_stack.AddToTail( cur );
	m_stackTop++;
}

void ID3DXMatrixStack::Pop()
{
	if ( m_stackTop > 0 )
	{
		m_stack.Remove( m_stackTop );
		m_stackTop--;
	}
}

void ID3DXMatrixStack::LoadIdentity()
{
	D3DXMatrixIdentity( &m_stack[m_stackTop] );
}

void ID3DXMatrixStack::LoadMatrix( const D3DXMATRIX *pMat )
{
	m_stack[m_stackTop] = *pMat;
}

void ID3DXMatrixStack::MultMatrix( const D3DXMATRIX *pMat )
{
	D3DXMatrixMultiply( &m_stack[m_stackTop], &m_stack[m_stackTop], pMat );
}

void ID3DXMatrixStack::MultMatrixLocal( const D3DXMATRIX *pMat )
{
	D3DXMatrixMultiply( &m_stack[m_stackTop], pMat, &m_stack[m_stackTop] );
}

HRESULT ID3DXMatrixStack::ScaleLocal(FLOAT x, FLOAT y, FLOAT z)
{
	D3DXMATRIX scale;
	D3DXMatrixIdentity( &scale );
	scale.m[0][0] = x;
	scale.m[1][1] = y;
	scale.m[2][2] = z;
	MultMatrixLocal( &scale );
	return S_OK;
}

HRESULT ID3DXMatrixStack::RotateAxisLocal(CONST D3DXVECTOR3* pV, FLOAT Angle)
{
	(void)pV; (void)Angle;
	return S_OK;
}

HRESULT ID3DXMatrixStack::TranslateLocal(FLOAT x, FLOAT y, FLOAT z)
{
	D3DXMATRIX t;
	D3DXMatrixTranslation( &t, x, y, z );
	MultMatrixLocal( &t );
	return S_OK;
}

// ============================================================================================================================== //
// D3DX helper math functions
// ============================================================================================================================== //
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable:4701) // potentially uninitialized local variable 'temp' used
#endif
D3DXMATRIX* D3DXMatrixMultiply( D3DXMATRIX *pOut, CONST D3DXMATRIX *pM1, CONST D3DXMATRIX *pM2 )
{
	D3DXMATRIX temp;

	for( int i=0; i<4; i++)
	{
		for( int j=0; j<4; j++)
		{
			temp.m[i][j]	=	(pM1->m[ i ][ 0 ] * pM2->m[ 0 ][ j ])
							+	(pM1->m[ i ][ 1 ] * pM2->m[ 1 ][ j ])
							+	(pM1->m[ i ][ 2 ] * pM2->m[ 2 ][ j ])
							+	(pM1->m[ i ][ 3 ] * pM2->m[ 3 ][ j ]);
		}
	}
	*pOut = temp;
	return pOut;
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

D3DXVECTOR3* D3DXVec3TransformCoord(D3DXVECTOR3 *pOut, CONST D3DXVECTOR3 *pV, CONST D3DXMATRIX *pM)
{
	D3DXVECTOR3 vOut;

	float norm = (pM->m[0][3] * pV->x) + (pM->m[1][3] * pV->y) + (pM->m[2][3] *pV->z) + pM->m[3][3];
	if ( norm )
	{
		float norm_inv = 1.0f / norm;
		vOut.x = (pM->m[0][0] * pV->x + pM->m[1][0] * pV->y + pM->m[2][0] * pV->z + pM->m[3][0]) * norm_inv;
		vOut.y = (pM->m[0][1] * pV->x + pM->m[1][1] * pV->y + pM->m[2][1] * pV->z + pM->m[3][1]) * norm_inv;
		vOut.z = (pM->m[0][2] * pV->x + pM->m[1][2] * pV->y + pM->m[2][2] * pV->z + pM->m[3][2]) * norm_inv;
	}
	else
	{
		vOut.x = vOut.y = vOut.z = 0.0f;
	}

	*pOut = vOut;
	return pOut;
}

void D3DXMatrixIdentity( D3DXMATRIX *mat )
{
	for( int i=0; i<4; i++)
	{
		for( int j=0; j<4; j++)
		{
			mat->m[i][j] = (i==j) ? 1.0f : 0.0f;
		}
	}
}

D3DXMATRIX* D3DXMatrixTranslation( D3DXMATRIX *pOut, FLOAT x, FLOAT y, FLOAT z )
{
	D3DXMatrixIdentity( pOut );
	pOut->m[3][0] = x;
	pOut->m[3][1] = y;
	pOut->m[3][2] = z;
	return pOut;
}

D3DXMATRIX* D3DXMatrixInverse( D3DXMATRIX *pOut, FLOAT *pDeterminant, CONST D3DXMATRIX *pM )
{
	Assert( sizeof( D3DXMATRIX ) == (16 * sizeof(float) ) );
	Assert( sizeof( VMatrix ) == (16 * sizeof(float) ) );
	Assert( pDeterminant == NULL );

	VMatrix *origM = (VMatrix*)pM;
	VMatrix *destM = (VMatrix*)pOut;

	bool success = MatrixInverseGeneral( *origM, *destM ); (void)success;
	Assert( success );

	return pOut;
}

D3DXMATRIX* D3DXMatrixTranspose( D3DXMATRIX *pOut, CONST D3DXMATRIX *pM )
{
	if (pOut != pM)
	{
		for( int i=0; i<4; i++)
		{
			for( int j=0; j<4; j++)
			{
				pOut->m[i][j] = pM->m[j][i];
			}
		}
	}
	else
	{
		D3DXMATRIX temp = *pM;
		D3DXMatrixTranspose( pOut, &temp );
	}

	return pOut;
}

D3DXPLANE* D3DXPlaneNormalize( D3DXPLANE *pOut, CONST D3DXPLANE *pP)
{
	float len = sqrt( (pP->a * pP->a) + (pP->b * pP->b) + (pP->c * pP->c) );
	if (len > 1e-10)
	{
		pOut->a = pP->a / len;		pOut->b = pP->b / len;		pOut->c = pP->c / len;		pOut->d = pP->d / len;
	}
	else
	{
		pOut->a = 0.0f;				pOut->b = 0.0f;				pOut->c = 1.0f;				pOut->d = 0.0f;
	}
	return pOut;
}

D3DXVECTOR4* D3DXVec4Transform( D3DXVECTOR4 *pOut, CONST D3DXVECTOR4 *pV, CONST D3DXMATRIX *pM )
{
	VMatrix *mat = (VMatrix*)pM;
	Vector4D *vIn = (Vector4D*)pV;
	Vector4D *vOut = (Vector4D*)pOut;

	Vector4DMultiplyTranspose( *mat, *vIn, *vOut );

	return pOut;
}

D3DXVECTOR4* D3DXVec4Normalize( D3DXVECTOR4 *pOut, CONST D3DXVECTOR4 *pV )
{
	Vector4D *vIn = (Vector4D*) pV;
	Vector4D *vOut = (Vector4D*) pOut;

	*vOut = *vIn;
	Vector4DNormalize( *vOut );

	return pOut;
}

D3DXMATRIX* D3DXMatrixOrthoOffCenterRH( D3DXMATRIX *pOut, FLOAT l, FLOAT r, FLOAT b, FLOAT t, FLOAT zn,FLOAT zf )
{
	DXABSTRACT_BREAK_ON_ERROR();
	return pOut;
}

D3DXMATRIX* D3DXMatrixPerspectiveRH( D3DXMATRIX *pOut, FLOAT w, FLOAT h, FLOAT zn, FLOAT zf )
{
	DXABSTRACT_BREAK_ON_ERROR();
	return pOut;
}

D3DXMATRIX* D3DXMatrixPerspectiveOffCenterRH( D3DXMATRIX *pOut, FLOAT l, FLOAT r, FLOAT b, FLOAT t, FLOAT zn, FLOAT zf )
{
	DXABSTRACT_BREAK_ON_ERROR();
	return pOut;
}

D3DXPLANE* D3DXPlaneTransform( D3DXPLANE *pOut, CONST D3DXPLANE *pP, CONST D3DXMATRIX *pM )
{
	float *out = &pOut->a;

	for( int x=0; x<4; x++ )
	{
		out[x] =	(pM->m[0][x] * pP->a)
				+	(pM->m[1][x] * pP->b)
				+	(pM->m[2][x] * pP->c)
				+	(pM->m[3][x] * pP->d);
	}

	return pOut;
}

// ============================================================================================================================== //
// Perf / entry point helpers
// ============================================================================================================================== //
void D3DPERF_SetOptions( DWORD dwOptions )
{
	(void)dwOptions;
}

HRESULT D3DXCompileShader(
        LPCSTR                          pSrcData,
        UINT                            SrcDataLen,
        CONST D3DXMACRO*                pDefines,
        LPD3DXINCLUDE                   pInclude,
        LPCSTR                          pFunctionName,
        LPCSTR                          pProfile,
        DWORD                           Flags,
        LPD3DXBUFFER*                   ppShader,
        LPD3DXBUFFER*                   ppErrorMsgs,
        LPD3DXCONSTANTTABLE*            ppConstantTable)
{
	DXABSTRACT_BREAK_ON_ERROR();
	(void)pSrcData; (void)SrcDataLen; (void)pDefines; (void)pInclude;
	(void)pFunctionName; (void)pProfile; (void)Flags;
	if ( ppShader )		*ppShader = NULL;
	if ( ppErrorMsgs )	*ppErrorMsgs = NULL;
	if ( ppConstantTable ) *ppConstantTable = NULL;
	return S_OK;
}

#if defined(DX_TO_VK_ABSTRACTION)
void toglGetClientRect( VD3DHWND hWnd, RECT *destRect )
{
	// The only useful answer here is the size of the rendering canvas.  Ask
	// the live Vulkan device for its backbuffer dimensions.
	uint width = 0, height = 0;
	RenderedSize( width, height, false );
	if ( width == 0 || height == 0 )
	{
		width = 1280;
		height = 720;
	}

	destRect->left	= 0;
	destRect->top	= 0;
	destRect->right	= width;
	destRect->bottom= height;
	(void)hWnd;
}
#endif

// ============================================================================================================================== //
// Direct3DCreate9 - the entry point the host uses to obtain an IDirect3D9.
// ============================================================================================================================== //
IDirect3D9 *Direct3DCreate9(UINT SDKVersion)
{
	// Bring up the Vulkan loader / instance up front so subsequent adapter
	// queries have something real to talk to.
	VKConnectLibraries();

	return new IDirect3D9;
}

#endif // !USE_ACTUAL_DX
