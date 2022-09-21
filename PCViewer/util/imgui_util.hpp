#pragma once
#include <imgui.h>
#include <filesystem>
#include <memory_view.hpp>
#include <vk_initializers.hpp>
#include <vk_util.hpp>
#include "../imgui_file_dialog/CustomFont.cpp"

namespace util{
namespace imgui{
inline void load_fonts(std::string_view font_folder, util::memory_view<float> font_sizes){
    if(!std::filesystem::exists(font_folder)){
        std::cout << "[warning] Font folder " << font_folder << " could not be found. Only standard font will be available" << std::endl;
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig font_conf{};
	font_conf.OversampleH = 2;
	font_conf.OversampleV = 2;
    ImWchar icons_ranges[] = { ICON_MIN_IGFD, ICON_MAX_IGFD, 0 };
	ImFontConfig icons_config{}; icons_config.MergeMode = true; icons_config.PixelSnapH = true;
    for(const auto& entry: std::filesystem::directory_iterator(font_folder)){
        if(entry.is_regular_file() && entry.path().has_extension() && entry.path().extension().string() == ".ttf"){
            // found regular file
            for(float size: font_sizes){
                io.Fonts->AddFontFromFileTTF(entry.path().c_str(), size, &font_conf, io.Fonts->GetGlyphRangesDefault());
                io.Fonts->AddFontFromMemoryCompressedBase85TTF(FONT_ICON_BUFFER_NAME_IGFD, size, &icons_config, icons_ranges);
            }
        }
    }
}

inline VkDescriptorPool create_desriptor_pool(){
    std::vector<VkDescriptorPoolSize> pool_sizes{
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };
    auto pool_info = util::vk::initializers::descriptorPoolCreateInfo(pool_sizes, 1000 * pool_sizes.size(), VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
    return util::vk::create_descriptor_pool(pool_info);
}

inline void frame_render(ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data){
    VkResult res;
    VkDevice device = globals::vk_context.device;
    auto image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    auto render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    res = vkAcquireNextImageKHR(device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
    check_vk_result(res);

    ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
	{
		res = vkWaitForFences(device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);    // wait indefinitely instead of periodically checking
		check_vk_result(res);

		res = vkResetFences(device, 1, &fd->Fence);
		check_vk_result(res);
	}
	{
		res = vkResetCommandPool(device, fd->CommandPool, 0);
		check_vk_result(res);
		VkCommandBufferBeginInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		res = vkBeginCommandBuffer(fd->CommandBuffer, &info);
		check_vk_result(res);
	}
	{
		VkRenderPassBeginInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		info.renderPass = wd->RenderPass;
		info.framebuffer = fd->Framebuffer;
		info.renderArea.extent.width = wd->Width;
		info.renderArea.extent.height = wd->Height;
		info.clearValueCount = 1;
		info.pClearValues = &wd->ClearValue;
		vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
	}

	// Record dear imgui primitives into command buffer
	ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

	// Submit command buffer
	vkCmdEndRenderPass(fd->CommandBuffer);
	{
		VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		info.waitSemaphoreCount = 1;
		info.pWaitSemaphores = &image_acquired_semaphore;
		info.pWaitDstStageMask = &wait_stage;
		info.commandBufferCount = 1;
		info.pCommandBuffers = &fd->CommandBuffer;
		info.signalSemaphoreCount = 1;
		info.pSignalSemaphores = &render_complete_semaphore;

		res = vkEndCommandBuffer(fd->CommandBuffer);
		check_vk_result(res);
		res = vkQueueSubmit(globals::vk_context.graphics_queue, 1, &info, fd->Fence);
		check_vk_result(res);
	}
}

// returns if the swapchain has to be rubuild including the width and height
inline std::tuple<bool, int, int> frame_present(ImGui_ImplVulkanH_Window* wd, SDL_Window* window){
    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkPresentInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &render_complete_semaphore;
    info.swapchainCount = 1;
    info.pSwapchains = &wd->Swapchain;
    info.pImageIndices = &wd->FrameIndex;
    VkResult res = vkQueuePresentKHR(globals::vk_context.graphics_queue, &info);
    if (res == VK_ERROR_OUT_OF_DATE_KHR)
    {
        int w, h;
        SDL_GetWindowSize(window, &w, &h);;
        return {true, w, h};
    }
    check_vk_result(res);
    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->ImageCount; // Now we can use the next set of semaphores
    return {false, wd->Width, wd->Height};
}

}
}