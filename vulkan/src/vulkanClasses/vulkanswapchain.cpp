

#include "vulkanswapchain.h"

#include <stdlib.h>
#include <string>
#include <fstream>
#include <assert.h>
#include <stdio.h>
#include <vector>

// global
#include "main/global.h"

// load SDL2 if using it
#if USE_SDL2
	#include <SDL2/SDL.h>
	#include <SDL2/SDL_syswm.h>
#endif



#if defined(_WIN32)
	#include <windows.h>
	#include <fcntl.h>
	#include <io.h>
#endif





#include <vulkan/vulkan.h>
#include "vulkanTools.h"



#if defined(__ANDROID__)
	#include "vulkanandroid.h"
#endif

// Macro to get a procedure address based on a vulkan instance
#define GET_INSTANCE_PROC_ADDR(inst, entrypoint)                        \
{                                                                       \
	fp##entrypoint = reinterpret_cast<PFN_vk##entrypoint>(vkGetInstanceProcAddr(inst, "vk"#entrypoint)); \
	if (fp##entrypoint == NULL)                                         \
	{																    \
		exit(1);                                                        \
	}                                                                   \
}

// Macro to get a procedure address based on a vulkan device
#define GET_DEVICE_PROC_ADDR(dev, entrypoint)                           \
{                                                                       \
	fp##entrypoint = reinterpret_cast<PFN_vk##entrypoint>(vkGetDeviceProcAddr(dev, "vk"#entrypoint));   \
	if (fp##entrypoint == NULL)                                         \
	{																    \
		exit(1);                                                        \
	}                                                                   \
}












// Creates an os specific surface
/**
* Create the surface object, an abstraction for the native platform window
*
* @pre Windows
* @param platformHandle HINSTANCE of the window to create the surface for
* @param platformWindow HWND of the window to create the surface for
*
* @pre Android 
* @param window A native platform window
*
* @pre Linux (XCB)
* @param connection xcb connection to the X Server
* @param window The xcb window to create the surface for
* @note Targets other than XCB ar not yet supported
*/
// define params for this function based on os and settings

#if defined(_WIN32)
	void VulkanSwapChain::initSurface(void * platformHandle, void * platformWindow)
#elif defined(__linux__)
	void VulkanSwapChain::initSurface(xcb_connection_t * connection, xcb_window_t window)
#elif defined(__ANDROID__)
	void VulkanSwapChain::initSurface(ANativeWindow * window)
#endif
{
	vk::Result err;

	// if not, make a native surface instead
	/* WINDOWS */
	#if defined(_WIN32)
		vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo;
		surfaceCreateInfo.hinstance = (HINSTANCE)platformHandle;
		surfaceCreateInfo.hwnd = (HWND)platformWindow;
		//err = vkCreateWin32SurfaceKHR(instance, &surfaceCreateInfo, nullptr, &surface);
		err = instance.createWin32SurfaceKHR(&surfaceCreateInfo, nullptr, &surface);
	/* LINUX */
	#elif defined(__linux__)
		vk::XcbSurfaceCreateInfoKHR surfaceCreateInfo;
		surfaceCreateInfo.connection = connection;
		surfaceCreateInfo.window = window;
		err = vkCreateXcbSurfaceKHR(instance, &surfaceCreateInfo, nullptr, &surface);
	/* ANDROID */
	#elif defined(__ANDROID__)
		vk::AndroidSurfaceCreateInfoKHR surfaceCreateInfo;
		surfaceCreateInfo.window = window;
		err = vkCreateAndroidSurfaceKHR(instance, &surfaceCreateInfo, NULL, &surface);
	#endif

	// Get available queue family properties
	uint32_t queueCount;
	//vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, NULL);
	physicalDevice.getQueueFamilyProperties(&queueCount, NULL);
	assert(queueCount >= 1);

	std::vector<vk::QueueFamilyProperties> queueProps(queueCount);
	//vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, queueProps.data());
	physicalDevice.getQueueFamilyProperties(&queueCount, queueProps.data());

	// Iterate over each queue to learn whether it supports presenting:
	// Find a queue with present support
	// Will be used to present the swap chain images to the windowing system
	std::vector<vk::Bool32> supportsPresent(queueCount);
	for (uint32_t i = 0; i < queueCount; i++) {
		//fpGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &supportsPresent[i]);
		physicalDevice.getSurfaceSupportKHR(i, surface, &supportsPresent[i]);
	}

	// Search for a graphics and a present queue in the array of queue
	// families, try to find one that supports both
	uint32_t graphicsQueueNodeIndex = UINT32_MAX;
	uint32_t presentQueueNodeIndex = UINT32_MAX;
	for (uint32_t i = 0; i < queueCount; i++) {
		if ((queueProps[i].queueFlags & vk::QueueFlagBits::eGraphics) != vk::QueueFlags()) {
			if (graphicsQueueNodeIndex == UINT32_MAX) {
				graphicsQueueNodeIndex = i;
			}

			if (supportsPresent[i] == VK_TRUE) {
				graphicsQueueNodeIndex = i;
				presentQueueNodeIndex = i;
				break;
			}
		}
	}
	if (presentQueueNodeIndex == UINT32_MAX) {	
		// If there's no queue that supports both present and graphics
		// try to find a separate present queue
		for (uint32_t i = 0; i < queueCount; ++i) {
			if (supportsPresent[i] == VK_TRUE) {
				presentQueueNodeIndex = i;
				break;
			}
		}
	}

	// Exit if either a graphics or a presenting queue hasn't been found
	if (graphicsQueueNodeIndex == UINT32_MAX || presentQueueNodeIndex == UINT32_MAX) {
		vkx::exitFatal("Could not find a graphics and/or presenting queue!", "Fatal error");
	}

	// todo : Add support for separate graphics and presenting queue
	if (graphicsQueueNodeIndex != presentQueueNodeIndex) {
		vkx::exitFatal("Separate graphics and presenting queues are not supported yet!", "Fatal error");
	}

	queueNodeIndex = graphicsQueueNodeIndex;

	// Get list of supported surface formats
	uint32_t formatCount;
	//err = fpGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, NULL);
	err = physicalDevice.getSurfaceFormatsKHR(surface, &formatCount, NULL);
	assert(!(bool)err);
	assert(formatCount > 0);

	std::vector<vk::SurfaceFormatKHR> surfaceFormats(formatCount);
	//err = fpGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, surfaceFormats.data());
	err = physicalDevice.getSurfaceFormatsKHR(surface, &formatCount, surfaceFormats.data());
	assert(!(bool)err);

	// If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
	// there is no preferered format, so we assume VK_FORMAT_B8G8R8A8_UNORM
	if ((formatCount == 1) && (surfaceFormats[0].format == vk::Format::eUndefined)) {
		colorFormat = vk::Format::eB8G8R8A8Unorm;
	} else {
		// Always select the first available color format
		// If you need a specific format (e.g. SRGB) you'd need to
		// iterate over the list of available surface format and
		// check for it's presence
		colorFormat = surfaceFormats[0].format;
	}
	colorSpace = surfaceFormats[0].colorSpace;
}



//void VulkanSwapChain::initSurfaceWindowsSurface()












































/**
* Set instance, physical and logical device to use for the swpachain and get all required function pointers
* 
* @param instance Vulkan instance to use
* @param physicalDevice Physical device used to query properties and formats relevant to the swapchain
* @param device Logical representation of the device to create the swapchain for
*
*/
void VulkanSwapChain::connect(vk::Instance instance, vk::PhysicalDevice physicalDevice, vk::Device device)
{
	this->instance = instance;
	this->physicalDevice = physicalDevice;
	this->device = device;
	GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfaceSupportKHR);
	GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfaceCapabilitiesKHR);
	GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfaceFormatsKHR);
	GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfacePresentModesKHR);
	GET_DEVICE_PROC_ADDR(device, CreateSwapchainKHR);
	GET_DEVICE_PROC_ADDR(device, DestroySwapchainKHR);
	GET_DEVICE_PROC_ADDR(device, GetSwapchainImagesKHR);
	GET_DEVICE_PROC_ADDR(device, AcquireNextImageKHR);
	GET_DEVICE_PROC_ADDR(device, QueuePresentKHR);
}





