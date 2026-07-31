//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Shader - Vulkan shader module wrapper
//          Manages SPIR-V shader modules, reflection info, and pipeline layouts
//
//===========================================================================//

#include "dxvk_adapter.h"
#include "tier0/dbg.h"
#include "tier0/threadtools.h"
#include "utlvector.h"
#include "utlbuffer.h"

#include <string.h>
#include <stdlib.h>

//-----------------------------------------------------------------------------
// DXVK Shader reflection entry - describes a single resource binding
//-----------------------------------------------------------------------------
struct DxvkShaderResourceBinding_t
{
	uint32_t nDescriptorSet;
	uint32_t nBinding;
	uint32_t nDescriptorType;
	uint32_t nDescriptorCount;
	uint32_t nStageFlags;
};

//-----------------------------------------------------------------------------
// DXVK Shader push constant block description
//-----------------------------------------------------------------------------
struct DxvkShaderPushConstant_t
{
	uint32_t nStageFlags;
	uint32_t nOffset;
	uint32_t nSize;
};

//-----------------------------------------------------------------------------
// DXVK Shader class - wraps VkShaderModule + reflection data
//-----------------------------------------------------------------------------
class CDxvkShader
{
public:
	CDxvkShader();
	~CDxvkShader();

	bool CreateFromSpirv( CDxvkAdapter* pAdapter,
						  uint32_t nStageFlags,
						  const uint32_t* pSpirvCode, size_t nSpirvWordCount,
						  const char* pDebugName = nullptr );
	bool CreateFromBytecode( CDxvkAdapter* pAdapter,
							 uint32_t nStageFlags,
							 const void* pBytecode, size_t nBytecodeSize,
							 const char* pDebugName = nullptr );
	void Destroy();

	// Reflection queries
	uint32_t GetStageFlags() const { return m_nStageFlags; }
	uint32_t GetResourceBindingCount() const { return m_bindings.Count(); }
	const DxvkShaderResourceBinding_t* GetResourceBinding( uint32_t nIndex ) const
	{
		return ( nIndex < m_bindings.Count() ) ? &m_bindings[ nIndex ] : nullptr;
	}
	uint32_t GetPushConstantCount() const { return m_pushConstants.Count(); }
	const DxvkShaderPushConstant_t* GetPushConstant( uint32_t nIndex ) const
	{
		return ( nIndex < m_pushConstants.Count() ) ? &m_pushConstants[ nIndex ] : nullptr;
	}

	// Input/output reflection
	uint32_t GetInputLocationCount() const { return m_nInputLocations; }
	uint32_t GetOutputLocationCount() const { return m_nOutputLocations; }

	// Entry point
	void GetEntryPoint( char* pOutBuf, uint32_t nBufSize ) const
	{
		if ( pOutBuf && nBufSize > 0 )
			Q_strncpy( pOutBuf, m_szEntryPoint, nBufSize );
	}
	void SetEntryPoint( const char* pEntryPoint )
	{
		if ( pEntryPoint )
			Q_strncpy( m_szEntryPoint, pEntryPoint, sizeof(m_szEntryPoint) - 1 );
	}

	// Accessors
	bool IsValid() const { return m_pVkShaderModule != nullptr; }
	void* GetShaderModuleHandle() const { return m_pVkShaderModule; }
	size_t GetSpirvWordCount() const { return m_spirvCode.Count(); }
	const uint32_t* GetSpirvCode() const { return m_spirvCode.Base(); }

	void GetDebugName( char* pOutBuf, uint32_t nBufSize ) const
	{
		if ( pOutBuf && nBufSize > 0 )
			Q_strncpy( pOutBuf, m_szDebugName, nBufSize );
	}

private:
	bool ParseReflection();
	void ClearReflection();

	CDxvkAdapter* m_pAdapter;
	void* m_pVkShaderModule;
	uint32_t m_nStageFlags;

	char m_szEntryPoint[ 64 ];
	char m_szDebugName[ 128 ];

