//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Shader Device Manager - Enumerates adapters and creates devices
//
//===========================================================================//

#include "utlvector.h"
#include "materialsystem/imaterialsystem.h"
#include "IHardwareConfigInternal.h"
#include "shadersystem.h"
#include "shaderapi/ishaderutil.h"
#include "shaderapi/ishaderapi.h"
#include "materialsystem/imesh.h"
#include "tier0/dbg.h"
#include "materialsystem/idebugtextureinfo.h"
#include "materialsystem/deformations.h"
#include "dxvk_adapter.h"

#include "tier1/interface.h"
#include "appframework/IAppSystem.h"
#include "bitmap/imageformat.h"
#include "tier1/utlbuffer.h"

// Forward declarations for mesh/shadow classes
class CDxvkMesh;
class CShaderShadowDxVk;
class CShaderDeviceDxVk;
class CShaderAPIDxVk;

//-----------------------------------------------------------------------------
// CDxvkMesh - Stub mesh implementation for DXVK
//-----------------------------------------------------------------------------
class CDxvkMesh : public IMesh
{
public:
	CDxvkMesh( bool bIsDynamic );
	virtual ~CDxvkMesh();

	virtual bool Lock( int nMaxIndexCount, bool bAppend, IndexDesc_t& desc );
	virtual void Unlock( int nWrittenIndexCount, IndexDesc_t& desc );
	virtual void ModifyBegin( bool bReadOnly, int nFirstIndex, int nIndexCount, IndexDesc_t& desc );
	virtual void ModifyEnd( IndexDesc_t& desc );
	virtual void Spew( int nIndexCount, const IndexDesc_t & desc );
	virtual void ValidateData( int nIndexCount, const IndexDesc_t &desc );
	virtual bool Lock( int nVertexCount, bool bAppend, VertexDesc_t &desc );
	virtual void Unlock( int nVertexCount, VertexDesc_t &desc );
	virtual void Spew( int nVertexCount, const VertexDesc_t &desc );
	virtual void ValidateData( int nVertexCount, const VertexDesc_t & desc );
	virtual bool IsDynamic() const { return m_bIsDynamic; }
	virtual void BeginCastBuffer( VertexFormat_t format ) {}
	virtual void BeginCastBuffer( MaterialIndexFormat_t format ) {}
	virtual void EndCastBuffer( ) {}
	virtual int GetRoomRemaining() const { return VERTEX_BUFFER_SIZE; }
	virtual MaterialIndexFormat_t IndexFormat() const { return MATERIAL_INDEX_FORMAT_16BIT; }

	void LockMesh( int numVerts, int numIndices, MeshDesc_t& desc );
	void UnlockMesh( int numVerts, int numIndices, MeshDesc_t& desc );
	void ModifyBeginEx( bool bReadOnly, int firstVertex, int numVerts, int firstIndex, int numIndices, MeshDesc_t& desc );
	void ModifyBegin( int firstVertex, int numVerts, int firstIndex, int numIndices, MeshDesc_t& desc );
	void ModifyEnd( MeshDesc_t& desc );
	int  VertexCount() const { return m_nVertexCount; }
	void SetPrimitiveType( MaterialPrimitiveType_t type ) { m_PrimitiveType = type; }
	void Draw(int firstIndex, int numIndices);
	void Draw(CPrimList *pPrims, int nPrims);
	virtual void CopyToMeshBuilder( int iStartVert, int nVerts, int iStartIndex, int nIndices,
									int indexOffset, CMeshBuilder &builder ) {}
	void Spew( int numVerts, int numIndices, const MeshDesc_t & desc );
	void ValidateData( int numVerts, int numIndices, const MeshDesc_t & desc );
	IMaterial* GetMaterial() { return nullptr; }
	void SetColorMesh( IMesh *pColorMesh, int nVertexOffset ) {}
	virtual int IndexCount() const { return m_nIndexCount; }
	virtual void SetFlexMesh( IMesh *pMesh, int nVertexOffset ) {}
	virtual void DisableFlexMesh() {}
	virtual void MarkAsDrawn() {}
	virtual unsigned ComputeMemoryUsed() { return VERTEX_BUFFER_SIZE; }
	virtual VertexFormat_t GetVertexFormat() const { return VERTEX_POSITION; }
	virtual IMesh *GetMesh() { return this; }

