//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DXVK Color format conversion utilities
//          Maps Source ImageFormat <-> Vulkan VkFormat
//
//===========================================================================//

#include "tier0/platform.h"
#include "bitmap/imageformat.h"
#include "dxvk_adapter.h"

//-----------------------------------------------------------------------------
// ImageFormat -> Vulkan format mapping
// Vulkan format enum values (from vulkan_core.h, hard-coded here to avoid
// requiring the Vulkan SDK at build time; kept in sync with Vulkan 1.2)
//-----------------------------------------------------------------------------
enum EDxvkVkFormat
{
	DXVK_VK_FORMAT_UNDEFINED = 0,
	DXVK_VK_FORMAT_R8G8B8A8_UNORM = 37,
	DXVK_VK_FORMAT_B8G8R8A8_UNORM = 44,
	DXVK_VK_FORMAT_A8B8G8R8_UNORM_PACK32 = 40,
	DXVK_VK_FORMAT_R8G8B8_UNORM = 23,
	DXVK_VK_FORMAT_B8G8R8_UNORM = 30,
	DXVK_VK_FORMAT_R5G6B5_UNORM_PACK16 = 3,
	DXVK_VK_FORMAT_BC1_RGBA_UNORM_BLOCK = 131,
	DXVK_VK_FORMAT_BC2_UNORM_BLOCK = 132,
	DXVK_VK_FORMAT_BC3_UNORM_BLOCK = 133,
	DXVK_VK_FORMAT_R16G16B16A16_UNORM = 92,
	DXVK_VK_FORMAT_R16G16B16A16_SFLOAT = 97,
	DXVK_VK_FORMAT_R32G32B32A32_SFLOAT = 109,
	DXVK_VK_FORMAT_R32_SFLOAT = 100,
	DXVK_VK_FORMAT_R16G16_SFLOAT = 95,
	DXVK_VK_FORMAT_R32G32_SFLOAT = 103,
	DXVK_VK_FORMAT_R16G16_UNORM = 84,
	DXVK_VK_FORMAT_R32_UINT = 98,
	DXVK_VK_FORMAT_D32_SFLOAT = 126,
	DXVK_VK_FORMAT_D24_UNORM_S8_UINT = 129,
	DXVK_VK_FORMAT_D32_SFLOAT_S8_UINT = 130,
};

//-----------------------------------------------------------------------------
// Convert a Source ImageFormat to the closest Vulkan format
//-----------------------------------------------------------------------------
uint32_t DxvkImageFormatToVkFormat( ImageFormat srcFmt, bool bSRGB = false )
{
	// bSRGB variant adds +1 in Vulkan for most UNORM formats
	const uint32_t SRGB_DELTA = 1;
	uint32_t fmt;

	switch ( srcFmt )
	{
	case IMAGE_FORMAT_RGBA8888:
		fmt = DXVK_VK_FORMAT_R8G8B8A8_UNORM; break;
	case IMAGE_FORMAT_ABGR8888:
		fmt = DXVK_VK_FORMAT_A8B8G8R8_UNORM_PACK32; break;
	case IMAGE_FORMAT_BGRA8888:
	case IMAGE_FORMAT_BGRA4444:
	case IMAGE_FORMAT_BGRA5551:
		fmt = DXVK_VK_FORMAT_B8G8R8A8_UNORM; break;
	case IMAGE_FORMAT_ARGB8888:
		fmt = DXVK_VK_FORMAT_A8B8G8R8_UNORM_PACK32; break;
	case IMAGE_FORMAT_RGB888:
		fmt = DXVK_VK_FORMAT_R8G8B8_UNORM; break;
	case IMAGE_FORMAT_BGR888:
		fmt = DXVK_VK_FORMAT_B8G8R8_UNORM; break;
	case IMAGE_FORMAT_RGB565:
		fmt = DXVK_VK_FORMAT_R5G6B5_UNORM_PACK16; break;
	case IMAGE_FORMAT_DXT1:
	case IMAGE_FORMAT_DXT1_ONEBITALPHA:
		fmt = DXVK_VK_FORMAT_BC1_RGBA_UNORM_BLOCK; break;
	case IMAGE_FORMAT_DXT3:
		fmt = DXVK_VK_FORMAT_BC2_UNORM_BLOCK; break;
	case IMAGE_FORMAT_DXT5:
		fmt = DXVK_VK_FORMAT_BC3_UNORM_BLOCK; break;
	case IMAGE_FORMAT_BGRX8888:
		fmt = DXVK_VK_FORMAT_B8G8R8A8_UNORM; break;
	case IMAGE_FORMAT_RGBA16161616:
		fmt = DXVK_VK_FORMAT_R16G16B16A16_UNORM; break;
	case IMAGE_FORMAT_RGBA16161616F:
		fmt = DXVK_VK_FORMAT_R16G16B16A16_SFLOAT; break;
	case IMAGE_FORMAT_RGBA32323232F:
		fmt = DXVK_VK_FORMAT_R32G32B32A32_SFLOAT; break;
	case IMAGE_FORMAT_R32F:
		fmt = DXVK_VK_FORMAT_R32_SFLOAT; break;
	default:
		fmt = DXVK_VK_FORMAT_B8G8R8A8_UNORM; break;
	}

	if ( bSRGB && ( fmt == DXVK_VK_FORMAT_R8G8B8A8_UNORM ||
					fmt == DXVK_VK_FORMAT_B8G8R8A8_UNORM ||
					fmt == DXVK_VK_FORMAT_BC1_RGBA_UNORM_BLOCK ||
					fmt == DXVK_VK_FORMAT_BC2_UNORM_BLOCK ||
					fmt == DXVK_VK_FORMAT_BC3_UNORM_BLOCK ) )
	{
		fmt += SRGB_DELTA;
	}
	return fmt;
}

