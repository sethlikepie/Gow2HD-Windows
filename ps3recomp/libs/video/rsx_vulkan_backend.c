/*
 * ps3recomp - Vulkan RSX backend
 *
 * See rsx_vulkan_backend.h for scope.
 *
 * The frame loop is deliberately the simplest correct one: a single frame in
 * flight, fenced on submit. The RSX flip rate is driven by the guest, not by
 * how deep we can pipeline, and a one-frame design keeps the readback path for
 * PS3_FRAME_DUMP straightforward. Deepening this is a change to make when
 * there are real draws to overlap, not before.
 */

#include "rsx_vulkan_backend.h"
#include "rsx_frame_dump.h"

#include <vulkan/vulkan.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SWAPCHAIN_IMAGES 8

typedef struct {
    SDL_Window* window;

    VkInstance       instance;
    VkSurfaceKHR     surface;
    VkPhysicalDevice phys;
    VkDevice         device;
    VkQueue          queue;
    u32              queue_family;

    VkSwapchainKHR swapchain;
    VkFormat       format;
    VkExtent2D     extent;
    u32            image_count;
    VkImage        images[MAX_SWAPCHAIN_IMAGES];
    VkImageView    views[MAX_SWAPCHAIN_IMAGES];
    VkFramebuffer  framebuffers[MAX_SWAPCHAIN_IMAGES];

    VkRenderPass    render_pass;
    VkCommandPool   cmd_pool;
    VkCommandBuffer cmd[MAX_SWAPCHAIN_IMAGES];

    VkSemaphore acquired;
    VkSemaphore rendered;
    VkFence     in_flight;

    /* Readback staging, allocated lazily and only when dumping. */
    VkBuffer       dump_buffer;
    VkDeviceMemory dump_memory;
    VkDeviceSize   dump_size;

    float clear_r, clear_g, clear_b;

    u64 frame_count;
    u64 last_fps_ticks;
    u64 last_fps_frame;
    u32 fps;

    int window_closed;
    int dump_frames;
    int bgra_order;   /* swapchain channel order is already BMP's */

    /* Window title base, "<game title> — Vulkan[ (MoltenVK)]"; the per-second
     * FPS title keeps it as its prefix so the graphics API is always visible. */
    char base_title[160];
} VulkanBackendState;

static VulkanBackendState s_state;

static const char* vk_err(VkResult r)
{
    switch (r) {
    case VK_SUCCESS:                     return "VK_SUCCESS";
    case VK_NOT_READY:                   return "VK_NOT_READY";
    case VK_TIMEOUT:                     return "VK_TIMEOUT";
    case VK_SUBOPTIMAL_KHR:              return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY:    return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:  return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:           return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_SURFACE_LOST_KHR:      return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:       return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:   return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:   return "VK_ERROR_INCOMPATIBLE_DRIVER";
    default:                             return "VK_ERROR_<other>";
    }
}

#define VK_CHECK(expr, what)                                                  \
    do {                                                                      \
        VkResult _r = (expr);                                                 \
        if (_r != VK_SUCCESS) {                                               \
            printf("[RSX vulkan] ERROR: %s: %s\n", (what), vk_err(_r));       \
            return -1;                                                        \
        }                                                                     \
    } while (0)

/* ---------------------------------------------------------------------------
 * Setup
 * -----------------------------------------------------------------------*/

