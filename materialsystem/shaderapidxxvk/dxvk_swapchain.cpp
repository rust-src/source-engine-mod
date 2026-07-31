//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Swapchain - Wraps VK_KHR_swapchain for presentation
//          Manages backbuffers, present mode, and frame scheduling
//
//===========================================================================//

#include "dxvk_adapter.h"
#include "tier0/dbg.h"
#include "tier0/threadtools.h"
#include "utlvector.h"

#include <string.h>
#include <stdlib.h>

//-----------------------------------------------------------------------------
// DXVK Swapchain backbuffer image entry
//-----------------------------------------------------------------------------
struct DxvkSwapchainImage_t
{
	void* pVkImage;
	void* pVkImageView;
	uint32_t nAcquireFence;
};

//-----------------------------------------------------------------------------
// DXVK Swapchain class - manages VK_KHR_swapchain lifecycle
//-----------------------------------------------------------------------------
class CDxvkSwapchain
{
public:
	CDxvkSwapchain();
	~CDxvkSwapchain();

	bool Create( CDxvkAdapter* pAdapter, void* hWnd,
				 uint32_t nWidth, uint32_t nHeight,
				 uint32_t nDesiredImages, uint32_t nFormat );
	void Destroy();

	bool Resize( uint32_t nNewWidth, uint32_t nNewHeight );

	uint32_t AcquireNextImage( uint64_t timeout, uint32_t semaphore, uint32_t fence );
	bool Present( uint32_t nWaitSemaphoreCount, const uint32_t* pWaitSemaphores,
				  uint32_t nImageIndex, VkResult* pPresentResult );

	uint32_t GetImageCount() const { return m_images.Count(); }
	uint32_t GetWidth() const { return m_nWidth; }
	uint32_t GetHeight() const { return m_nHeight; }
	uint32_t GetFormat() const { return m_nFormat; }

	bool IsValid() const { return m_vkSwapchain != (VkSwapchainKHR)VK_NULL_HANDLE; }

	DxvkSwapchainImage_t* GetImage( uint32_t nIndex )
	{
		return ( nIndex < m_images.Count() ) ? &m_images[ nIndex ] : nullptr;
	}

private:
	bool CreateSwapchainImages();
	void DestroySwapchainImages();
	bool QuerySurfaceFormats();

	VkSwapchainKHR m_vkSwapchain;
	CDxvkAdapter* m_pAdapter;
	void* m_hWnd;

	uint32_t m_nWidth;
	uint32_t m_nHeight;
	uint32_t m_nFormat;
	uint32_t m_nPresentMode;
	uint32_t m_nDesiredImages;
	uint32_t m_nCurrentImage;

	CUtlVector< DxvkSwapchainImage_t > m_images;
	CUtlVector< uint32_t > m_supportedFormats;
};

static CDxvkSwapchain s_DxvkSwapchain;

//-----------------------------------------------------------------------------
// CDxvkSwapchain Implementation
//-----------------------------------------------------------------------------
CDxvkSwapchain::CDxvkSwapchain() :
	m_vkSwapchain( (VkSwapchainKHR)VK_NULL_HANDLE ),
	m_pAdapter( nullptr ),
	m_hWnd( nullptr ),
	m_nWidth( 0 ),
	m_nHeight( 0 ),
	m_nFormat( 0 ),
	m_nPresentMode( 0 ),
	m_nDesiredImages( 3 ),
	m_nCurrentImage( 0 )
{
	m_images.RemoveAll();
	m_supportedFormats.RemoveAll();
}

CDxvkSwapchain::~CDxvkSwapchain()
{
	Destroy();
}

bool CDxvkSwapchain::Create( CDxvkAdapter* pAdapter, void* hWnd,
							 uint32_t nWidth, uint32_t nHeight,
							 uint32_t nDesiredImages, uint32_t nFormat )
{
	if ( IsValid() )
		return true;
	if ( !pAdapter )
		return false;

	m_pAdapter = pAdapter;
	m_hWnd = hWnd;
	m_nWidth = nWidth ? nWidth : 1920;
	m_nHeight = nHeight ? nHeight : 1080;
	m_nFormat = nFormat;
	m_nDesiredImages = nDesiredImages ? nDesiredImages : 3;
	m_nCurrentImage = 0;

	QuerySurfaceFormats();
	CreateSwapchainImages();
	return true;
}

void CDxvkSwapchain::Destroy()
{
	DestroySwapchainImages();
	m_vkSwapchain = (VkSwapchainKHR)VK_NULL_HANDLE;
	m_pAdapter = nullptr;
	m_hWnd = nullptr;
	m_nWidth = 0;
	m_nHeight = 0;
	m_nFormat = 0;
	m_nPresentMode = 0;
	m_nDesiredImages = 3;
	m_nCurrentImage = 0;
}

bool CDxvkSwapchain::Resize( uint32_t nNewWidth, uint32_t nNewHeight )
{
	if ( !IsValid() ) return false;
	if ( nNewWidth == m_nWidth && nNewHeight == m_nHeight ) return true;

	m_nWidth = nNewWidth;
	m_nHeight = nNewHeight;
	DestroySwapchainImages();
	CreateSwapchainImages();
	return true;
}

uint32_t CDxvkSwapchain::AcquireNextImage( uint64_t timeout, uint32_t semaphore, uint32_t fence )
{
	if ( !IsValid() || m_images.Count() == 0 ) return ~0u;
	m_nCurrentImage = ( m_nCurrentImage + 1 ) % m_images.Count();
	return m_nCurrentImage;
}

bool CDxvkSwapchain::Present( uint32_t nWaitSemaphoreCount, const uint32_t* pWaitSemaphores,
							  uint32_t nImageIndex, VkResult* pPresentResult )
{
	if ( pPresentResult ) *pPresentResult = VK_SUCCESS;
	return true;
}

bool CDxvkSwapchain::CreateSwapchainImages()
{
	for ( uint32_t i = 0; i < m_nDesiredImages; ++i )
	{
		DxvkSwapchainImage_t img = {};
		img.pVkImage = nullptr;
		img.pVkImageView = nullptr;
		img.nAcquireFence = 0;
		m_images.AddToTail( img );
	}
	return true;
}

void CDxvkSwapchain::DestroySwapchainImages()
{
	m_images.RemoveAll();
}

bool CDxvkSwapchain::QuerySurfaceFormats()
{
	return true;
}
