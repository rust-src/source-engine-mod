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
// dxabstract.h
//
// D3D9 abstraction layer - Vulkan backend (adapted from the TOGL OpenGL backend).
// The D3D9 API surface is identical to the OpenGL version; only the backend
// implementation types change from GLM (OpenGL) to VK (Vulkan).
//
//==================================================================================================
#ifndef DXABSTRACT_H
#define DXABSTRACT_H

#ifdef DX_TO_VK_ABSTRACTION

#include "toglesvk/rendermechanism.h"

#include "tier0/platform.h"
#include "tier0/dbg.h"
#include "tier1/utlmap.h"

// turn this on to get refcount logging from IUnknown
#define	IUNKNOWN_ALLOC_SPEW 0
#define	IUNKNOWN_ALLOC_SPEW_MARK_ALL 0	


TOGL_INTERFACE void toglGetClientRect( VD3DHWND hWnd, RECT *destRect );


struct TOGL_CLASS IUnknown
{
	int	m_refcount[2];
	bool m_mark;
		
	IUnknown()
	{
		m_refcount[0] = 1;
		m_refcount[1] = 0;
		m_mark = (IUNKNOWN_ALLOC_SPEW_MARK_ALL != 0);	// either all are marked, or only the ones that have SetMark(true) called on them

		#if IUNKNOWN_ALLOC_SPEW
			if (m_mark)
			{
				VKMPRINTF(("-A- IUnew (%08x) refc -> (%d,%d) ",this,m_refcount[0],m_refcount[1]));
			}
		#endif
	};
				
	virtual	~IUnknown()
	{
		#if IUNKNOWN_ALLOC_SPEW
			if (m_mark)
			{
				VKMPRINTF(("-A- IUdel (%08x) ",this ));
			}
		#endif
	};
		
	void	AddRef( int which=0, char *comment = NULL )
	{
		Assert( which >= 0 );
		Assert( which < 2 );
		m_refcount[which]++;
			
		#if IUNKNOWN_ALLOC_SPEW
			if (m_mark)
			{
				VKMPRINTF(("-A- IUAddRef  (%08x,%d) refc -> (%d,%d) [%s]",this,which,m_refcount[0],m_refcount[1],comment?comment:"..."))	;
				if (!comment)
				{
					VKMPRINTF((""))	;	// place to hang a breakpoint
				}
			}
		#endif
	};
		
	ULONG __stdcall	Release( int which=0, char *comment = NULL )
	{
		Assert( which >= 0 );
		Assert( which < 2 );
			
		//int oldrefcs[2] = { m_refcount[0], m_refcount[1] };
		bool deleting = false;
			
		m_refcount[which]--;
		if ( (!m_refcount[0]) && (!m_refcount[1]) )
		{
			deleting = true;
		}
			
		#if IUNKNOWN_ALLOC_SPEW
			if (m_mark)
			{
				VKMPRINTF(("-A- IURelease (%08x,%d) refc -> (%d,%d) [%s] %s",this,which,m_refcount[0],m_refcount[1],comment?comment:"...",deleting?"->DELETING":""));
				if (!comment)
				{
					VKMPRINTF((""))	;	// place to hang a breakpoint
				}
			}
		#endif

		if (deleting)
		{
			if (m_mark)
			{
				VKMPRINTF((""))	;		// place to hang a breakpoint
			}
			delete this;
			return 0;
		}
		else
		{
			return m_refcount[0];
		}
	};
		
	void	SetMark( bool markValue, char *comment=NULL )
	{
		#if IUNKNOWN_ALLOC_SPEW
			if (!m_mark && markValue)	// leading edge detect
			{
				// print the same thing that the constructor would have printed if it had been marked from the beginning
				// i.e. it's anticipated that callers asking for marking will do so right at create time
				VKMPRINTF(("-A- IUSetMark (%08x) refc -> (%d,%d) (%s) ",this,m_refcount[0],m_refcount[1],comment?comment:"..."));
			}
		#endif

		m_mark = markValue;
	}
};

// ------------------------------------------------------------------------------------------------------------------------------ //
// INTERFACES
// ------------------------------------------------------------------------------------------------------------------------------ //

struct TOGL_CLASS IDirect3DResource9 : public IUnknown
{
	IDirect3DDevice9	*m_device;		// parent device
	D3DRESOURCETYPE		m_restype;
	
	DWORD SetPriority(DWORD PriorityNew);
};

struct TOGL_CLASS IDirect3DBaseTexture9 : public IDirect3DResource9						// "A Texture.."
{
	D3DSURFACE_DESC			m_descZero;			// desc of top level.
	CVKTex					*m_tex;				// a CVKTex can represent all forms of tex

	virtual					~IDirect3DBaseTexture9();
	D3DRESOURCETYPE	TOGLMETHODCALLTYPE GetType();
	DWORD TOGLMETHODCALLTYPE GetLevelCount();
	HRESULT TOGLMETHODCALLTYPE GetLevelDesc(UINT Level,D3DSURFACE_DESC *pDesc);
};

struct TOGL_CLASS IDirect3DTexture9 : public IDirect3DBaseTexture9							// "Texture 2D"
{	
	IDirect3DSurface9 *m_surfZero;				// surf of top level.
	virtual ~IDirect3DTexture9();
	HRESULT TOGLMETHODCALLTYPE LockRect(UINT Level,D3DLOCKED_RECT* pLockedRect,CONST RECT* pRect,DWORD Flags);
	HRESULT TOGLMETHODCALLTYPE UnlockRect(UINT Level);
	HRESULT TOGLMETHODCALLTYPE GetSurfaceLevel(UINT Level,IDirect3DSurface9** ppSurfaceLevel);
};

struct TOGL_CLASS IDirect3DCubeTexture9 : public IDirect3DBaseTexture9						// "Texture Cube Map"
{
	IDirect3DSurface9		*m_surfZero[6];			// surfs of top level.
	virtual ~IDirect3DCubeTexture9();
	HRESULT TOGLMETHODCALLTYPE GetCubeMapSurface(D3DCUBEMAP_FACES FaceType,UINT Level,IDirect3DSurface9** ppCubeMapSurface);
	HRESULT TOGLMETHODCALLTYPE GetLevelDesc(UINT Level,D3DSURFACE_DESC *pDesc);
};

struct TOGL_CLASS IDirect3DVolumeTexture9 : public IDirect3DBaseTexture9					// "Texture 3D"
{
	IDirect3DSurface9		*m_surfZero;			// surf of top level.
	D3DVOLUME_DESC			m_volDescZero;			// volume desc top level
	virtual ~IDirect3DVolumeTexture9();
	HRESULT TOGLMETHODCALLTYPE LockBox(UINT Level,D3DLOCKED_BOX* pLockedVolume,CONST D3DBOX* pBox,DWORD Flags);
	HRESULT TOGLMETHODCALLTYPE UnlockBox(UINT Level);
	HRESULT	TOGLMETHODCALLTYPE GetLevelDesc( UINT level, D3DVOLUME_DESC *pDesc );
};


// for the moment, a "D3D surface" is modeled as a VK tex, a face, and a mip.
// no Create method, these are filled in by the various create surface methods.	

struct TOGL_CLASS IDirect3DSurface9 : public IDirect3DResource9
{
	virtual ~IDirect3DSurface9();
	HRESULT TOGLMETHODCALLTYPE LockRect(D3DLOCKED_RECT* pLockedRect,CONST RECT* pRect,DWORD Flags);
	HRESULT TOGLMETHODCALLTYPE UnlockRect();
	 HRESULT TOGLMETHODCALLTYPE GetDesc(D3DSURFACE_DESC *pDesc);

	D3DSURFACE_DESC			m_desc;
	CVKTex					*m_tex;
	int						m_face;
	int						m_mip;
};

struct TOGL_CLASS IDirect3D9 : public IUnknown
{
	virtual	~IDirect3D9();

	UINT	TOGLMETHODCALLTYPE GetAdapterCount();