static int create_instance(void)
{
    unsigned sdl_ext_count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(s_state.window, &sdl_ext_count, NULL)) {
        printf("[RSX vulkan] ERROR: SDL_Vulkan_GetInstanceExtensions: %s\n",
               SDL_GetError());
        return -1;
    }

    /* Room for SDL's extensions plus the portability one MoltenVK needs. */
    const char* exts[16];
    if (sdl_ext_count > 14) {
        printf("[RSX vulkan] ERROR: too many SDL instance extensions (%u)\n",
               sdl_ext_count);
        return -1;
    }
    if (!SDL_Vulkan_GetInstanceExtensions(s_state.window, &sdl_ext_count, exts)) {
        printf("[RSX vulkan] ERROR: SDL_Vulkan_GetInstanceExtensions(2): %s\n",
               SDL_GetError());
        return -1;
    }

    /* MoltenVK is a portability driver: without opting in, the loader hides it
     * and vkCreateInstance fails with INCOMPATIBLE_DRIVER on macOS. Only add
     * the extension when the loader actually advertises it, so this stays
     * correct on native Vulkan drivers too. */
    u32 avail_count = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &avail_count, NULL);
    VkExtensionProperties* avail =
        (VkExtensionProperties*)calloc(avail_count ? avail_count : 1, sizeof(*avail));
    if (!avail) {
        return -1;
    }
    vkEnumerateInstanceExtensionProperties(NULL, &avail_count, avail);

    int portability = 0;
    for (u32 i = 0; i < avail_count; i++) {
        if (strcmp(avail[i].extensionName,
                   VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0) {
            portability = 1;
            break;
        }
    }
    free(avail);

    u32 ext_count = sdl_ext_count;
    VkInstanceCreateFlags flags = 0;
    if (portability) {
        exts[ext_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    VkApplicationInfo app = {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName   = "ps3recomp",
        .applicationVersion = VK_MAKE_VERSION(0, 3, 0),
        .pEngineName        = "ps3recomp",
        .engineVersion      = VK_MAKE_VERSION(0, 3, 0),
        /* MoltenVK reports 1.0; asking for more would fail on this host. */
        .apiVersion         = VK_API_VERSION_1_0,
    };

    VkInstanceCreateInfo ci = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .flags                   = flags,
        .pApplicationInfo        = &app,
        .enabledExtensionCount   = ext_count,
        .ppEnabledExtensionNames = exts,
    };

    VK_CHECK(vkCreateInstance(&ci, NULL, &s_state.instance), "vkCreateInstance");
    return 0;
}

static int pick_device(void)
{
    u32 n = 0;
    vkEnumeratePhysicalDevices(s_state.instance, &n, NULL);
    if (n == 0) {
        printf("[RSX vulkan] ERROR: no Vulkan physical devices\n");
        return -1;
    }

    VkPhysicalDevice* devs = (VkPhysicalDevice*)calloc(n, sizeof(*devs));
    if (!devs) {
        return -1;
    }
    vkEnumeratePhysicalDevices(s_state.instance, &n, devs);

    int found = 0;
    for (u32 i = 0; i < n && !found; i++) {
        u32 qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qn, NULL);
        VkQueueFamilyProperties* qs =
            (VkQueueFamilyProperties*)calloc(qn ? qn : 1, sizeof(*qs));
        if (!qs) {
            continue;
        }
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qn, qs);

        for (u32 q = 0; q < qn; q++) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(devs[i], q, s_state.surface, &present);
            /* One family that both draws and presents keeps the swapchain in
             * EXCLUSIVE sharing mode and avoids ownership transfers. */
            if ((qs[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                s_state.phys         = devs[i];
                s_state.queue_family = q;
                found = 1;
                break;
            }
        }
        free(qs);
    }
    free(devs);

    if (!found) {
        printf("[RSX vulkan] ERROR: no device with a graphics+present queue\n");
        return -1;
    }

    VkPhysicalDeviceProperties p;
    vkGetPhysicalDeviceProperties(s_state.phys, &p);
    printf("[RSX vulkan] device: %s (API %u.%u.%u)\n", p.deviceName,
           VK_VERSION_MAJOR(p.apiVersion), VK_VERSION_MINOR(p.apiVersion),
           VK_VERSION_PATCH(p.apiVersion));
    return 0;
}

