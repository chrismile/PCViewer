#pragma once
#include <memory_view.hpp>
#include <vk_context.hpp>
#include <robin_hood.h>
#include <change_tracker.hpp>
#include <rendering_structs.hpp>
#include <drawlists.hpp>
#include <optional>
#include <chrono>

namespace workbenches{
    class parallel_coordinates_workbench;
}

namespace pipelines{

class parallel_coordinates_renderer{
    using appearance_tracker = structures::change_tracker<structures::drawlist::appearance>;
    using output_specs = structures::parallel_coordinates_renderer::output_specs;
    using pipeline_data = structures::parallel_coordinates_renderer::pipeline_data;
    using time_point = std::chrono::time_point<std::chrono::system_clock>;

    struct push_constants{VkDeviceAddress attribute_info_address;};

    const std::string_view vertex_shader_path{""};
    const std::string_view large_vis_vertex_shader_path{""};
    const std::string_view geometry_shader_path{""};
    const std::string_view fragment_shader_path{""};

    // vulkan resources that are the same for all drawlists/parallel_coordinates_windows
    structures::buffer_info                                 _attribute_info_buffer;
    VkCommandPool                                           _command_pool;

    robin_hood::unordered_map<output_specs, pipeline_data>  _pipelines;
    robin_hood::unordered_map<VkPipeline, time_point>       _pipeline_last_use;

    const pipeline_data& get_or_create_pipeline(const output_specs& output_specs);

    parallel_coordinates_renderer();

public:
    using drawlist_info = structures::parallel_coordinates_renderer::drawlist_info;

    parallel_coordinates_renderer(const parallel_coordinates_renderer&) = delete;
    parallel_coordinates_renderer& operator=(const parallel_coordinates_renderer&) = delete;

    static parallel_coordinates_renderer& instance();

    VkSemaphore render(const workbenches::parallel_coordinates_workbench& workbench);

    uint32_t max_pipeline_count{20};
};
    
};
