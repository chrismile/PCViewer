#include "GpuRadixSorter.hpp"
#include "PCUtil.h"
#include <algorithm>
#include <chrono>
#include <numeric>

GpuRadixSorter::GpuRadixSorter(const VkUtil::Context& context):
_vkContext(context)
{
    if(context.device){
        //query subgroup size to set the specialization constant correctly
        VkPhysicalDeviceSubgroupProperties subgroupProperties;
        subgroupProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
        subgroupProperties.pNext = NULL;
        VkPhysicalDeviceProperties2 physicalDeviceProperties;
        physicalDeviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        physicalDeviceProperties.pNext = &subgroupProperties;
        vkGetPhysicalDeviceProperties2(_vkContext.physicalDevice, &physicalDeviceProperties);
        if(subgroupProperties.subgroupSize < 16){   // we need at least 16 wide subgroups for global scan
            std::cout << "Gpu has only " << subgroupProperties.subgroupSize << " wide subgroups available, however 16 wide subgroups are needed." << std::endl;
            std::cout << "Gpu sorter falls back to c++ <algorithm>std::sort for sorting" << std::endl;
            return;
        }

        std::vector<VkDescriptorSetLayoutBinding> bindings;
        VkDescriptorSetLayoutBinding binding;
        binding.binding = 0;                        //keys
        binding.descriptorCount = 2;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings.push_back(binding);

        binding.binding = 1;                        //groupInfos
        binding.descriptorCount = 1;
        bindings.push_back(binding);
        
        binding.binding = 2;                        //Dispatch infos
        bindings.push_back(binding);

        binding.binding = 3;                        //global uniform info
        binding.descriptorCount = 2;
        bindings.push_back(binding);

        std::vector<VkSpecializationMapEntry> entries{{0,0,sizeof(uint32_t)}};   // subgroup size
        std::vector<uint32_t> values{subgroupProperties.subgroupSize};
        VkSpecializationInfo specializationConstants{
            static_cast<uint32_t>(entries.size()),
            entries.data(),
            sizeof(uint32_t),
            values.data()
        };

        VkShaderModule shaderModule = VkUtil::createShaderModule(context.device, PCUtil::readByteFile(_shaderLocalSortPath));
        VkUtil::createDescriptorSetLayout(context.device, bindings, &_localSortPipeline.descriptorSetLayout);
        VkUtil::createComputePipeline(context.device, shaderModule, {_localSortPipeline.descriptorSetLayout}, &_localSortPipeline.pipelineLayout, &_localSortPipeline.pipeline, &specializationConstants);
    
        shaderModule = VkUtil::createShaderModule(context.device, PCUtil::readByteFile(_shaderHistogramPath));
        VkUtil::createComputePipeline(context.device, shaderModule, {_localSortPipeline.descriptorSetLayout}, & _histogramPipeline.pipelineLayout, &_histogramPipeline.pipeline, &specializationConstants);

        shaderModule = VkUtil::createShaderModule(context.device, PCUtil::readByteFile(_shaderGlobalScanPath));
        VkUtil::createComputePipeline(context.device, shaderModule, {_localSortPipeline.descriptorSetLayout}, & _globalScanPipeline.pipelineLayout, &_globalScanPipeline.pipeline, &specializationConstants);
        
        shaderModule = VkUtil::createShaderModule(context.device, PCUtil::readByteFile(_shaderScatteringPath));
        VkUtil::createComputePipeline(context.device, shaderModule, {_localSortPipeline.descriptorSetLayout}, & _scatterPipeline.pipelineLayout, &_scatterPipeline.pipeline, &specializationConstants);
    
        shaderModule = VkUtil::createShaderModule(context.device, PCUtil::readByteFile(_shaderControlPath));
        VkUtil::createComputePipeline(context.device, shaderModule, {_localSortPipeline.descriptorSetLayout}, & _controlPipeline.pipelineLayout, &_controlPipeline.pipeline, &specializationConstants);

        shaderModule = VkUtil::createShaderModule(context.device, PCUtil::readByteFile(_shaderDispatchPath));
        VkUtil::createComputePipeline(context.device, shaderModule, {_localSortPipeline.descriptorSetLayout}, & _dispatchPipeline.pipelineLayout, &_dispatchPipeline.pipeline, &specializationConstants);
    }
}