	void SetVertexCount( int c ) { m_nVertexCount = c; }
	void SetIndexCount( int c ) { m_nIndexCount = c; }

private:
	enum { VERTEX_BUFFER_SIZE = 4 * 1024 * 1024 };
	unsigned char* m_pVertexMemory;
	unsigned char* m_pIndexMemory;
	bool m_bIsDynamic;
	int m_nVertexCount;
	int m_nIndexCount;
	MaterialPrimitiveType_t m_PrimitiveType;
};

//-----------------------------------------------------------------------------
// CShaderShadowDxVk - Shadow state tracker (defined in shadershadownxvk.cpp)
//-----------------------------------------------------------------------------
class CShaderShadowDxVk;

//-----------------------------------------------------------------------------
// CShaderDeviceDxVk - Device-level DXVK shader API
//-----------------------------------------------------------------------------
class CShaderDeviceDxVk : public IShaderDevice
{
public:
	CShaderDeviceDxVk() : m_DynamicMesh( true ), m_Mesh( false ) {}
	~CShaderDeviceDxVk() {}

	virtual void ReleaseResources();
	virtual void ReacquireResources();
	virtual ImageFormat GetBackBufferFormat() const { return IMAGE_FORMAT_BGRA8888; }
	virtual void GetBackBufferDimensions( int& width, int& height ) const;
	virtual int GetCurrentAdapter() const { return 0; }
	virtual bool IsUsingGraphics() const { return m_bGraphicsInited; }
	virtual void SpewDriverInfo() const;
	virtual int StencilBufferBits() const { return 8; }
	virtual bool IsAAEnabled() const { return m_nAASamples > 1; }
	virtual void Present();
	virtual void GetWindowSize( int &nWidth, int &nHeight ) const;
	virtual void SetHardwareGammaRamp( float fGamma, float fGammaTVRangeMin,
									   float fGammaTVRangeMax, float fGammaTVExponent, bool bTVEnabled ) {}
	virtual bool AddView( void* hWnd );
	virtual void RemoveView( void* hWnd );
	virtual void SetView( void* hWnd );
	virtual IShaderBuffer* CompileShader( const char *pProgram, size_t nBufLen, const char *pShaderVersion ) { return NULL; }
	virtual VertexShaderHandle_t CreateVertexShader( IShaderBuffer* pShaderBuffer ) { return VERTEX_SHADER_HANDLE_INVALID; }
	virtual void DestroyVertexShader( VertexShaderHandle_t hShader ) {}
	virtual GeometryShaderHandle_t CreateGeometryShader( IShaderBuffer* pShaderBuffer ) { return GEOMETRY_SHADER_HANDLE_INVALID; }
	virtual void DestroyGeometryShader( GeometryShaderHandle_t hShader ) {}
	virtual PixelShaderHandle_t CreatePixelShader( IShaderBuffer* pShaderBuffer ) { return PIXEL_SHADER_HANDLE_INVALID; }
	virtual void DestroyPixelShader( PixelShaderHandle_t hShader ) {}
	virtual IMesh* CreateStaticMesh( VertexFormat_t fmt, const char *pTextureBudgetGroup, IMaterial * pMaterial = NULL );
	virtual void DestroyStaticMesh( IMesh* mesh ) {}
	virtual IVertexBuffer *CreateVertexBuffer( ShaderBufferType_t type, VertexFormat_t fmt, int nVertexCount, const char *pBudgetGroup );
	virtual void DestroyVertexBuffer( IVertexBuffer *pVertexBuffer ) {}
	virtual IIndexBuffer *CreateIndexBuffer( ShaderBufferType_t bufferType, MaterialIndexFormat_t fmt, int nIndexCount, const char *pBudgetGroup );
	virtual void DestroyIndexBuffer( IIndexBuffer *pIndexBuffer ) {}
	virtual IVertexBuffer *GetDynamicVertexBuffer( int nStreamID, VertexFormat_t vertexFormat, bool bBuffered = true );
	virtual IIndexBuffer *GetDynamicIndexBuffer( MaterialIndexFormat_t fmt, bool bBuffered = true );
	virtual void EnableNonInteractiveMode( MaterialNonInteractiveMode_t mode, ShaderNonInteractiveInfo_t *pInfo = NULL ) {}
	virtual void RefreshFrontBufferNonInteractive( ) {}
	virtual void HandleThreadEvent( uint32 threadEvent ) {}

#ifdef DX_TO_GL_ABSTRACTION
	virtual void DoStartupShaderPreloading( void ) {}
#endif

