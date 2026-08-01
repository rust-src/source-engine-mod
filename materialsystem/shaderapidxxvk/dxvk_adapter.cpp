//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK-based Shader API - Core Vulkan adapter implementation
//          Manages VkInstance, VkPhysicalDevice, VkDevice
//
//===========================================================================//

#include "dxvk_adapter.h"
#include "tier0/threadtools.h"
#include "tier0/icommandline.h"
#include "tier1/strtools.h"

#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// Global singleton
CDxvkAdapter* g_pDxvkAdapter = nullptr;

//-----------------------------------------------------------------------------
// Minimal Vulkan typedefs (to avoid pulling in full vulkan.h in header)
//-----------------------------------------------------------------------------
#ifndef VULKAN_H_
typedef struct VkAllocationCallbacks VkAllocationCallbacks;

typedef struct VkInstanceCreateInfo {
	uint32_t sType;
	const void* pNext;
	uint32_t flags;
	const void* pApplicationInfo;
	uint32_t enabledLayerCount;
	const char* const* ppEnabledLayerNames;
	uint32_t enabledExtensionCount;
	const char* const* ppEnabledExtensionNames;
} VkInstanceCreateInfo;

typedef struct VkDeviceQueueCreateInfo {
	uint32_t sType;
	const void* pNext;
	uint32_t flags;
	uint32_t queueFamilyIndex;
	uint32_t queueCount;
	const float* pQueuePriorities;
} VkDeviceQueueCreateInfo;

typedef struct VkDeviceCreateInfo {
	uint32_t sType;
	const void* pNext;
	uint32_t flags;
	uint32_t queueCreateInfoCount;
	const VkDeviceQueueCreateInfo* pQueueCreateInfos;
	uint32_t enabledLayerCount;
	const char* const* ppEnabledLayerNames;
	uint32_t enabledExtensionCount;
	const char* const* ppEnabledExtensionNames;
	const void* pEnabledFeatures;
} VkDeviceCreateInfo;

typedef struct VkPhysicalDeviceFeatures VkPhysicalDeviceFeatures;
typedef struct VkPhysicalDeviceProperties VkPhysicalDeviceProperties;
typedef struct VkPhysicalDeviceMemoryProperties VkPhysicalDeviceMemoryProperties;

#endif // !VULKAN_H_

//-----------------------------------------------------------------------------
// Vulkan function pointers - we load them dynamically to avoid hard-linking
//-----------------------------------------------------------------------------
struct DxvkVkFunctions
{
	// Instance functions
	PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
	PFN_vkCreateInstance vkCreateInstance;
	PFN_vkDestroyInstance vkDestroyInstance;
	PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
	PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
	PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures;
	PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
	PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
	PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
	PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR;
	PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties;

	// Device functions
	PFN_vkCreateDevice vkCreateDevice;
	PFN_vkDestroyDevice vkDestroyDevice;
	PFN_vkGetDeviceQueue vkGetDeviceQueue;
	PFN_vkDeviceWaitIdle vkDeviceWaitIdle;

	// Surface
	PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;

#ifdef _WIN32
	PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;
#else
	PFN_vkCreateXcbSurfaceKHR vkCreateXcbSurfaceKHR;
	PFN_vkCreateXlibSurfaceKHR vkCreateXlibSurfaceKHR;
#endif
};

static DxvkVkFunctions s_vk;
static void* s_pVulkanLib = nullptr;

//-----------------------------------------------------------------------------
// CDxvkAdapter
//-----------------------------------------------------------------------------
CDxvkAdapter::CDxvkAdapter() :
	m_vkInstance( (VkInstance)VK_NULL_HANDLE ),
	m_vkPhysicalDevice( (VkPhysicalDevice)VK_NULL_HANDLE ),
	m_vkDevice( (VkDevice)VK_NULL_HANDLE ),
	m_vkGraphicsQueue( (VkQueue)VK_NULL_HANDLE ),
	m_vkPresentQueue( (VkQueue)VK_NULL_HANDLE ),
	m_vkSurface( (VkSurfaceKHR)VK_NULL_HANDLE ),
	m_pDebugMessenger( nullptr ),
	m_graphicsQueueFamily( ~0u ),
	m_presentQueueFamily( ~0u ),
	m_bInitialized( false ),
	m_bValidationEnabled( false )
{
	memset( m_szLastError, 0, sizeof(m_szLastError) );
}