/** 
* Create the swapchain and get it's images with given width and height
* 
* @param width Pointer to the width of the swapchain (may be adjusted to fit the requirements of the swapchain)
* @param height Pointer to the height of the swapchain (may be adjusted to fit the requirements of the swapchain)
* @param vsync (Optional) Can be used to force vsync'd rendering (by using VK_PRESENT_MODE_FIFO_KHR as presentation mode)
*/
void VulkanSwapChain::create(uint32_t *width, uint32_t *height, bool vsync = false)
{
	vk::Result err;
	vk::SwapchainKHR oldSwapchain = swapChain;

	// Get physical device surface properties and formats
	vk::SurfaceCapabilitiesKHR surfCaps;
	//err = fpGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfCaps);
	err = physicalDevice.getSurfaceCapabilitiesKHR(surface, &surfCaps);
	assert(!(VkResult)err);

	// Get available present modes
	uint32_t presentModeCount;
	//err = fpGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, NULL);
	err = physicalDevice.getSurfacePresentModesKHR(surface, &presentModeCount, NULL);
	assert(!(VkResult)err);
	assert(presentModeCount > 0);

	std::vector<vk::PresentModeKHR> presentModes(presentModeCount);

	//err = fpGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());
	err = physicalDevice.getSurfacePresentModesKHR(surface, &presentModeCount, presentModes.data());
	assert(!(VkResult)err);

	vk::Extent2D swapchainExtent;
	// If width (and height) equals the special value 0xFFFFFFFF, the size of the surface will be set by the swapchain
	if (surfCaps.currentExtent.width == (uint32_t)-1) {
		// If the surface size is undefined, the size is set to
		// the size of the images requested.
		swapchainExtent.width = *width;
		swapchainExtent.height = *height;
	} else {
		// If the surface size is defined, the swap chain size must match
		swapchainExtent = surfCaps.currentExtent;
		*width = surfCaps.currentExtent.width;
		*height = surfCaps.currentExtent.height;
	}


	// Select a present mode for the swapchain

	// The VK_PRESENT_MODE_FIFO_KHR mode must always be present as per spec
	// This mode waits for the vertical blank ("v-sync")
	vk::PresentModeKHR swapchainPresentMode = vk::PresentModeKHR::eFifo;

	// If v-sync is not requested, try to find a mailbox mode
	// It's the lowest latency non-tearing present mode available
	if (!vsync) {
		for (size_t i = 0; i < presentModeCount; i++) {
			if (presentModes[i] == vk::PresentModeKHR::eMailbox) {
				swapchainPresentMode = vk::PresentModeKHR::eMailbox;
				break;
			}
			if ((swapchainPresentMode != vk::PresentModeKHR::eMailbox) && (presentModes[i] == vk::PresentModeKHR::eImmediate)) {
				swapchainPresentMode = vk::PresentModeKHR::eImmediate;
			}
		}
	}

	// Determine the number of images
	uint32_t desiredNumberOfSwapchainImages = surfCaps.minImageCount + 1;
	if ((surfCaps.maxImageCount > 0) && (desiredNumberOfSwapchainImages > surfCaps.maxImageCount)) {
		desiredNumberOfSwapchainImages = surfCaps.maxImageCount;
	}

	// Find the transformation of the surface
	vk::SurfaceTransformFlagBitsKHR preTransform;
	if (surfCaps.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity) {
		// We prefer a non-rotated transform
		preTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;//lol @ rotate
	} else {
		preTransform = surfCaps.currentTransform;
	}

	vk::SwapchainCreateInfoKHR swapchainCI;
	swapchainCI.pNext = NULL;
	swapchainCI.surface = surface;
	swapchainCI.minImageCount = desiredNumberOfSwapchainImages;
	swapchainCI.imageFormat = colorFormat;
	swapchainCI.imageColorSpace = colorSpace;
	swapchainCI.imageExtent = { swapchainExtent.width, swapchainExtent.height };
	swapchainCI.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
	swapchainCI.preTransform = preTransform;
	swapchainCI.imageArrayLayers = 1;
	swapchainCI.imageSharingMode = vk::SharingMode::eExclusive;
	swapchainCI.queueFamilyIndexCount = 0;
	swapchainCI.pQueueFamilyIndices = NULL;
	swapchainCI.presentMode = swapchainPresentMode;
	swapchainCI.oldSwapchain = oldSwapchain;
	// Setting clipped to VK_TRUE allows the implementation to discard rendering outside of the surface area
	swapchainCI.clipped = VK_TRUE;
	swapchainCI.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;

	//err = fpCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapChain);
	err = device.createSwapchainKHR(&swapchainCI, nullptr, &swapChain);
	assert(!(bool)err);

	// If an existing swap chain is re-created, destroy the old swap chain
	// This also cleans up all the presentable images
	if ((bool)oldSwapchain != VK_NULL_HANDLE) {
		for (uint32_t i = 0; i < imageCount; i++) {
			//vkDestroyImageView(device, buffers[i].view, nullptr);
			device.destroyImageView(buffers[i].view, nullptr);
		}
		//fpDestroySwapchainKHR(device, oldSwapchain, nullptr);
		device.destroySwapchainKHR(oldSwapchain, nullptr);
	}

	//err = fpGetSwapchainImagesKHR(device, swapChain, &imageCount, NULL);
	err = device.getSwapchainImagesKHR(swapChain, &imageCount, NULL);
	assert(!(bool)err);

	// Get the swap chain images
	images.resize(imageCount);
	//err = fpGetSwapchainImagesKHR(device, swapChain, &imageCount, images.data());
	err = device.getSwapchainImagesKHR(swapChain, &imageCount, images.data());
	assert(!(bool)err);

	// Get the swap chain buffers containing the image and imageview
	buffers.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; i++) {
		vk::ImageViewCreateInfo colorAttachmentView;
		colorAttachmentView.pNext = NULL;
		colorAttachmentView.format = colorFormat;
		colorAttachmentView.components = {
			vk::ComponentSwizzle::eR,
			vk::ComponentSwizzle::eG,
			vk::ComponentSwizzle::eB,
			vk::ComponentSwizzle::eA
		};
		colorAttachmentView.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		colorAttachmentView.subresourceRange.baseMipLevel = 0;
		colorAttachmentView.subresourceRange.levelCount = 1;
		colorAttachmentView.subresourceRange.baseArrayLayer = 0;
		colorAttachmentView.subresourceRange.layerCount = 1;
		colorAttachmentView.viewType = vk::ImageViewType::e2D;
		colorAttachmentView.flags = vk::ImageViewCreateFlags();

		buffers[i].image = images[i];

		colorAttachmentView.image = buffers[i].image;

		//err = vkCreateImageView(device, &colorAttachmentView, nullptr, &buffers[i].view);
		device.createImageView(&colorAttachmentView, nullptr, &buffers[i].view);
		assert(!(bool)err);
	}
}