//-----------------------------------------------------------------------------
// Bits per pixel for Source ImageFormat
//-----------------------------------------------------------------------------
int DxvkBitsPerPixelForFormat( ImageFormat fmt )
{
	return ImageLoader::GetMemRequired( 1, 1, 1, fmt, false );
}

//-----------------------------------------------------------------------------
// Compute the block size for compressed formats, or 1 for linear
//-----------------------------------------------------------------------------
void DxvkGetFormatBlockSize( ImageFormat fmt, int& blockW, int& blockH, int& blockBytes )
{
	blockW = 1; blockH = 1; blockBytes = 4;
	switch ( fmt )
	{
	case IMAGE_FORMAT_DXT1:
	case IMAGE_FORMAT_DXT1_ONEBITALPHA:
		blockW = 4; blockH = 4; blockBytes = 8; break;
	case IMAGE_FORMAT_DXT3:
	case IMAGE_FORMAT_DXT5:
		blockW = 4; blockH = 4; blockBytes = 16; break;
	case IMAGE_FORMAT_RGBA8888:
	case IMAGE_FORMAT_ABGR8888:
	case IMAGE_FORMAT_BGRA8888:
	case IMAGE_FORMAT_ARGB8888:
	case IMAGE_FORMAT_BGRX8888:
	case IMAGE_FORMAT_RGB888:
	case IMAGE_FORMAT_BGR888:
		blockW = 1; blockH = 1; blockBytes = 3; break;
	case IMAGE_FORMAT_RGB565:
	case IMAGE_FORMAT_BGRA4444:
	case IMAGE_FORMAT_BGRA5551:
		blockW = 1; blockH = 1; blockBytes = 2; break;
	case IMAGE_FORMAT_R32F:
		blockW = 1; blockH = 1; blockBytes = 4; break;
	case IMAGE_FORMAT_RGBA16161616:
	case IMAGE_FORMAT_RGBA16161616F:
		blockW = 1; blockH = 1; blockBytes = 8; break;
	case IMAGE_FORMAT_RGBA32323232F:
		blockW = 1; blockH = 1; blockBytes = 16; break;
	default:
		blockW = 1; blockH = 1; blockBytes = 4; break;
	}
}

//-----------------------------------------------------------------------------
// Exported color-format entry points used by rest of the module
//-----------------------------------------------------------------------------
ImageFormat DxvkGetNearestSupportedFormat( ImageFormat fmt, bool bRenderTarget, bool bFilteringRequired )
{
	// Vulkan supports most Source formats natively. Simplify by passing through.
	if ( bRenderTarget )
	{
		switch ( fmt )
		{
		case IMAGE_FORMAT_RGBA8888:
		case IMAGE_FORMAT_BGRA8888:
		case IMAGE_FORMAT_ARGB8888:
		case IMAGE_FORMAT_ABGR8888:
		case IMAGE_FORMAT_RGBA16161616F:
		case IMAGE_FORMAT_RGBA32323232F:
		case IMAGE_FORMAT_R32F:
		case IMAGE_FORMAT_BGRX8888:
			return fmt;
		default:
			return IMAGE_FORMAT_BGRA8888;
		}
	}
	return fmt;
}

//-----------------------------------------------------------------------------
// Stub implementations of symbols referenced from build files but defined
// elsewhere in shaderapidx9's colorformatdx8.cpp.
//-----------------------------------------------------------------------------
ImageFormat GetNearestSupportedFormat_DxVK( ImageFormat fmt, bool bFilteringRequired )
{
	return DxvkGetNearestSupportedFormat( fmt, false, bFilteringRequired );
}

ImageFormat GetNearestRenderTargetFormat_DxVK( ImageFormat fmt )
{
	return DxvkGetNearestSupportedFormat( fmt, true, true );
}