static int create_device(void)
{
    u32 n = 0;
    vkEnumerateDeviceExtensionProperties(s_state.phys, NULL, &n, NULL);
    VkExtensionProperties* avail =
        (VkExtensionProperties*)calloc(n ? n : 1, sizeof(*avail));
    if (!avail) {
        return -1;
    }
    vkEnumerateDeviceExtensionProperties(s_state.phys, NULL, &n, avail);

    const char* exts[4];
    u32 ext_count = 0;
    exts[ext_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

    /* If a device advertises VK_KHR_portability_subset, the spec requires us to
     * enable it. MoltenVK does. */
    for (u32 i = 0; i < n; i++) {
        if (strcmp(avail[i].extensionName, "VK_KHR_portability_subset") == 0) {
            exts[ext_count++] = "VK_KHR_portability_subset";
#ifdef __APPLE__
            /* On Apple the portability driver is MoltenVK (Vulkan over Metal):
             * say so in the title, it is one layer above the native backend. */
            size_t tl = strlen(s_state.base_title);
            snprintf(s_state.base_title + tl, sizeof(s_state.base_title) - tl, " (MoltenVK)");
            if (s_state.window) SDL_SetWindowTitle(s_state.window, s_state.base_title);
#endif
            break;
        }
    }
    free(avail);

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = s_state.queue_family,
        .queueCount       = 1,
        .pQueuePriorities = &priority,
    };

    VkDeviceCreateInfo dci = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &qci,
        .enabledExtensionCount   = ext_count,
        .ppEnabledExtensionNames = exts,
    };

    VK_CHECK(vkCreateDevice(s_state.phys, &dci, NULL, &s_state.device),
             "vkCreateDevice");
    vkGetDeviceQueue(s_state.device, s_state.queue_family, 0, &s_state.queue);
    return 0;
}

static int create_swapchain(void)
{
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(s_state.phys, s_state.surface, &caps),
             "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    /* Drawable size, not window size: the swapchain is in pixels, and on a
     * Retina panel those differ by the backing scale factor. */
    int dw = 0, dh = 0;
    SDL_Vulkan_GetDrawableSize(s_state.window, &dw, &dh);

    VkExtent2D extent;
    if (caps.currentExtent.width != 0xFFFFFFFFu) {
        extent = caps.currentExtent;
    } else {
        extent.width  = (u32)dw;
        extent.height = (u32)dh;
        if (extent.width  < caps.minImageExtent.width)  extent.width  = caps.minImageExtent.width;
        if (extent.height < caps.minImageExtent.height) extent.height = caps.minImageExtent.height;
        if (extent.width  > caps.maxImageExtent.width)  extent.width  = caps.maxImageExtent.width;
        if (extent.height > caps.maxImageExtent.height) extent.height = caps.maxImageExtent.height;
    }
    s_state.extent = extent;

    u32 fn = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(s_state.phys, s_state.surface, &fn, NULL);
    VkSurfaceFormatKHR* formats =
        (VkSurfaceFormatKHR*)calloc(fn ? fn : 1, sizeof(*formats));
    if (!formats) {
        return -1;
    }
    vkGetPhysicalDeviceSurfaceFormatsKHR(s_state.phys, s_state.surface, &fn, formats);

    /* Prefer B8G8R8A8_UNORM: it is what MoltenVK exposes natively and its
     * memory order is already BMP's, so the frame dump needs no swizzle. */
    VkSurfaceFormatKHR chosen = formats[0];
    for (u32 i = 0; i < fn; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
            chosen = formats[i];
            break;
        }
    }
    free(formats);

    s_state.format     = chosen.format;
    s_state.bgra_order = (chosen.format == VK_FORMAT_B8G8R8A8_UNORM ||
                          chosen.format == VK_FORMAT_B8G8R8A8_SRGB);

    u32 want = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && want > caps.maxImageCount) {
        want = caps.maxImageCount;
    }
    if (want > MAX_SWAPCHAIN_IMAGES) {
        want = MAX_SWAPCHAIN_IMAGES;
    }

    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    /* TRANSFER_SRC is what lets the dump path copy the presented image out.
     * Only ask for it when dumping, and only if the surface allows it. */
    if (s_state.dump_frames) {
        if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
            usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        } else {
            printf("[RSX vulkan] WARNING: surface has no TRANSFER_SRC; "
                   "frame dump disabled\n");
            s_state.dump_frames = 0;
        }
    }

    VkSwapchainCreateInfoKHR sci = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = s_state.surface,
        .minImageCount    = want,
        .imageFormat      = chosen.format,
        .imageColorSpace  = chosen.colorSpace,
        .imageExtent      = extent,
        .imageArrayLayers = 1,
        .imageUsage       = usage,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform     = caps.currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        /* FIFO is the only mode guaranteed present everywhere. */
        .presentMode      = VK_PRESENT_MODE_FIFO_KHR,
        .clipped          = VK_TRUE,
    };

    VK_CHECK(vkCreateSwapchainKHR(s_state.device, &sci, NULL, &s_state.swapchain),
             "vkCreateSwapchainKHR");

    s_state.image_count = MAX_SWAPCHAIN_IMAGES;
    VK_CHECK(vkGetSwapchainImagesKHR(s_state.device, s_state.swapchain,
                                     &s_state.image_count, s_state.images),
             "vkGetSwapchainImagesKHR");

    for (u32 i = 0; i < s_state.image_count; i++) {
        VkImageViewCreateInfo vci = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = s_state.images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format   = s_state.format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        VK_CHECK(vkCreateImageView(s_state.device, &vci, NULL, &s_state.views[i]),
                 "vkCreateImageView");
    }

    return 0;
}