/** 
* Acquires the next image in the swap chain
*
* @param presentCompleteSemaphore (Optional) Semaphore that is signaled when the image is ready for use
* @param imageIndex Pointer to the image index that will be increased if the next image could be acquired
*
* @note The function will always wait until the next image has been acquired by setting timeout to UINT64_MAX
*
* @return vk::Result of the image acquisition
*/
vk::Result VulkanSwapChain::acquireNextImage(vk::Semaphore presentCompleteSemaphore, uint32_t *imageIndex) {
	// By setting timeout to UINT64_MAX we will always wait until the next image has been acquired or an actual error is thrown
	// With that we don't have to handle VK_NOT_READY
	//return fpAcquireNextImageKHR(device, swapChain, UINT64_MAX, presentCompleteSemaphore, (vk::Fence)nullptr, imageIndex);
	return device.acquireNextImageKHR(swapChain, UINT64_MAX, presentCompleteSemaphore, (vk::Fence)nullptr, imageIndex);
}



/**
* Queue an image for presentation
*
* @param queue Presentation queue for presenting the image
* @param imageIndex Index of the swapchain image to queue for presentation
* @param waitSemaphore (Optional) Semaphore that is waited on before the image is presented (only used if != VK_NULL_HANDLE)
*
* @return vk::Result of the queue presentation
*/
vk::Result VulkanSwapChain::queuePresent(vk::Queue queue, uint32_t imageIndex, vk::Semaphore waitSemaphore = VK_NULL_HANDLE) {
	vk::PresentInfoKHR presentInfo;
	presentInfo.pNext = NULL;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapChain;
	presentInfo.pImageIndices = &imageIndex;
	// Check if a wait semaphore has been specified to wait for before presenting the image
	if ((bool)waitSemaphore != VK_NULL_HANDLE) {
		presentInfo.pWaitSemaphores = &waitSemaphore;
		presentInfo.waitSemaphoreCount = 1;
	}
	//return fpQueuePresentKHR(queue, &presentInfo);
	return queue.presentKHR(&presentInfo);
}




/**
* Destroy and free Vulkan resources used for the swapchain
*/
void VulkanSwapChain::cleanup() {
	for (uint32_t i = 0; i < imageCount; i++) {
		//vkDestroyImageView(device, buffers[i].view, nullptr);
		device.destroyImageView(buffers[i].view, nullptr);
	}
	//fpDestroySwapchainKHR(device, swapChain, nullptr);
	device.destroySwapchainKHR(swapChain, nullptr);
	//vkDestroySurfaceKHR(instance, surface, nullptr);
	instance.destroySurfaceKHR(surface, nullptr);
}