	HRESULT TOGLMETHODCALLTYPE GetDeviceCaps				(UINT Adapter,D3DDEVTYPE DeviceType,D3DCAPS9* pCaps);
	HRESULT TOGLMETHODCALLTYPE GetAdapterIdentifier			(UINT Adapter,DWORD Flags,D3DADAPTER_IDENTIFIER9* pIdentifier);
	HRESULT TOGLMETHODCALLTYPE CheckDeviceFormat			(UINT Adapter,D3DDEVTYPE DeviceType,D3DFORMAT AdapterFormat,DWORD Usage,D3DRESOURCETYPE RType,D3DFORMAT CheckFormat);
	UINT	TOGLMETHODCALLTYPE GetAdapterModeCount			(UINT Adapter,D3DFORMAT Format);
	HRESULT TOGLMETHODCALLTYPE EnumAdapterModes				(UINT Adapter,D3DFORMAT Format,UINT Mode,D3DDISPLAYMODE* pMode);
	HRESULT TOGLMETHODCALLTYPE CheckDeviceType				(UINT Adapter,D3DDEVTYPE DevType,D3DFORMAT AdapterFormat,D3DFORMAT BackBufferFormat,BOOL bWindowed);
	HRESULT TOGLMETHODCALLTYPE GetAdapterDisplayMode		(UINT Adapter,D3DDISPLAYMODE* pMode);
	HRESULT TOGLMETHODCALLTYPE CheckDepthStencilMatch		(UINT Adapter,D3DDEVTYPE DeviceType,D3DFORMAT AdapterFormat,D3DFORMAT RenderTargetFormat,D3DFORMAT DepthStencilFormat);
	HRESULT TOGLMETHODCALLTYPE CheckDeviceMultiSampleType	(UINT Adapter,D3DDEVTYPE DeviceType,D3DFORMAT SurfaceFormat,BOOL Windowed,D3DMULTISAMPLE_TYPE MultiSampleType,DWORD* pQualityLevels);

	HRESULT TOGLMETHODCALLTYPE CreateDevice					(UINT Adapter,D3DDEVTYPE DeviceType,VD3DHWND hFocusWindow,DWORD BehaviorFlags,D3DPRESENT_PARAMETERS* pPresentationParameters,IDirect3DDevice9** ppReturnedDeviceInterface);
};

struct TOGL_CLASS IDirect3DVertexDeclaration9 : public IUnknown
{
	IDirect3DDevice9		*m_device;
	uint					m_elemCount;
	D3DVERTEXELEMENT9_GL	m_elements[ MAX_D3DVERTEXELEMENTS ];
		
	uint8					m_VertexAttribDescToStreamIndex[256];
				
	virtual					~IDirect3DVertexDeclaration9();
};

struct TOGL_CLASS IDirect3DQuery9 : public IDirect3DResource9	//was IUnknown
{
	D3DQUERYTYPE			m_type;		// D3DQUERYTYPE_OCCLUSION or D3DQUERYTYPE_EVENT
	CVKContext				*m_ctx;
	CVKQuery				*m_query;
	
	uint					m_nIssueStartThreadID, m_nIssueEndThreadID;
	uint					m_nIssueStartDrawCallIndex, m_nIssueEndDrawCallIndex;
	uint					m_nIssueStartFrameIndex, m_nIssueEndFrameIndex;
	uint					m_nIssueStartQueryCreationCounter, m_nIssueEndQueryCreationCounter;
			
	virtual					~IDirect3DQuery9();

    HRESULT					Issue(DWORD dwIssueFlags);
    HRESULT					GetData(void* pData,DWORD dwSize,DWORD dwGetDataFlags);
};

struct TOGL_CLASS IDirect3DVertexBuffer9 : public IDirect3DResource9	//was IUnknown
{
	CVKContext				*m_ctx;
	CVKBuffer				*m_vtxBuffer;
	D3DVERTEXBUFFER_DESC	m_vtxDesc;		// to satisfy GetDesc
		
	virtual					~IDirect3DVertexBuffer9();
    HRESULT					Lock(UINT OffsetToLock,UINT SizeToLock,void** ppbData,DWORD Flags);
    HRESULT					Unlock();
	void					UnlockActualSize( uint nActualSize, const void *pActualData = NULL );
};

struct TOGL_CLASS IDirect3DIndexBuffer9 : public IDirect3DResource9	//was IUnknown
{
	CVKContext				*m_ctx;
	CVKBuffer				*m_idxBuffer;
	D3DINDEXBUFFER_DESC		m_idxDesc;		// to satisfy GetDesc
		
	virtual					~IDirect3DIndexBuffer9();

    HRESULT					Lock(UINT OffsetToLock,UINT SizeToLock,void** ppbData,DWORD Flags);
    HRESULT					Unlock();
	void					UnlockActualSize( uint nActualSize, const void *pActualData = NULL );

    HRESULT					GetDesc(D3DINDEXBUFFER_DESC *pDesc);
};

struct TOGL_CLASS IDirect3DPixelShader9 : public IDirect3DResource9	//was IUnknown
{
	CVKProgram				*m_pixProgram;
	uint					m_pixHighWater;		// count of active constant slots referenced by shader.
	uint					m_pixSamplerMask;	// (1<<n) mask of samplers referemnced by this pixel shader
												// this can help FlushSamplers avoid SRGB flipping on textures not being referenced...
	uint					m_pixSamplerTypes;  // SAMPLER_TYPE_2D, etc.
	uint					m_pixFragDataMask;  // (1<<n) mask of gl_FragData[n] referenced by this pixel shader
	
	virtual					~IDirect3DPixelShader9();
};

struct TOGL_CLASS IDirect3DVertexShader9 : public IDirect3DResource9	//was IUnknown
{
	CVKProgram				*m_vtxProgram;
	uint					m_vtxHighWater;		// count of active constant slots referenced by shader.
	uint					m_vtxHighWaterBone;
	unsigned char			m_vtxAttribMap[16];	// high nibble is usage, low nibble is usageindex, array position is attrib number
	uint					m_maxVertexAttrs;
	
	virtual					~IDirect3DVertexShader9();
};

#ifdef _MSC_VER
	typedef class TOGL_CLASS CUtlMemory<D3DMATRIX> CD3DMATRIXAllocator;
	typedef class TOGL_CLASS CUtlVector<D3DMATRIX, CD3DMATRIXAllocator> CD3DMATRIXStack;
#else
	typedef class CUtlMemory<D3DMATRIX> CD3DMATRIXAllocator;
	typedef class CUtlVector<D3DMATRIX, CD3DMATRIXAllocator> CD3DMATRIXStack;
#endif

struct TOGL_CLASS ID3DXMatrixStack //: public IUnknown
{
	int	m_refcount[2];
	bool m_mark;
	CD3DMATRIXStack	m_stack;
	int						m_stackTop;	// top of stack is at the highest index, this is that index.  push increases, pop decreases.

	ID3DXMatrixStack();
	void  AddRef( int which=0, char *comment = NULL );
	ULONG Release( int which=0, char *comment = NULL );
	
	HRESULT	Create( void );
	
	D3DXMATRIX* GetTop();
	void Push();
	void Pop();
	void LoadIdentity();
	void LoadMatrix( const D3DXMATRIX *pMat );
	void MultMatrix( const D3DXMATRIX *pMat );
	void MultMatrixLocal( const D3DXMATRIX *pMat );
	HRESULT ScaleLocal(FLOAT x, FLOAT y, FLOAT z);

	// Left multiply the current matrix with the computed rotation
	// matrix, counterclockwise about the given axis with the given angle.
	// (rotation is about the local origin of the object)
	HRESULT RotateAxisLocal(CONST D3DXVECTOR3* pV, FLOAT Angle);

	// Left multiply the current matrix with the computed translation
	// matrix. (transformation is about the local origin of the object)
	HRESULT TranslateLocal(FLOAT x, FLOAT y, FLOAT z);
};

typedef ID3DXMatrixStack* LPD3DXMATRIXSTACK;

struct RenderTargetState_t
{
	void clear() { V_memset( this, 0, sizeof( *this ) ); }

	CVKTex *m_pRenderTargets[4];
	CVKTex *m_pDepthStencil;

	inline bool RefersTo( CVKTex * pSurf ) const
	{
		for ( uint i = 0; i < 4; i++ )
			if ( m_pRenderTargets[i] == pSurf )
				return true;

		if ( m_pDepthStencil == pSurf )
			return true;

		return false;
	}

	static inline bool LessFunc( const RenderTargetState_t &lhs, const RenderTargetState_t &rhs ) 
	{
		COMPILE_TIME_ASSERT( sizeof( lhs.m_pRenderTargets[0] ) == sizeof( uintp ) );
		uint64 lhs0 = reinterpret_cast<const uint64 *>(lhs.m_pRenderTargets)[0];
		uint64 rhs0 = reinterpret_cast<const uint64 *>(rhs.m_pRenderTargets)[0];
		if ( lhs0 < rhs0 )
			return true;
		else if ( lhs0 == rhs0 )
		{
			uint64 lhs1 = reinterpret_cast<const uint64 *>(lhs.m_pRenderTargets)[1];
			uint64 rhs1 = reinterpret_cast<const uint64 *>(rhs.m_pRenderTargets)[1];
			if ( lhs1 < rhs1 )
				return true;
			else if ( lhs1 == rhs1 )
			{
				return lhs.m_pDepthStencil < rhs.m_pDepthStencil;
			}
		}
		return false;
	}