static int create_render_pass(void)
{
    VkAttachmentDescription color = {
        .format         = s_state.format,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        /* The clear happens here, on load. That is why an otherwise empty
         * render pass still produces a correct frame. */
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    VkAttachmentReference ref = {
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &ref,
    };

    VkSubpassDependency dep = {
        .srcSubpass    = VK_SUBPASS_EXTERNAL,
        .dstSubpass    = 0,
        .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    VkRenderPassCreateInfo rci = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments    = &color,
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = &dep,
    };

    VK_CHECK(vkCreateRenderPass(s_state.device, &rci, NULL, &s_state.render_pass),
             "vkCreateRenderPass");

    for (u32 i = 0; i < s_state.image_count; i++) {
        VkFramebufferCreateInfo fci = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = s_state.render_pass,
            .attachmentCount = 1,
            .pAttachments    = &s_state.views[i],
            .width           = s_state.extent.width,
            .height          = s_state.extent.height,
            .layers          = 1,
        };
        VK_CHECK(vkCreateFramebuffer(s_state.device, &fci, NULL,
                                     &s_state.framebuffers[i]),
                 "vkCreateFramebuffer");
    }

    return 0;
}

static int create_commands(void)
{
    VkCommandPoolCreateInfo pci = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = s_state.queue_family,
    };
    VK_CHECK(vkCreateCommandPool(s_state.device, &pci, NULL, &s_state.cmd_pool),
             "vkCreateCommandPool");

    VkCommandBufferAllocateInfo ai = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = s_state.cmd_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = s_state.image_count,
    };
    VK_CHECK(vkAllocateCommandBuffers(s_state.device, &ai, s_state.cmd),
             "vkAllocateCommandBuffers");

    VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VK_CHECK(vkCreateSemaphore(s_state.device, &sci, NULL, &s_state.acquired),
             "vkCreateSemaphore(acquired)");
    VK_CHECK(vkCreateSemaphore(s_state.device, &sci, NULL, &s_state.rendered),
             "vkCreateSemaphore(rendered)");

    VkFenceCreateInfo fci = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    VK_CHECK(vkCreateFence(s_state.device, &fci, NULL, &s_state.in_flight),
             "vkCreateFence");

    return 0;
}

/* ---------------------------------------------------------------------------
 * Frame dump
 * -----------------------------------------------------------------------*/