	virtual char *GetDisplayDeviceName() { return (char*)"DXVK Vulkan Device"; }

	// DXVK-specific
	bool InitDxVk( void* hWnd, int nAdapter, const ShaderDeviceInfo_t& info );
	void ShutdownDxVk();
	void SetAASamples( int n ) { m_nAASamples = n; }
	void SetGraphicsInited( bool b ) { m_bGraphicsInited = b; }
	void SetWindowSize( int w, int h ) { m_nWindowWidth = w; m_nWindowHeight = h; }

private:
	CDxvkMesh m_Mesh;
	CDxvkMesh m_DynamicMesh;
	int m_nAASamples;
	bool m_bGraphicsInited;
	int m_nWindowWidth;
	int m_nWindowHeight;
	int m_nBackBufferWidth;
	int m_nBackBufferHeight;
	void* m_hWnd;
};

//-----------------------------------------------------------------------------
// CShaderDeviceMgrDxVk - Top-level manager: enumerates adapters, modes, creates device
//-----------------------------------------------------------------------------
class CShaderDeviceMgrDxVk : public IShaderDeviceMgr
{
public:
	virtual bool Connect( CreateInterfaceFn factory );
	virtual void Disconnect();
	virtual void *QueryInterface( const char *pInterfaceName );
	virtual InitReturnVal_t Init();
	virtual void Shutdown();

	virtual int	 GetAdapterCount() const;
	virtual void GetAdapterInfo( int nAdapter, MaterialAdapterInfo_t& info ) const;
	virtual bool GetRecommendedConfigurationInfo( int nAdapter, int nDXLevel, KeyValues *pConfiguration );
	virtual int	 GetModeCount( int nAdapter ) const;
	virtual void GetModeInfo( ShaderDisplayMode_t* pInfo, int nAdapter, int nMode ) const;
	virtual void GetCurrentModeInfo( ShaderDisplayMode_t* pInfo, int nAdapter ) const;
	virtual bool SetAdapter( int nAdapter, int nFlags );
	virtual CreateInterfaceFn SetMode( void *hWnd, int nAdapter, const ShaderDeviceInfo_t& mode );
	virtual void AddModeChangeCallback( ShaderModeChangeCallbackFunc_t func ) {}
	virtual void RemoveModeChangeCallback( ShaderModeChangeCallbackFunc_t func ) {}

	// DXVK helpers
	bool EnsureAdapterInitialized();
};

// ---- Singleton instances ----
static CDxvkMesh* s_pStaticMesh = nullptr;
static CDxvkMesh* s_pDynamicMesh = nullptr;
static CShaderDeviceDxVk s_ShaderDeviceDxVk;
static CShaderDeviceMgrDxVk s_ShaderDeviceMgrDxVk;
extern CShaderShadowDxVk s_ShaderShadowDxVk;
extern CShaderAPIDxVk g_ShaderAPIDxVk;