	// Reflection data
	CUtlVector< uint32_t > m_spirvCode;
	CUtlVector< DxvkShaderResourceBinding_t > m_bindings;
	CUtlVector< DxvkShaderPushConstant_t > m_pushConstants;
	uint32_t m_nInputLocations;
	uint32_t m_nOutputLocations;
	uint32_t m_nWorkgroupSizeX;
	uint32_t m_nWorkgroupSizeY;
	uint32_t m_nWorkgroupSizeZ;
};

static CDxvkShader s_DxvkShader;

//-----------------------------------------------------------------------------
// CDxvkShader Implementation
//-----------------------------------------------------------------------------
CDxvkShader::CDxvkShader() :
	m_pAdapter( nullptr ),
	m_pVkShaderModule( nullptr ),
	m_nStageFlags( 0 ),
	m_nInputLocations( 0 ),
	m_nOutputLocations( 0 ),
	m_nWorkgroupSizeX( 1 ),
	m_nWorkgroupSizeY( 1 ),
	m_nWorkgroupSizeZ( 1 )
{
	memset( m_szEntryPoint, 0, sizeof(m_szEntryPoint) );
	memset( m_szDebugName, 0, sizeof(m_szDebugName) );
	Q_strncpy( m_szEntryPoint, "main", sizeof(m_szEntryPoint) - 1 );
	m_bindings.RemoveAll();
	m_pushConstants.RemoveAll();
	m_spirvCode.RemoveAll();
}

CDxvkShader::~CDxvkShader()
{
	Destroy();
}

bool CDxvkShader::CreateFromSpirv( CDxvkAdapter* pAdapter,
								   uint32_t nStageFlags,
								   const uint32_t* pSpirvCode, size_t nSpirvWordCount,
								   const char* pDebugName )
{
	if ( IsValid() ) Destroy();
	if ( !pAdapter || !pSpirvCode || nSpirvWordCount == 0 )
		return false;

	m_pAdapter = pAdapter;
	m_nStageFlags = nStageFlags;

	if ( pDebugName )
		Q_strncpy( m_szDebugName, pDebugName, sizeof(m_szDebugName) - 1 );

	m_spirvCode.EnsureCapacity( (int)nSpirvWordCount );
	for ( size_t i = 0; i < nSpirvWordCount; ++i )
		m_spirvCode.AddToTail( pSpirvCode[ i ] );

	if ( !ParseReflection() )
	{
		ClearReflection();
		return false;
	}

	return true;
}

bool CDxvkShader::CreateFromBytecode( CDxvkAdapter* pAdapter,
									  uint32_t nStageFlags,
									  const void* pBytecode, size_t nBytecodeSize,
									  const char* pDebugName )
{
	if ( nBytecodeSize % 4 != 0 )
		return false;
	size_t nWordCount = nBytecodeSize / 4;
	return CreateFromSpirv( pAdapter, nStageFlags,
							(const uint32_t*)pBytecode, nWordCount, pDebugName );
}

void CDxvkShader::Destroy()
{
	ClearReflection();
	m_pVkShaderModule = nullptr;
	m_nStageFlags = 0;
	memset( m_szEntryPoint, 0, sizeof(m_szEntryPoint) );
	memset( m_szDebugName, 0, sizeof(m_szDebugName) );
	Q_strncpy( m_szEntryPoint, "main", sizeof(m_szEntryPoint) - 1 );
	m_nInputLocations = 0;
	m_nOutputLocations = 0;
	m_nWorkgroupSizeX = 1;
	m_nWorkgroupSizeY = 1;
	m_nWorkgroupSizeZ = 1;
	m_pAdapter = nullptr;
}

bool CDxvkShader::ParseReflection()
{
	return true;
}

void CDxvkShader::ClearReflection()
{
	m_bindings.RemoveAll();
	m_pushConstants.RemoveAll();
	m_spirvCode.RemoveAll();
}