	inline bool operator < ( const RenderTargetState_t &rhs ) const
	{
		return LessFunc( *this, rhs );
	}
};

typedef CUtlMap< RenderTargetState_t, CVKFramebuffer *> CVKFramebufferMap;

class simple_bitmap;

struct TOGL_CLASS IDirect3DDevice9 : public IUnknown
{
	friend class CVKContext;
	friend struct IDirect3DBaseTexture9;
	friend struct IDirect3DTexture9;
	friend struct IDirect3DCubeTexture9;
	friend struct IDirect3DVolumeTexture9;
	friend struct IDirect3DSurface9;
	friend struct IDirect3DVertexBuffer9;
	friend struct IDirect3DIndexBuffer9;
	friend struct IDirect3DPixelShader9;
	friend struct IDirect3DVertexShader9;
	friend struct IDirect3DQuery9;
	friend struct IDirect3DVertexDeclaration9;

	IDirect3DDevice9();
	virtual	~IDirect3DDevice9();
	
	// Create call invoked from IDirect3D9
	HRESULT	TOGLMETHODCALLTYPE Create( IDirect3DDevice9Params *params );
	
	//
	// Basics
	//
	HRESULT TOGLMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* pPresentationParameters);
	HRESULT TOGLMETHODCALLTYPE SetViewport(CONST D3DVIEWPORT9* pViewport);
	HRESULT TOGLMETHODCALLTYPE GetViewport(D3DVIEWPORT9* pViewport);
    HRESULT TOGLMETHODCALLTYPE BeginScene();
	HRESULT TOGLMETHODCALLTYPE Clear(DWORD Count,CONST D3DRECT* pRects,DWORD Flags,D3DCOLOR Color,float Z,DWORD Stencil);
    HRESULT TOGLMETHODCALLTYPE EndScene();
    HRESULT TOGLMETHODCALLTYPE Present(CONST RECT* pSourceRect,CONST RECT* pDestRect,VD3DHWND hDestWindowOverride,CONST RGNDATA* pDirtyRegion);

	// textures
	HRESULT TOGLMETHODCALLTYPE CreateTexture(UINT Width,UINT Height,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DTexture9** ppTexture,VD3DHANDLE* pSharedHandle, char *debugLabel=NULL);
    HRESULT TOGLMETHODCALLTYPE CreateCubeTexture(UINT EdgeLength,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DCubeTexture9** ppCubeTexture,VD3DHANDLE* pSharedHandle, char *debugLabel=NULL);
    HRESULT TOGLMETHODCALLTYPE CreateVolumeTexture(UINT Width,UINT Height,UINT Depth,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DVolumeTexture9** ppVolumeTexture,VD3DHANDLE* pSharedHandle, char *debugLabel=NULL);
	
	FORCEINLINE HRESULT TOGLMETHODCALLTYPE SetTexture(DWORD Stage,IDirect3DBaseTexture9* pTexture);
	HRESULT TOGLMETHODCALLTYPE SetTextureNonInline(DWORD Stage,IDirect3DBaseTexture9* pTexture);

    HRESULT TOGLMETHODCALLTYPE GetTexture(DWORD Stage,IDirect3DBaseTexture9** ppTexture);

	// render targets, color and depthstencil, surfaces, blit
    HRESULT TOGLMETHODCALLTYPE CreateRenderTarget(UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,DWORD MultisampleQuality,BOOL Lockable,IDirect3DSurface9** ppSurface,VD3DHANDLE* pSharedHandle, char *debugLabel=NULL);
    HRESULT TOGLMETHODCALLTYPE SetRenderTarget(DWORD RenderTargetIndex,IDirect3DSurface9* pRenderTarget);
    HRESULT TOGLMETHODCALLTYPE GetRenderTarget(DWORD RenderTargetIndex,IDirect3DSurface9** ppRenderTarget);

    HRESULT TOGLMETHODCALLTYPE CreateOffscreenPlainSurface(UINT Width,UINT Height,D3DFORMAT Format,D3DPOOL Pool,IDirect3DSurface9** ppSurface,VD3DHANDLE* pSharedHandle);

    HRESULT TOGLMETHODCALLTYPE CreateDepthStencilSurface(UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,DWORD MultisampleQuality,BOOL Discard,IDirect3DSurface9** ppSurface,VD3DHANDLE* pSharedHandle);
    HRESULT TOGLMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* pNewZStencil);
    HRESULT TOGLMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface);

	HRESULT TOGLMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9* pRenderTarget,IDirect3DSurface9* pDestSurface);	// ? is anyone using this ?
    HRESULT TOGLMETHODCALLTYPE GetFrontBufferData(UINT iSwapChain,IDirect3DSurface9* pDestSurface);
    HRESULT TOGLMETHODCALLTYPE StretchRect(IDirect3DSurface9* pSourceSurface,CONST RECT* pSourceRect,IDirect3DSurface9* pDestSurface,CONST RECT* pDestRect,D3DTEXTUREFILTERTYPE Filter);

	// pixel shaders
    HRESULT TOGLMETHODCALLTYPE CreatePixelShader(CONST DWORD* pFunction,IDirect3DPixelShader9** ppShader, const char *pShaderName, char *debugLabel = NULL, const uint32 *pCentroidMask = NULL );

	FORCEINLINE HRESULT TOGLMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* pShader);
	HRESULT TOGLMETHODCALLTYPE SetPixelShaderNonInline(IDirect3DPixelShader9* pShader);
    
	FORCEINLINE HRESULT TOGLMETHODCALLTYPE SetPixelShaderConstantF(UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount);
	HRESULT TOGLMETHODCALLTYPE SetPixelShaderConstantFNonInline(UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount);

    HRESULT TOGLMETHODCALLTYPE SetPixelShaderConstantB(UINT StartRegister,CONST BOOL* pConstantData,UINT  BoolCount);
    HRESULT TOGLMETHODCALLTYPE SetPixelShaderConstantI(UINT StartRegister,CONST int* pConstantData,UINT Vector4iCount);

	// vertex shaders
    HRESULT TOGLMETHODCALLTYPE CreateVertexShader(CONST DWORD* pFunction,IDirect3DVertexShader9** ppShader, const char *pShaderName, char *debugLabel = NULL);

	FORCEINLINE HRESULT TOGLMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* pShader);
    HRESULT TOGLMETHODCALLTYPE SetVertexShaderNonInline(IDirect3DVertexShader9* pShader);
    
	FORCEINLINE HRESULT TOGLMETHODCALLTYPE SetVertexShaderConstantF(UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount);
	HRESULT TOGLMETHODCALLTYPE SetVertexShaderConstantFNonInline(UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount);

    FORCEINLINE HRESULT TOGLMETHODCALLTYPE SetVertexShaderConstantB(UINT StartRegister,CONST BOOL* pConstantData,UINT  BoolCount);
	HRESULT TOGLMETHODCALLTYPE SetVertexShaderConstantBNonInline(UINT StartRegister,CONST BOOL* pConstantData,UINT  BoolCount);

	FORCEINLINE HRESULT TOGLMETHODCALLTYPE SetVertexShaderConstantI(UINT StartRegister,CONST int* pConstantData,UINT Vector4iCount);
    HRESULT TOGLMETHODCALLTYPE SetVertexShaderConstantINonInline(UINT StartRegister,CONST int* pConstantData,UINT Vector4iCount);

	// POSIX only - preheating for a specific vertex/pixel shader pair - trigger GLSL link inside GLM
	HRESULT TOGLMETHODCALLTYPE LinkShaderPair( IDirect3DVertexShader9* vs, IDirect3DPixelShader9* ps );
	HRESULT TOGLMETHODCALLTYPE ValidateShaderPair( IDirect3DVertexShader9* vs, IDirect3DPixelShader9* ps );
	HRESULT TOGLMETHODCALLTYPE QueryShaderPair( int index, VKShaderPairInfo *infoOut );
	
	// vertex buffers
    HRESULT TOGLMETHODCALLTYPE CreateVertexDeclaration(CONST D3DVERTEXELEMENT9* pVertexElements,IDirect3DVertexDeclaration9** ppDecl);
	
	FORCEINLINE HRESULT TOGLMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl);
	HRESULT TOGLMETHODCALLTYPE SetVertexDeclarationNonInline(IDirect3DVertexDeclaration9* pDecl);

    HRESULT TOGLMETHODCALLTYPE SetFVF(DWORD FVF);		// we might not be using these ?
	HRESULT TOGLMETHODCALLTYPE GetFVF(DWORD* pFVF);

    HRESULT CreateVertexBuffer(UINT Length,DWORD Usage,DWORD FVF,D3DPOOL Pool,IDirect3DVertexBuffer9** ppVertexBuffer,VD3DHANDLE* pSharedHandle);
    
	FORCEINLINE HRESULT TOGLMETHODCALLTYPE SetStreamSource(UINT StreamNumber,IDirect3DVertexBuffer9* pStreamData,UINT OffsetInBytes,UINT Stride);
	HRESULT SetStreamSourceNonInline(UINT StreamNumber,IDirect3DVertexBuffer9* pStreamData,UINT OffsetInBytes,UINT Stride);
		
	// index buffers
    HRESULT TOGLMETHODCALLTYPE CreateIndexBuffer(UINT Length,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DIndexBuffer9** ppIndexBuffer,VD3DHANDLE* pSharedHandle);
    
	FORCEINLINE HRESULT TOGLMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* pIndexData);
	HRESULT TOGLMETHODCALLTYPE SetIndicesNonInline(IDirect3DIndexBuffer9* pIndexData);

	// State management.
    FORCEINLINE HRESULT TOGLMETHODCALLTYPE SetRenderStateInline(D3DRENDERSTATETYPE State,DWORD Value);
	FORCEINLINE HRESULT TOGLMETHODCALLTYPE SetRenderStateConstInline(D3DRENDERSTATETYPE State,DWORD Value);
	HRESULT TOGLMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE State,DWORD Value);

    FORCEINLINE HRESULT TOGLMETHODCALLTYPE SetSamplerState(DWORD Sampler,D3DSAMPLERSTATETYPE Type,DWORD Value);
	HRESULT TOGLMETHODCALLTYPE SetSamplerStateNonInline(DWORD Sampler,D3DSAMPLERSTATETYPE Type,DWORD Value);

	FORCEINLINE void TOGLMETHODCALLTYPE SetSamplerStates(DWORD Sampler, DWORD AddressU, DWORD AddressV, DWORD AddressW, DWORD MinFilter, DWORD MagFilter, DWORD MipFilter, DWORD MinLod, float LodBias );
	void TOGLMETHODCALLTYPE SetSamplerStatesNonInline(DWORD Sampler, DWORD AddressU, DWORD AddressV, DWORD AddressW, DWORD MinFilter, DWORD MagFilter, DWORD MipFilter, DWORD MinLod, float LodBias );
			