GpuRadixSorter::~GpuRadixSorter(){
    _controlPipeline.vkDestroy(_vkContext);
    _dispatchPipeline.vkDestroy(_vkContext);
    _localSortPipeline.vkDestroy(_vkContext);
    _histogramPipeline.vkDestroy(_vkContext);
    _globalScanPipeline.vkDestroy(_vkContext);
    _scatterPipeline.vkDestroy(_vkContext);
}

void GpuRadixSorter::sort(std::vector<uint32_t>& v){
    if(!_histogramPipeline.pipeline){
        std::cout << "Gpu sorting not available, using standard c++ sort" << std::endl;
        std::sort(v.begin(), v.end());
        return;
    }
    const uint32_t controlSize = 64;
    const uint32_t KPB = 384 * 18;  //keys per block/workgroup
    uint32_t firstPassGroups = (v.size() + KPB - 1) / KPB; 
    uint32_t uniformBufferSize = 258 * 258 * 258 + 4;   // current max amount of histograms (not histogram mergin currently possible)
    uint32_t groupInfoSize = uniformBufferSize;         // in worst case for each global histogram a single worker...
    uint32_t dispatchSize = 9;

    // Buffer creation and filling -------------------------------------------------------------------------------------
    VkBuffer dispatch, uniformFront, uniformBack, keysFront, keysBack, groupInfos;
    VkDeviceMemory memory;
    VkUtil::createBuffer(_vkContext.device, controlSize * sizeof(uint32_t), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &dispatch);
    VkUtil::createBuffer(_vkContext.device, v.size() * sizeof(v[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &keysFront);
    VkUtil::createBuffer(_vkContext.device, v.size() * sizeof(v[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &keysBack);
    VkUtil::createBuffer(_vkContext.device, uniformBufferSize * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &uniformFront);
    VkUtil::createBuffer(_vkContext.device, uniformBufferSize * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &uniformBack);
    VkUtil::createBuffer(_vkContext.device, groupInfoSize * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &groupInfos);
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkMemoryRequirements memReq;
    uint32_t memBits{};
    vkGetBufferMemoryRequirements(_vkContext.device, dispatch, &memReq);
    allocInfo.allocationSize = memReq.size;
    memBits |= memReq.memoryTypeBits;

    uint32_t uniformFrontOffset = allocInfo.allocationSize;
    vkGetBufferMemoryRequirements(_vkContext.device, uniformFront, &memReq);
    allocInfo.allocationSize += memReq.size;
    memBits |= memReq.memoryTypeBits;

    uint32_t uniformBackOffset = allocInfo.allocationSize;
    vkGetBufferMemoryRequirements(_vkContext.device, uniformBack, &memReq);
    allocInfo.allocationSize += memReq.size;
    memBits |= memReq.memoryTypeBits;

    uint32_t keysFrontOffset = allocInfo.allocationSize;
    vkGetBufferMemoryRequirements(_vkContext.device, keysFront, &memReq);
    allocInfo.allocationSize += memReq.size;
    memBits |= memReq.memoryTypeBits;

    uint32_t keysBackOffset = allocInfo.allocationSize;
    vkGetBufferMemoryRequirements(_vkContext.device, keysBack, &memReq);
    allocInfo.allocationSize += memReq.size;
    memBits |= memReq.memoryTypeBits;

    uint32_t groupInfosOffset = allocInfo.allocationSize;
    vkGetBufferMemoryRequirements(_vkContext.device, groupInfos, &memReq);
    allocInfo.allocationSize += memReq.size;
    memBits |= memReq.memoryTypeBits;
    allocInfo.memoryTypeIndex = VkUtil::findMemoryType(_vkContext.physicalDevice, memBits, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    vkAllocateMemory(_vkContext.device, &allocInfo, nullptr, &memory);
    vkBindBufferMemory(_vkContext.device, dispatch, memory, 0);
    vkBindBufferMemory(_vkContext.device, uniformFront, memory, uniformFrontOffset);
    vkBindBufferMemory(_vkContext.device, uniformBack, memory, uniformBackOffset);
    vkBindBufferMemory(_vkContext.device, keysFront, memory, keysFrontOffset);
    vkBindBufferMemory(_vkContext.device, keysBack, memory, keysBackOffset);
    vkBindBufferMemory(_vkContext.device, groupInfos, memory, groupInfosOffset);

    VkUtil::uploadData(_vkContext.device, memory, keysFrontOffset, v.size() * sizeof(v[0]), v.data());
    std::vector<uint32_t> cpuUniform(5, 0);
    cpuUniform[0] = 0;  //always start with pass nr 0
    cpuUniform[1] = 1;  //1 global histogram in the beginning
    cpuUniform[2] = firstPassGroups;
    cpuUniform[3] = 0;  //beginning of the list to sort
    cpuUniform[4] = v.size(); //end of the list to sort
    VkUtil::uploadData(_vkContext.device, memory, uniformFrontOffset, cpuUniform.size() * sizeof(cpuUniform[0]), cpuUniform.data());
    VkUtil::uploadData(_vkContext.device, memory, uniformBackOffset, cpuUniform.size() * sizeof(cpuUniform[0]), cpuUniform.data());
    std::vector<uint32_t> controlBytes(controlSize, 1);
    controlBytes[0] = (v.size() + KPB - 1) / KPB;
    controlBytes[4] = 1 + controlBytes[0];  //one reduction of the global histogram, + reduction of all local groups
    controlBytes[8] = 1;
    VkUtil::uploadData(_vkContext.device, memory, 0, controlBytes.size() * sizeof(uint32_t), controlBytes.data());

    // Descriptorset setup filling -------------------------------------------------------------------------------------
    VkDescriptorSet descSet;
    VkUtil::createDescriptorSets(_vkContext.device, {_localSortPipeline.descriptorSetLayout}, _vkContext.descriptorPool, &descSet);
    VkUtil::updateDescriptorSet(_vkContext.device, keysFront, v.size() * sizeof(uint32_t), 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descSet);
    VkUtil::updateArrayDescriptorSet(_vkContext.device, keysBack, v.size() * sizeof(uint32_t), 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descSet);
    VkUtil::updateDescriptorSet(_vkContext.device, groupInfos, groupInfoSize * sizeof(uint32_t), 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descSet);
    VkUtil::updateDescriptorSet(_vkContext.device, dispatch, controlSize * sizeof(uint32_t), 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descSet);
    VkUtil::updateDescriptorSet(_vkContext.device, uniformFront, uniformBufferSize * sizeof(uint32_t), 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descSet);
    VkUtil::updateArrayDescriptorSet(_vkContext.device, uniformBack, uniformBufferSize * sizeof(uint32_t), 3, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descSet);

    // Creating and submitting commands -------------------------------------------------------------------------------------
    VkCommandBuffer commands;
    VkUtil::createCommandBuffer(_vkContext.device, _vkContext.commandPool, &commands);
    for(int i = 0; i < 4; ++i){
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, _localSortPipeline.pipelineLayout, 0, 1, &descSet, 0, nullptr);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, _histogramPipeline.pipeline);
        vkCmdDispatchIndirect(commands, dispatch, 0);
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, {}, 0, {}, 0, {});
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, _globalScanPipeline.pipeline);
        vkCmdDispatchIndirect(commands, dispatch, 4);
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, {}, 0, {}, 0, {});
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, _scatterPipeline.pipeline);
        vkCmdDispatchIndirect(commands, dispatch, 0);
        if(i != 3){
            vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, {}, 0, {}, 0, {});
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, _controlPipeline.pipeline);
            vkCmdDispatchIndirect(commands, dispatch, 8);
            vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, {}, 0, {}, 0, {});
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, _dispatchPipeline.pipeline);
            vkCmdDispatch(commands, 1, 1, 1);
        }
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    VkUtil::commitCommandBuffer(_vkContext.queue, commands);
    VkResult res = vkQueueWaitIdle(_vkContext.queue); check_vk_result(res);
    auto t2 = std::chrono::high_resolution_clock::now();
    VkUtil::downloadData(_vkContext.device, memory, keysFrontOffset, v.size() * sizeof(v[0]), v.data());
    #ifdef ENABLE_TEST_SORT
    std::cout << "Sorting " << v.size() << " elements took " << std::chrono::duration<double, std::milli>(t2 - t1).count() << " ms" << std::endl;
    #endif
}

