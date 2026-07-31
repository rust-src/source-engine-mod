//========= Copyright Valve Corporation, All rights reserved. ============//
//                       TOGL CODE LICENSE
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
// vkentrypoints.h - Vulkan function pointer loading and management
// Replaces togles/linuxwin/glentrypoints.h
//
#ifndef VKENTRYPOINTS_H
#define VKENTRYPOINTS_H

#ifdef DX_TO_VK_ABSTRACTION

#include <vulkan/vulkan.h>
#include "tier0/dbg.h"
#include "tier0/platform.h"

// Vulkan entry points loaded at runtime.
// On Android, we load via dlopen("libvulkan.so") + vkGetInstanceProcAddr.
// On desktop, we can link directly or use SDL_vulkan.

class CVulkanEntryPoints
{
public:
	CVulkanEntryPoints();
	~CVulkanEntryPoints();

	bool Initialize();
	void Shutdown();

	// Instance-level functions
	VkInstance m_instance;

	// Core instance functions
	PFN_vkDestroyInstance                    vkDestroyInstance;
	PFN_vkEnumeratePhysicalDevices           vkEnumeratePhysicalDevices;
	PFN_vkGetPhysicalDeviceProperties        vkGetPhysicalDeviceProperties;
	PFN_vkGetPhysicalDeviceFeatures          vkGetPhysicalDeviceFeatures;
	PFN_vkGetPhysicalDeviceMemoryProperties  vkGetPhysicalDeviceMemoryProperties;
	PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
	PFN_vkGetPhysicalDeviceFormatProperties  vkGetPhysicalDeviceFormatProperties;
	PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties;

	// Device-level functions
	VkDevice m_device;
	VkPhysicalDevice m_physicalDevice;

	PFN_vkCreateDevice                       vkCreateDevice;
	PFN_vkDestroyDevice                      vkDestroyDevice;
	PFN_vkGetDeviceQueue                     vkGetDeviceQueue;
	PFN_vkDeviceWaitIdle                     vkDeviceWaitIdle;

	// Queue and command buffer
	PFN_vkQueueSubmit                        vkQueueSubmit;
	PFN_vkQueueWaitIdle                      vkQueueWaitIdle;
	PFN_vkAllocateCommandBuffers             vkAllocateCommandBuffers;
	PFN_vkFreeCommandBuffers                 vkFreeCommandBuffers;
	PFN_vkBeginCommandBuffer                 vkBeginCommandBuffer;
	PFN_vkEndCommandBuffer                   vkEndCommandBuffer;
	PFN_vkResetCommandBuffer                 vkResetCommandBuffer;

	// Memory
	PFN_vkAllocateMemory                     vkAllocateMemory;
	PFN_vkFreeMemory                         vkFreeMemory;
	PFN_vkMapMemory                          vkMapMemory;
	PFN_vkUnmapMemory                        vkUnmapMemory;
	PFN_vkFlushMappedMemoryRanges            vkFlushMappedMemoryRanges;
	PFN_vkInvalidateMappedMemoryRanges       vkInvalidateMappedMemoryRanges;
	PFN_vkBindBufferMemory                   vkBindBufferMemory;
	PFN_vkBindImageMemory                    vkBindImageMemory;
	PFN_vkGetBufferMemoryRequirements        vkGetBufferMemoryRequirements;
	PFN_vkGetImageMemoryRequirements         vkGetImageMemoryRequirements;
	PFN_vkGetImageSubresourceLayout          vkGetImageSubresourceLayout;

	// Buffers
	PFN_vkCreateBuffer                       vkCreateBuffer;
	PFN_vkDestroyBuffer                      vkDestroyBuffer;

	// Images
	PFN_vkCreateImage                        vkCreateImage;
	PFN_vkDestroyImage                       vkDestroyImage;
	PFN_vkCreateImageView                    vkCreateImageView;
	PFN_vkDestroyImageView                   vkDestroyImageView;

	// Samplers
	PFN_vkCreateSampler                      vkCreateSampler;
	PFN_vkDestroySampler                     vkDestroySampler;

	// Shaders
	PFN_vkCreateShaderModule                 vkCreateShaderModule;
	PFN_vkDestroyShaderModule                vkDestroyShaderModule;

	// Pipelines
	PFN_vkCreatePipelineCache                vkCreatePipelineCache;
	PFN_vkDestroyPipelineCache               vkDestroyPipelineCache;
	PFN_vkCreateGraphicsPipelines            vkCreateGraphicsPipelines;
	PFN_vkDestroyPipeline                    vkDestroyPipeline;

	// Pipeline layout and descriptor sets
	PFN_vkCreatePipelineLayout               vkCreatePipelineLayout;
	PFN_vkDestroyPipelineLayout              vkDestroyPipelineLayout;
	PFN_vkCreateDescriptorSetLayout          vkCreateDescriptorSetLayout;
	PFN_vkDestroyDescriptorSetLayout         vkDestroyDescriptorSetLayout;
	PFN_vkAllocateDescriptorSets             vkAllocateDescriptorSets;
	PFN_vkFreeDescriptorSets                 vkFreeDescriptorSets;
	PFN_vkUpdateDescriptorSets               vkUpdateDescriptorSets;