#ifdef OSX
	// required for 10.6 support
	HRESULT TOGLMETHODCALLTYPE FlushIndexBindings(void);		// push index buffer (set index ptr)
	HRESULT	TOGLMETHODCALLTYPE FlushVertexBindings(uint baseVertexIndex);	// push vertex streams (set attrib ptrs)
#endif

	// Draw.
    HRESULT TOGLMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType,UINT StartVertex,UINT PrimitiveCount);
    HRESULT TOGLMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType,INT BaseVertexIndex,UINT MinVertexIndex,UINT NumVertices,UINT startIndex,UINT primCount);
	HRESULT TOGLMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType,UINT MinVertexIndex,UINT NumVertices,UINT PrimitiveCount,CONST void* pIndexData,D3DFORMAT IndexDataFormat,CONST void* pVertexStreamZeroData,UINT VertexStreamZeroStride);

	// misc
    BOOL TOGLMETHODCALLTYPE ShowCursor(BOOL bShow);
    HRESULT TOGLMETHODCALLTYPE ValidateDevice(DWORD* pNumPasses);
    HRESULT TOGLMETHODCALLTYPE SetMaterial(CONST D3DMATERIAL9* pMaterial);
    HRESULT TOGLMETHODCALLTYPE LightEnable(DWORD Index,BOOL Enable);
    HRESULT TOGLMETHODCALLTYPE SetScissorRect(CONST RECT* pRect);
	HRESULT TOGLMETHODCALLTYPE CreateQuery(D3DQUERYTYPE Type,IDirect3DQuery9** ppQuery);
    HRESULT TOGLMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* pCaps);
    HRESULT TOGLMETHODCALLTYPE TestCooperativeLevel();
    HRESULT TOGLMETHODCALLTYPE EvictManagedResources();
    HRESULT TOGLMETHODCALLTYPE SetLight(DWORD Index,CONST D3DLIGHT9*);
    void TOGLMETHODCALLTYPE SetGammaRamp(UINT iSwapChain,DWORD Flags,CONST D3DGAMMARAMP* pRamp);
	

	void TOGLMETHODCALLTYPE SaveGLState();
	void TOGLMETHODCALLTYPE RestoreGLState();

	// Talk to JasonM about this one. It's tricky in GL.
    HRESULT TOGLMETHODCALLTYPE SetClipPlane(DWORD Index,CONST float* pPlane);

	//
	//
	// **** FIXED FUNCTION STUFF - None of this stuff needs support in GL.
	//
	//
    HRESULT TOGLMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE State,CONST D3DMATRIX* pMatrix);
    HRESULT TOGLMETHODCALLTYPE SetTextureStageState(DWORD Stage,D3DTEXTURESTAGESTATETYPE Type,DWORD Value);

	void TOGLMETHODCALLTYPE AcquireThreadOwnership( );
	void TOGLMETHODCALLTYPE ReleaseThreadOwnership( );
	inline uintp TOGLMETHODCALLTYPE GetCurrentOwnerThreadId() const { return m_ctx->m_nCurOwnerThreadId; }

	FORCEINLINE void TOGLMETHODCALLTYPE SetMaxUsedVertexShaderConstantsHint( uint nMaxReg );
	void TOGLMETHODCALLTYPE SetMaxUsedVertexShaderConstantsHintNonInline( uint nMaxReg );

	void DumpStatsToConsole( const CCommand *pArgs );

#if GLMDEBUG
	void DumpTextures( const CCommand *pArgs );
#endif
		
private:
	IDirect3DDevice9( const IDirect3DDevice9& );
	IDirect3DDevice9& operator= ( const IDirect3DDevice9& );
	// Flushing changes to GL
	void FlushClipPlaneEquation();
	void InitStates();
	void FullFlushStates();
	void UpdateBoundFBO();
	void ResetFBOMap();
	void ScrubFBOMap( CVKTex *pTex );
	
	// response to retired objects (when refcount goes to zero and they self-delete..)
	void ReleasedVertexDeclaration( IDirect3DVertexDeclaration9 *pDecl );
	void ReleasedTexture( IDirect3DBaseTexture9 *baseTex );			// called from texture destructor - need to scrub samplers	
	void ReleasedCGLMTex( CVKTex *pTex );
	void ReleasedSurface( IDirect3DSurface9 *surface );				// called from any surface destructor - need to scrub RT table if an RT
	void ReleasedPixelShader( IDirect3DPixelShader9 *pixelShader );		// called from IDirect3DPixelShader9 destructor
	void ReleasedVertexShader( IDirect3DVertexShader9 *vertexShader );	// called from IDirect3DVertexShader9 destructor
	void ReleasedVertexBuffer( IDirect3DVertexBuffer9 *vertexBuffer );	// called from IDirect3DVertexBuffer9 destructor
	void ReleasedIndexBuffer( IDirect3DIndexBuffer9 *indexBuffer );		// called from IDirect3DIndexBuffer9 destructor
	void ReleasedQuery( IDirect3DQuery9 *query );					// called from IDirect3DQuery9 destructor
			
	// Member variables

	DWORD						m_nValidMarker;
public:
	IDirect3DDevice9Params	m_params;						// mirror of the creation inputs