void GpuRadixSorter::sort(std::vector<float>& v){
    if(!_histogramPipeline.pipeline){
        std::cout << "Gpu sorting not available, using standard c++ sort" << std::endl;
        std::sort(v.begin(), v.end());
        return;
    }
    // sorting floats simply by mappgin them to uints and then running sortUints
    auto mapFloat2Uint = [](float n){uint32_t f = *reinterpret_cast<uint32_t*>(&n); uint32_t mask = -int(f >> 31) | 0x80000000; return f ^ mask;};
    auto mapUint2Float = [](uint32_t n){uint32_t mask = ((n >> 31) - 1) | 0x80000000; n ^= mask; return *reinterpret_cast<float*>(&n);};
    std::vector<uint32_t> m(v.size());
    for(uint32_t i = 0; i < v.size(); ++i)
        m[i] = mapFloat2Uint(v[i]);
    sort(m);
    for(uint32_t i = 0; i < m.size(); ++i)
        v[i] = mapUint2Float(m[i]);
}

bool GpuRadixSorter::checkLocalSort(){
    bool correct = true;
    const uint32_t threadsPerLocalSort = 512, keysPerThread = 16;
    const uint32_t keysPerLocalSort = threadsPerLocalSort * keysPerThread;
    const uint32_t uinformBufferSize = 64;

    VkDescriptorSet descSet;
    VkUtil::createDescriptorSets(_vkContext.device, {_localSortPipeline.descriptorSetLayout}, _vkContext.descriptorPool, &descSet);
    VkBuffer keys, uniformBuffer;
    VkDeviceMemory memory;
    VkUtil::createBuffer(_vkContext.device, keysPerLocalSort * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &keys);
    VkUtil::createBuffer(_vkContext.device, uinformBufferSize * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &uniformBuffer);
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkMemoryRequirements memReq;
    uint32_t memBits{};
    vkGetBufferMemoryRequirements(_vkContext.device, keys, &memReq);
    allocInfo.allocationSize = memReq.size;
    memBits |= memReq.memoryTypeBits;
    vkGetBufferMemoryRequirements(_vkContext.device, uniformBuffer, &memReq);
    allocInfo.allocationSize += memReq.size;
    memBits |= memReq.memoryTypeBits;
    allocInfo.memoryTypeIndex = VkUtil::findMemoryType(_vkContext.physicalDevice, memBits, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    vkAllocateMemory(_vkContext.device, &allocInfo, nullptr, &memory);
    vkBindBufferMemory(_vkContext.device, uniformBuffer, memory, 0);
    vkBindBufferMemory(_vkContext.device, keys, memory, uinformBufferSize * sizeof(uint32_t));

    VkUtil::updateDescriptorSet(_vkContext.device, keys, keysPerLocalSort * sizeof(uint32_t), 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descSet);
    VkUtil::updateArrayDescriptorSet(_vkContext.device, keys, keysPerLocalSort * sizeof(uint32_t), 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descSet);
    VkUtil::updateDescriptorSet(_vkContext.device, uniformBuffer, uinformBufferSize * sizeof(uint32_t), 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descSet);

    std::vector<uint32_t> cpuKeys(keysPerLocalSort);
    for(auto& n: cpuKeys) {int r = rand() & 0xff; n = *reinterpret_cast<uint32_t*>(&r);}
    std::vector<uint32_t> orig = cpuKeys;
    std::vector<uint32_t> c = cpuKeys;
    std::vector<uint32_t> cpuUniform{1};    // pass number 1 to save in front buffer
    VkUtil::uploadData(_vkContext.device, memory, 0, cpuUniform.size() * sizeof(cpuUniform[0]), cpuUniform.data());
    VkCommandBuffer commands;
    VkUtil::createCommandBuffer(_vkContext.device, _vkContext.commandPool, &commands);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, _localSortPipeline.pipelineLayout, 0, 1, &descSet, 0, nullptr);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, _localSortPipeline.pipeline);
    vkCmdDispatch(commands, 1, 1, 1);
    auto t1 = std::chrono::high_resolution_clock::now();
    VkUtil::uploadData(_vkContext.device, memory, uinformBufferSize * sizeof(uint32_t), cpuKeys.size() * sizeof(cpuKeys[0]), cpuKeys.data());
    VkUtil::commitCommandBuffer(_vkContext.queue, commands);
    VkResult res = vkQueueWaitIdle(_vkContext.queue); check_vk_result(res);
    VkUtil::downloadData(_vkContext.device, memory, uinformBufferSize * sizeof(uint32_t), cpuKeys.size() * sizeof(cpuKeys[0]), cpuKeys.data());
    auto t2 = std::chrono::high_resolution_clock::now();

    //test sorting on cpu
    auto t3 = std::chrono::high_resolution_clock::now();
    std::sort(c.begin(), c.end());
    auto t4 = std::chrono::high_resolution_clock::now();
    std::cout << "GPU: " << std::chrono::duration<double, std::milli>(t2 - t1).count() << std::endl;
    std::cout << "CPU: " << std::chrono::duration<double, std::milli>(t4 - t2).count() << std::endl;
    //check for number loss
    for(uint32_t n: orig){
        if(std::find(cpuKeys.begin(), cpuKeys.end(), n) == cpuKeys.end()){
            std::cout << "Lost " << n << std::endl;
        }
    }
    bool doubled = false;
    for(int i = 0; i < cpuKeys.size(); ++i){
        if(c[i] != cpuKeys[i]){
            std::cout << "[" << i << "]:" << c[i] << " - " << cpuKeys[i] << std::endl;
            correct = false;
        }
        if (i != 0 && c[i] == c[i - 1]) doubled = true;
    }

    vkDestroyBuffer(_vkContext.device, uniformBuffer, nullptr);
    vkDestroyBuffer(_vkContext.device, keys, nullptr);
    vkFreeMemory(_vkContext.device, memory, nullptr);

    return correct;
}