CDxvkAdapter::~CDxvkAdapter()
{
	ShutdownVulkan();
}

void CDxvkAdapter::ShutdownVulkan()
{
	if ( !m_bInitialized )
		return;

	// Wait for device idle before destroying anything
	if ( m_vkDevice && s_vk.vkDeviceWaitIdle )
	{
		s_vk.vkDeviceWaitIdle( m_vkDevice );
	}

	// Destroy surface
	if ( m_vkSurface != (VkSurfaceKHR)VK_NULL_HANDLE && s_vk.vkDestroySurfaceKHR )
	{
		s_vk.vkDestroySurfaceKHR( m_vkInstance, m_vkSurface, nullptr );
		m_vkSurface = (VkSurfaceKHR)VK_NULL_HANDLE;
	}

	// Destroy device
	if ( m_vkDevice != (VkDevice)VK_NULL_HANDLE && s_vk.vkDestroyDevice )
	{
		s_vk.vkDestroyDevice( m_vkDevice, nullptr );
		m_vkDevice = (VkDevice)VK_NULL_HANDLE;
	}

	// Destroy instance
	if ( m_vkInstance != (VkInstance)VK_NULL_HANDLE && s_vk.vkDestroyInstance )
	{
		s_vk.vkDestroyInstance( m_vkInstance, nullptr );
		m_vkInstance = (VkInstance)VK_NULL_HANDLE;
	}

	m_vkPhysicalDevice = (VkPhysicalDevice)VK_NULL_HANDLE;
	m_vkGraphicsQueue = (VkQueue)VK_NULL_HANDLE;
	m_vkPresentQueue = (VkQueue)VK_NULL_HANDLE;
	m_bInitialized = false;
}

bool CDxvkAdapter::InitVulkan( void* hWnd, int nAdapterIdx )
{
	if ( m_bInitialized )
		return true;

	if ( g_pDxvkAdapter == nullptr )
		g_pDxvkAdapter = this;

	// Check for validation enable
	m_bValidationEnabled = CommandLine()->FindParm( "-vkinfo" ) != 0 ||
						   CommandLine()->FindParm( "-vkvalidate" ) != 0;

	// 1. Load Vulkan library dynamically
	if ( !LoadVulkanFunctions() )
	{
		V_snprintf( m_szLastError, sizeof(m_szLastError),
					"Failed to load Vulkan library. Please install Vulkan drivers." );
		Warning( "DXVK: %s\n", m_szLastError );
		return false;
	}

	// 2. Create Vulkan instance
	if ( !CreateInstance() )
		return false;

	// 3. Create surface for the window
	if ( !CreateSurface( hWnd ) )
		return false;

	// 4. Select a physical device (GPU)
	if ( !SelectPhysicalDevice( nAdapterIdx ) )
		return false;

	// 5. Find queue families
	if ( !FindQueueFamilies() )
		return false;

	// 6. Create logical device
	if ( !CreateLogicalDevice() )
		return false;

	m_bInitialized = true;
	Msg( "DXVK: Vulkan adapter initialized successfully\n" );
	return true;
}