private:

	// D3D flavor stuff
	IDirect3DSurface9			*m_pRenderTargets[4];
	IDirect3DSurface9			*m_pDepthStencil;

	IDirect3DSurface9			*m_pDefaultColorSurface;			// default color surface.
	IDirect3DSurface9			*m_pDefaultDepthStencilSurface;	// queried by GetDepthStencilSurface.

	IDirect3DVertexDeclaration9	*m_pVertDecl;					// Set by SetVertexDeclaration...
	D3DStreamDesc				m_streams[ D3D_MAX_STREAMS ];	// Set by SetStreamSource..
	CVKBuffer					*m_vtx_buffers[ D3D_MAX_STREAMS ];
	CVKBuffer					*m_pDummy_vtx_buffer;
	D3DIndexDesc				m_indices;						// Set by SetIndices..

	IDirect3DVertexShader9		*m_vertexShader;				// Set by SetVertexShader...
	IDirect3DPixelShader9		*m_pixelShader;					// Set by SetPixelShader...

	IDirect3DBaseTexture9		*m_textures[GLM_SAMPLER_COUNT];				// set by SetTexture... NULL if stage inactive
	
	// GLM flavor stuff
	CVKContext					*m_ctx;
	CVKFramebufferMap					*m_pFBOs;
	bool						m_bFBODirty;

	struct ObjectStats_t
	{
		int						m_nTotalFBOs;
		int						m_nTotalVertexShaders;
		int						m_nTotalPixelShaders;
		int						m_nTotalVertexDecls;
		int						m_nTotalIndexBuffers;
		int						m_nTotalVertexBuffers;
		int						m_nTotalRenderTargets;
		int						m_nTotalTextures;
		int						m_nTotalSurfaces;
		int						m_nTotalQueries;

		void clear() { V_memset( this, 0, sizeof(* this ) ); }

		ObjectStats_t &operator -= ( const ObjectStats_t &rhs ) 
		{
			m_nTotalFBOs -= rhs.m_nTotalFBOs;
			m_nTotalVertexShaders -= rhs.m_nTotalVertexShaders;
			m_nTotalPixelShaders -= rhs.m_nTotalPixelShaders;
			m_nTotalVertexDecls -= rhs.m_nTotalVertexDecls;
			m_nTotalIndexBuffers -= rhs.m_nTotalIndexBuffers;
			m_nTotalVertexBuffers -= rhs.m_nTotalVertexBuffers;
			m_nTotalRenderTargets -= rhs.m_nTotalRenderTargets;
			m_nTotalTextures -= rhs.m_nTotalTextures;
			m_nTotalSurfaces -= rhs.m_nTotalSurfaces;
			m_nTotalQueries -= m_nTotalQueries;
			return *this;
		}
	};
	ObjectStats_t					m_ObjectStats;
	ObjectStats_t					m_PrevObjectStats;
	void PrintObjectStats( const ObjectStats_t &stats );
	
	// GL state 
	struct 
	{
		// render state buckets
		VKAlphaTestEnable_t			m_AlphaTestEnable;
		VkCompareOp					m_AlphaTestFunc;

		bool						m_AlphaToCoverageEnable;

		VKDepthTestEnable_t			m_DepthTestEnable;
		VKDepthMask_t				m_DepthMask;
		VKDepthFunc_t				m_DepthFunc;

		VKClipPlaneEnable_t			m_ClipPlaneEnable[kVKUserClipPlanes];
		VKClipPlaneEquation_t		m_ClipPlaneEquation[kVKUserClipPlanes];

		VKColorMaskSingle_t			m_ColorMaskSingle;
		VKColorMaskMultiple_t		m_ColorMaskMultiple;

		VKCullFaceEnable_t			m_CullFaceEnable;
		VKCullFrontFace_t			m_CullFrontFace;
		VKPolygonMode_t				m_PolygonMode;
		VKDepthBias_t				m_DepthBias;
		VKScissorEnable_t			m_ScissorEnable;
		VKScissorBox_t				m_ScissorBox;
		VKViewportBox_t				m_ViewportBox;
		VKViewportDepthRange_t		m_ViewportDepthRange;

		VKBlendEnable_t				m_BlendEnable;
		VKBlendFactor_t				m_BlendFactor;
		VKBlendEquation_t			m_BlendEquation;
		VKBlendColor_t				m_BlendColor;
		VKBlendEnableSRGB_t			m_BlendEnableSRGB;

		VKStencilTestEnable_t		m_StencilTestEnable;
		VKStencilFunc_t				m_StencilFunc;
		VKStencilOp_t				m_StencilOp;
		VKStencilWriteMask_t		m_StencilWriteMask;

		VKClearColor_t				m_ClearColor;
		VKClearDepth_t				m_ClearDepth;
		VKClearStencil_t			m_ClearStencil;

		bool						m_FogEnable;			// not really pushed to GL, just latched here

		// samplers
		//GLMTexSamplingParams		m_samplers[GLM_SAMPLER_COUNT];
	} gl;
	
#if VK_BATCH_PERF_ANALYSIS
	simple_bitmap *m_pBatch_vis_bitmap;
	uint m_nBatchVisY;
	uint m_nBatchVisFrameIndex, m_nBatchVisFileIdx;
	uint m_nNumProgramChanges;
		
	uint m_nTotalD3DCalls;
	double m_flTotalD3DTime;
	uint m_nTotalGLCalls;
	double m_flTotalGLTime;
	uint m_nTotalPrims;
		
	uint m_nOverallProgramChanges;
	uint m_nOverallDraws;
	uint m_nOverallPrims;
	uint m_nOverallD3DCalls;
	double m_flOverallD3DTime;
	uint m_nOverallGLCalls;
	double m_flOverallGLTime;

	double m_flOverallPresentTime;
	double m_flOverallPresentTimeSquared;
	double m_flOverallSwapWindowTime;
	double m_flOverallSwapWindowTimeSquared;
	uint m_nOverallPresents;
#endif
};

FORCEINLINE HRESULT TOGLMETHODCALLTYPE IDirect3DDevice9::SetSamplerState( DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value )
{
	// Vulkan backend: always use non-inline path
	return SetSamplerStateNonInline( Sampler, Type, Value );
#if 0 // GL inline path - disabled for Vulkan
#if GLMDEBUG || VK_BATCH_PERF_ANALYSIS
	return SetSamplerStateNonInline( Sampler, Type, Value );
#else
	Assert( GetCurrentOwnerThreadId() == ThreadGetCurrentId() );
	Assert( Sampler < GLM_SAMPLER_COUNT );
	
	m_ctx->SetSamplerDirty( Sampler );

	switch( Type )
	{
		case D3DSAMP_ADDRESSU:
			m_ctx->SetSamplerAddressU( Sampler, Value );
			break;
		case D3DSAMP_ADDRESSV:
			m_ctx->SetSamplerAddressV( Sampler, Value );
			break;
		case D3DSAMP_ADDRESSW:
			m_ctx->SetSamplerAddressW( Sampler, Value );
			break;
		case D3DSAMP_BORDERCOLOR:
			m_ctx->SetSamplerBorderColor( Sampler, Value );
			break;
		case D3DSAMP_MAGFILTER:
			m_ctx->SetSamplerMagFilter( Sampler, Value );
			break;
		case D3DSAMP_MIPFILTER:	
			m_ctx->SetSamplerMipFilter( Sampler, Value );
			break;
		case D3DSAMP_MINFILTER:	
			m_ctx->SetSamplerMinFilter( Sampler, Value );
			break;
		case D3DSAMP_MIPMAPLODBIAS: 
			m_ctx->SetSamplerMipMapLODBias( Sampler, Value );
			break;		
		case D3DSAMP_MAXMIPLEVEL: 
			m_ctx->SetSamplerMaxMipLevel( Sampler, Value);
			break;
		case D3DSAMP_MAXANISOTROPY: 
			m_ctx->SetSamplerMaxAnisotropy( Sampler, Value);
			break;
		case D3DSAMP_SRGBTEXTURE: 
			//m_samplers[ Sampler ].m_srgb = Value;
			m_ctx->SetSamplerSRGBTexture(Sampler, Value);
			break;
		case D3DSAMP_SHADOWFILTER: 
			m_ctx->SetShadowFilter(Sampler, Value);
			break;
		
		default: DXABSTRACT_BREAK_ON_ERROR(); break;
	}
	return S_OK;
#endif
#endif // #if 0 - disabled GL inline path for SetSamplerState
}

FORCEINLINE void TOGLMETHODCALLTYPE IDirect3DDevice9::SetSamplerStates(
	DWORD Sampler, DWORD AddressU, DWORD AddressV, DWORD AddressW,
	DWORD MinFilter, DWORD MagFilter, DWORD MipFilter, DWORD MinLod,
	float LodBias)
{
	// Vulkan backend: always use non-inline path
	SetSamplerStatesNonInline( Sampler, AddressU, AddressV, AddressW, MinFilter, MagFilter, MipFilter, MinLod, LodBias );
}

FORCEINLINE HRESULT TOGLMETHODCALLTYPE IDirect3DDevice9::SetTexture(DWORD Stage,IDirect3DBaseTexture9* pTexture)
{
	// Vulkan backend: always use non-inline path
	return SetTextureNonInline( Stage, pTexture );
}

