//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK-based Shader API - Header for Vulkan adapter
//
// $NoKeywords: $
//
//===========================================================================//

#ifndef DXVK_ADAPTER_H
#define DXVK_ADAPTER_H

#ifdef _WIN32
#pragma once
#endif

#include "tier0/platform.h"
#include "tier0/dbg.h"

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#else
#define VK_USE_PLATFORM_XLIB_KHR
#define VK_USE_PLATFORM_XCB_KHR
#endif

#include <vulkan/vulkan.h>

// Vulkan forward declarations (only used when vulkan.h is unavailable)
#ifndef VULKAN_H_
typedef struct VkInstance_T* VkInstance;
typedef struct VkPhysicalDevice_T* VkPhysicalDevice;
typedef struct VkDevice_T* VkDevice;
typedef struct VkQueue_T* VkQueue;
typedef struct VkSurfaceKHR_T* VkSurfaceKHR;
typedef struct VkSwapchainKHR_T* VkSwapchainKHR;
typedef struct VkCommandPool_T* VkCommandPool;
typedef struct VkCommandBuffer_T* VkCommandBuffer;
typedef struct VkFence_T* VkFence;
typedef struct VkSemaphore_T* VkSemaphore;
typedef uint32_t VkResult;
typedef uint32_t VkFlags;
typedef uint64_t VkDeviceSize;

#define VK_NULL_HANDLE 0ULL
#endif // VULKAN_H_

//-----------------------------------------------------------------------------
// DXVK Adapter - Core Vulkan instance and device management
//-----------------------------------------------------------------------------
class CDxvkAdapter
{
public:
	CDxvkAdapter();
	~CDxvkAdapter();

	// Initialization
	bool		InitVulkan( void* hWnd, int nAdapterIdx = 0 );
	void		ShutdownVulkan();

	// Instance accessors
	VkInstance	GetInstance() const { return m_vkInstance; }
	VkPhysicalDevice GetPhysicalDevice() const { return m_vkPhysicalDevice; }
	VkDevice	GetDevice() const { return m_vkDevice; }
	VkQueue		GetGraphicsQueue() const { return m_vkGraphicsQueue; }
	VkQueue		GetPresentQueue() const { return m_vkPresentQueue; }
	VkSurfaceKHR GetSurface() const { return m_vkSurface; }

	// Queue family indices
	uint32_t	GetGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
	uint32_t	GetPresentQueueFamily() const { return m_presentQueueFamily; }

	// Surface capabilities
	bool		GetSurfaceCapabilities( uint32_t& nMinImageCount, uint32_t& nMaxImageCount,
									  uint32_t& nWidth, uint32_t& nHeight );

	// Supported formats
	bool		IsFormatSupported( uint32_t vkFormat, uint32_t vkImageTiling,
								  uint32_t vkFormatFeatureFlags );

	// Memory type lookup
	uint32_t	FindMemoryType( uint32_t typeBits, uint32_t properties );

	// Debug utilities
	void		BeginDebugLabel( VkCommandBuffer cmdBuf, const char* pLabelName, float color[4] = nullptr );
	void		EndDebugLabel( VkCommandBuffer cmdBuf );
	void		InsertDebugLabel( VkCommandBuffer cmdBuf, const char* pLabelName, float color[4] = nullptr );

	// Status
	bool		IsInitialized() const { return m_bInitialized; }
	const char* GetLastError() const { return m_szLastError; }

private:
	// Internal helpers
	bool		CreateInstance();
	bool		CreateDebugMessenger();
	bool		SelectPhysicalDevice( int nAdapterIdx );
	bool		CreateSurface( void* hWnd );
	bool		FindQueueFamilies();
	bool		CreateLogicalDevice();
	bool		LoadVulkanFunctions();

	// Vulkan handles
	VkInstance		m_vkInstance;
	VkPhysicalDevice m_vkPhysicalDevice;
	VkDevice		m_vkDevice;
	VkQueue			m_vkGraphicsQueue;
	VkQueue			m_vkPresentQueue;
	VkSurfaceKHR	m_vkSurface;
	void*			m_pDebugMessenger;

	// Queue families
	uint32_t		m_graphicsQueueFamily;
	uint32_t		m_presentQueueFamily;

	// State
	bool			m_bInitialized;
	bool			m_bValidationEnabled;
	char			m_szLastError[ 512 ];
};

//-----------------------------------------------------------------------------
// Global DXVK adapter singleton
//-----------------------------------------------------------------------------
extern CDxvkAdapter* g_pDxvkAdapter;

// Helper macro to check Vulkan results
#define DXVK_CHECK_VK(result, msg) \
	do { \
		if ( (result) != 0 ) { \
			Warning( "DXVK: %s failed (result=%d)\n", msg, (int)(result) ); \
			return false; \
		} \
	} while(0)

#endif // DXVK_ADAPTER_H