// ---- Global shader util pointer (required by other code) ----
IShaderUtil* g_pShaderUtil;

//-----------------------------------------------------------------------------
// Shader interface factory - exposed by SetMode()
//-----------------------------------------------------------------------------
static void* ShaderInterfaceFactoryDxVk( const char *pInterfaceName, int *pReturnCode )
{
	if ( pReturnCode )
		*pReturnCode = IFACE_OK;

	if ( !Q_stricmp( pInterfaceName, SHADER_DEVICE_INTERFACE_VERSION ) )
		return static_cast< IShaderDevice* >( &s_ShaderDeviceDxVk );
	if ( !Q_stricmp( pInterfaceName, SHADERAPI_INTERFACE_VERSION ) )
		return reinterpret_cast< IShaderAPI* >( &g_ShaderAPIDxVk );
	if ( !Q_stricmp( pInterfaceName, SHADERSHADOW_INTERFACE_VERSION ) )
		return reinterpret_cast< IShaderShadow* >( &s_ShaderShadowDxVk );

	if ( pReturnCode )
		*pReturnCode = IFACE_FAILED;
	return NULL;
}

//=============================================================================
// CDxvkMesh Implementation
//=============================================================================
CDxvkMesh::CDxvkMesh( bool bIsDynamic ) :
	m_bIsDynamic( bIsDynamic ),
	m_nVertexCount( 0 ),
	m_nIndexCount( 0 ),
	m_PrimitiveType( MATERIAL_TRIANGLES )
{
	m_pVertexMemory = new unsigned char[ VERTEX_BUFFER_SIZE ];
	m_pIndexMemory  = new unsigned char[ VERTEX_BUFFER_SIZE ];
}

CDxvkMesh::~CDxvkMesh()
{
	delete[] m_pVertexMemory;
	delete[] m_pIndexMemory;
}

bool CDxvkMesh::Lock( int nMaxIndexCount, bool bAppend, IndexDesc_t& desc )
{
	desc.m_pIndices = (unsigned short*)m_pIndexMemory;
	desc.m_nIndexSize = sizeof(unsigned short);
	desc.m_nFirstIndex = 0;
	desc.m_nOffset = 0;
	return true;
}

void CDxvkMesh::Unlock( int nWrittenIndexCount, IndexDesc_t& desc )
{
	m_nIndexCount = nWrittenIndexCount;
}

void CDxvkMesh::ModifyBegin( bool bReadOnly, int nFirstIndex, int nIndexCount, IndexDesc_t& desc )
{
	Lock( nIndexCount, false, desc );
}

void CDxvkMesh::ModifyEnd( IndexDesc_t& desc )
{
}

void CDxvkMesh::Spew( int nIndexCount, const IndexDesc_t & desc ) {}
void CDxvkMesh::ValidateData( int nIndexCount, const IndexDesc_t &desc ) {}

bool CDxvkMesh::Lock( int nVertexCount, bool bAppend, VertexDesc_t &desc )
{
	desc.m_pPosition = (float*)m_pVertexMemory;
	desc.m_pNormal   = (float*)m_pVertexMemory;
	desc.m_pColor    = m_pVertexMemory;

	for ( int i = 0; i < VERTEX_MAX_TEXTURE_COORDINATES; ++i )
	{
		desc.m_pTexCoord[i] = (float*)m_pVertexMemory;
	}

	desc.m_pBoneWeight       = (float*)m_pVertexMemory;
	desc.m_pBoneMatrixIndex  = (unsigned char*)m_pVertexMemory;
	desc.m_pTangentS         = (float*)m_pVertexMemory;
	desc.m_pTangentT         = (float*)m_pVertexMemory;
	desc.m_pUserData         = (float*)m_pVertexMemory;
	desc.m_NumBoneWeights    = 2;

	desc.m_VertexSize_Position       = 0;
	desc.m_VertexSize_BoneWeight     = 0;
	desc.m_VertexSize_BoneMatrixIndex= 0;
	desc.m_VertexSize_Normal         = 0;
	desc.m_VertexSize_Color          = 0;
	for ( int i = 0; i < VERTEX_MAX_TEXTURE_COORDINATES; i++ )
		desc.m_VertexSize_TexCoord[i] = 0;
	desc.m_VertexSize_TangentS       = 0;
	desc.m_VertexSize_TangentT       = 0;
	desc.m_VertexSize_UserData       = 0;
	desc.m_ActualVertexSize         = 0;

	desc.m_nFirstVertex = 0;
	desc.m_nOffset = 0;
	return true;
}