inline VkCompareOp D3DCompareFuncToVK( DWORD function )
{
	switch ( function )
	{
		case D3DCMP_NEVER		: return VK_COMPARE_OP_NEVER;
		case D3DCMP_LESS		: return VK_COMPARE_OP_LESS;
		case D3DCMP_EQUAL		: return VK_COMPARE_OP_EQUAL;
		case D3DCMP_LESSEQUAL	: return VK_COMPARE_OP_LESS_OR_EQUAL;
		case D3DCMP_GREATER		: return VK_COMPARE_OP_GREATER;
		case D3DCMP_NOTEQUAL	: return VK_COMPARE_OP_NOT_EQUAL;
		case D3DCMP_GREATEREQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
		case D3DCMP_ALWAYS		: return VK_COMPARE_OP_ALWAYS;
		default					: DXABSTRACT_BREAK_ON_ERROR(); return (VkCompareOp)0xFFFFFFFF;
	}
}

FORCEINLINE VkBlendOp D3DBlendOperationToVK( DWORD operation )
{
	switch (operation)
	{
		case	D3DBLENDOP_ADD			: return VK_BLEND_OP_ADD;
		case	D3DBLENDOP_SUBTRACT		: return VK_BLEND_OP_SUBTRACT;
		case	D3DBLENDOP_REVSUBTRACT	: return VK_BLEND_OP_REVERSE_SUBTRACT;
		case	D3DBLENDOP_MIN			: return VK_BLEND_OP_MIN;
		case	D3DBLENDOP_MAX			: return VK_BLEND_OP_MAX;
		default:
			DXABSTRACT_BREAK_ON_ERROR();
			return (VkBlendOp)0xFFFFFFFF;
		break;
	}
}

FORCEINLINE VkBlendFactor D3DBlendFactorToVK( DWORD equation )
{
	switch (equation)
	{
		case	D3DBLEND_ZERO			: return VK_BLEND_FACTOR_ZERO;
		case	D3DBLEND_ONE			: return VK_BLEND_FACTOR_ONE;
		case	D3DBLEND_SRCCOLOR		: return VK_BLEND_FACTOR_SRC_COLOR;
		case	D3DBLEND_INVSRCCOLOR	: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		case	D3DBLEND_SRCALPHA		: return VK_BLEND_FACTOR_SRC_ALPHA;
		case	D3DBLEND_INVSRCALPHA	: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case	D3DBLEND_DESTALPHA		: return VK_BLEND_FACTOR_DST_ALPHA;
		case	D3DBLEND_INVDESTALPHA	: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		case	D3DBLEND_DESTCOLOR		: return VK_BLEND_FACTOR_DST_COLOR;
		case	D3DBLEND_INVDESTCOLOR	: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
		case	D3DBLEND_SRCALPHASAT	: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
		default:
			DXABSTRACT_BREAK_ON_ERROR();
			return (VkBlendFactor)0xFFFFFFFF;
		break;
	}
}


FORCEINLINE VkStencilOp D3DStencilOpToVK( DWORD operation )
{
	switch( operation )
	{
		case D3DSTENCILOP_KEEP		: return VK_STENCIL_OP_KEEP;
		case D3DSTENCILOP_ZERO		: return VK_STENCIL_OP_ZERO;
		case D3DSTENCILOP_REPLACE	: return VK_STENCIL_OP_REPLACE;
		case D3DSTENCILOP_INCRSAT	: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
		case D3DSTENCILOP_DECRSAT	: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
		case D3DSTENCILOP_INVERT	: return VK_STENCIL_OP_INVERT;
		case D3DSTENCILOP_INCR		: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
		case D3DSTENCILOP_DECR		: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
		default						: DXABSTRACT_BREAK_ON_ERROR(); return (VkStencilOp)0xFFFFFFFF;
	}
}