static int find_memory_type(u32 type_bits, VkMemoryPropertyFlags want, u32* out)
{
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(s_state.phys, &mp);
    for (u32 i = 0; i < mp.memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & want) == want) {
            *out = i;
            return 0;
        }
    }
    return -1;
}

static int ensure_dump_buffer(VkDeviceSize size)
{
    if (s_state.dump_buffer && s_state.dump_size >= size) {
        return 0;
    }

    if (s_state.dump_buffer) {
        vkDestroyBuffer(s_state.device, s_state.dump_buffer, NULL);
        vkFreeMemory(s_state.device, s_state.dump_memory, NULL);
        s_state.dump_buffer = VK_NULL_HANDLE;
        s_state.dump_memory = VK_NULL_HANDLE;
    }

    VkBufferCreateInfo bci = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vkCreateBuffer(s_state.device, &bci, NULL, &s_state.dump_buffer),
             "vkCreateBuffer(dump)");

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(s_state.device, s_state.dump_buffer, &req);

    u32 type = 0;
    if (find_memory_type(req.memoryTypeBits,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &type) != 0) {
        printf("[RSX vulkan] ERROR: no host-visible memory type for readback\n");
        return -1;
    }

    VkMemoryAllocateInfo mai = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = req.size,
        .memoryTypeIndex = type,
    };
    VK_CHECK(vkAllocateMemory(s_state.device, &mai, NULL, &s_state.dump_memory),
             "vkAllocateMemory(dump)");
    VK_CHECK(vkBindBufferMemory(s_state.device, s_state.dump_buffer,
                                s_state.dump_memory, 0),
             "vkBindBufferMemory(dump)");

    s_state.dump_size = size;
    return 0;
}

/* Record the copy of the just-presented image into the staging buffer. The
 * image arrives in PRESENT_SRC (the render pass put it there) and must be
 * handed back in PRESENT_SRC for vkQueuePresentKHR. */
static void record_dump_copy(VkCommandBuffer cb, VkImage image)
{
    VkImageMemoryBarrier to_src = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image,
        .subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, NULL, 0, NULL, 1, &to_src);

    VkBufferImageCopy region = {
        .bufferOffset      = 0,
        .bufferRowLength   = 0,   /* tightly packed */
        .bufferImageHeight = 0,
        .imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset       = { 0, 0, 0 },
        .imageExtent       = { s_state.extent.width, s_state.extent.height, 1 },
    };
    vkCmdCopyImageToBuffer(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           s_state.dump_buffer, 1, &region);

    VkImageMemoryBarrier back = to_src;
    back.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    back.dstAccessMask = 0;
    back.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    back.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                         0, NULL, 0, NULL, 1, &back);
}

static void write_dump(void)
{
    const u32 w      = s_state.extent.width;
    const u32 h      = s_state.extent.height;
    const size_t size = (size_t)w * h * 4u;

    void* mapped = NULL;
    if (vkMapMemory(s_state.device, s_state.dump_memory, 0, size, 0, &mapped) != VK_SUCCESS) {
        printf("[RSX vulkan] frame dump: vkMapMemory failed\n");
        return;
    }

    u8* pixels = (u8*)mapped;
    u8* swizzled = NULL;

    /* rsx_write_bmp_bgra32 wants B,G,R,A. If the swapchain handed us RGBA
     * instead, swap the red and blue channels rather than writing a picture
     * with the colours transposed. */
    if (!s_state.bgra_order) {
        swizzled = (u8*)malloc(size);
        if (!swizzled) {
            vkUnmapMemory(s_state.device, s_state.dump_memory);
            return;
        }
        for (size_t i = 0; i < size; i += 4) {
            swizzled[i + 0] = pixels[i + 2];
            swizzled[i + 1] = pixels[i + 1];
            swizzled[i + 2] = pixels[i + 0];
            swizzled[i + 3] = pixels[i + 3];
        }
        pixels = swizzled;
    }

    char path[64];
    snprintf(path, sizeof(path), "frame_%llu.bmp",
             (unsigned long long)s_state.frame_count);
    rsx_write_bmp_bgra32(path, pixels, w, h);

    free(swizzled);
    vkUnmapMemory(s_state.device, s_state.dump_memory);
}