	// Render pass and framebuffer
	PFN_vkCreateRenderPass                   vkCreateRenderPass;
	PFN_vkDestroyRenderPass                  vkDestroyRenderPass;
	PFN_vkCreateFramebuffer                  vkCreateFramebuffer;
	PFN_vkDestroyFramebuffer                 vkDestroyFramebuffer;

	// Command recording
	PFN_vkCmdBeginRenderPass                 vkCmdBeginRenderPass;
	PFN_vkCmdEndRenderPass                   vkCmdEndRenderPass;
	PFN_vkCmdBindPipeline                    vkCmdBindPipeline;
	PFN_vkCmdBindVertexBuffers               vkCmdBindVertexBuffers;
	PFN_vkCmdBindIndexBuffer                 vkCmdBindIndexBuffer;
	PFN_vkCmdBindDescriptorSets              vkCmdBindDescriptorSets;
	PFN_vkCmdDraw                            vkCmdDraw;
	PFN_vkCmdDrawIndexed                     vkCmdDrawIndexed;
	PFN_vkCmdClearAttachments                vkCmdClearAttachments;
	PFN_vkCmdClearColorImage                 vkCmdClearColorImage;
	PFN_vkCmdClearDepthStencilImage          vkCmdClearDepthStencilImage;
	PFN_vkCmdPipelineBarrier                 vkCmdPipelineBarrier;
	PFN_vkCmdSetViewport                     vkCmdSetViewport;
	PFN_vkCmdSetScissor                      vkCmdSetScissor;
	PFN_vkCmdSetBlendConstants               vkCmdSetBlendConstants;
	PFN_vkCmdSetStencilReference             vkCmdSetStencilReference;
	PFN_vkCmdSetDepthBias                    vkCmdSetDepthBias;
	PFN_vkCmdPushConstants                   vkCmdPushConstants;
	PFN_vkCmdCopyBuffer                      vkCmdCopyBuffer;
	PFN_vkCmdCopyImage                       vkCmdCopyImage;
	PFN_vkCmdBlitImage                       vkCmdBlitImage;
	PFN_vkCmdCopyBufferToImage               vkCmdCopyBufferToImage;
	PFN_vkCmdCopyImageToBuffer               vkCmdCopyImageToBuffer;

	// Fences and semaphores
	PFN_vkCreateFence                        vkCreateFence;
	PFN_vkDestroyFence                       vkDestroyFence;
	PFN_vkResetFences                        vkResetFences;
	PFN_vkWaitForFences                      vkWaitForFences;
	PFN_vkGetFenceStatus                     vkGetFenceStatus;
	PFN_vkCreateSemaphore                    vkCreateSemaphore;
	PFN_vkDestroySemaphore                   vkDestroySemaphore;

	// Query pool
	PFN_vkCreateQueryPool                    vkCreateQueryPool;
	PFN_vkDestroyQueryPool                   vkDestroyQueryPool;
	PFN_vkCmdBeginQuery                      vkCmdBeginQuery;
	PFN_vkCmdEndQuery                        vkCmdEndQuery;
	PFN_vkCmdResetQueryPool                  vkCmdResetQueryPool;
	PFN_vkGetQueryPoolResults                vkGetQueryPoolResults;

	// Swapchain (VK_KHR_swapchain)
	PFN_vkCreateSwapchainKHR                 vkCreateSwapchainKHR;
	PFN_vkDestroySwapchainKHR                vkDestroySwapchainKHR;
	PFN_vkGetSwapchainImagesKHR              vkGetSwapchainImagesKHR;
	PFN_vkAcquireNextImageKHR                vkAcquireNextImageKHR;
	PFN_vkQueuePresentKHR                    vkQueuePresentKHR;

	// Surface (VK_KHR_surface)
	PFN_vkDestroySurfaceKHR                  vkDestroySurfaceKHR;
	PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
	PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR;

	// Driver info
	VKDriverProvider m_nDriverProvider;
	const char *m_pDriverStrings[3]; // vendor, renderer, version
	int m_nApiVersionMajor;
	int m_nApiVersionMinor;
	int m_nApiVersionPatch;

private:
	bool LoadInstanceFunctions();
	bool LoadDeviceFunctions();
	bool LoadSwapchainFunctions();
	void *m_vulkanLib; // dlopen handle for libvulkan.so
};

// Global entry points singleton
extern CVulkanEntryPoints *gVK;

// Helper to load the Vulkan library and create instance
bool VKConnectLibraries();

// Helper to get instance proc addr
PFN_vkVoidFunction VKGetInstanceProcAddr( const char *name );

#endif // DX_TO_VK_ABSTRACTION

#endif // VKENTRYPOINTS_H