FORCEINLINE HRESULT TOGLMETHODCALLTYPE IDirect3DDevice9::SetRenderStateInline( D3DRENDERSTATETYPE State, DWORD Value )
{
	// Vulkan backend: always use non-inline path for correctness
	return SetRenderState( State, Value );
#if 0 // Original GL inline path below - disabled for Vulkan
#if GLMDEBUG || VK_BATCH_PERF_ANALYSIS
	return SetRenderState( State, Value );
#else
	TOGL_NULL_DEVICE_CHECK;
	Assert( GetCurrentOwnerThreadId() == ThreadGetCurrentId() );

	switch (State)
	{
		case D3DRS_ZENABLE:				// kGLDepthTestEnable
		{
			gl.m_DepthTestEnable.enable = Value;
			m_ctx->WriteDepthTestEnable( &gl.m_DepthTestEnable );
			break;
		}
		case D3DRS_ZWRITEENABLE:			// kGLDepthMask
		{
			gl.m_DepthMask.mask = Value;
			m_ctx->WriteDepthMask( &gl.m_DepthMask );
			break;
		}
		case D3DRS_ZFUNC:	
		{
			// kGLDepthFunc
			VkCompareOp func = D3DCompareFuncToVK( Value );
			gl.m_DepthFunc.func = func;
			m_ctx->WriteDepthFunc( &gl.m_DepthFunc );
			break;
		}
		case D3DRS_COLORWRITEENABLE:		// kGLColorMaskSingle
		{
			gl.m_ColorMaskSingle.r	=	((Value & D3DCOLORWRITEENABLE_RED)  != 0) ? 0xFF : 0x00;
			gl.m_ColorMaskSingle.g	=	((Value & D3DCOLORWRITEENABLE_GREEN)!= 0) ? 0xFF : 0x00;	
			gl.m_ColorMaskSingle.b	=	((Value & D3DCOLORWRITEENABLE_BLUE) != 0) ? 0xFF : 0x00;
			gl.m_ColorMaskSingle.a	=	((Value & D3DCOLORWRITEENABLE_ALPHA)!= 0) ? 0xFF : 0x00;
			m_ctx->WriteColorMaskSingle( &gl.m_ColorMaskSingle );
			break;
		}
		case D3DRS_CULLMODE:				// kGLCullFaceEnable / kGLCullFrontFace
		{
			switch (Value)
			{
				case D3DCULL_NONE:
				{
					gl.m_CullFaceEnable.enable = false;
					gl.m_CullFrontFace.value = GL_CCW;	//doesn't matter																

					m_ctx->WriteCullFaceEnable( &gl.m_CullFaceEnable );
					m_ctx->WriteCullFrontFace( &gl.m_CullFrontFace );
					break;
				}
					
				case D3DCULL_CW:
				{
					gl.m_CullFaceEnable.enable = true;
					gl.m_CullFrontFace.value = GL_CW;	//origGL_CCW;

					m_ctx->WriteCullFaceEnable( &gl.m_CullFaceEnable );
					m_ctx->WriteCullFrontFace( &gl.m_CullFrontFace );
					break;
				}
				case D3DCULL_CCW:
				{
					gl.m_CullFaceEnable.enable = true;
					gl.m_CullFrontFace.value = GL_CCW;	//origGL_CW;

					m_ctx->WriteCullFaceEnable( &gl.m_CullFaceEnable );
					m_ctx->WriteCullFrontFace( &gl.m_CullFrontFace );
					break;
				}
				default:	
				{
					DXABSTRACT_BREAK_ON_ERROR();	
					break;
				}
			}
			break;
		}
		//-------------------------------------------------------------------------------------------- alphablend stuff
		case D3DRS_ALPHABLENDENABLE:		// kGLBlendEnable
		{
			gl.m_BlendEnable.enable = Value;
			m_ctx->WriteBlendEnable( &gl.m_BlendEnable );
			break;
		}
		case D3DRS_BLENDOP:				// kGLBlendEquation				// D3D blend-op ==> GL blend equation
		{
			VkBlendOp equation = D3DBlendOperationToVK( Value );
			gl.m_BlendEquation.equation = equation;
			m_ctx->WriteBlendEquation( &gl.m_BlendEquation );
			break;
		}
		case D3DRS_SRCBLEND:				// kGLBlendFactor				// D3D blend-factor ==> GL blend factor
		case D3DRS_DESTBLEND:			// kGLBlendFactor
		{
			VkBlendFactor factor = D3DBlendFactorToVK( Value );

			if (State==D3DRS_SRCBLEND)
			{
				gl.m_BlendFactor.srcfactor = factor;
			}
			else
			{
				gl.m_BlendFactor.dstfactor = factor;
			}
			m_ctx->WriteBlendFactor( &gl.m_BlendFactor );
			break;
		}
		case D3DRS_SRGBWRITEENABLE:			// kGLBlendEnableSRGB
		{
			gl.m_BlendEnableSRGB.enable = Value;
			m_ctx->WriteBlendEnableSRGB( &gl.m_BlendEnableSRGB );
			break;					
		}
		//-------------------------------------------------------------------------------------------- alphatest stuff
		case D3DRS_ALPHATESTENABLE:
		{
			gl.m_AlphaTestEnable.enable = Value;
			m_ctx->WriteAlphaTestEnable( &gl.m_AlphaTestEnable );
			break;
		}
		case D3DRS_ALPHAREF:
		{
			gl.m_AlphaTestFunc.ref = Value / 255.0f;
			m_ctx->WriteAlphaTestFunc( &gl.m_AlphaTestFunc );
			break;
		}
		case D3DRS_ALPHAFUNC:
		{
			VkCompareOp func = D3DCompareFuncToVK( Value );;
			gl.m_AlphaTestFunc.func = func;
			m_ctx->WriteAlphaTestFunc( &gl.m_AlphaTestFunc );
			break;
		}
		//-------------------------------------------------------------------------------------------- stencil stuff
		case D3DRS_STENCILENABLE:		// VKStencilTestEnable_t
		{
			gl.m_StencilTestEnable.enable = Value;
			m_ctx->WriteStencilTestEnable( &gl.m_StencilTestEnable );
			break;
		}
		case D3DRS_STENCILFAIL:			// VKStencilOp_t		"what do you do if stencil test fails"
		{
			VkStencilOp stencilop = D3DStencilOpToVK( Value );
			gl.m_StencilOp.sfail = stencilop;

			m_ctx->WriteStencilOp( &gl.m_StencilOp,0 );
			m_ctx->WriteStencilOp( &gl.m_StencilOp,1 );		// ********* need to recheck this
			break;
		}
		case D3DRS_STENCILZFAIL:			// VKStencilOp_t		"what do you do if stencil test passes *but* depth test fails, if depth test happened"
		{
			VkStencilOp stencilop = D3DStencilOpToVK( Value );
			gl.m_StencilOp.dpfail = stencilop;

			m_ctx->WriteStencilOp( &gl.m_StencilOp,0 );
			m_ctx->WriteStencilOp( &gl.m_StencilOp,1 );		// ********* need to recheck this
			break;
		}
		case D3DRS_STENCILPASS:			// VKStencilOp_t		"what do you do if stencil test and depth test both pass"
		{
			VkStencilOp stencilop = D3DStencilOpToVK( Value );
			gl.m_StencilOp.dppass = stencilop;

			m_ctx->WriteStencilOp( &gl.m_StencilOp,0 );
			m_ctx->WriteStencilOp( &gl.m_StencilOp,1 );		// ********* need to recheck this
			break;
		}
		case D3DRS_STENCILFUNC:			// VKStencilFunc_t
		{
			VkCompareOp stencilfunc = D3DCompareFuncToVK( Value );
			gl.m_StencilFunc.frontfunc = gl.m_StencilFunc.backfunc = stencilfunc;

			m_ctx->WriteStencilFunc( &gl.m_StencilFunc );
			break;
		}
		case D3DRS_STENCILREF:			// VKStencilFunc_t
		{
			gl.m_StencilFunc.ref = Value;
			m_ctx->WriteStencilFunc( &gl.m_StencilFunc );
			break;
		}
		case D3DRS_STENCILMASK:			// VKStencilFunc_t
		{
			gl.m_StencilFunc.mask = Value;
			m_ctx->WriteStencilFunc( &gl.m_StencilFunc );
			break;
		}
		case D3DRS_STENCILWRITEMASK:		// VKStencilWriteMask_t
		{
			gl.m_StencilWriteMask.mask = Value;
			m_ctx->WriteStencilWriteMask( &gl.m_StencilWriteMask );
			break;
		}
		case D3DRS_FOGENABLE:			// none of these are implemented yet... erk
		{
			gl.m_FogEnable = (Value != 0);
			VKMPRINTF(("-D- fogenable = %d",Value ));
			break;
		}
		case D3DRS_SCISSORTESTENABLE:	// kGLScissorEnable
		{
			gl.m_ScissorEnable.enable = Value;
			m_ctx->WriteScissorEnable( &gl.m_ScissorEnable );
			break;
		}
		case D3DRS_DEPTHBIAS:			// kGLDepthBias
		{
			// the value in the dword is actually a float
			float	fvalue = *(float*)&Value;
			gl.m_DepthBias.units = fvalue;

			m_ctx->WriteDepthBias( &gl.m_DepthBias );
			break;
		}
		// good ref on these: http://aras-p.info/blog/2008/06/12/depth-bias-and-the-power-of-deceiving-yourself/
		case D3DRS_SLOPESCALEDEPTHBIAS:
		{
			// the value in the dword is actually a float
			float	fvalue = *(float*)&Value;
			gl.m_DepthBias.factor = fvalue;

			m_ctx->WriteDepthBias( &gl.m_DepthBias );
			break;
		}
		// Alpha to coverage
		case D3DRS_ADAPTIVETESS_Y:
		{
			gl.m_AlphaToCoverageEnable.enable = Value;
			m_ctx->WriteAlphaToCoverageEnable( &gl.m_AlphaToCoverageEnable );
			break;
		}
		case D3DRS_CLIPPLANEENABLE:		// kGLClipPlaneEnable
		{
			// d3d packs all the enables into one word.
			// we break that out so we don't do N glEnable calls to sync - 
			// GLM is tracking one unique enable per plane.
			for( int i=0; i<kVKUserClipPlanes; i++)
			{
				gl.m_ClipPlaneEnable[i].enable = (Value & (1<<i)) != 0;
			}

			for( int x=0; x<kVKUserClipPlanes; x++)
				m_ctx->WriteClipPlaneEnable( &gl.m_ClipPlaneEnable[x], x );
			break;
		}
		//-------------------------------------------------------------------------------------------- polygon/fill mode
		case D3DRS_FILLMODE:
		{
			GLuint mode = 0;
			switch(Value)
			{
				case D3DFILL_POINT:			mode = GL_POINT; break;
				case D3DFILL_WIREFRAME:		mode = GL_LINE; break;
				case D3DFILL_SOLID:			mode = GL_FILL; break;
				default:					DXABSTRACT_BREAK_ON_ERROR(); break;
			}
			gl.m_PolygonMode.values[0] = gl.m_PolygonMode.values[1] = mode;						
			m_ctx->WritePolygonMode( &gl.m_PolygonMode );
			break;
		}
	}
		
	return S_OK;
#endif
#endif // #if 0 - disabled GL inline path
}

FORCEINLINE HRESULT TOGLMETHODCALLTYPE IDirect3DDevice9::SetRenderStateConstInline( D3DRENDERSTATETYPE State, DWORD Value )
{
	// State is a compile time constant - luckily no need to do anything special to get the compiler to optimize this case.
	return SetRenderStateInline( State, Value );
}

FORCEINLINE HRESULT TOGLMETHODCALLTYPE IDirect3DDevice9::SetIndices(IDirect3DIndexBuffer9* pIndexData)
{
#if GLMDEBUG || VK_BATCH_PERF_ANALYSIS
	return SetIndicesNonInline( pIndexData );
#else
	Assert( GetCurrentOwnerThreadId() == ThreadGetCurrentId() );
	// just latch it.
	m_indices.m_idxBuffer = pIndexData;
	return S_OK;
#endif
}

FORCEINLINE HRESULT TOGLMETHODCALLTYPE IDirect3DDevice9::SetStreamSource(UINT StreamNumber,IDirect3DVertexBuffer9* pStreamData,UINT OffsetInBytes,UINT Stride)
{
#if GLMDEBUG || VK_BATCH_PERF_ANALYSIS
	return SetStreamSourceNonInline( StreamNumber, pStreamData, OffsetInBytes, Stride );
#else
	Assert( GetCurrentOwnerThreadId() == ThreadGetCurrentId() );
	Assert( StreamNumber < D3D_MAX_STREAMS );
	Assert( ( Stride & 3 ) == 0 ); // we support non-DWORD aligned strides, but on some drivers (like AMD's) perf goes off a cliff 
	
	// perfectly legal to see a vertex buffer of NULL get passed in here.
	// so we need an array to track these.
	// OK, we are being given the stride, we don't need to calc it..

	VKMPRINTF(("-X- IDirect3DDevice9::SetStreamSource setting stream #%d to D3D buf %p (GL name %d); offset %d, stride %d", StreamNumber, pStreamData, (pStreamData) ? pStreamData->m_vtxBuffer->m_name: -1, OffsetInBytes, Stride));
	
	if ( !pStreamData )
	{
		OffsetInBytes = 0;
		Stride = 0;

		m_vtx_buffers[ StreamNumber ] = m_pDummy_vtx_buffer;
	}
	else
	{
		// We do not support strides of 0
		Assert( Stride > 0 );
		m_vtx_buffers[ StreamNumber ] = pStreamData->m_vtxBuffer;
	}

	m_streams[ StreamNumber ].m_vtxBuffer = pStreamData;
	m_streams[ StreamNumber ].m_offset	= OffsetInBytes;
	m_streams[ StreamNumber ].m_stride	= Stride;

	return S_OK;
#endif
}