/* ---------------------------------------------------------------------------
 * Backend callbacks
 * -----------------------------------------------------------------------*/

static int vulkan_init_cb(void* ud, u32 width, u32 height)
{
    (void)ud;
    printf("[RSX vulkan] init(%ux%u)\n", width, height);
    return 0;
}

static void vulkan_shutdown_cb(void* ud)
{
    (void)ud;
    printf("[RSX vulkan] shutdown\n");
}

static void vulkan_begin_frame(void* ud)
{
    (void)ud;
}

static void vulkan_end_frame(void* ud)
{
    (void)ud;
    s_state.frame_count++;

    u64 now = SDL_GetTicks64();
    if (now - s_state.last_fps_ticks >= 1000) {
        u64 df = s_state.frame_count - s_state.last_fps_frame;
        u64 dt = now - s_state.last_fps_ticks;
        s_state.fps = (u32)((df * 1000) / (dt ? dt : 1));
        s_state.last_fps_ticks = now;
        s_state.last_fps_frame = s_state.frame_count;

        char title[224];
        snprintf(title, sizeof(title),
                 "%s | %u FPS | frame %llu",
                 s_state.base_title, s_state.fps, (unsigned long long)s_state.frame_count);
        SDL_SetWindowTitle(s_state.window, title);
    }
}

static void vulkan_present(void* ud, u32 buffer_id)
{
    (void)ud;
    (void)buffer_id;

    if (!s_state.swapchain) {
        return;
    }

    vkWaitForFences(s_state.device, 1, &s_state.in_flight, VK_TRUE, UINT64_MAX);
    vkResetFences(s_state.device, 1, &s_state.in_flight);

    u32 index = 0;
    VkResult r = vkAcquireNextImageKHR(s_state.device, s_state.swapchain,
                                       UINT64_MAX, s_state.acquired,
                                       VK_NULL_HANDLE, &index);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        /* The window was resized out from under us. Skip this frame; the
         * swapchain is rebuilt on the resize event. */
        return;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        printf("[RSX vulkan] vkAcquireNextImageKHR: %s\n", vk_err(r));
        return;
    }

    int dumping = s_state.dump_frames &&
                  ensure_dump_buffer((VkDeviceSize)s_state.extent.width *
                                     s_state.extent.height * 4u) == 0;

    VkCommandBuffer cb = s_state.cmd[index];
    vkResetCommandBuffer(cb, 0);

    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cb, &bi);

    VkClearValue clear;
    clear.color.float32[0] = s_state.clear_r;
    clear.color.float32[1] = s_state.clear_g;
    clear.color.float32[2] = s_state.clear_b;
    clear.color.float32[3] = 1.0f;

    VkRenderPassBeginInfo rpb = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass      = s_state.render_pass,
        .framebuffer     = s_state.framebuffers[index],
        .renderArea      = { { 0, 0 }, s_state.extent },
        .clearValueCount = 1,
        .pClearValues    = &clear,
    };
    vkCmdBeginRenderPass(cb, &rpb, VK_SUBPASS_CONTENTS_INLINE);
    /* No draws yet: the attachment's CLEAR load op is the whole frame. */
    vkCmdEndRenderPass(cb);

    if (dumping) {
        record_dump_copy(cb, s_state.images[index]);
    }

    vkEndCommandBuffer(cb);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &s_state.acquired,
        .pWaitDstStageMask    = &wait_stage,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &cb,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &s_state.rendered,
    };
    if (vkQueueSubmit(s_state.queue, 1, &si, s_state.in_flight) != VK_SUCCESS) {
        printf("[RSX vulkan] vkQueueSubmit failed\n");
        return;
    }

    if (dumping) {
        /* The copy has to land before the buffer is read. A full wait is fine
         * here: dumping is a debug path, not the play path. */
        vkWaitForFences(s_state.device, 1, &s_state.in_flight, VK_TRUE, UINT64_MAX);
        write_dump();
    }

    VkPresentInfoKHR pi = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &s_state.rendered,
        .swapchainCount     = 1,
        .pSwapchains        = &s_state.swapchain,
        .pImageIndices      = &index,
    };
    r = vkQueuePresentKHR(s_state.queue, &pi);
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR && r != VK_ERROR_OUT_OF_DATE_KHR) {
        printf("[RSX vulkan] vkQueuePresentKHR: %s\n", vk_err(r));
    }
}

