#pragma once
#include <workbench_base.hpp>
#include <dataset_convert_data.hpp>

namespace workbenches{

// main data workbench which exists exactly once and is not closable
class data_workbench: public structures::workbench, public structures::drawlist_dataset_dependency{
    std::string_view                _popup_ds_id{};
    std::string_view                _popup_tl_id{};
    std::string                     _open_filename{};
    
    structures::dataset_convert_data _ds_convert_data{};

public:
    data_workbench(std::string_view id): workbench(id) {};

    void show() override;

    void add_dataset(std::string_view datasetId) override {};
    void signal_dataset_update(const util::memory_view<std::string_view>& datasetIds, update_flags flags) override {};
    void remove_dataset(std::string_view datasetId) override {};

    void add_drawlist(std::string_view drawlistId) override {};
    void signal_drawlist_update(const util::memory_view<std::string_view>& drawlistIds) override {};
    void remove_drawlist(std::string_view drawlistId) override {};
};

}