void CDxvkMesh::Unlock( int nVertexCount, VertexDesc_t &desc )
{
	m_nVertexCount = nVertexCount;
}

void CDxvkMesh::Spew( int nVertexCount, const VertexDesc_t & desc ) {}
void CDxvkMesh::ValidateData( int nVertexCount, const VertexDesc_t & desc ) {}

void CDxvkMesh::LockMesh( int numVerts, int numIndices, MeshDesc_t& desc )
{
	Lock( numVerts, false, *static_cast<VertexDesc_t*>( &desc ) );
	Lock( numIndices, false, *static_cast<IndexDesc_t*>( &desc ) );
}

void CDxvkMesh::UnlockMesh( int numVerts, int numIndices, MeshDesc_t& desc )
{
	m_nVertexCount = numVerts;
	m_nIndexCount  = numIndices;
}

void CDxvkMesh::ModifyBeginEx( bool bReadOnly, int firstVertex, int numVerts, int firstIndex, int numIndices, MeshDesc_t& desc )
{
	Lock( numVerts, false, *static_cast<VertexDesc_t*>( &desc ) );
	Lock( numIndices, false, *static_cast<IndexDesc_t*>( &desc ) );
}

void CDxvkMesh::ModifyBegin( int firstVertex, int numVerts, int firstIndex, int numIndices, MeshDesc_t& desc )
{
	ModifyBeginEx( false, firstVertex, numVerts, firstIndex, numIndices, desc );
}

void CDxvkMesh::ModifyEnd( MeshDesc_t& desc ) {}

void CDxvkMesh::Draw( int firstIndex, int numIndices )
{
	// DXVK would record a draw call here via the DXVK context.
	// For the integrated stub, this is a no-op.
}

void CDxvkMesh::Draw( CPrimList *pPrims, int nPrims )
{
}

void CDxvkMesh::Spew( int numVerts, int numIndices, const MeshDesc_t & desc ) {}
void CDxvkMesh::ValidateData( int numVerts, int numIndices, const MeshDesc_t & desc ) {}

//=============================================================================
// CShaderDeviceDxVk Implementation
//=============================================================================
void CShaderDeviceDxVk::ReleaseResources()
{
	// Release DXVK swapchain images, transient resources
}

void CShaderDeviceDxVk::ReacquireResources()
{
	// Reacquire DXVK swapchain images after resize
}

void CShaderDeviceDxVk::GetBackBufferDimensions( int& width, int& height ) const
{
	width  = ( m_nBackBufferWidth  > 0 ) ? m_nBackBufferWidth  : 1920;
	height = ( m_nBackBufferHeight > 0 ) ? m_nBackBufferHeight : 1080;
}

void CShaderDeviceDxVk::SpewDriverInfo() const
{
	Msg( "DXVK Vulkan driver - using DXVK translation layer\n" );
	if ( g_pDxvkAdapter && g_pDxvkAdapter->IsInitialized() )
	{
		// Would query and print actual physical device name + driver info here
	}
}

void CShaderDeviceDxVk::Present()
{
	// DXVK: submit present queue operation via swapchain
}

