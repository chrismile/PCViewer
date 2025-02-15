#pragma once

#include <brushes.hpp>
#include <drawlists.hpp>
#include <array_struct.hpp>
#include <vk_util.hpp>
#include <vma_initializers.hpp>
#include <vma_util.hpp>
#include <brusher.hpp>
#include <vk_mem_alloc.h>
#include <stager.hpp>
#include <robin_hood.h>
#include <util.hpp>
#include <data_util.hpp>

namespace util{
namespace brushes{
struct gpu_brush{
    uint32_t range_brush_count;
    uint32_t lasso_brush_count;
    uint32_t lasso_brush_offset;
    uint32_t _;
};
using range_brush_refs = std::vector<const structures::range_brush*>;
using lasso_brush_refs = std::vector<const structures::lasso_brush*>;

inline structures::dynamic_struct<gpu_brush, float> create_gpu_brush_data(const range_brush_refs& range_brushes, const lasso_brush_refs& lasso_brushes, util::memory_view<const structures::attribute> attributes){
    const auto attribute_to_index = util::data::attribute_to_index(attributes);
    // creating an attribute major storage for the range_brushes to be able to convert the data more easily
    std::vector<std::map<uint32_t, std::vector<structures::min_max<float>>>> axis_brushes(range_brushes.size());
    uint32_t dynamic_size{static_cast<uint32_t>(range_brushes.size())};// brush_offsets 
    for(size_t i: size_range(range_brushes)){
        auto& b = axis_brushes[i];
        for(const auto& range: *range_brushes[i])
            b[attribute_to_index.at(range.attr)].push_back({range.min, range.max});
        dynamic_size += 1;                                              // nAxisMaps
        dynamic_size += static_cast<uint32_t>(b.size());                // axisOffsets
        for(const auto& [axis, ranges]: b){
            dynamic_size += 1;                                          // nRanges
            dynamic_size += 1;                                          // axis
            dynamic_size += static_cast<uint32_t>(ranges.size() * 2);   // ranges
        }
    }
    dynamic_size += static_cast<uint32_t>(lasso_brushes.size());        // brush_offsets
    for(size_t i: size_range(lasso_brushes)){
        const auto& lasso_brush = *lasso_brushes[i];
        dynamic_size += 1;                          // nPolygons
        dynamic_size += static_cast<uint32_t>(lasso_brush.size());      // polygonOffsets
        for(const auto& polygon: lasso_brush){
            dynamic_size += 2;                      // attr1, attr2
            dynamic_size += 1;                      // nBorderPoints
            dynamic_size += static_cast<uint32_t>(polygon.borderPoints.size()) * 2; // borderPoints
        }
    }

    // converting teh brush data to a linearized array
    // priority for linearising trhe brush data: brushes, axismap, ranges
    // the float array following the brushing info has the following layout
    //      vector<float> brushOffsets, vector<Brush> brushes;                                  // where brush offests describe the index in the float array from which the brush at index i is positioned
    //      with Brush = {flaot nAxisMaps, vector<float> axisOffsets, vector<AxisMap> axisMaps} // axisOffsets same as brushOffsetsf for the axisMap
    //      with AxisMap = {float nrRanges, fl_brushEventoat axis, vector<Range> ranges}
    //      with Range = {float min, float max}
    structures::dynamic_struct<gpu_brush, float> gpu_data(dynamic_size);
    gpu_data->range_brush_count = static_cast<uint32_t>(axis_brushes.size());
    uint32_t cur_offset{static_cast<uint32_t>(axis_brushes.size())};   // base offset for the first brush comes after the offsets
    for(size_t brush: size_range(axis_brushes)){
        gpu_data[brush] = float(cur_offset);           // brush_offset
        gpu_data[cur_offset++] = float(axis_brushes[brush].size());    // nAxisMaps
        uint32_t axis_offsets = cur_offset;
        cur_offset += static_cast<uint32_t>(axis_brushes[brush].size());               // skipping the axis offsets
        for(const auto& [axis, ranges]: axis_brushes[brush]){
            gpu_data[axis_offsets++] = float(cur_offset);              // axisOffsets
            gpu_data[cur_offset++] = float(ranges.size());             // nRanges
            gpu_data[cur_offset++] = float(axis);                      // axis
            for(const auto& range: ranges){
                gpu_data[cur_offset++] = range.min;             // min
                gpu_data[cur_offset++] = range.max;             // max
            }
        }
    }

    // converting the lasso data to a linearized array and appending after the range bruhes
    // priority for linearising the data: brushes, lassos, border_points
    // the float array following the range_brushes has the following layout
    //      vector<float> brush_offsets, vector<lasso_brush> brushes;           // where offsets are the absolute offsets of the lassos, offsets begin at gpu_brush.lasso_brush_offset
    //      with lasso_brush = {float nPolygons, vector<float> polygonOffsets, vector<polygon> polygons}
    //      with polygon = {float attr1, float attr2, float nBorderPoints, vector<vec2> borderPoints}
    //      with vec2 = {float p1, float p2}
    gpu_data->lasso_brush_count = static_cast<uint32_t>(lasso_brushes.size());
    gpu_data->lasso_brush_offset = cur_offset;
    cur_offset += static_cast<uint32_t>(lasso_brushes.size());
    for(size_t i: size_range(lasso_brushes)){
        const auto& lasso_brush = *lasso_brushes[i];
        gpu_data[gpu_data->lasso_brush_offset + i] = float(cur_offset);    // brush_offset
        gpu_data[cur_offset++] = float(lasso_brush.size());                // nPolygons
        uint32_t polygon_offsets = cur_offset;
        cur_offset += static_cast<uint32_t>(lasso_brush.size());           // skipping the polygon_offsets
        for(const auto& polygon: lasso_brush){
            gpu_data[polygon_offsets++] = float(cur_offset);               // polygon_offsets
            gpu_data[cur_offset++] = float(attribute_to_index.at(polygon.attr1));                 // attr1
            gpu_data[cur_offset++] = float(attribute_to_index.at(polygon.attr2));                 // attr2
            gpu_data[cur_offset++] = float(polygon.borderPoints.size());   // nBorderPoints
            for(const auto& b: polygon.borderPoints){
                gpu_data[cur_offset++] = b.x;                       // p1
                gpu_data[cur_offset++] = b.y;                       // p2
            }
        }
    }

    return std::move(gpu_data);
}

// uploads changed local and global brushes
inline void upload_changed_brushes(){
    std::optional<structures::semaphore> global_brushes_semaphore;
    std::optional<structures::semaphore> local_brushes_semaphore;
    // global brushes
    std::vector<structures::dynamic_struct<gpu_brush, float>> brush_datas;
    if(globals::global_brushes.changed){
        size_t global_brush_byte_size{};
        std::vector<std::string_view> ds_order{};
        // for each dataset a global_brush_data is needded, as different datasets have different memory layouts
        for(const auto& [ds_id, ds]: globals::datasets.read()){
            range_brush_refs range_brushes;
            lasso_brush_refs lasso_brushes;
            for(const auto& brush: globals::global_brushes.read()){
                if(!brush.read().active)
                    continue;
                range_brushes.push_back(&brush.read().ranges);
                lasso_brushes.push_back(&brush.read().lassos);
            }
            brush_datas.emplace_back(create_gpu_brush_data(range_brushes, lasso_brushes, ds.read().attributes));
            globals::global_brushes.dataset_brush_info_offsets[ds_id] = global_brush_byte_size;
            global_brush_byte_size += brush_datas.back().byte_size();
            ds_order.emplace_back(ds_id);
        }
        if(global_brush_byte_size > globals::global_brushes.brushes_gpu.size){
            util::vk::destroy_buffer(globals::global_brushes.brushes_gpu);
            auto buffer_info = util::vk::initializers::bufferCreateInfo(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, global_brush_byte_size);
            auto alloc_info = util::vma::initializers::allocationCreateInfo();
            globals::global_brushes.brushes_gpu = util::vk::create_buffer(buffer_info, alloc_info);
        }
        for(const auto& [brush_data, i]: util::enumerate(brush_datas)){
            global_brushes_semaphore.emplace();
            structures::stager::staging_buffer_info staging_info{};
            staging_info.dst_buffer = globals::global_brushes.brushes_gpu.buffer;
            staging_info.dst_buffer_offset = globals::global_brushes.dataset_brush_info_offsets[ds_order[i]];
            staging_info.data_upload = brush_data.data();
            staging_info.cpu_semaphore = &global_brushes_semaphore.value();
            globals::stager.add_staging_task(staging_info);
        }
    }

    // local brushes
    if(globals::drawlists.changed){
        for(const auto& [id, dl]: globals::drawlists.read()){
            if(!dl.changed || !dl.read().local_brushes.changed)
                continue;
            range_brush_refs range_brushes{&dl.read().local_brushes.read().ranges};
            lasso_brush_refs lasso_brushes{&dl.read().local_brushes.read().lassos};
            brush_datas.emplace_back(create_gpu_brush_data(range_brushes, lasso_brushes, dl.read().dataset_read().attributes));
            auto& brush_data = brush_datas.back();

            if(brush_data.byte_size() > dl.read().local_brushes_gpu.size){
                util::vk::destroy_buffer(globals::drawlists.ref_no_track()[id].ref_no_track().local_brushes_gpu);
                auto buffer_info = util::vk::initializers::bufferCreateInfo(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, brush_data.byte_size());
                auto alloc_info = util::vma::initializers::allocationCreateInfo();
                globals::drawlists()[id]().local_brushes_gpu = util::vk::create_buffer(buffer_info, alloc_info);
            }

            local_brushes_semaphore.emplace();
            structures::stager::staging_buffer_info staging_info{};
            staging_info.dst_buffer = dl.read().local_brushes_gpu.buffer;
            staging_info.data_upload = brush_data.data();
            staging_info.cpu_semaphore = &local_brushes_semaphore.value();
            globals::stager.add_staging_task(staging_info);
        }
    }

    if(global_brushes_semaphore)
        global_brushes_semaphore.value().acquire();
    if(local_brushes_semaphore)
        local_brushes_semaphore.value().acquire();
}

inline void update_drawlist_active_indices(){
    if(!globals::global_brushes.changed && !globals::drawlists.changed)
        return;

    // delay brush update when histogram update or delayed ops not yet ready
    for(const auto& [id, dl]: globals::drawlists.read()){
        if(!dl.read().histogram_registry.const_access()->registrators_done)
            return;
        auto registry_access = dl.read().histogram_registry.const_access();
        //if(dl.changed && dl.read().local_brushes.changed && !registry_access->registrators_done && registry_access->dataset_update_done)
        //    return;

        if(!dl.read().delayed_ops.delayed_ops_done)
            return;
    }
    
    for(const auto& [id, dl]: globals::drawlists.read()){
        if(!globals::global_brushes.changed && !dl.read().immune_to_global_brushes.changed && !dl.read().local_brushes.changed)
            continue;

        if(globals::global_brushes.changed && !dl.read().local_brushes.changed && dl.read().immune_to_global_brushes.read() && !dl.read().immune_to_global_brushes.changed)
            continue;

        pipelines::brusher::brush_info brush_info{};
        brush_info.drawlist_id = id;
        pipelines::brusher::instance().brush(brush_info);

        auto& drawlist = globals::drawlists()[id]();

        // notifying update of the histograms
        drawlist.histogram_registry.access()->request_change_all();
        // check priority updates
        if(drawlist.delayed_ops.priority_rendering_requested && drawlist.histogram_registry.const_access()->is_used()){
            drawlist.delayed_ops.priority_sorting_done = false;
            drawlist.delayed_ops.priority_rendering_sorting_started = false;
            drawlist.delayed_ops.delayed_ops_done = false;
        }

        drawlist.local_brushes.changed = false;
        drawlist.immune_to_global_brushes.changed = false;

        if(logger.logging_level >= logging::level::l_5)
            logger << logging::info_prefix << " util::brushes::update_drawlist_active_indices() Updated activations" << logging::endl;
    }

    globals::global_brushes.changed = false;
    pipelines::brusher::instance().wait_for_fence();
}

inline const structures::range_brush& get_selected_range_brush_const(){
    switch(globals::brush_edit_data.brush_type){
    case structures::brush_edit_data::brush_type::global:
        assert(std::count_if(globals::global_brushes.read().begin(), globals::global_brushes.read().end(), [&](const structures::tracked_brush& b){return b.read().id == globals::brush_edit_data.global_brush_id;}) != 0);
        return std::find_if(globals::global_brushes.read().begin(), globals::global_brushes.read().end(), [&](const structures::tracked_brush& b){return b.read().id == globals::brush_edit_data.global_brush_id;})->read().ranges;
    case structures::brush_edit_data::brush_type::local:
        return globals::drawlists.read().at(globals::brush_edit_data.local_brush_id).read().local_brushes.read().ranges;
    default:
        assert(false && "Not yet implementd");
        static structures::range_brush none{};
        return none;
    }
}

inline structures::range_brush& get_selected_range_brush(){
    switch(globals::brush_edit_data.brush_type){
    case structures::brush_edit_data::brush_type::global:
        assert(std::count_if(globals::global_brushes.read().begin(), globals::global_brushes.read().end(), [&](const structures::tracked_brush& b){return b.read().id == globals::brush_edit_data.global_brush_id;}) != 0);
        return std::find_if(globals::global_brushes().begin(), globals::global_brushes().end(), [&](const structures::tracked_brush& b){return b.read().id == globals::brush_edit_data.global_brush_id;})->write().ranges;
    case structures::brush_edit_data::brush_type::local:
        return globals::drawlists().at(globals::brush_edit_data.local_brush_id)().local_brushes().ranges;
    default:
        assert(false && "Not yet implementd");
        static structures::range_brush none{};
        return none;
    }
}

inline const structures::lasso_brush& get_selected_lasso_brush_const(){
    switch(globals::brush_edit_data.brush_type){
    case structures::brush_edit_data::brush_type::global:
        assert(std::count_if(globals::global_brushes.read().begin(), globals::global_brushes.read().end(), [&](const structures::tracked_brush& b){return b.read().id == globals::brush_edit_data.global_brush_id;}) != 0);
        return std::find_if(globals::global_brushes.read().begin(), globals::global_brushes.read().end(), [&](const structures::tracked_brush& b){return b.read().id == globals::brush_edit_data.global_brush_id;})->read().lassos;
    case structures::brush_edit_data::brush_type::local:
        return globals::drawlists.read().at(globals::brush_edit_data.local_brush_id).read().local_brushes.read().lassos;
    default:
        assert(false && "Not yet implementd");
        static structures::lasso_brush none{};
        return none;
    }
}

inline structures::lasso_brush& get_selected_lasso_brush(){
    switch(globals::brush_edit_data.brush_type){
    case structures::brush_edit_data::brush_type::global:
        assert(std::count_if(globals::global_brushes.read().begin(), globals::global_brushes.read().end(), [&](const structures::tracked_brush& b){return b.read().id == globals::brush_edit_data.global_brush_id;}) != 0);
        return std::find_if(globals::global_brushes().begin(), globals::global_brushes().end(), [&](const structures::tracked_brush& b){return b.read().id == globals::brush_edit_data.global_brush_id;})->write().lassos;
    case structures::brush_edit_data::brush_type::local:
        return globals::drawlists().at(globals::brush_edit_data.local_brush_id)().local_brushes().lassos;
    default:
        assert(false && "Not yet implementd");
        static structures::lasso_brush none{};
        return none;
    }
}

inline ImU32 get_brush_color(){
    switch(globals::brush_edit_data.brush_type){
    case structures::brush_edit_data::brush_type::global:
        return static_cast<ImU32>(globals::brush_edit_data.global_color);
    case structures::brush_edit_data::brush_type::local:
        return static_cast<ImU32>(globals::brush_edit_data.local_color);
    default:
    return IM_COL32_WHITE;
    }
}

inline void delete_brushes(const robin_hood::unordered_set<structures::range_id>& brush_delete){
    auto& ranges = util::brushes::get_selected_range_brush();
    for(structures::range_id range: brush_delete){
        ranges.erase(std::find_if(ranges.begin(), ranges.end(), [&](const structures::axis_range& r){return r.id == range;}));
    }
    globals::brush_edit_data.selected_ranges.clear();
}
}   
}