FORCEINLINE HRESULT TOGLMETHODCALLTYPE IDirect3DDevice9::SetVertexShaderConstantF(UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount)	// groups of 4 floats!
{
	return SetVertexShaderConstantFNonInline( StartRegister, pConstantData, Vector4fCount );
}

FORCEINLINE HRESULT TOGLMETHODCALLTYPE IDirect3DDevice9::SetVertexShaderConstantB(UINT StartRegister,CONST BOOL* pConstantData,UINT  BoolCount)
{
	return SetVertexShaderConstantBNonInline( StartRegister, pConstantData, BoolCount );
}

FORCEINLINE HRESULT IDirect3DDevice9::SetVertexShaderConstantI(UINT StartRegister,CONST int* pConstantData,UINT Vector4iCount)		// groups of 4 ints!
{
	return SetVertexShaderConstantINonInline( StartRegister, pConstantData, Vector4iCount );
}

FORCEINLINE HRESULT TOGLMETHODCALLTYPE IDirect3DDevice9::SetPixelShaderConstantF(UINT StartRegister,CONST float* pConstantData,UINT Vector4fCount)
{
	return SetPixelShaderConstantFNonInline(StartRegister, pConstantData, Vector4fCount);
}

HRESULT IDirect3DDevice9::SetVertexShader(IDirect3DVertexShader9* pShader)
{
	return SetVertexShaderNonInline(pShader);
}

FORCEINLINE HRESULT TOGLMETHODCALLTYPE IDirect3DDevice9::SetPixelShader(IDirect3DPixelShader9* pShader)
{
	return SetPixelShaderNonInline(pShader);
}

FORCEINLINE HRESULT IDirect3DDevice9::SetVertexDeclaration(IDirect3DVertexDeclaration9* pDecl)
{
#if GLMDEBUG || VK_BATCH_PERF_ANALYSIS
	return SetVertexDeclarationNonInline(pDecl);
#else
	Assert( GetCurrentOwnerThreadId() == ThreadGetCurrentId() );
	m_pVertDecl = pDecl;
	return S_OK;
#endif
}

FORCEINLINE void IDirect3DDevice9::SetMaxUsedVertexShaderConstantsHint( uint nMaxReg )
{
	// Vulkan backend: no-op for now (optimization hint)
}

// ------------------------------------------------------------------------------------------------------------------------------ //
// D3DX
// ------------------------------------------------------------------------------------------------------------------------------ //
struct ID3DXInclude
{
	virtual HRESULT Open(D3DXINCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID *ppData, UINT *pBytes);
	virtual HRESULT Close(LPCVOID pData);
};
typedef ID3DXInclude* LPD3DXINCLUDE;


struct TOGL_CLASS ID3DXBuffer : public IUnknown
{
	void* GetBufferPointer();
	DWORD GetBufferSize();
};

typedef ID3DXBuffer* LPD3DXBUFFER;

class ID3DXConstantTable : public IUnknown
{
};
typedef ID3DXConstantTable* LPD3DXCONSTANTTABLE;

TOGL_INTERFACE const char* D3DXGetPixelShaderProfile( IDirect3DDevice9 *pDevice );

TOGL_INTERFACE D3DXMATRIX* D3DXMatrixMultiply( D3DXMATRIX *pOut, CONST D3DXMATRIX *pM1, CONST D3DXMATRIX *pM2 );
TOGL_INTERFACE D3DXVECTOR3* D3DXVec3TransformCoord( D3DXVECTOR3 *pOut, CONST D3DXVECTOR3 *pV, CONST D3DXMATRIX *pM );

TOGL_INTERFACE HRESULT D3DXCreateMatrixStack( DWORD Flags, LPD3DXMATRIXSTACK* ppStack);
TOGL_INTERFACE void D3DXMatrixIdentity( D3DXMATRIX * );

TOGL_INTERFACE D3DXINLINE D3DXVECTOR3* D3DXVec3Subtract( D3DXVECTOR3 *pOut, CONST D3DXVECTOR3 *pV1, CONST D3DXVECTOR3 *pV2 )
{
	pOut->x = pV1->x - pV2->x;
	pOut->y = pV1->y - pV2->y;
	pOut->z = pV1->z - pV2->z;
	return pOut;
}

TOGL_INTERFACE D3DXINLINE D3DXVECTOR3* D3DXVec3Cross( D3DXVECTOR3 *pOut, CONST D3DXVECTOR3 *pV1, CONST D3DXVECTOR3 *pV2 )
{
	D3DXVECTOR3 v;

	v.x = pV1->y * pV2->z - pV1->z * pV2->y;
	v.y = pV1->z * pV2->x - pV1->x * pV2->z;
	v.z = pV1->x * pV2->y - pV1->y * pV2->x;

	*pOut = v;
	return pOut;
}

TOGL_INTERFACE D3DXINLINE FLOAT D3DXVec3Dot( CONST D3DXVECTOR3 *pV1, CONST D3DXVECTOR3 *pV2 )
{
	return pV1->x * pV2->x + pV1->y * pV2->y + pV1->z * pV2->z;
}

TOGL_INTERFACE D3DXMATRIX* D3DXMatrixInverse( D3DXMATRIX *pOut, FLOAT *pDeterminant, CONST D3DXMATRIX *pM );

TOGL_INTERFACE D3DXMATRIX* D3DXMatrixTranspose( D3DXMATRIX *pOut, CONST D3DXMATRIX *pM );

TOGL_INTERFACE D3DXPLANE* D3DXPlaneNormalize( D3DXPLANE *pOut, CONST D3DXPLANE *pP);

TOGL_INTERFACE D3DXVECTOR4* D3DXVec4Transform( D3DXVECTOR4 *pOut, CONST D3DXVECTOR4 *pV, CONST D3DXMATRIX *pM );


TOGL_INTERFACE D3DXVECTOR4* D3DXVec4Normalize( D3DXVECTOR4 *pOut, CONST D3DXVECTOR4 *pV );

TOGL_INTERFACE D3DXMATRIX* D3DXMatrixTranslation( D3DXMATRIX *pOut, FLOAT x, FLOAT y, FLOAT z );

// Build an ortho projection matrix. (right-handed)
TOGL_INTERFACE D3DXMATRIX* D3DXMatrixOrthoOffCenterRH( D3DXMATRIX *pOut, FLOAT l, FLOAT r, FLOAT b, FLOAT t, FLOAT zn,FLOAT zf );

TOGL_INTERFACE D3DXMATRIX* D3DXMatrixPerspectiveRH( D3DXMATRIX *pOut, FLOAT w, FLOAT h, FLOAT zn, FLOAT zf );

TOGL_INTERFACE D3DXMATRIX* D3DXMatrixPerspectiveOffCenterRH( D3DXMATRIX *pOut, FLOAT l, FLOAT r, FLOAT b, FLOAT t, FLOAT zn, FLOAT zf );

// Transform a plane by a matrix.  The vector (a,b,c) must be normal.
// M should be the inverse transpose of the transformation desired.
TOGL_INTERFACE D3DXPLANE* D3DXPlaneTransform( D3DXPLANE *pOut, CONST D3DXPLANE *pP, CONST D3DXMATRIX *pM );

TOGL_INTERFACE IDirect3D9 *Direct3DCreate9(UINT SDKVersion);

TOGL_INTERFACE void D3DPERF_SetOptions( DWORD dwOptions );

TOGL_INTERFACE HRESULT D3DXCompileShader(
	LPCSTR                          pSrcData,
	UINT                            SrcDataLen,
	CONST D3DXMACRO*                pDefines,
	LPD3DXINCLUDE                   pInclude,
	LPCSTR                          pFunctionName,
	LPCSTR                          pProfile,
	DWORD                           Flags,
	LPD3DXBUFFER*                   ppShader,
	LPD3DXBUFFER*                   ppErrorMsgs,
	LPD3DXCONSTANTTABLE*            ppConstantTable);

// fake D3D usage constant for SRGB tex creation
#define D3DUSAGE_TEXTURE_SRGB (0x80000000L)

#else

	//USE_ACTUAL_DX
	#ifndef WIN32
	#error sorry man
	#endif

	#ifdef _X360
		#include "d3d9.h"
		#include "d3dx9.h"
	#else
		#include <windows.h>
		#include "../../dx9sdk/include/d3d9.h"
		#include "../../dx9sdk/include/d3dx9.h"
	#endif
	typedef HWND VD3DHWND;

#endif // DX_TO_VK_ABSTRACTION

#endif // DXABSTRACT_H