bool GpuRadixSorter::checkHistogram(){
    bool correct = true;
    const uint32_t threadsPerBlock = 384, keysPerThread = 18;
    const uint32_t uinformBufferSize = 320;
    const uint32_t keysSize = threadsPerBlock * keysPerThread;
    const uint32_t numBuckets = 256;
    const uint32_t numWorkGroups = threadsPerBlock / 32;
    const uint32_t pass = 3;

    // Buffer creation and filling -------------------------------------------------------------------------------------
    VkBuffer uniform, keysFront, keysBack, groupInfos;
    VkDeviceMemory memory;
    VkUtil::createBuffer(_vkContext.device, keysSize * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &keysFront);
    VkUtil::createBuffer(_vkContext.device, keysSize * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &keysBack);
    VkUtil::createBuffer(_vkContext.device, uinformBufferSize * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &uniform);
    VkUtil::createBuffer(_vkContext.device, numBuckets * numWorkGroups * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &groupInfos);
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkMemoryRequirements memReq;
    uint32_t memBits{};
    vkGetBufferMemoryRequirements(_vkContext.device, keysFront, &memReq);
    allocInfo.allocationSize = memReq.size;
    memBits |= memReq.memoryTypeBits;
    vkGetBufferMemoryRequirements(_vkContext.device, keysBack, &memReq);
    allocInfo.allocationSize += memReq.size;
    memBits |= memReq.memoryTypeBits;
    vkGetBufferMemoryRequirements(_vkContext.device, uniform, &memReq);
    allocInfo.allocationSize += memReq.size;
    memBits |= memReq.memoryTypeBits;
    vkGetBufferMemoryRequirements(_vkContext.device, groupInfos, &memReq);
    allocInfo.allocationSize += memReq.size;
    memBits |= memReq.memoryTypeBits;
    allocInfo.memoryTypeIndex = VkUtil::findMemoryType(_vkContext.physicalDevice, memBits, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    vkAllocateMemory(_vkContext.device, &allocInfo, nullptr, &memory);
    vkBindBufferMemory(_vkContext.device, uniform, memory, 0);
    vkBindBufferMemory(_vkContext.device, keysFront, memory, uinformBufferSize * sizeof(uint32_t));
    vkBindBufferMemory(_vkContext.device, keysBack, memory, uinformBufferSize * sizeof(uint32_t) + keysSize * sizeof(uint32_t));
    vkBindBufferMemory(_vkContext.device, groupInfos, memory, uinformBufferSize * sizeof(uint32_t) + 2 * keysSize * sizeof(uint32_t));
    std::vector<uint32_t> cpuKeys(keysSize);
    for(auto& n: cpuKeys) {int r = rand() & 0xff; n = *reinterpret_cast<uint32_t*>(&r);}
    VkUtil::uploadData(_vkContext.device, memory, uinformBufferSize * sizeof(uint32_t) + keysSize * sizeof(uint32_t), cpuKeys.size() * sizeof(cpuKeys[0]), cpuKeys.data());
    std::vector<uint32_t> cpuUniform(uinformBufferSize, 0);
    cpuUniform[0] = pass;
    cpuUniform[1] = 1; //1 global histogram
    VkUtil::uploadData(_vkContext.device, memory, 0, cpuUniform.size() * sizeof(cpuUniform[0]), cpuUniform.data());

    // Descriptorset setup filling -------------------------------------------------------------------------------------
    VkDescriptorSet descSet;
    VkUtil::createDescriptorSets(_vkContext.device, {_localSortPipeline.descriptorSetLayout}, _vkContext.descriptorPool, &descSet);
    VkUtil::updateDescriptorSet(_vkContext.device, keysFront, keysSize * sizeof(uint32_t), 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descSet);
    VkUtil::updateArrayDescriptorSet(_vkContext.device, keysBack, keysSize * sizeof(uint32_t), 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descSet);
    VkUtil::updateDescriptorSet(_vkContext.device, uniform, uinformBufferSize * sizeof(uint32_t), 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descSet);
    VkUtil::updateDescriptorSet(_vkContext.device, groupInfos, numBuckets * numWorkGroups * sizeof(uint32_t), 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descSet);

    // Creating and submitting commands -------------------------------------------------------------------------------------
    VkCommandBuffer commands;
    VkUtil::createCommandBuffer(_vkContext.device, _vkContext.commandPool, &commands);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, _localSortPipeline.pipelineLayout, 0, 1, &descSet, 0, nullptr);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, _histogramPipeline.pipeline);
    vkCmdDispatch(commands, 1, 1, 1);
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, {}, 0, {}, 0, {});
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, _globalScanPipeline.pipeline);
    vkCmdDispatch(commands, 1 + 1, 1, 1);   // 1 local work group has to be reduced as well
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, {}, 0, {}, 0, {});
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, _scatterPipeline.pipeline);
    vkCmdDispatch(commands, 1, 1, 1);
    auto t1 = std::chrono::high_resolution_clock::now();
    VkUtil::commitCommandBuffer(_vkContext.queue, commands);
    VkResult res = vkQueueWaitIdle(_vkContext.queue); check_vk_result(res);
    auto t2 = std::chrono::high_resolution_clock::now();
    VkUtil::downloadData(_vkContext.device, memory, 0 ,uinformBufferSize * sizeof(uint32_t), cpuUniform.data());
    std::vector<uint32_t> sortedGpu(keysSize);
    VkUtil::downloadData(_vkContext.device, memory, (uinformBufferSize) * sizeof(uint32_t), keysSize * sizeof(uint32_t), sortedGpu.data());


    //checking the results --------------------------------------------------------------------------------------------
    std::vector<uint32_t> hist(256);
    std::vector<uint32_t> prefix(256, 0);
    std::vector<uint32_t> sorted(keysSize);
    auto getBin = [](uint32_t n, uint32_t pass){return (n >> ((3 - pass) * 8)) & 0xff;};
    uint32_t bin = getBin(255, 3);
    for(auto n: cpuKeys) hist[getBin(n, pass)]++;
    uint cpuBinCount = 0, gpuBinCount = 0;
    if(false){  // histogram computation
        for(int i = 0; i < hist.size(); ++i){
            cpuBinCount += hist[i];
            gpuBinCount += cpuUniform[i + 1];
        }
        for(int i = 0; i < hist.size(); ++i){
            if(hist[i] != cpuUniform[1 + i]){
                std::cout << "Miss: CPU " << hist[i] << " | GPU " << cpuUniform[1 + i] << std::endl;
                correct = false;
            }
        }
    }
    if(false){   // exclusive summation
        for(int i = 1; i < hist.size(); ++i){
            prefix[i] = prefix[i - 1] + hist[i - 1];
        }
        for(int i = 0; i < prefix.size(); ++i){
            if(prefix[i] != cpuUniform[1 + i]){
                std::cout << "Miss: CPU " << prefix[i] << " | GPU " << cpuUniform[1 + i] << std::endl;
                correct = false;
            }
        }
    }
    if(true){   // key scattering
        for(int i = 1; i < hist.size(); ++i){
            prefix[i] = prefix[i - 1] + hist[i - 1];
        }
        for(auto n: cpuKeys){
            sorted[prefix[getBin(n, pass)]++] = n;
        }
        for(int i = 0; i < sorted.size(); ++i){
            if(sorted[i] != sortedGpu[i]){
                std::cout << "Miss: CPU " << sorted[i] << " | GPU " << sortedGpu[i] << std::endl;
                correct = false;
            }
        }
    }

    
    std::cout << "GPU: " << std::chrono::duration<double, std::milli>(t2 - t1).count() << std::endl;

    // Freeing resources -------------------------------------------------------------------------------------
    vkDestroyBuffer(_vkContext.device, uniform, nullptr);
    vkDestroyBuffer(_vkContext.device, keysFront, nullptr);
    vkDestroyBuffer(_vkContext.device, keysBack, nullptr);
    vkFreeMemory(_vkContext.device, memory, nullptr);

    return correct;
}