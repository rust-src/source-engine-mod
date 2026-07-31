//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Device - Logical Vulkan device wrapper
//          Manages VkDevice, queues, and device-level function dispatch
//
//===========================================================================//

#include "dxvk_adapter.h"
#include "tier0/dbg.h"
#include "tier0/threadtools.h"
#include "utlvector.h"

#include <string.h>

//-----------------------------------------------------------------------------
// DXVK Device class - wraps VkDevice with queue access and feature tracking
//-----------------------------------------------------------------------------
class CDxvkDevice
{
public:
	CDxvkDevice();
	~CDxvkDevice();

	bool CreateFromAdapter( CDxvkAdapter* pAdapter );
	void Destroy();

	VkDevice GetHandle() const { return m_vkDevice; }
	VkQueue GetGraphicsQueue() const { return m_vkGraphicsQueue; }
	VkQueue GetPresentQueue() const { return m_vkPresentQueue; }
	VkQueue GetTransferQueue() const { return m_vkTransferQueue; }

	uint32_t GetGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
	uint32_t GetPresentQueueFamily() const { return m_presentQueueFamily; }
	uint32_t GetTransferQueueFamily() const { return m_transferQueueFamily; }

	bool IsValid() const { return m_vkDevice != (VkDevice)VK_NULL_HANDLE; }
	bool IsFeatureEnabled( uint32_t featureBit ) const;

	void WaitIdle();

private:
	bool LoadDeviceFunctions();
	bool QueryQueueFamilies();

	VkDevice m_vkDevice;
	VkQueue m_vkGraphicsQueue;
	VkQueue m_vkPresentQueue;
	VkQueue m_vkTransferQueue;

	uint32_t m_graphicsQueueFamily;
	uint32_t m_presentQueueFamily;
	uint32_t m_transferQueueFamily;

	uint64_t m_enabledFeatureBits;
	CDxvkAdapter* m_pAdapter;
};

static CDxvkDevice s_DxvkDevice;

//-----------------------------------------------------------------------------
// CDxvkDevice Implementation
//-----------------------------------------------------------------------------
CDxvkDevice::CDxvkDevice() :
	m_vkDevice( (VkDevice)VK_NULL_HANDLE ),
	m_vkGraphicsQueue( (VkQueue)VK_NULL_HANDLE ),
	m_vkPresentQueue( (VkQueue)VK_NULL_HANDLE ),
	m_vkTransferQueue( (VkQueue)VK_NULL_HANDLE ),
	m_graphicsQueueFamily( ~0u ),
	m_presentQueueFamily( ~0u ),
	m_transferQueueFamily( ~0u ),
	m_enabledFeatureBits( 0 ),
	m_pAdapter( nullptr )
{
}

CDxvkDevice::~CDxvkDevice()
{
	Destroy();
}

bool CDxvkDevice::CreateFromAdapter( CDxvkAdapter* pAdapter )
{
	if ( !pAdapter ) return false;
	if ( IsValid() ) return true;

	m_pAdapter = pAdapter;

	if ( pAdapter->IsInitialized() )
	{
		m_vkDevice = pAdapter->GetDevice();
		m_vkGraphicsQueue = pAdapter->GetGraphicsQueue();
		m_vkPresentQueue = pAdapter->GetPresentQueue();
		m_graphicsQueueFamily = pAdapter->GetGraphicsQueueFamily();
		m_presentQueueFamily = pAdapter->GetPresentQueueFamily();
		m_transferQueueFamily = m_graphicsQueueFamily;
		m_vkTransferQueue = m_vkGraphicsQueue;
		m_enabledFeatureBits = ~0ull;
		return IsValid();
	}

	return false;
}

void CDxvkDevice::Destroy()
{
	m_vkDevice = (VkDevice)VK_NULL_HANDLE;
	m_vkGraphicsQueue = (VkQueue)VK_NULL_HANDLE;
	m_vkPresentQueue = (VkQueue)VK_NULL_HANDLE;
	m_vkTransferQueue = (VkQueue)VK_NULL_HANDLE;
	m_graphicsQueueFamily = ~0u;
	m_presentQueueFamily = ~0u;
	m_transferQueueFamily = ~0u;
	m_enabledFeatureBits = 0;
	m_pAdapter = nullptr;
}

bool CDxvkDevice::IsFeatureEnabled( uint32_t featureBit ) const
{
	return ( m_enabledFeatureBits & ( 1ull << featureBit ) ) != 0;
}

void CDxvkDevice::WaitIdle()
{
}

bool CDxvkDevice::LoadDeviceFunctions()
{
	return true;
}

bool CDxvkDevice::QueryQueueFamilies()
{
	return true;
}