static void vulkan_clear(void* ud, u32 flags, u32 color, float depth, u8 stencil)
{
    (void)ud;
    (void)flags;
    (void)depth;
    (void)stencil;

    /* RSX delivers ARGB; Vulkan wants normalised floats. */
    s_state.clear_r = (float)((color >> 16) & 0xFF) / 255.0f;
    s_state.clear_g = (float)((color >> 8)  & 0xFF) / 255.0f;
    s_state.clear_b = (float)( color        & 0xFF) / 255.0f;
}

static void vulkan_set_render_target(void* ud, const rsx_state* state)
{
    (void)ud;
    printf("[RSX vulkan] set_render_target(format=0x%X, %ux%u)\n",
           state->surface_format, state->surface_clip_w, state->surface_clip_h);
}

static void vulkan_set_viewport(void* ud, const rsx_state* state)
{
    (void)ud;
    printf("[RSX vulkan] set_viewport(%u,%u %ux%u)\n",
           state->viewport_x, state->viewport_y,
           state->viewport_w, state->viewport_h);
}

static void vulkan_draw_arrays(void* ud, u32 primitive, u32 first, u32 count)
{
    (void)ud;
    static int s_log = 0;
    if (s_log < 20) {
        printf("[RSX vulkan] draw_arrays(prim=%u, first=%u, count=%u) -- TODO: no pipeline yet\n",
               primitive, first, count);
        s_log++;
    }
}

static void vulkan_draw_indexed(void* ud, u32 primitive, u32 offset, u32 count)
{
    (void)ud;
    static int s_log = 0;
    if (s_log < 20) {
        printf("[RSX vulkan] draw_indexed(prim=%u, offset=%u, count=%u) -- TODO: no pipeline yet\n",
               primitive, offset, count);
        s_log++;
    }
}

static rsx_backend s_vulkan_backend = {
    .userdata          = &s_state,
    .init              = vulkan_init_cb,
    .shutdown          = vulkan_shutdown_cb,
    .begin_frame       = vulkan_begin_frame,
    .end_frame         = vulkan_end_frame,
    .present           = vulkan_present,
    .set_render_target = vulkan_set_render_target,
    .set_viewport      = vulkan_set_viewport,
    .set_blend         = NULL,
    .set_depth_stencil = NULL,
    .clear             = vulkan_clear,
    .draw_arrays       = vulkan_draw_arrays,
    .draw_indexed      = vulkan_draw_indexed,
    .bind_texture      = NULL,
};

/* ---------------------------------------------------------------------------
 * Public API
 * -----------------------------------------------------------------------*/

