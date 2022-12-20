#pragma once
#include <json_util.hpp>
#include <enum_names.hpp>
#include <drawlists.hpp>
#include <vk_util.hpp>
#include <imgui.h>
#include <default_hash.hpp>
#include <data_type.hpp>
#include <default_struct_equal.hpp>
#include "../imgui_nodes/crude_json.h"

namespace structures{
namespace scatterplot_wb{
    enum class plot_type_t{
        matrix,
        list,
        COUNT
    };
    const structures::enum_names<plot_type_t> plot_type_names{
        "matrix",
        "list"
    };

    enum class data_source_t{
        array_t,
        hsitogram_t
    };

    struct settings_t{
        uint32_t    plot_width{150};   // width of 1 subplot
        uint32_t    sample_count{1};
        VkFormat    plot_format{VK_FORMAT_R16G16B16A16_UNORM};
        double      plot_padding{5};   // padding inbeteween the scatter plot images
        ImVec4      plot_background_color{0, 0, 0, 1};
        plot_type_t plot_type{plot_type_t::matrix};
        size_t      large_vis_threshold{500000};

        bool operator==(const settings_t& o){
            COMP_EQ_OTHER(o, plot_width);
            COMP_EQ_OTHER(o, sample_count);
            COMP_EQ_OTHER(o, plot_padding);
            COMP_EQ_OTHER(o, plot_format);
            COMP_EQ_OTHER_VEC4(o, plot_background_color);
            COMP_EQ_OTHER(o, plot_type);
            COMP_EQ_OTHER(o, large_vis_threshold);
            return true;
        }
        settings_t() = default;
        settings_t(const crude_json::value& json){
            auto& t = *this;
            JSON_ASSIGN_JSON_FIELD_TO_STRUCT_CAST(json, t, plot_width, double);
            JSON_ASSIGN_JSON_FIELD_TO_STRUCT_CAST(json, t, sample_count, double);
            JSON_ASSIGN_JSON_FIELD_TO_STRUCT(json, t, plot_padding);
            JSON_ASSIGN_JSON_FIELD_TO_STRUCT_CAST(json, t, plot_format, double);
            JSON_ASSIGN_JSON_FIELD_TO_STRUCT_VEC4(json, t, plot_background_color);
            JSON_ASSIGN_JSON_FIELD_TO_STRUCT_ENUM_NAME(json, t, plot_type, plot_type_names);
            JSON_ASSIGN_JSON_FIELD_TO_STRUCT_CAST(json, t, large_vis_threshold, double);
        }
        operator crude_json::value() const {
            auto& t = *this;
            crude_json::value json(crude_json::type_t::object);
            JSON_ASSIGN_STRUCT_FIELD_TO_JSON_CAST(json, t, plot_width, double);
            JSON_ASSIGN_STRUCT_FIELD_TO_JSON_CAST(json, t, sample_count, double);
            JSON_ASSIGN_STRUCT_FIELD_TO_JSON(json, t, plot_padding);
            JSON_ASSIGN_STRUCT_FIELD_TO_JSON_CAST(json, t, plot_format, double);
            JSON_ASSIGN_STRUCT_FIELD_TO_JSON_VEC4(json, t, plot_background_color);
            JSON_ASSIGN_STRUCT_FIELD_TO_JSON_ENUM_NAME(json, t, plot_type, plot_type_names);
            JSON_ASSIGN_STRUCT_FIELD_TO_JSON_CAST(json, t, large_vis_threshold, double);
            return json;
        }
    };

    using appearance_tracker = structures::change_tracker<structures::drawlist::appearance>;
    struct drawlist_info{
        std::string_view                    drawlist_id;
        bool                                linked_with_drawlist;
        util::memory_view<appearance_tracker> appearance;

        bool any_change() const             {return appearance->changed;}
        void clear_changes()                {appearance->changed = false;}
        DECL_DRAWLIST_READ(drawlist_id)
        DECL_DRAWLIST_WRITE(drawlist_id)
        DECL_DATASET_READ(drawlist_read().parent_dataset)
        DECL_DATASET_WRITE(drawlist_read().parent_dataset)
        DECL_DL_TEMPLATELIST_READ(drawlist_read().parent_templatelist)
    };

    struct plot_data_t{
        image_info  image{};
        VkImageView image_view{};
        uint32_t    image_width{};
        VkFormat    image_format{};
        ImTextureID image_descriptor{}; // called desccriptor as it internally is, contains the descriptor set for imgui to render
        // samples are never stored here, as the sample count is only relevant for rendering, the image here always has only 1spp
    };

    struct attribute_pair{
        uint32_t a;
        uint32_t b;

        bool operator==(const attribute_pair& o) const {return a == o.a && b == o.b;}
    };

    struct pipeline_data{
        VkPipeline          pipeline{};
        VkPipelineLayout    pipeline_layout{};
        VkRenderPass        render_pass{};
        // frambuffer for each image is stored independent
    };

    struct output_specs{
        VkFormat                format{};
        VkSampleCountFlagBits   sample_count{};
        uint32_t                width{};
        data_type_t             data_type{};
        data_source_t           data_source{};

        DEFAULT_EQUALS(output_specs);
    };

    struct framebuffer_key{
        VkImageView image{};
        VkImageView multisample_image{};

        DEFAULT_EQUALS(framebuffer_key);
    };
}
}

DEFAULT_HASH(structures::scatterplot_wb::attribute_pair);
DEFAULT_HASH(structures::scatterplot_wb::output_specs);
DEFAULT_HASH(structures::scatterplot_wb::framebuffer_key);