#include "dataset_util.hpp"
#include <netcdf_file.hpp>
#include <c_file.hpp>
#include <file_util.hpp>
#include <sstream>
#include <filesystem>
#include <fast_float.h>
#include <robin_hood.h>
#include <functional>
#include <set>
#include <numeric>
#include <fstream>
#include <load_behaviour.hpp>
#include <drawlist_creation_behaviour.hpp>
#include <drawlists.hpp>
#include <random>
#include <vk_util.hpp>
#include <vk_initializers.hpp>
#include <vma_initializers.hpp>
#include <stager.hpp>
#include <util.hpp>
#include <workbench_base.hpp>
#include <logger.hpp>
#include <data_util.hpp>
#include <string_view_util.hpp>
#include <sys_info.hpp>

namespace util{
namespace dataset{
namespace open_internals{

load_result<float> open_netcdf_float(std::string_view filename, memory_view<structures::query_attribute> query_attributes, const load_information* partial_info){
    structures::netcdf_file netcdf(filename);
    auto& variables = netcdf.get_variable_infos();
    if(query_attributes.size()){
        for(int var: util::size_range(variables)){
            if(query_attributes[var].id != variables[var].name)
                throw std::runtime_error{"open_netcdf() Attributes of the attribute query are not consistent with the netcdf file"};
        }
    }
    load_result<float> ret{};
    for(const auto& d: netcdf.get_dimension_infos())
        ret.data.dimension_sizes.push_back(d.size);
    
    for(int var: util::size_range(variables)){
        if(query_attributes.size() > var && !query_attributes[var].is_active)
            continue;
        ret.data.column_dimensions.push_back(std::vector<uint32_t>(variables[var].dependant_dimensions.begin(), variables[var].dependant_dimensions.end()));
        auto [data, fill_value] = netcdf.read_variable<float>(var);
        ret.data.columns.push_back(std::move(data));
        ret.fill_values.push_back(fill_value);
        ret.attributes.push_back(structures::attribute{variables[var].name, variables[var].name});
        for(float f: ret.data.columns.back()){
            if(ret.attributes.back().bounds.read().min > f)
                ret.attributes.back().bounds().min = f;
            if(ret.attributes.back().bounds.read().max < f)
                ret.attributes.back().bounds().max = f;
        }
    }

    return ret;
}

load_result<half> open_netcdf_half(std::string_view filename, memory_view<structures::query_attribute> query_attributes, const load_information* partial_info){
    structures::netcdf_file netcdf(filename);
    auto variables = netcdf.get_variable_infos();
    if(query_attributes.size()){
        for(int var: util::size_range(variables)){
            if(query_attributes[var].id != variables[var].name)
                throw std::runtime_error{"open_netcdf() Attributes of the attribute query are not consistent with the netcdf file"};
        }
    }
    load_result<half> ret{};
    for(const auto& d: netcdf.get_dimension_infos())
        ret.data.dimension_sizes.push_back(d.size);
    
    for(int var: util::size_range(variables)){
        if(query_attributes.size() > var && !query_attributes[var].is_active)
            continue;
        ret.data.column_dimensions.push_back(std::vector<uint32_t>(variables[var].dependant_dimensions.begin(), variables[var].dependant_dimensions.end()));
        auto [data, fill_value] = netcdf.read_variable<half>(var);
        ret.data.columns.push_back(std::move(data));
        ret.fill_values.push_back(fill_value);
        ret.attributes.push_back(structures::attribute{variables[var].name, variables[var].name});
        for(float f: ret.data.columns.back()){
            if(ret.attributes.back().bounds.read().min > f)
                ret.attributes.back().bounds().min = f;
            if(ret.attributes.back().bounds.read().max < f)
                ret.attributes.back().bounds().max = f;
        }
    }

    return ret;
}

template<> load_result<float> open_netcdf<float>(std::string_view filename, memory_view<structures::query_attribute> query_attributes, const load_information* partial_info){
    return open_netcdf_float(filename, query_attributes, partial_info);
}
template<> load_result<half> open_netcdf<half>(std::string_view filename, memory_view<structures::query_attribute> query_attributes, const load_information* partial_info){
    return open_netcdf_half(filename, query_attributes, partial_info);
}

template<typename T, typename predicate>
void erase_if(std::vector<T>& c, predicate pred){
    auto it = std::remove_if(c.begin(), c.end(), pred);
    c.erase(it, c.end());
}

template<typename T>
load_result<T> open_csv_impl(std::string_view filename, memory_view<structures::query_attribute> query_attributes, const load_information* partial_info){
    std::string input;
    {
        size_t file_size = std::filesystem::file_size(filename);
        input.resize(file_size);
        structures::c_file csv(filename, "rb");
        assert(csv);
        csv.read(util::memory_view<char>{input.data(), input.size()});
    }
    std::string_view input_view(input);

    const char delimiter = ',';
    std::vector<std::string> variable_names;
    // reading header(including attribute checks)
    {
        std::string_view line; getline(input_view, line);
        for(std::string_view variable; getline(line, variable, ',');){
            if(variable[0] == '\"'){
                if(variable.back() != '\"'){
                    auto start = variable.data();
                    getline(line, variable, '\"');
                    std::string_view tmp{}; getline(line, tmp, ',');      // skipping things until komma
                    variable = std::string_view(start + 1, variable.data() + variable.size() - start);
                }
                else
                    variable = std::string_view(variable.data() + 1, variable.size() - 2);
            }
            variable_names.push_back(std::string(variable));
        }
        if(query_attributes.size()){
            for(int var: util::size_range(variable_names)){
                if(query_attributes.size() <= var || variable_names[var] != query_attributes[var].id)
                    throw std::runtime_error{"open_csv() Attributes of the attribute query are not consistent with the csv file"};
            }
        }
    }

    // parsing the data
    load_result<T> ret{};
    std::map<uint32_t, T> category_values;
    ret.data.columns.resize(variable_names.size());
    ret.attributes.resize(variable_names.size());
    for(std::string_view line; getline(input_view, line);){
        int var = 0;
        for(std::string_view element; getline(line, element, ','); ++var){
            trim_inplace(element);

            if(element[0] == '\"'){
                if(element.back() != '\"'){
                    auto start = element.data();
                    getline(line, element, '\"');
                    std::string_view tmp; getline(line, tmp, ',');      // skipping things until komma
                    element = std::string_view(start + 1, element.data() + element.size() - start);
                }
                else
                    element = std::string_view(element.data() + 1, element.size() - 2);

                trim_inplace(element);
            }
            
            if(query_attributes.size() && !query_attributes[var].is_active)
                continue;
            if(var >= variable_names.size())
                throw std::runtime_error{"open_csv() Too much values for data row"};
            
            float val{};
            if(element.size()){
                auto parse_res = fast_float::from_chars(element.data(), element.data() + element.size(), val);
                if(parse_res.ec != std::errc{}){    // parsing error -> exchnage for category
                    std::string el(element);
                    if(ret.attributes[var].categories.count(el) > 0)
                        val = ret.attributes[var].categories[el];
                    else{
                        val = category_values[var];
                        category_values[var] += 1;
                        ret.attributes[var].categories[el] = val;
                    }
                }
            }

            if(val > ret.attributes[var].bounds.read().max)
                ret.attributes[var].bounds().max = val;
            if(val < ret.attributes[var].bounds.read().min)
                ret.attributes[var].bounds().min = val;
            ret.data.columns[var].push_back(val);
        }
    }
    // lexicographically ordering categorical data (using the automatical sorting provided by map)
    for(int var: util::size_range(ret.attributes)){
        auto& categories = ret.attributes[var].categories;
        if(categories.empty())
            continue;
        std::map<T, T> category_conversion;
        uint32_t counter{};
        for(auto& [category, value]: categories){
            T val = counter++;
            category_conversion[value] = val;
            value = val;
        }
        for(T& f: ret.data.columns[var])
            f = category_conversion[f];
        
    }

    for(int var: util::size_range(ret.attributes)){
        ret.attributes[var].id = variable_names[var];
        ret.attributes[var].display_name = variable_names[var];
    }
    if(query_attributes.size()){
        std::set<std::string_view> active_attributes;
        for(const auto& a: query_attributes)
            if(a.is_active)
                active_attributes.insert(a.id);
        erase_if(ret.attributes, [&](const structures::attribute& att){return active_attributes.count(att.id) == 0;});
    }
    erase_if(ret.data.columns, [](const std::vector<T>& v){return v.empty();});
    ret.data.dimension_sizes = {static_cast<uint32_t>(ret.data.columns[0].size())};
    ret.data.column_dimensions = std::vector<std::vector<uint32_t>>(ret.data.columns.size(), {0});
    if(query_attributes.size())
        ret.data.subsampleTrim({static_cast<uint32_t>(query_attributes.back().dimension_subsample)}, {{0, ret.data.dimension_sizes[0]}});
    ret.data.compress();
    
    return ret;
};

template<> load_result<float> open_csv(std::string_view filename, memory_view<structures::query_attribute> query_attributes, const load_information* partial_info){
    return open_csv_impl<float>(filename, query_attributes, partial_info);
}

template<> load_result<half> open_csv<half>(std::string_view filename, memory_view<structures::query_attribute> query_attributes, const load_information* partial_info){
    return open_csv_impl<half>(filename, query_attributes, partial_info);
}

load_result<half> open_combined(std::string_view folder, memory_view<structures::query_attribute> query_attributes, const load_information* partial_info){
    load_result<half> ret;
    // getting the needed attribute bounds to have the scale and translate parameter
    std::string attribute_info = std::string(folder) + "/attr.info";
    if(!std::filesystem::exists(attribute_info))
        throw std::runtime_error{"open_combined() file " + attribute_info + " does not exist"};
    std::ifstream attribute_info_file(attribute_info, std::ios::binary);
    std::string variable; float min, max;
    int c{};
    while(attribute_info_file >> variable >> min >> max){
        if(!query_attributes[c++].is_active)
            continue;
        ret.data.column_transforms.push_back(structures::scale_offset<float>{max - min, min});
        ret.attributes.push_back(structures::attribute{variable, variable, structures::change_tracker<structures::min_max<float>>{structures::min_max<float>{min, max}}});
    }

    // loading the data
    for(int i: util::i_range(query_attributes.size() - 1)){    // last element is dimension
        if(!query_attributes[i].is_active)
            continue;
        std::string column_file_name = std::string(folder) + "/" + std::to_string(i) + ".col";
        ret.data.columns.emplace_back(query_attributes.back().dimension_size);
        structures::c_file column_file(column_file_name, "rb");
        column_file.read<half>(ret.data.columns.back());
        if(logger.logging_level >= logging::level::l_4)
            logger << logging::info_prefix << " open_combined() Loaded " << query_attributes[i].id << logging::endl;
    }

    // setting up the data header
    ret.data.dimension_sizes.push_back(query_attributes.back().dimension_size);
    ret.data.column_dimensions = std::vector<std::vector<uint32_t>>(ret.data.columns.size(), {0});
    assert(ret.data.columns.size() == ret.data.column_transforms.size());
    return ret;
}
load_result<uint32_t> open_combined_compressed(std::string_view folder, memory_view<structures::query_attribute> query_attributes, const load_information* partial_info){
    return {};
}

std::vector<structures::query_attribute> get_netcdf_qeuery_attributes(std::string_view file){
    if(!std::filesystem::exists(file))
        throw std::runtime_error{"get_netcdf_qeuery_attributes() file " + std::string(file) + " does not exist"};

    structures::netcdf_file netcdf(file);
    auto& variables = netcdf.get_variable_infos();
    auto& dimensions = netcdf.get_dimension_infos();
    std::vector<structures::query_attribute> query(variables.size());

    // checking stringlength dimensions
    std::vector<bool> is_string_length_dim(dimensions.size());
    for(int dim: util::size_range(dimensions)){
        bool is_string_length = true;
        for(int var: util::size_range(variables)){
            auto& variable = variables[var];
            if(std::count(variable.dependant_dimensions.begin(), variable.dependant_dimensions.end(), dim) > 0){
                if(netcdf.var_type(var) != NC_CHAR){
                    is_string_length = false;
                    break;
                }
            }
        }
        is_string_length_dim[dim] = is_string_length;    
    }
    
    // adding the variables
    size_t data_size = 1;
    for(int dim: util::size_range(dimensions))
        if(!is_string_length_dim[dim])
            data_size *= dimensions[dim].size;
    for(int var: util::size_range(variables)){
        auto dims_without_stringlength = variables[var].dependant_dimensions;
        erase_if(dims_without_stringlength, [&](int dim){return is_string_length_dim[dim];});
        query[var] = structures::query_attribute{
            false,    // no dim
            false,     // string dim
            true,    // active
            false,    // linearize
            variables[var].name,     //id
            0,         // dimensions size
            dims_without_stringlength, // dependant dimensions
            1,        // dimension subsampling
            0,        // dimension slice
            {0, data_size}// trim bounds
        };
    }

    // adding dimension values
    for(int dim: util::size_range(dimensions)){
        query.push_back(structures::query_attribute{
            true,    // dim
            is_string_length_dim[dim],     // string dim
            true,    // active
            false,    // linearize
            dimensions[dim].name, //id
            dimensions[dim].size, // dimensions size
            {},     // dependant dimensions
            1,        // dimension subsampling
            0,        // dimension slice
            {0, dimensions[dim].size}// trim bounds
        });
    }

    return std::move(query);
}
std::vector<structures::query_attribute> get_csv_query_attributes(std::string_view file){
    if(!std::filesystem::exists(file))
        throw std::runtime_error{"get_csv_query_attributes() file " + std::string(file) + " does not exist"};

    std::ifstream filestream(std::string(file), std::ios_base::binary);
    std::string first_line; std::getline(filestream, first_line);
    std::string_view first_line_view{first_line};

    std::vector<structures::query_attribute> query{};
    for(std::string_view variable; getline(first_line_view, variable, ',');){
        query.push_back(structures::query_attribute{
            false,      // dim
            false,      // string dim
            true,       // active
            false,      // linearize
            std::string(variable), //id
            0,          // dimensions size
            {0},        // dependant dimensions
            1,          // dimension subsampling
            0,          // dimension slice
            {0, size_t(-1)}// trim bounds
        });
    }
    query.push_back(structures::query_attribute{
        true,           // dim
        false,          // string dim
        true,           // active
        false,          // linearize
        "index",        // id
        size_t(-1),     // dimensions size
        {},             // dependant dimensions
        1,              // dimension subsampling
        0,              // dimension slice
        {0, size_t(-1)} // trim bounds
    });

    return std::move(query);
}
std::vector<structures::query_attribute> get_combined_query_attributes(std::string_view folder){
    if(!std::filesystem::exists(folder))
        throw std::runtime_error{"get_combined_query_attributes() folder " + std::string(folder) + " does not exist"};
    std::string attribute_info = std::string(folder) + "/attr.info";
    std::string data_info = std::string(folder) + "/data.info";
    if(!std::filesystem::exists(attribute_info))
        throw std::runtime_error{"get_combined_query_attributes() file " + attribute_info + " does not exist"};
    if(!std::filesystem::exists(data_info))
        throw std::runtime_error{"get_combined_query_attributes() file " + data_info + " does not exist"};

    std::vector<structures::query_attribute> query;
    std::string variable; float min, max;
    std::ifstream attribute_info_file(attribute_info, std::ios::binary);
    while(attribute_info_file >> variable >> min >> max){
        query.push_back(structures::query_attribute{
            false,      // dim
            false,      // string dim
            true,       // active
            false,      // linearize
            variable,   //id
            0,          // dimensions size
            {0},        // dependant dimensions
            1,          // dimension subsampling
            0,          // dimension slice
            {0, size_t(-1)}// trim bounds
        });
    }
    std::ifstream data_info_file(data_info, std::ios::binary);
    size_t data_size;
    uint32_t data_flags;
    data_info_file >> data_size >> reinterpret_cast<uint32_t&>(data_flags);
    query.push_back(structures::query_attribute{
        true,           // dim
        false,          // string dim
        true,           // active
        false,          // linearize
        "index",        // id
        data_size,      // dimensions size
        {},             // dependant dimensions
        1,              // dimension subsampling
        0,              // dimension slice
        {0, data_size}  // trim bounds
    });
    return query;
}
}

globals::dataset_t open_dataset(std::string_view filename, memory_view<structures::query_attribute> query_attributes, data_type_preference data_type_pref){    
    // this method will open only a single dataset. If all datasets from a folder should be opened, handle the readout of all datasets outside this function
    // compressed data will be read nevertheless

    if(!std::filesystem::exists(filename))
        throw std::runtime_error{"open_dataset() file " + std::string(filename) + " does not exist"};

    globals::dataset_t dataset;
    auto [file, file_extension] = util::get_file_extension(filename);
    dataset().id = file;
    switch(data_type_pref){
    case data_type_preference::none:
        if(file_extension.empty() && std::filesystem::exists(std::string(filename) + "/attr.info")){
            assert(query_attributes.size()); // checking if the queried attributes are filled
            dataset().data_size = query_attributes.back().dimension_size;
            // checking if partial data is needed
            std::unique_ptr<open_internals::load_information> load_information{};
            if(dataset().data_size * sizeof(half) * query_attributes.size() >= globals::sys_info.ram_size)
                throw std::runtime_error{"open_dataset() File size too big"};
            auto data = open_internals::open_combined(filename, query_attributes, load_information.get());
            dataset().attributes = std::move(data.attributes);
            dataset().cpu_data() = std::move(data.data);
            dataset().data_flags.data_typ = structures::data_type::half_t;
        }
        else if(file_extension == ".nc"){
            auto data = open_internals::open_netcdf<float>(filename, query_attributes);
            dataset().attributes = data.attributes;
            dataset().cpu_data() = std::move(data.data);
            dataset().data_size = std::get<structures::data<float>>(dataset.read().cpu_data.read()).size();
            dataset().data_flags.data_typ = structures::data_type::float_t;
        }
        else if(file_extension == ".csv"){
            auto data = open_internals::open_csv<float>(filename, query_attributes);
            dataset().attributes = data.attributes;
            dataset().cpu_data() = std::move(data.data);
            dataset().data_size = std::get<structures::data<float>>(dataset.read().cpu_data.read()).size();
            dataset().data_flags.data_typ = structures::data_type::float_t;
        }
        else
            throw std::runtime_error{"open_dataset() Unkown file extension " + std::string(file_extension)};
        break;
    case data_type_preference::float_precision:
        if(file_extension.empty()){
            throw std::runtime_error{"open_dataset() opening folder data not yet supported"};
        }
        else if(file_extension == ".nc"){
            auto data = open_internals::open_netcdf<float>(filename, query_attributes);
            dataset().attributes = data.attributes;
            dataset().cpu_data() = std::move(data.data);
        }
        else if(file_extension == ".csv"){
            auto data = open_internals::open_csv<float>(filename, query_attributes);
            dataset().attributes = data.attributes;
            dataset().cpu_data() = std::move(data.data);
        }
        else
            throw std::runtime_error{"open_dataset() Unkown file extension " + std::string(file_extension)};
        dataset().data_size = std::get<structures::data<float>>(dataset.read().cpu_data.read()).size();
        dataset().data_flags.data_typ = structures::data_type::float_t;
        break;
    case data_type_preference::half_precision:
        if(file_extension.empty()){
            throw std::runtime_error{"open_dataset() opening folder data not yet supported"};
        }
        else if(file_extension == ".nc"){
            auto data = open_internals::open_netcdf<half>(filename, query_attributes);
            dataset().attributes = data.attributes;
            dataset().cpu_data() = std::move(data.data);
        }
        else if(file_extension == ".csv"){
            auto data = open_internals::open_csv<half>(filename, query_attributes);
            dataset().attributes = data.attributes;
            dataset().cpu_data() = std::move(data.data);
        }
        else
            throw std::runtime_error{"open_dataset() Unkown file extension " + std::string(file_extension)};
        dataset().data_size = std::get<structures::data<half>>(dataset.read().cpu_data.read()).size();
        dataset().data_flags.data_typ = structures::data_type::half_t;
        break;
    default:
        throw std::runtime_error{"open_dataset() unrecognized data_type_preference"};
    }
    dataset().display_name = dataset.read().id;
    dataset().backing_data = filename;
    structures::templatelist templatelist{};
    templatelist.name = structures::templatelist_name_all_indices;
    templatelist.flags.identity_indices = true;
    templatelist.min_maxs.resize(dataset.read().attributes.size());
    for(int i: util::size_range(dataset.read().attributes))
        templatelist.min_maxs[i] = dataset.read().attributes[i].bounds.read();
    templatelist.data_size = dataset.read().data_size;
    dataset().templatelists.push_back(std::make_unique<const structures::templatelist>(std::move(templatelist)));
    dataset().templatelist_index[structures::templatelist_name_all_indices] = dataset().templatelists.back().get();
    
    // gpu_data setup
    const size_t header_size = std::visit([](auto&& data) {return util::data::header_size(data);}, dataset.read().cpu_data.read());
    const uint32_t column_count = std::visit([](auto&& data) {return data.columns.size();}, dataset.read().cpu_data.read());
    VkBufferUsageFlags buffer_usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    auto header_info = util::vk::initializers::bufferCreateInfo(buffer_usage, header_size);
    auto header_alloc_info = util::vma::initializers::allocationCreateInfo();
    dataset().gpu_data.header = util::vk::create_buffer(header_info, header_alloc_info);
    dataset().gpu_data.columns.resize(column_count);
    // calculating the max data size (if data does not fit into gpu memory make things smaller)
    size_t full_byte_size = std::visit([](auto&& data) {size_t size{}; for(const auto& c: data.columns) size += c.size() * sizeof(c[0]); return size;}, dataset.read().cpu_data.read());
    size_t max_size_per_col(-1);
    if(full_byte_size > globals::vk_context.gpu_mem_size * .9){
        if(file_extension.size())   // somethings else than a tabelized data does not work
            throw std::runtime_error{"open_dataset() Data too large for gpu(" + std::to_string(double(full_byte_size) / (1 << 30)) + " GByte but only " + std::to_string(double(globals::vk_context.gpu_mem_size) / (1 << 30)) + " GByte available). Only data which was converted before with the compression workbench that is larger than gpu memory can be used"};
        
        if(logger.logging_level >= logging::level::l_3)
            logger << logging::info_prefix << " open_dataset() Data too large for gpu (" << double(full_byte_size) / (1 << 30) << " GByte but only " << double(globals::vk_context.gpu_mem_size) / (1 << 30) << " GByte available)" << logging::endl;
        max_size_per_col = globals::vk_context.gpu_mem_size * .8 / column_count;
        // creating the streaiming infos
        const uint32_t element_size = std::visit([](auto&& data){return sizeof(data.columns[0][0]);}, dataset.read().cpu_data.read());
        const uint32_t block_size = max_size_per_col / element_size;
        dataset().gpu_stream_infos.emplace();
        dataset().gpu_stream_infos->block_size = block_size;
        dataset().gpu_stream_infos->block_count = (dataset().data_size + block_size - 1) / block_size;
        dataset().gpu_stream_infos->cur_block_index = 0;
        dataset().gpu_stream_infos->cur_block_size = block_size;
        dataset().gpu_stream_infos->forward_upload = true;
        dataset().gpu_stream_infos->signal_block_upload_done = true;    // we wait for the first block at the end of this function
        dataset().registry.emplace();   // creating the registry
    }
    structures::stager::staging_buffer_info staging_info{};
    for(int i: util::i_range(column_count)){
        auto column_alloc_info = util::vma::initializers::allocationCreateInfo();
        VkBufferCreateInfo column_info{};
        util::memory_view<const uint8_t> upload_data = std::visit([&i](auto&& data) {return util::memory_view<const uint8_t>(util::memory_view(data.columns[i].data(), data.columns[i].size()));}, dataset.read().cpu_data.read());
        upload_data = util::memory_view(upload_data.data(), std::min(upload_data.size(), max_size_per_col));
        column_info = util::vk::initializers::bufferCreateInfo(buffer_usage, upload_data.byte_size());
        dataset().gpu_data.columns[i] = util::vk::create_buffer(column_info, column_alloc_info);
        // uploading the data as soon as buffer is available via the staging buffer
        staging_info.dst_buffer = dataset().gpu_data.columns[i].buffer;
        staging_info.common.data_upload = upload_data;
        globals::stager.add_staging_task(staging_info);
    }
    structures::dynamic_struct<util::data::gpu_header, uint32_t> header_bytes = std::visit([&dataset](auto&& data){return util::data::create_packed_header(data, dataset.read().gpu_data.columns);}, dataset.read().cpu_data.read());
    staging_info.dst_buffer = dataset().gpu_data.header.buffer;
    staging_info.common.data_upload = header_bytes.data();
    globals::stager.add_staging_task(staging_info);

    globals::stager.wait_for_completion();    // wait for uploadsd, then continue

    return std::move(dataset);
}

void convert_templatelist(const structures::templatelist_convert_data& convert_data){
    assert(convert_data.ds_id.size() && convert_data.tl_id.size());
    auto& ds = globals::datasets.ref_no_track()[convert_data.ds_id];
    switch(convert_data.dst){
    case structures::templatelist_convert_data::destination::drawlist:{
        structures::tracked_drawlist drawlist{};
        drawlist().id = convert_data.dst_name;
        drawlist().name = convert_data.dst_name;
        drawlist().parent_dataset = convert_data.ds_id;
        drawlist().parent_templatelist = convert_data.tl_id;
        drawlist().active_indices_bitset.resize(ds.read().templatelist_index.at(convert_data.tl_id)->data_size, true);

        auto median_buffer_info = util::vk::initializers::bufferCreateInfo(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, (ds.read().attributes.size() + 2) * sizeof(float));
        auto alloc_info = util::vma::initializers::allocationCreateInfo();
        drawlist().median_buffer = util::vk::create_buffer(median_buffer_info, alloc_info);
        auto bitmap_buffer_info = util::vk::initializers::bufferCreateInfo(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, drawlist.read().active_indices_bitset.num_blocks() * sizeof(*drawlist.read().active_indices_bitset.data()));
        drawlist().active_indices_bitset_gpu = util::vk::create_buffer(bitmap_buffer_info, alloc_info);
        if(ds.read().registry)
            drawlist().dataset_registrator = ds.ref_no_track().registry->access()->scoped_registrator();
        
        // uploading bitset_vector
        // TODO: might be unnecesary, as this bitvector will be filled by brushing pipeline
        util::memory_view<const uint8_t> data = util::memory_view(drawlist.read().active_indices_bitset.data(), (drawlist.read().active_indices_bitset.size() + drawlist.read().active_indices_bitset.bits_per_block - 1) / drawlist.read().active_indices_bitset.bits_per_block);
        structures::stager::staging_buffer_info staging_info = {structures::stager::transfer_direction::upload, drawlist.read().active_indices_bitset_gpu.buffer, 0ul, structures::stager::task_common{data, {}, {}, {}, {}, {}}};
        globals::stager.add_staging_task(staging_info);

        // waiting uploads
        globals::stager.wait_for_completion();
        globals::drawlists.write().insert({drawlist.read().id, std::move(drawlist)});

        // executing drawlist_creation_behaviour
        for(std::string_view wb_id: globals::drawlist_creation_behaviour.coupled_workbenches){
            auto wb = util::memory_view(globals::workbenches).find([&](const globals::unique_workbench& work){return work->id == wb_id;}).get();
            if(structures::drawlist_dataset_dependency* ddd = dynamic_cast<structures::drawlist_dataset_dependency*>(wb)){
                try{
                    std::vector<std::string_view> dl{globals::drawlists.read().at(std::string_view(convert_data.dst_name)).read().id};
                    ddd->add_drawlists(dl);
                }
                catch(const std::runtime_error& e){
                    logger << "[error] " << e.what() << logging::endl;
                }
            }
        }
        break;
    }
    case structures::templatelist_convert_data::destination::templatelist:{
        structures::templatelist templatelist{};
        templatelist.name = convert_data.dst_name;
        templatelist.flags.identity_indices = convert_data.dst_name == structures::templatelist_name_all_indices;
        templatelist.min_maxs.resize(ds.read().attributes.size());
        if(!templatelist.flags.identity_indices){
                templatelist.indices.resize((convert_data.trim.max - convert_data.trim.min + convert_data.subsampling - 1) / convert_data.subsampling);
            if(convert_data.random_subsampling){
                std::default_random_engine generator;
                std::uniform_int_distribution<uint32_t> distribution(convert_data.trim.min, convert_data.trim.max);
                for(uint32_t& u: templatelist.indices)
                    u = distribution(generator);
            }
            else{
                for(uint32_t i: util::i_range(convert_data.trim.min, convert_data.trim.max, convert_data.subsampling))
                    templatelist.indices[(i - convert_data.trim.min) / convert_data.subsampling] = i;
            }
            auto indices_info = util::vk::initializers::bufferCreateInfo(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, templatelist.indices.size() * sizeof(templatelist.indices[0]));
            auto indices_alloc_info = util::vma::initializers::allocationCreateInfo();
            templatelist.gpu_indices = util::vk::create_buffer(indices_info, indices_alloc_info);
            structures::stager::staging_buffer_info staging_info{};
            staging_info.dst_buffer = templatelist.gpu_indices.buffer;
            staging_info.common.data_upload = util::memory_view(templatelist.indices);
            globals::stager.add_staging_task(staging_info);
        
            std::visit([&templatelist](auto&& data){
                for(int var: util::size_range(templatelist.min_maxs)){
                    for(uint32_t i: templatelist.indices){
                        auto val = data(i, var);
                        if(val < templatelist.min_maxs[var].min)
                            templatelist.min_maxs[var].min = val;
                        if(val > templatelist.min_maxs[var].max)
                            templatelist.min_maxs[var].max = val;
                    }
                }
            }, ds.read().cpu_data.read());

            templatelist.data_size = templatelist.indices.size();
            globals::stager.wait_for_completion();    // waiting before moving to make sure the memory view for the data upload stays valid
        }
        else{
            for(int i: util::size_range(ds.read().attributes))
                templatelist.min_maxs[i] = ds.read().attributes[i].bounds.read();
            templatelist.data_size = ds.read().data_size;
        }
        templatelist.point_ratio = templatelist.indices.size() / float(ds.read().data_size);
        ds.ref_no_track().templatelists.push_back(std::make_unique<const structures::templatelist>(std::move(templatelist)));
        ds.ref_no_track().templatelist_index.insert({ds.read().templatelists.back()->name, ds.read().templatelists.back().get()});
        break;
    }
    default:
        throw std::runtime_error("convert_templatelist() Destination unkown (should be either structures::dataset_convert_data::destination::drawlist or structures::dataset_convert_data::destination::templatelist)");
    }
}

void execute_laod_behaviour(globals::dataset_t& ds){
    for(int load_behaviour_index: util::size_range(globals::load_behaviour.on_load)){
        auto &load_behaviour = globals::load_behaviour.on_load[load_behaviour_index];

        if(load_behaviour.subsampling != 1 || load_behaviour.trim.min != 0 || load_behaviour.trim.max < ds.read().data_size){
            // creating a new template list
        
            structures::templatelist_convert_data convert_info{};
            convert_info.ds_id = ds.read().id;
            convert_info.tl_id = ds.read().templatelists[0]->name;
            convert_info.dst_name = std::string(structures::templatelist_name_load_behaviour);
            convert_info.trim = load_behaviour.trim;
            convert_info.dst = structures::templatelist_convert_data::destination::templatelist;
            convert_templatelist(convert_info);
        }

        structures::templatelist_convert_data convert_info{};
        convert_info.ds_id = ds.read().id;
        convert_info.tl_id = ds.read().templatelists.back()->name;
        convert_info.dst_name = ds.read().id;
        convert_info.trim = {0, ds.read().templatelist_index.at(convert_info.tl_id)->indices.size()};
        convert_info.dst = structures::templatelist_convert_data::destination::drawlist;
        convert_templatelist(convert_info);
    }
}

void check_datasets_to_open(){
    {
    const std::string_view open_dataset_popup_name{"Open dataset(s)"};

    if(globals::paths_to_open.size()){
        if(globals::attribute_query.empty()){
            fill_query_attributes();
        }

        ImGui::OpenPopup(open_dataset_popup_name.data());
    }

    if(ImGui::BeginPopupModal(open_dataset_popup_name.data(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
        if(ImGui::CollapsingHeader("Variables/Dimensions settings")){
            if(ImGui::Button("Activate all")){
                for(auto& q: globals::attribute_query){
                    if(q.is_dimension)
                        break;
                    q.is_active = true;
                }
            }
            ImGui::SameLine();
            if(ImGui::Button("Deactivate all")){
                for(auto& q: globals::attribute_query){
                    if(q.is_dimension)
                        break;
                    q.is_active = false;
                }
            }
            if(ImGui::BeginTable("Var query table", 4, ImGuiTableFlags_BordersOuter)){
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Dimensionality");
                ImGui::TableSetupColumn("Active");
                ImGui::TableSetupColumn("Linearize");
                ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
                ImGui::TableNextColumn();
                ImGui::TableHeader("Variable name");
                ImGui::TableNextColumn();
                ImGui::TableHeader("Dimensionality");
                ImGui::TableNextColumn();
                ImGui::TableHeader("Active");
                ImGui::TableNextColumn();
                ImGui::TableHeader("Linearize");

                   for(auto& q: globals::attribute_query){
                    if(q.is_dimension)
                        continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", q.id.c_str());
                    ImGui::TableNextColumn();
                    std::stringstream dims; dims << util::memory_view(q.dependant_dimensions);
                    ImGui::Text("%s", dims.str().c_str());
                    ImGui::TableNextColumn();
                    bool act = q.is_active; ImGui::Checkbox(("##active" + q.id).c_str(), &act); q.is_active = act;
                    ImGui::TableNextColumn();
                    act = q.linearize; ImGui::Checkbox(("##lin" + q.id).c_str(), &act); q.linearize = act;
                } 
            ImGui::EndTable();
            }
            if(ImGui::BeginTable("Dim query table", 4, ImGuiTableFlags_BordersOuter)){
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Active");
                ImGui::TableSetupColumn("Subsampling");
                ImGui::TableSetupColumn("Trim indices");
                ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
                ImGui::TableNextColumn();
                ImGui::TableHeader("Dimension name");
                ImGui::TableNextColumn();
                ImGui::TableHeader("Active");
                ImGui::TableNextColumn();
                ImGui::TableHeader("Subsampling");
                ImGui::TableNextColumn();
                ImGui::TableHeader("Trim indices");

                for(auto& q: globals::attribute_query){
                    if(!q.is_dimension && !q.is_string_length_dimension)
                        continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", q.id.c_str());
                    ImGui::TableNextColumn();
                    bool act = q.is_active; ImGui::Checkbox(("##active" + q.id).c_str(), &act); q.is_active = act;
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(75);
                    ImGui::InputInt(("##sub" + q.id).c_str(), &q.dimension_subsample);
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(150);
                    if(ImGui::InputScalarN(("##trim" + q.id).c_str(), ImGuiDataType_U64, q.trim_indices.data(), 2)){
                        q.trim_indices.max = std::min(q.trim_indices.max, q.dimension_size);
                        q.trim_indices.max = std::max(q.trim_indices.min + 1, q.trim_indices.max);
                        q.trim_indices.min = std::min(q.trim_indices.min, q.trim_indices.max - 1);
                    }
                    if(!act)
                        q.trim_indices.max = q.trim_indices.min + 1;
                }
                ImGui::EndTable();
            }
        }
        static std::vector<uint8_t> activations;
        if(activations.size() != globals::paths_to_open.size())
            activations = std::vector<uint8_t>(globals::paths_to_open.size(), true);
        for(int i: util::size_range(activations))
            ImGui::Checkbox(globals::paths_to_open[i].c_str(), reinterpret_cast<bool*>(activations.data()));

        if(ImGui::Button("Open") || ImGui::IsKeyPressed(ImGuiKey_Enter)){
            for(std::string_view path: globals::paths_to_open){
                try{
                    auto dataset = open_dataset(path, globals::attribute_query);
                    auto [ds, inserted] = globals::datasets().insert({dataset.read().id, std::move(dataset)});
                    if(!inserted)
                        throw std::runtime_error{"check_dataset_to_open() Could not add dataset to the internal datasets"};
                    if(logger.logging_level >= logging::level::l_4)
                        logger << logging::info_prefix << " Loaded dataset with size " << ds->second.read().data_size << logging::endl;

                    execute_laod_behaviour(ds->second);
                }
                catch(const std::runtime_error& e){
                    logger << "[error] " << e.what() << logging::endl;
                }
            }
            ImGui::CloseCurrentPopup();
            globals::paths_to_open.clear();
            globals::attribute_query.clear();
        }
        ImGui::SameLine();
        if(ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)){
            ImGui::CloseCurrentPopup();
            globals::paths_to_open.clear();
            globals::attribute_query.clear();
        }
        ImGui::EndPopup();
    }
}
}

void check_dataset_deletion(){
    if(globals::datasets_to_delete.size()){
        // signaling all dependant workbenches
        std::vector<std::string_view> datasets(globals::datasets_to_delete.begin(), globals::datasets_to_delete.end());
        for(auto& workbench: globals::dataset_dependencies)
            workbench->remove_datasets(datasets);
        
        // adding all drawlists created from the datasets to the drawlist deletion list
        for(const auto& [dl_id, dl]: globals::drawlists.read()){
            if(globals::datasets_to_delete.count(dl.read().parent_dataset))
                globals::drawlists_to_delete.insert(dl_id);
        }

        // deleting the datasets
        bool prev_dataset_state = globals::datasets.changed;
        for(auto& ds: globals::datasets_to_delete){
            globals::datasets()[ds]().destroy_local_gpu_buffer();
            globals::datasets().erase(ds);
        }
        globals::datasets.changed = prev_dataset_state;
        globals::datasets_to_delete.clear();
    }
}

void check_dataset_gpu_stream(){
    // or if there has been an update requested to either the ds or a drawlist derived from the ds
    for(const auto& [ds_id, ds]: globals::datasets.read()){
        if(!ds.read().gpu_stream_infos || !ds.read().registry->const_access()->all_registrators_done || !ds.read().gpu_stream_infos->signal_block_update_request)
            continue;
        
        const auto& dataset = ds.read();
        assert(dataset.gpu_stream_infos);
        if(dataset.gpu_stream_infos->last_block()){
            dataset.gpu_stream_infos->forward_upload ^= 1;
            dataset.gpu_stream_infos->signal_block_upload_done = true;      // last block is already in memory for next round of update
            dataset.gpu_stream_infos->signal_block_update_request = false;  // block update done
            dataset.registry->access()->reset_registrators();
            continue;
        }
        if(dataset.gpu_stream_infos->forward_upload)
            ++dataset.gpu_stream_infos->cur_block_index;
        else
            --dataset.gpu_stream_infos->cur_block_index;

        if(logger.logging_level >= logging::level::l_4)
            logger << logging::info_prefix << " Uploading data block " << dataset.gpu_stream_infos->cur_block_index + 1 << " of " << dataset.gpu_stream_infos->block_count << " blocks" << logging::endl;
        dataset.registry->access()->reset_registrators();
        dataset.gpu_stream_infos->signal_block_upload_done = false;
        // appending upload task
        const structures::data<half>& data = std::get<structures::data<half>>(dataset.cpu_data.read());
        structures::stager::staging_buffer_info staging_info{};
        for(int i: util::size_range(data.columns)){
            staging_info.dst_buffer = dataset.gpu_data.columns[i].buffer;
            size_t offset = dataset.gpu_stream_infos->cur_block_index * dataset.gpu_stream_infos->block_size;
            size_t rest_size = data.columns[i].size() - offset;
            size_t upload_size = std::min(dataset.gpu_stream_infos->block_size, data.columns[i].size() - offset);
            dataset.gpu_stream_infos->cur_block_size = upload_size;
            staging_info.common.data_upload = util::memory_view(data.columns[i].data() + offset, upload_size);
            if(i == data.columns.size() - 1)
                staging_info.common.signal_flags = {&dataset.gpu_stream_infos->signal_block_upload_done, &globals::datasets.ref_no_track()[ds_id].changed, &globals::datasets.changed};
            globals::stager.add_staging_task(staging_info);
        }
    }
}

void check_dataset_update(){
    if(globals::datasets.changed){
        std::vector<std::string_view> changed_datasets;
        for(const auto& [ds_id, ds]: globals::datasets.read()){
            if(ds.changed)
                changed_datasets.push_back(ds_id);
        }
        for(auto& workbench: globals::drawlist_dataset_dependencies)
            workbench->signal_dataset_update(changed_datasets, {});
        for(auto id: changed_datasets){
            globals::datasets.ref_no_track()[id].ref_no_track().clear_change();
            globals::datasets.ref_no_track()[id].changed = false;
        }
        globals::datasets.changed = false;
        // setting the changed flags on drawlists created from this dataset
        for(auto dl: changed_datasets){
            if(globals::drawlists.read().at(dl).read().histogram_registry.const_access()->name_to_registry_key.empty())
                continue;
            if(!globals::drawlists()[dl]().local_brushes.read().empty())    // if local brushes exist notify that these should be reapplied
                globals::drawlists()[dl]().local_brushes();
            globals::drawlists()[dl]().histogram_registry.access()->request_change_all();
        }
    }
}
}
}