void CShaderDeviceDxVk::GetWindowSize( int &nWidth, int &nHeight ) const
{
	nWidth  = ( m_nWindowWidth  > 0 ) ? m_nWindowWidth  : 1024;
	nHeight = ( m_nWindowHeight > 0 ) ? m_nWindowHeight : 768;
}

bool CShaderDeviceDxVk::AddView( void* hwnd ) { return true; }
void CShaderDeviceDxVk::RemoveView( void* hwnd ) {}
void CShaderDeviceDxVk::SetView( void* hwnd ) { m_hWnd = hwnd; }

IMesh* CShaderDeviceDxVk::CreateStaticMesh( VertexFormat_t fmt, const char *pTextureBudgetGroup, IMaterial * pMaterial )
{
	if ( !s_pStaticMesh )
		s_pStaticMesh = new CDxvkMesh( false );
	return s_pStaticMesh;
}

IVertexBuffer *CShaderDeviceDxVk::CreateVertexBuffer( ShaderBufferType_t type, VertexFormat_t fmt, int nVertexCount, const char *pBudgetGroup )
{
	return ( IsDynamicBufferType( type ) ) ? (IVertexBuffer*)&m_DynamicMesh : (IVertexBuffer*)&m_Mesh;
}

IIndexBuffer *CShaderDeviceDxVk::CreateIndexBuffer( ShaderBufferType_t bufferType, MaterialIndexFormat_t fmt, int nIndexCount, const char *pBudgetGroup )
{
	switch( bufferType )
	{
	case SHADER_BUFFER_TYPE_STATIC:
	case SHADER_BUFFER_TYPE_STATIC_TEMP:
		return (IIndexBuffer*)&m_Mesh;
	default:
	case SHADER_BUFFER_TYPE_DYNAMIC:
	case SHADER_BUFFER_TYPE_DYNAMIC_TEMP:
		return (IIndexBuffer*)&m_DynamicMesh;
	}
}

IVertexBuffer *CShaderDeviceDxVk::GetDynamicVertexBuffer( int streamID, VertexFormat_t vertexFormat, bool bBuffered )
{
	return (IVertexBuffer*)&m_DynamicMesh;
}

IIndexBuffer *CShaderDeviceDxVk::GetDynamicIndexBuffer( MaterialIndexFormat_t fmt, bool bBuffered )
{
	return (IIndexBuffer*)&m_Mesh;
}

bool CShaderDeviceDxVk::InitDxVk( void* hWnd, int nAdapter, const ShaderDeviceInfo_t& info )
{
	m_hWnd = hWnd;
	m_nWindowWidth  = info.m_DisplayMode.m_nWidth;
	m_nWindowHeight = info.m_DisplayMode.m_nHeight;
	m_nBackBufferWidth  = ( info.m_DisplayMode.m_nWidth  > 0 ) ? info.m_DisplayMode.m_nWidth  : 1920;
	m_nBackBufferHeight = ( info.m_DisplayMode.m_nHeight > 0 ) ? info.m_DisplayMode.m_nHeight : 1080;
	m_nAASamples = info.m_nAASamples;

	// Initialize the singleton DXVK adapter
	static CDxvkAdapter s_adapter;
	if ( g_pDxvkAdapter == nullptr )
		g_pDxvkAdapter = &s_adapter;

	if ( !g_pDxvkAdapter->IsInitialized() )
	{
		// If Vulkan init fails, fall back gracefully (software-ish path) and still report success
		// so material system keeps running. The draw calls will be no-ops.
		g_pDxvkAdapter->InitVulkan( hWnd, nAdapter );
	}

	m_bGraphicsInited = true;
	return true;
}

void CShaderDeviceDxVk::ShutdownDxVk()
{
	m_bGraphicsInited = false;
}

//=============================================================================
// CShaderDeviceMgrDxVk Implementation
//=============================================================================
bool CShaderDeviceMgrDxVk::EnsureAdapterInitialized()
{
	if ( !g_pDxvkAdapter )
	{
		static CDxvkAdapter s_globalAdapter;
		g_pDxvkAdapter = &s_globalAdapter;
	}
	return true;
}