int rsx_vulkan_backend_init(u32 width, u32 height, const char* title)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.clear_b     = 64.0f / 255.0f;  /* dark blue, as the other backends */
    s_state.dump_frames = rsx_frame_dump_enabled();

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        printf("[RSX vulkan] ERROR: SDL_InitSubSystem(VIDEO) failed: %s\n",
               SDL_GetError());
        return -1;
    }

    if (SDL_Vulkan_LoadLibrary(NULL) != 0) {
        printf("[RSX vulkan] ERROR: SDL_Vulkan_LoadLibrary: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return -1;
    }

    snprintf(s_state.base_title, sizeof(s_state.base_title), "%s \xE2\x80\x94 Vulkan",
             (title && title[0]) ? title : "ps3recomp");
    s_state.window = SDL_CreateWindow(
        s_state.base_title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        (int)width, (int)height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_VULKAN);
    if (!s_state.window) {
        printf("[RSX vulkan] ERROR: SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return -1;
    }

    if (create_instance() != 0) {
        rsx_vulkan_backend_shutdown();
        return -1;
    }

    if (!SDL_Vulkan_CreateSurface(s_state.window, s_state.instance, &s_state.surface)) {
        printf("[RSX vulkan] ERROR: SDL_Vulkan_CreateSurface: %s\n", SDL_GetError());
        rsx_vulkan_backend_shutdown();
        return -1;
    }

    if (pick_device()      != 0 ||
        create_device()    != 0 ||
        create_swapchain() != 0 ||
        create_render_pass() != 0 ||
        create_commands()  != 0) {
        rsx_vulkan_backend_shutdown();
        return -1;
    }

    s_state.last_fps_ticks = SDL_GetTicks64();

    rsx_set_backend(&s_vulkan_backend);

    printf("[RSX vulkan] Window created: %ux%u (swapchain %ux%u, %u images)%s\n",
           width, height, s_state.extent.width, s_state.extent.height,
           s_state.image_count,
           s_state.dump_frames ? " (frame dump on)" : "");
    return 0;
}

void rsx_vulkan_backend_shutdown(void)
{
    if (s_state.device) {
        vkDeviceWaitIdle(s_state.device);

        if (s_state.dump_buffer) {
            vkDestroyBuffer(s_state.device, s_state.dump_buffer, NULL);
            vkFreeMemory(s_state.device, s_state.dump_memory, NULL);
        }
        if (s_state.in_flight) vkDestroyFence(s_state.device, s_state.in_flight, NULL);
        if (s_state.rendered)  vkDestroySemaphore(s_state.device, s_state.rendered, NULL);
        if (s_state.acquired)  vkDestroySemaphore(s_state.device, s_state.acquired, NULL);
        if (s_state.cmd_pool)  vkDestroyCommandPool(s_state.device, s_state.cmd_pool, NULL);

        for (u32 i = 0; i < s_state.image_count; i++) {
            if (s_state.framebuffers[i])
                vkDestroyFramebuffer(s_state.device, s_state.framebuffers[i], NULL);
            if (s_state.views[i])
                vkDestroyImageView(s_state.device, s_state.views[i], NULL);
        }
        if (s_state.render_pass) vkDestroyRenderPass(s_state.device, s_state.render_pass, NULL);
        if (s_state.swapchain)   vkDestroySwapchainKHR(s_state.device, s_state.swapchain, NULL);

        vkDestroyDevice(s_state.device, NULL);
        s_state.device = VK_NULL_HANDLE;
    }

    if (s_state.surface && s_state.instance) {
        vkDestroySurfaceKHR(s_state.instance, s_state.surface, NULL);
    }
    if (s_state.instance) {
        vkDestroyInstance(s_state.instance, NULL);
        s_state.instance = VK_NULL_HANDLE;
    }
    if (s_state.window) {
        SDL_DestroyWindow(s_state.window);
        s_state.window = NULL;
    }
    SDL_Vulkan_UnloadLibrary();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    rsx_set_backend(NULL);
    printf("[RSX vulkan] Backend shut down after %llu frames\n",
           (unsigned long long)s_state.frame_count);
}

int rsx_vulkan_backend_pump_messages(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            s_state.window_closed = 1;
            break;

        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_CLOSE) {
                s_state.window_closed = 1;
            }
            break;

        case SDL_KEYDOWN:
            if (ev.key.keysym.sym == SDLK_ESCAPE) {
                s_state.window_closed = 1;
            }
            break;

        default:
            break;
        }
    }

    return s_state.window_closed ? -1 : 0;
}
