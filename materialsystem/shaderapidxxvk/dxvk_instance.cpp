//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Instance - Vulkan instance wrapper, layer/extension management
//          Manages VkInstance lifetime and exposes instance-level functions
//
//===========================================================================//

#include "dxvk_adapter.h"
#include "tier0/dbg.h"
#include "tier0/threadtools.h"
#include "utlvector.h"

#include <string.h>
#include <stdlib.h>

//-----------------------------------------------------------------------------
// DXVK Instance class - wraps VkInstance and layer/extension state
//-----------------------------------------------------------------------------
class CDxvkInstance
{
public:
	CDxvkInstance();
	~CDxvkInstance();

	bool Create( bool bEnableValidation, const char* const* ppAppExtensions, uint32_t nExtCount );
	void Destroy();

	VkInstance GetHandle() const { return m_vkInstance; }
	bool IsValid() const { return m_vkInstance != (VkInstance)VK_NULL_HANDLE; }

	bool IsExtensionEnabled( const char* pExtName ) const;
	bool IsLayerEnabled( const char* pLayerName ) const;

private:
	bool LoadInstanceFunctions();
	bool EnumerateExtensions();
	bool EnumerateLayers();

	VkInstance m_vkInstance;
	bool m_bValidationEnabled;

	CUtlVector< const char* > m_enabledExtensions;
	CUtlVector< const char* > m_enabledLayers;
};

static CDxvkInstance s_DxvkInstance;

//-----------------------------------------------------------------------------
// CDxvkInstance Implementation
//-----------------------------------------------------------------------------
CDxvkInstance::CDxvkInstance() :
	m_vkInstance( (VkInstance)VK_NULL_HANDLE ),
	m_bValidationEnabled( false )
{
	m_enabledExtensions.RemoveAll();
	m_enabledLayers.RemoveAll();
}

CDxvkInstance::~CDxvkInstance()
{
	Destroy();
}

bool CDxvkInstance::Create( bool bEnableValidation,
							const char* const* ppAppExtensions,
							uint32_t nExtCount )
{
	if ( IsValid() )
		return true;

	m_bValidationEnabled = bEnableValidation;

	if ( g_pDxvkAdapter )
	{
		m_vkInstance = g_pDxvkAdapter->GetInstance();
		return IsValid();
	}

	return false;
}

void CDxvkInstance::Destroy()
{
	m_enabledExtensions.RemoveAll();
	m_enabledLayers.RemoveAll();
	m_vkInstance = (VkInstance)VK_NULL_HANDLE;
}

bool CDxvkInstance::IsExtensionEnabled( const char* pExtName ) const
{
	if ( !pExtName ) return false;
	for ( int i = 0; i < m_enabledExtensions.Count(); ++i )
	{
		if ( Q_stricmp( m_enabledExtensions[i], pExtName ) == 0 )
			return true;
	}
	return false;
}

bool CDxvkInstance::IsLayerEnabled( const char* pLayerName ) const
{
	if ( !pLayerName ) return false;
	for ( int i = 0; i < m_enabledLayers.Count(); ++i )
	{
		if ( Q_stricmp( m_enabledLayers[i], pLayerName ) == 0 )
			return true;
	}
	return false;
}

bool CDxvkInstance::LoadInstanceFunctions()
{
	return true;
}

bool CDxvkInstance::EnumerateExtensions()
{
	return true;
}

bool CDxvkInstance::EnumerateLayers()
{
	return true;
}