bool CShaderDeviceMgrDxVk::Connect( CreateInterfaceFn factory )
{
	g_pShaderUtil = (IShaderUtil*)factory( SHADER_UTIL_INTERFACE_VERSION, NULL );
	EnsureAdapterInitialized();
	return true;
}

void CShaderDeviceMgrDxVk::Disconnect()
{
	g_pShaderUtil = NULL;
}

void *CShaderDeviceMgrDxVk::QueryInterface( const char *pInterfaceName )
{
	if ( !Q_stricmp( pInterfaceName, SHADER_DEVICE_MGR_INTERFACE_VERSION ) )
		return static_cast< IShaderDeviceMgr* >( this );
	if ( !Q_stricmp( pInterfaceName, MATERIALSYSTEM_HARDWARECONFIG_INTERFACE_VERSION ) )
		return reinterpret_cast< IMaterialSystemHardwareConfig* >( &g_ShaderAPIDxVk );
	return NULL;
}

InitReturnVal_t CShaderDeviceMgrDxVk::Init()
{
	return INIT_OK;
}

void CShaderDeviceMgrDxVk::Shutdown()
{
	s_ShaderDeviceDxVk.ShutdownDxVk();
}

int CShaderDeviceMgrDxVk::GetAdapterCount() const
{
	// Would enumerate via Vulkan. For now, report at least 1 adapter to allow startup.
	return 1;
}

void CShaderDeviceMgrDxVk::GetAdapterInfo( int nAdapter, MaterialAdapterInfo_t& info ) const
{
	memset( &info, 0, sizeof( info ) );
	info.m_nDXSupportLevel = 98;   // Report DX9.0c+ equivalent
	info.m_VendorID = 0x10DE;      // NVIDIA placeholder (cosmetic only)
	info.m_DeviceID = 0;
	Q_strncpy( info.m_pDriverName, "dxvk_vulkan", sizeof( info.m_pDriverName ) );
}

bool CShaderDeviceMgrDxVk::GetRecommendedConfigurationInfo( int nAdapter, int nDXLevel, KeyValues *pConfiguration )
{
	return true;
}

int CShaderDeviceMgrDxVk::GetModeCount( int nAdapter ) const
{
	return 1;
}

void CShaderDeviceMgrDxVk::GetModeInfo( ShaderDisplayMode_t* pInfo, int nAdapter, int nMode ) const
{
	if ( !pInfo ) return;
	memset( pInfo, 0, sizeof(*pInfo) );
	pInfo->m_nVersion = SHADER_DISPLAY_MODE_VERSION;
	pInfo->m_nWidth = 1920;
	pInfo->m_nHeight = 1080;
	pInfo->m_Format = IMAGE_FORMAT_BGRA8888;
	pInfo->m_nRefreshRateNumerator = 60;
	pInfo->m_nRefreshRateDenominator = 1;
}

void CShaderDeviceMgrDxVk::GetCurrentModeInfo( ShaderDisplayMode_t* pInfo, int nAdapter ) const
{
	GetModeInfo( pInfo, nAdapter, 0 );
}

bool CShaderDeviceMgrDxVk::SetAdapter( int nAdapter, int nFlags )
{
	return true;
}

CreateInterfaceFn CShaderDeviceMgrDxVk::SetMode( void *hWnd, int nAdapter, const ShaderDeviceInfo_t& mode )
{
	if ( !s_ShaderDeviceDxVk.InitDxVk( hWnd, nAdapter, mode ) )
		return NULL;
	return ShaderInterfaceFactoryDxVk;
}

// ---- Export single interfaces so material system can find us without SetMode ----
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CShaderDeviceMgrDxVk, IShaderDeviceMgr,
								   SHADER_DEVICE_MGR_INTERFACE_VERSION, s_ShaderDeviceMgrDxVk )