bool CDxvkAdapter::LoadVulkanFunctions()
{
#ifdef _WIN32
	s_pVulkanLib = (void*)LoadLibraryA( "vulkan-1.dll" );
#else
	s_pVulkanLib = dlopen( "libvulkan.so.1", RTLD_NOW | RTLD_LOCAL );
	if ( !s_pVulkanLib )
		s_pVulkanLib = dlopen( "libvulkan.so", RTLD_NOW | RTLD_LOCAL );
#endif

	if ( !s_pVulkanLib )
	{
		V_snprintf( m_szLastError, sizeof(m_szLastError),
					"Could not load Vulkan runtime library" );
		return false;
	}

	// Load the global "loader" function
#ifdef _WIN32
	s_vk.vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress( (HMODULE)s_pVulkanLib, "vkGetInstanceProcAddr" );
#else
	s_vk.vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym( s_pVulkanLib, "vkGetInstanceProcAddr" );
#endif

	if ( !s_vk.vkGetInstanceProcAddr )
	{
		V_snprintf( m_szLastError, sizeof(m_szLastError),
					"Could not find vkGetInstanceProcAddr in Vulkan library" );
		return false;
	}

	return true;
}

#define DXVK_LOAD_INSTANCE_FN(name) \
	s_vk.name = (PFN_##name)s_vk.vkGetInstanceProcAddr( m_vkInstance, #name ); \
	if ( !s_vk.name ) { \
		V_snprintf( m_szLastError, sizeof(m_szLastError), \
					"Missing instance function: " #name ); \
		return false; \
	}

bool CDxvkAdapter::CreateInstance()
{
	// Build extension list
	const char* ppExtensions[ 16 ];
	uint32_t nExtCount = 0;

	ppExtensions[ nExtCount++ ] = "VK_KHR_surface";

#ifdef _WIN32
	ppExtensions[ nExtCount++ ] = "VK_KHR_win32_surface";
#else
	// Linux: try both Xlib and Xcb; real impl would use Wayland/X11
	ppExtensions[ nExtCount++ ] = "VK_KHR_xlib_surface";
	ppExtensions[ nExtCount++ ] = "VK_KHR_xcb_surface";
#endif

	if ( m_bValidationEnabled )
	{
		ppExtensions[ nExtCount++ ] = "VK_EXT_debug_utils";
	}

	// Build layer list
	const char* ppLayers[ 4 ];
	uint32_t nLayerCount = 0;
	if ( m_bValidationEnabled )
	{
		ppLayers[ nLayerCount++ ] = "VK_LAYER_KHRONOS_validation";
	}

	// Application info (optional but good practice)
	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Source Engine DXVK";
	appInfo.applicationVersion = (1 << 22);
	appInfo.pEngineName = "Source";
	appInfo.engineVersion = 0;
	appInfo.apiVersion = (1 << 22) | (2 << 12); // 1.2.0

	VkInstanceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledExtensionCount = nExtCount;
	createInfo.ppEnabledExtensionNames = ppExtensions;
	createInfo.enabledLayerCount = nLayerCount;
	createInfo.ppEnabledLayerNames = ppLayers;

	// Load vkCreateInstance via loader
	PFN_vkCreateInstance pCreateInstance = (PFN_vkCreateInstance)
		s_vk.vkGetInstanceProcAddr( (VkInstance)VK_NULL_HANDLE, "vkCreateInstance" );

	if ( !pCreateInstance )
	{
		V_snprintf( m_szLastError, sizeof(m_szLastError),
					"Could not load vkCreateInstance" );
		return false;
	}

	uint32_t result = pCreateInstance( &createInfo, nullptr, &m_vkInstance );
	if ( result != 0 )
	{
		V_snprintf( m_szLastError, sizeof(m_szLastError),
					"vkCreateInstance failed (result=%d). Try running without -vkvalidate.", result );
		Warning( "DXVK: %s\n", m_szLastError );
		// Fallback: try without validation
		if ( m_bValidationEnabled )
		{
			m_bValidationEnabled = false;
			createInfo.enabledLayerCount = 0;
			createInfo.ppEnabledLayerNames = nullptr;
			if ( nExtCount > 0 && Q_stricmp( ppExtensions[ nExtCount-1 ], "VK_EXT_debug_utils" ) == 0 )
				nExtCount--;
			createInfo.enabledExtensionCount = nExtCount;
			result = pCreateInstance( &createInfo, nullptr, &m_vkInstance );
			if ( result != 0 )
				return false;
		}
		else
		{
			return false;
		}
	}

	// Now load the rest of the instance-level functions
	DXVK_LOAD_INSTANCE_FN( vkDestroyInstance );
	DXVK_LOAD_INSTANCE_FN( vkEnumeratePhysicalDevices );
	DXVK_LOAD_INSTANCE_FN( vkGetPhysicalDeviceProperties );
	DXVK_LOAD_INSTANCE_FN( vkGetPhysicalDeviceFeatures );
	DXVK_LOAD_INSTANCE_FN( vkGetPhysicalDeviceMemoryProperties );
	DXVK_LOAD_INSTANCE_FN( vkGetPhysicalDeviceQueueFamilyProperties );
	DXVK_LOAD_INSTANCE_FN( vkGetPhysicalDeviceSurfaceSupportKHR );
	DXVK_LOAD_INSTANCE_FN( vkGetPhysicalDeviceSurfaceCapabilitiesKHR );
	DXVK_LOAD_INSTANCE_FN( vkGetPhysicalDeviceSurfaceFormatsKHR );
	DXVK_LOAD_INSTANCE_FN( vkGetPhysicalDeviceSurfacePresentModesKHR );
	DXVK_LOAD_INSTANCE_FN( vkGetPhysicalDeviceFormatProperties );
	DXVK_LOAD_INSTANCE_FN( vkDestroySurfaceKHR );
#ifdef _WIN32
	DXVK_LOAD_INSTANCE_FN( vkCreateWin32SurfaceKHR );
#else
	DXVK_LOAD_INSTANCE_FN( vkCreateXlibSurfaceKHR );
	DXVK_LOAD_INSTANCE_FN( vkCreateXcbSurfaceKHR );
#endif

	return true;
}

#undef DXVK_LOAD_INSTANCE_FN

bool CDxvkAdapter::CreateSurface( void* hWnd )
{
	// On non-interactive builds hWnd may be null; create a headless-surface workaround
	if ( !hWnd )
	{
		// Return success - we'll create a dummy surface later or operate headless
		// For the integrated DXVK stub, this is acceptable
		return true;
	}

#ifdef _WIN32
	VkWin32SurfaceCreateInfoKHR surfInfo = {};
	surfInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	surfInfo.hinstance = GetModuleHandleA( nullptr );
	surfInfo.hwnd = (HWND)hWnd;

	uint32_t res = s_vk.vkCreateWin32SurfaceKHR( m_vkInstance, &surfInfo, nullptr, &m_vkSurface );
	DXVK_CHECK_VK( res, "vkCreateWin32SurfaceKHR" );
#else
	// Linux: simplified. Real impl would use the display connection
	(void)hWnd;
#endif

	return true;
}

bool CDxvkAdapter::SelectPhysicalDevice( int nAdapterIdx )
{
	uint32_t nDeviceCount = 0;
	uint32_t res = s_vk.vkEnumeratePhysicalDevices( m_vkInstance, &nDeviceCount, nullptr );
	DXVK_CHECK_VK( res, "vkEnumeratePhysicalDevices(count)" );

	if ( nDeviceCount == 0 )
	{
		V_snprintf( m_szLastError, sizeof(m_szLastError),
					"No Vulkan-capable physical devices found" );
		return false;
	}

	if ( nAdapterIdx < 0 ) nAdapterIdx = 0;
	if ( nAdapterIdx >= (int)nDeviceCount ) nAdapterIdx = 0;

	VkPhysicalDevice* pDevices = (VkPhysicalDevice*)stackalloc( sizeof(VkPhysicalDevice) * nDeviceCount );
	res = s_vk.vkEnumeratePhysicalDevices( m_vkInstance, &nDeviceCount, pDevices );
	DXVK_CHECK_VK( res, "vkEnumeratePhysicalDevices" );

	m_vkPhysicalDevice = pDevices[ nAdapterIdx ];

	// Log device info
	VkPhysicalDeviceProperties* pProps = (VkPhysicalDeviceProperties*)stackalloc( 1024 );
	memset( pProps, 0, 1024 );
	s_vk.vkGetPhysicalDeviceProperties( m_vkPhysicalDevice, pProps );

	// Read device name at offset 0x10 in VkPhysicalDeviceProperties
	const char* pDeviceName = (const char*)pProps;
	pDeviceName += 0x10; // offsetof(VkPhysicalDeviceProperties, deviceName)

	Msg( "DXVK: Selected GPU #%d: %s\n", nAdapterIdx, pDeviceName );
	return true;
}

bool CDxvkAdapter::FindQueueFamilies()
{
	uint32_t nQueueFamilyCount = 0;
	s_vk.vkGetPhysicalDeviceQueueFamilyProperties( m_vkPhysicalDevice, &nQueueFamilyCount, nullptr );

	if ( nQueueFamilyCount == 0 )
	{
		V_snprintf( m_szLastError, sizeof(m_szLastError),
					"No queue families found on physical device" );
		return false;
	}

	// Allocate queue family properties. Each is ~56 bytes.
	const uint32_t kQueueFamilySize = 64;
	uint8_t* pPropsBuf = (uint8_t*)stackalloc( kQueueFamilySize * nQueueFamilyCount );
	memset( pPropsBuf, 0, kQueueFamilySize * nQueueFamilyCount );
	s_vk.vkGetPhysicalDeviceQueueFamilyProperties( m_vkPhysicalDevice, &nQueueFamilyCount,
											   (VkQueueFamilyProperties*)pPropsBuf );

	const uint32_t GRAPHICS_BIT = 0x1; // VK_QUEUE_GRAPHICS_BIT

	for ( uint32_t i = 0; i < nQueueFamilyCount; i++ )
	{
		uint32_t queueFlags = *(uint32_t*)( pPropsBuf + i * kQueueFamilySize );
		uint32_t queueCount = *(uint32_t*)( pPropsBuf + i * kQueueFamilySize + 4 );

		if ( queueCount == 0 ) continue;

		// Check for graphics capability
		bool bSupportsGraphics = ( queueFlags & GRAPHICS_BIT ) != 0;

		// Check for present support
		uint32_t bSupportsPresent = 0;
		if ( m_vkSurface != (VkSurfaceKHR)VK_NULL_HANDLE )
		{
			s_vk.vkGetPhysicalDeviceSurfaceSupportKHR( m_vkPhysicalDevice, i, m_vkSurface, &bSupportsPresent );
		}
		else
		{
			bSupportsPresent = bSupportsGraphics ? 1 : 0;
		}

		if ( m_graphicsQueueFamily == ~0u && bSupportsGraphics )
		{
			m_graphicsQueueFamily = i;
		}

		if ( m_presentQueueFamily == ~0u && bSupportsPresent )
		{
			m_presentQueueFamily = i;
		}

		if ( m_graphicsQueueFamily != ~0u && m_presentQueueFamily != ~0u )
			break;
	}

	if ( m_graphicsQueueFamily == ~0u )
	{
		V_snprintf( m_szLastError, sizeof(m_szLastError),
					"Could not find graphics-capable queue family" );
		return false;
	}

	// If we didn't find a separate present family, use the graphics one
	if ( m_presentQueueFamily == ~0u )
	{
		m_presentQueueFamily = m_graphicsQueueFamily;
	}

	return true;
}

bool CDxvkAdapter::CreateLogicalDevice()
{
	// Queue create info
	uint32_t queueFamilies[ 2 ];
	uint32_t nQueueFamilies = 0;
	queueFamilies[ nQueueFamilies++ ] = m_graphicsQueueFamily;
	if ( m_presentQueueFamily != m_graphicsQueueFamily )
		queueFamilies[ nQueueFamilies++ ] = m_presentQueueFamily;

	const int kMaxQueues = 2;
	VkDeviceQueueCreateInfo queueInfos[ kMaxQueues ];
	float queuePriority = 1.0f;
	memset( queueInfos, 0, sizeof(queueInfos) );

	for ( uint32_t i = 0; i < nQueueFamilies; i++ )
	{
		queueInfos[ i ].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueInfos[ i ].queueFamilyIndex = queueFamilies[ i ];
		queueInfos[ i ].queueCount = 1;
		queueInfos[ i ].pQueuePriorities = &queuePriority;
	}

	// Required device extensions
	const char* ppExtensions[ 8 ];
	uint32_t nExtCount = 0;
	ppExtensions[ nExtCount++ ] = "VK_KHR_swapchain";

	// Device features - enable basics we need
	uint8_t featuresBuf[ 512 ];
	memset( featuresBuf, 0, sizeof(featuresBuf) );
	s_vk.vkGetPhysicalDeviceFeatures( m_vkPhysicalDevice, (VkPhysicalDeviceFeatures*)featuresBuf );

	VkDeviceCreateInfo deviceInfo = {};
	deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceInfo.queueCreateInfoCount = nQueueFamilies;
	deviceInfo.pQueueCreateInfos = queueInfos;
	deviceInfo.enabledExtensionCount = nExtCount;
	deviceInfo.ppEnabledExtensionNames = ppExtensions;
	deviceInfo.pEnabledFeatures = (VkPhysicalDeviceFeatures*)featuresBuf;

	// Load vkCreateDevice via instance
	PFN_vkCreateDevice pCreateDevice = (PFN_vkCreateDevice)
		s_vk.vkGetInstanceProcAddr( m_vkInstance, "vkCreateDevice" );
	if ( !pCreateDevice )
	{
		V_snprintf( m_szLastError, sizeof(m_szLastError),
					"Could not load vkCreateDevice" );
		return false;
	}

	uint32_t res = pCreateDevice( m_vkPhysicalDevice, &deviceInfo, nullptr, &m_vkDevice );
	DXVK_CHECK_VK( res, "vkCreateDevice" );

	// Load device-level functions
#define DXVK_LOAD_DEVICE_FN(name) \
	s_vk.name = (PFN_##name)s_vk.vkGetInstanceProcAddr( m_vkInstance, #name ); \
	if ( !s_vk.name ) { return true; } // some may be optional

	s_vk.vkGetDeviceQueue = (PFN_vkGetDeviceQueue)
		s_vk.vkGetInstanceProcAddr( m_vkInstance, "vkGetDeviceQueue" );
	s_vk.vkDestroyDevice = (PFN_vkDestroyDevice)
		s_vk.vkGetInstanceProcAddr( m_vkInstance, "vkDestroyDevice" );
	s_vk.vkDeviceWaitIdle = (PFN_vkDeviceWaitIdle)
		s_vk.vkGetInstanceProcAddr( m_vkInstance, "vkDeviceWaitIdle" );

#undef DXVK_LOAD_DEVICE_FN

	// Get queues
	if ( s_vk.vkGetDeviceQueue )
	{
		s_vk.vkGetDeviceQueue( m_vkDevice, m_graphicsQueueFamily, 0, &m_vkGraphicsQueue );
		s_vk.vkGetDeviceQueue( m_vkDevice, m_presentQueueFamily, 0, &m_vkPresentQueue );
	}

	return true;
}

bool CDxvkAdapter::GetSurfaceCapabilities( uint32_t& nMinImageCount, uint32_t& nMaxImageCount,
										   uint32_t& nWidth, uint32_t& nHeight )
{
	nMinImageCount = 2;
	nMaxImageCount = 3;
	nWidth = 1920;
	nHeight = 1080;

	if ( m_vkSurface == (VkSurfaceKHR)VK_NULL_HANDLE )
		return true;

	// Read VkSurfaceCapabilitiesKHR (simplified - we only need min/max image count and extent)
	uint8_t capsBuf[ 128 ];
	memset( capsBuf, 0, sizeof(capsBuf) );

	uint32_t res = s_vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
		m_vkPhysicalDevice, m_vkSurface, (VkSurfaceCapabilitiesKHR*)capsBuf );

	if ( res != 0 )
		return true; // Fallback to defaults

	// Layout:
	//   uint32_t minImageCount at offset 0x08
	//   uint32_t maxImageCount at offset 0x0C
	//   VkExtent2D currentExtent (width, height) at offset 0x18
	nMinImageCount = *(uint32_t*)( capsBuf + 0x08 );
	nMaxImageCount = *(uint32_t*)( capsBuf + 0x0C );
	if ( nMaxImageCount == 0 ) nMaxImageCount = 8;

	uint32_t w = *(uint32_t*)( capsBuf + 0x18 );
	uint32_t h = *(uint32_t*)( capsBuf + 0x1C );
	if ( w != 0xFFFFFFFF && w > 0 ) nWidth = w;
	if ( h != 0xFFFFFFFF && h > 0 ) nHeight = h;

	return true;
}

bool CDxvkAdapter::IsFormatSupported( uint32_t vkFormat, uint32_t vkImageTiling,
									  uint32_t vkFormatFeatureFlags )
{
	// VkFormatProperties { VkFormatFeatureFlags linearFeatures; VkFormatFeatureFlags optimalFeatures; VkFormatFeatureFlags bufferFeatures; }
	uint64_t props[ 3 ] = { 0, 0, 0 };
	s_vk.vkGetPhysicalDeviceFormatProperties( m_vkPhysicalDevice, (VkFormat)vkFormat, (VkFormatProperties*)props );

	uint64_t features = ( vkImageTiling == 0 ) ? props[0] : props[1]; // LINEAR vs OPTIMAL
	return ( features & vkFormatFeatureFlags ) == vkFormatFeatureFlags;
}

uint32_t CDxvkAdapter::FindMemoryType( uint32_t typeBits, uint32_t properties )
{
	// VkPhysicalDeviceMemoryProperties layout (simplified):
	//   uint32_t memoryTypeCount at offset 0
	//   VkMemoryType memoryTypes[32] at offset 8 (each = uint32_t propertyFlags + uint32_t heapIndex = 8 bytes)
	//   uint32_t memoryHeapCount at offset 8 + 32*8 = 264
	uint8_t memPropsBuf[ 512 ];
	memset( memPropsBuf, 0, sizeof(memPropsBuf) );
	s_vk.vkGetPhysicalDeviceMemoryProperties( m_vkPhysicalDevice, (VkPhysicalDeviceMemoryProperties*)memPropsBuf );

	uint32_t memTypeCount = *(uint32_t*)memPropsBuf;
	if ( memTypeCount > 32 ) memTypeCount = 32;

	for ( uint32_t i = 0; i < memTypeCount; i++ )
	{
		uint32_t memFlags = *(uint32_t*)( memPropsBuf + 8 + i * 8 + 0 );
		if ( ( typeBits & ( 1u << i ) ) &&
			 ( memFlags & properties ) == properties )
		{
			return i;
		}
	}

	return ~0u;
}

void CDxvkAdapter::BeginDebugLabel( VkCommandBuffer cmdBuf, const char* pLabelName, float color[4] )
{
	(void)cmdBuf; (void)pLabelName; (void)color;
}

void CDxvkAdapter::EndDebugLabel( VkCommandBuffer cmdBuf )
{
	(void)cmdBuf;
}

void CDxvkAdapter::InsertDebugLabel( VkCommandBuffer cmdBuf, const char* pLabelName, float color[4] )
{
	(void)cmdBuf; (void)pLabelName; (void)color;
}
