#pragma once

#include <vulkan/vulkan.h>
#include <string.h>
#include <vector>
#include <map>
#include <set>

#include "VkUtil.h"
#include "PCUtil.h"
#include "MultivariateGauss.h"
#include "LassoBrush.hpp"

#define BRUSHERLOCALSIZE 256
#define SHADERPATHCOMP "shader/brushComp.spv"
#define SHADERPATHFRACTURE "shader/brushFractureComp.spv"
#define SHADERPATHMULTIVARIATE "shader/brushMultvarComp.spv"
#define SHADERPATHLASSO "shader/brushLasso.spv"

class GpuBrusher {
private:
    struct BrushInfo {
        uint32_t axis;
        uint32_t brushArrayOffset;
        uint32_t amtOfBrushes;
        uint32_t padding;
    };

    struct UBOinfo {
        uint32_t amtOfAttributes;
        uint32_t amtOfBrushAxes;
        uint32_t amtOfIndices;
        uint32_t lineCount;                    //count for active lines would only one brush be applied
        int globalLineCount;                //count for actually active lines, if -1 this should not be counted
        uint32_t first;
        uint32_t andOr;
        uint32_t padding;
        uint32_t* indicesOffsets;            //index of brush axis, offset in brushes array and amount of brushes: ind1 offesetBrushes1 amtOfBrushes1 PADDING ind2 offsetBrushes2 amtOfBrushes2... 
    };

    struct UBOFractureInfo {                //brush informations for the fractures(nearly the same as the infos for standard brushing)
        uint32_t amtOfAttributes;
        uint32_t amtOfFractureAxes;
        uint32_t amtOfFractures;
        uint32_t amtOfIndices;
        uint32_t lineCount;
        int globalLineCount;
        uint32_t first;
        uint32_t andOr;
        //[i1,i2,i3,...]                    Array containing the indices of the fracture axis
    };

    struct UBObrushes {
        uint32_t* brushes;            //min max PADDING PADDING min max PADDING PADDING min....
    };

    struct UBOdata {
        float* data;                //data array is without padding!
    };

    struct UBOindices {
        uint32_t amtOfIndices;
        uint32_t* indices;            //indices which should be brushed
    };

    struct UBOstorage {
        uint32_t counter;
        uint32_t* indices;            //indices remaining after brush
    };

    //stored vulkan resources to create temporary gpu resources
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VkCommandPool commandPool;
    VkQueue queue;
    VkDescriptorPool descriptorPool;

    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    VkDescriptorSetLayout descriptorSetLayout;
    VkPipeline fracturePipeline;
    VkPipelineLayout fracturePipelineLayout;
    VkPipeline multivariatePipeline;
    VkPipelineLayout multivariatePipelineLayout;
    VkPipeline lassoPipeline;
    VkPipelineLayout lassoPipelineLayout;
    VkBuffer uboBuffers[2];
    uint32_t uboOffsets[2];
    VkDeviceMemory uboMemory;
public:
    GpuBrusher(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue, VkDescriptorPool descriptorPool) : device(device), physicalDevice(physicalDevice), commandPool(commandPool), queue(queue), descriptorPool(descriptorPool) {
        VkResult err;

        VkShaderModule module = VkUtil::createShaderModule(device, PCUtil::readByteFile(std::string(SHADERPATHCOMP)));

        std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = 0;
        binding.descriptorCount = 1;        //informations
        binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        layoutBindings.push_back(binding);

        binding.binding = 1;                //brush ranges
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        layoutBindings.push_back(binding);

        binding.binding = 2;                //data buffer
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        layoutBindings.push_back(binding);

        binding.binding = 3;                //input indices
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        layoutBindings.push_back(binding);

        binding.binding = 4;                //output active indices
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        layoutBindings.push_back(binding);

        VkUtil::createDescriptorSetLayout(device, layoutBindings, &descriptorSetLayout);

        std::vector<VkDescriptorSetLayout> layouts;
        layouts.push_back(descriptorSetLayout);
        VkUtil::createComputePipeline(device, module, layouts, &pipelineLayout, &pipeline);

        module = VkUtil::createShaderModule(device, PCUtil::readByteFile(std::string(SHADERPATHFRACTURE)));
        VkUtil::createComputePipeline(device, module, layouts, &fracturePipelineLayout, &fracturePipeline);

        module = VkUtil::createShaderModule(device, PCUtil::readByteFile(std::string(SHADERPATHMULTIVARIATE)));
        VkUtil::createComputePipeline(device, module, layouts, &multivariatePipelineLayout, &multivariatePipeline);

        module = VkUtil::createShaderModule(device, PCUtil::readByteFile(std::string(SHADERPATHLASSO)));
        VkUtil::createComputePipeline(device, module, layouts, &lassoPipelineLayout, &lassoPipeline);
    };

    ~GpuBrusher() {
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        vkDestroyPipeline(device, fracturePipeline, nullptr);
        vkDestroyPipelineLayout(device, fracturePipelineLayout, nullptr);
        vkDestroyPipeline(device, multivariatePipeline, nullptr);
        vkDestroyPipelineLayout(device, multivariatePipelineLayout, nullptr);
        vkDestroyPipeline(device, lassoPipeline, nullptr);
        vkDestroyPipelineLayout(device, lassoPipelineLayout, nullptr);
    };

    struct ReturnStruct{
        uint32_t singleBrushActiveLines;
        int activeLines;
    };

    //returns a pair containing the number of lines which would be active, would only one brush be applied, and the number of lines that really are still active
    ReturnStruct brushIndices(std::map<int, std::vector<std::pair<float, float>>>& brushes, uint32_t dataSize, VkBuffer data, VkBuffer indices, uint32_t indicesSize, VkBufferView activeIndices, uint32_t amtOfAttributes, bool first, bool andy, bool lastBrush) {
        //allocating all ubos and collection iformation about amount of brushes etc.
        uint32_t infoBytesSize = sizeof(UBOinfo) - sizeof(uint32_t) + sizeof(uint32_t) * 4 * brushes.size();
        UBOinfo* informations = (UBOinfo*)malloc(infoBytesSize);
        
        std::vector<BrushInfo> brushInfos;
        uint32_t off = 0;
        for (auto axis : brushes) {
            brushInfos.push_back({ (uint32_t)axis.first,off,(uint32_t)axis.second.size() });
            off += axis.second.size();
        }
        uint32_t brushesByteSize = sizeof(uint32_t) * 4 * off;
        UBObrushes* gpuBrushes = (UBObrushes*)malloc(brushesByteSize);

        //allocating buffers and memory for ubos
        uboOffsets[0] = 0;
        VkUtil::createBuffer(device, infoBytesSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &uboBuffers[0]);
        VkUtil::createBuffer(device, brushesByteSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &uboBuffers[1]);

        VkResult err;
        VkMemoryRequirements memReq;
        uint32_t memTypeBits;

        uboOffsets[0] = 0;
        vkGetBufferMemoryRequirements(device, uboBuffers[0], &memReq);
        VkMemoryAllocateInfo memalloc = {};
        memalloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memalloc.allocationSize = memReq.size;
        memTypeBits = memReq.memoryTypeBits;

        uboOffsets[1] = memalloc.allocationSize;
        vkGetBufferMemoryRequirements(device, uboBuffers[1], &memReq);
        memalloc.allocationSize += memReq.size;
        memTypeBits |= memReq.memoryTypeBits;

        memalloc.memoryTypeIndex = VkUtil::findMemoryType(physicalDevice, memTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        err = vkAllocateMemory(device, &memalloc, nullptr, &uboMemory);
        check_vk_result(err);

        //binding buffers to memory
        vkBindBufferMemory(device, uboBuffers[0], uboMemory, uboOffsets[0]);
        vkBindBufferMemory(device, uboBuffers[1], uboMemory, uboOffsets[1]);

        //creating the descriptor set and binding all buffers to the corrsponding bindings
        VkDescriptorSet descriptorSet;
        std::vector<VkDescriptorSetLayout> layouts;
        layouts.push_back(descriptorSetLayout);
        VkUtil::createDescriptorSets(device, layouts, descriptorPool, &descriptorSet);

        VkUtil::updateDescriptorSet(device, uboBuffers[0], infoBytesSize, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet);
        VkUtil::updateDescriptorSet(device, uboBuffers[1], brushesByteSize, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet);
        VkUtil::updateDescriptorSet(device, data, VK_WHOLE_SIZE, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet);
        VkUtil::updateDescriptorSet(device, indices, indicesSize * sizeof(uint32_t), 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet);
        VkUtil::updateTexelBufferDescriptorSet(device, activeIndices, 4, descriptorSet);

        //uploading data for brushing
        void* d;
        informations->amtOfBrushAxes = brushes.size();
        informations->amtOfAttributes = amtOfAttributes;
        informations->amtOfIndices = indicesSize;
        informations->lineCount = 0;
        informations->globalLineCount = (lastBrush) ? 0 : -1;
        informations->first = first;
        informations->andOr = andy;
        uint32_t* point = (uint32_t*)informations +((sizeof(UBOinfo)-sizeof(uint32_t))/sizeof(uint32_t)) - 1;        //strangeley the size of UBOinfo is too big
        uint32_t offset = 0;
        for (BrushInfo bi : brushInfos) {
            memcpy(point + offset, &bi, sizeof(uint32_t) * 4);
            offset +=  4;
        }
//#ifdef _DEBUG
//        std::cout << "Brush informations:" << std::endl;
//        std::cout << "amtOfBrushAxes" << informations->amtOfBrushAxes << std::endl;
//        std::cout << "amtOfAttributes" << informations->amtOfAttributes << std::endl;
//        std::cout << "amtOfIndices" << informations->amtOfIndices << std::endl;
//        std::cout << "lineCount" << informations->lineCount << std::endl;
//        std::cout << "first" << informations->first << std::endl;
//        std::cout << "andOr" << informations->andOr << std::endl;
//        for (int i = 0; i < brushInfos.size(); ++i) {
//            std::cout << point[i * 4] << " " << point[i * 4 + 1] << " " << point[i * 4 + 2] << " " << point[i * 4 + 3] << std::endl;
//        }
//#endif
        vkMapMemory(device, uboMemory, uboOffsets[0], infoBytesSize, 0, &d);
        memcpy(d, informations, infoBytesSize);
        vkUnmapMemory(device, uboMemory);

        offset = 0;
        float* bru = (float*)gpuBrushes;
        for (auto& axis : brushes) {
            for (auto& range : axis.second) {
                bru[offset++] = range.first;
                bru[offset++] = range.second;
                offset += 2;
            }
        }
        vkMapMemory(device, uboMemory, uboOffsets[1], brushesByteSize, 0, &d);
        memcpy(d, bru, brushesByteSize);
        vkUnmapMemory(device, uboMemory);

        //dispatching the command buffer to calculate active indices
        VkCommandBuffer command;
        VkUtil::createCommandBuffer(device, commandPool, &command);

        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, {});
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        int patchAmount = indicesSize / BRUSHERLOCALSIZE;
        patchAmount += (indicesSize % BRUSHERLOCALSIZE) ? 1 : 0;
        vkCmdDispatch(command, patchAmount, 1, 1);
        VkUtil::commitCommandBuffer(queue, command);
        err = vkQueueWaitIdle(queue);
        check_vk_result(err);

        //getting the amount of remaining lines(if only this brush would have been applied)
        VkUtil::downloadData(device, uboMemory, 0, sizeof(UBOinfo), informations);
        ReturnStruct res{informations->lineCount,informations->globalLineCount};
        
        vkFreeCommandBuffers(device, commandPool, 1, &command);
        vkFreeDescriptorSets(device, descriptorPool, 1, &descriptorSet);
        vkFreeMemory(device, uboMemory, nullptr);
        vkDestroyBuffer(device, uboBuffers[0], nullptr);
        vkDestroyBuffer(device, uboBuffers[1], nullptr);

        free(informations);
        free(gpuBrushes);

        return res;
    };

    //returns a pair containing the number of lines which would be active, would only one brush be applied, and the number of lines that really are still active
    //this pipeline does not change the contents of the active indices bool array
    ReturnStruct brushIndices(std::map<int, std::vector<std::pair<float, float>>>& brushes, uint32_t dataSize, VkBuffer data, VkBuffer indices, uint32_t indicesSize, VkBufferView activeIndices, uint32_t amtOfAttributes) {
        //allocating all ubos and collection information about amount of brushes etc.
        uint32_t infoBytesSize = sizeof(UBOinfo) - sizeof(uint32_t) + sizeof(uint32_t) * 4 * brushes.size();
        UBOinfo* informations = (UBOinfo*)malloc(infoBytesSize);

        std::vector<BrushInfo> brushInfos;
        uint32_t off = 0;
        for (auto axis : brushes) {
            brushInfos.push_back({ (uint32_t)axis.first,off,(uint32_t)axis.second.size() });
            off += axis.second.size();
        }
        uint32_t brushesByteSize = sizeof(uint32_t) * 4 * off;
        UBObrushes* gpuBrushes = (UBObrushes*)malloc(brushesByteSize);

        //allocating buffers and memory for ubos
        uboOffsets[0] = 0;
        VkUtil::createBuffer(device, infoBytesSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &uboBuffers[0]);
        VkUtil::createBuffer(device, brushesByteSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &uboBuffers[1]);

        VkResult err;
        VkMemoryRequirements memReq;
        uint32_t memTypeBits;

        uboOffsets[0] = 0;
        vkGetBufferMemoryRequirements(device, uboBuffers[0], &memReq);
        VkMemoryAllocateInfo memalloc = {};
        memalloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memalloc.allocationSize = memReq.size;
        memTypeBits = memReq.memoryTypeBits;

        uboOffsets[1] = memalloc.allocationSize;
        vkGetBufferMemoryRequirements(device, uboBuffers[1], &memReq);
        memalloc.allocationSize += memReq.size;
        memTypeBits |= memReq.memoryTypeBits;

        memalloc.memoryTypeIndex = VkUtil::findMemoryType(physicalDevice, memTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        err = vkAllocateMemory(device, &memalloc, nullptr, &uboMemory);
        check_vk_result(err);

        //binding buffers to memory
        vkBindBufferMemory(device, uboBuffers[0], uboMemory, uboOffsets[0]);
        vkBindBufferMemory(device, uboBuffers[1], uboMemory, uboOffsets[1]);

        //creating the descriptor set and binding all buffers to the corrsponding bindings
        VkDescriptorSet descriptorSet;
        std::vector<VkDescriptorSetLayout> layouts;
        layouts.push_back(descriptorSetLayout);
        VkUtil::createDescriptorSets(device, layouts, descriptorPool, &descriptorSet);

        VkUtil::updateDescriptorSet(device, uboBuffers[0], infoBytesSize, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet);
        VkUtil::updateDescriptorSet(device, uboBuffers[1], brushesByteSize, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet);
        VkUtil::updateDescriptorSet(device, data, VK_WHOLE_SIZE, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet);
        VkUtil::updateDescriptorSet(device, indices, indicesSize * sizeof(uint32_t), 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet);
        VkUtil::updateTexelBufferDescriptorSet(device, activeIndices, 4, descriptorSet);

        //uploading data for brushing
        void* d;
        informations->amtOfBrushAxes = brushes.size();
        informations->amtOfAttributes = amtOfAttributes;
        informations->amtOfIndices = indicesSize;
        informations->lineCount = 0;
        informations->globalLineCount = 0;
        informations->first = 2;        //special number ot detect no write
        informations->andOr = true;
        uint32_t* point = (uint32_t*)informations + ((sizeof(UBOinfo) - sizeof(uint32_t)) / sizeof(uint32_t)) - 1;        //strangeley the size of UBOinfo is too big
        uint32_t offset = 0;
        for (BrushInfo bi : brushInfos) {
            memcpy(point + offset, &bi, sizeof(uint32_t) * 4);
            offset += 4;
        }
        //#ifdef _DEBUG
        //        std::cout << "Brush informations:" << std::endl;
        //        std::cout << "amtOfBrushAxes" << informations->amtOfBrushAxes << std::endl;
        //        std::cout << "amtOfAttributes" << informations->amtOfAttributes << std::endl;
        //        std::cout << "amtOfIndices" << informations->amtOfIndices << std::endl;
        //        std::cout << "lineCount" << informations->lineCount << std::endl;
        //        std::cout << "first" << informations->first << std::endl;
        //        std::cout << "andOr" << informations->andOr << std::endl;
        //        for (int i = 0; i < brushInfos.size(); ++i) {
        //            std::cout << point[i * 4] << " " << point[i * 4 + 1] << " " << point[i * 4 + 2] << " " << point[i * 4 + 3] << std::endl;
        //        }
        //#endif
        vkMapMemory(device, uboMemory, uboOffsets[0], infoBytesSize, 0, &d);
        memcpy(d, informations, infoBytesSize);
        vkUnmapMemory(device, uboMemory);

        offset = 0;
        float* bru = (float*)gpuBrushes;
        for (auto& axis : brushes) {
            for (auto& range : axis.second) {
                bru[offset++] = range.first;
                bru[offset++] = range.second;
                offset += 2;
            }
        }
        vkMapMemory(device, uboMemory, uboOffsets[1], brushesByteSize, 0, &d);
        memcpy(d, bru, brushesByteSize);
        vkUnmapMemory(device, uboMemory);

        //dispatching the command buffer to calculate active indices
        VkCommandBuffer command;
        VkUtil::createCommandBuffer(device, commandPool, &command);

        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, {});
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        int patchAmount = indicesSize / BRUSHERLOCALSIZE;
        patchAmount += (indicesSize % BRUSHERLOCALSIZE) ? 1 : 0;
        vkCmdDispatch(command, patchAmount, 1, 1);
        VkUtil::commitCommandBuffer(queue, command);
        err = vkQueueWaitIdle(queue);
        check_vk_result(err);

        //getting the amount of remaining lines(if only this brush would have been applied)
        VkUtil::downloadData(device, uboMemory, 0, sizeof(UBOinfo), informations);
        ReturnStruct res{informations->lineCount, informations->globalLineCount};

        vkFreeCommandBuffers(device, commandPool, 1, &command);
        vkFreeDescriptorSets(device, descriptorPool, 1, &descriptorSet);
        vkFreeMemory(device, uboMemory, nullptr);
        vkDestroyBuffer(device, uboBuffers[0], nullptr);
        vkDestroyBuffer(device, uboBuffers[1], nullptr);

        free(informations);
        free(gpuBrushes);

        return res;
    };

    //returns a pair containing the number of lines which would be active, would only these fractures be applied, and the number of lines that really are still active
    ReturnStruct brushIndices(std::vector<std::vector<std::pair<float, float>>>& fractures, std::vector<int>& attributes, uint32_t dataSize, VkBuffer data, VkBuffer indices, uint32_t indicesSize, VkBufferView activeIndices, uint32_t amtOfAttributes, bool first, bool andy, bool lastBrush) {
        //allocating all ubos and collection iformation about amount of brushes etc.
        uint32_t infoBytesSize = sizeof(UBOFractureInfo) + attributes.size() * sizeof(uint32_t);
        UBOFractureInfo* informations = (UBOFractureInfo*)malloc(infoBytesSize);

        uint32_t fracturesByteSize = attributes.size() * fractures.size() * 2 * sizeof(float);
        UBObrushes* gpuFractures = (UBObrushes*)malloc(fracturesByteSize);

        //allocating buffers and memory for ubos
        uboOffsets[0] = 0;
        VkUtil::createBuffer(device, infoBytesSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &uboBuffers[0]);
        VkUtil::createBuffer(device, fracturesByteSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &uboBuffers[1]);

        VkResult err;
        VkMemoryRequirements memReq;
        uint32_t memTypeBits;

        uboOffsets[0] = 0;
        vkGetBufferMemoryRequirements(device, uboBuffers[0], &memReq);
        VkMemoryAllocateInfo memalloc = {};
        memalloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memalloc.allocationSize = memReq.size;
        memTypeBits = memReq.memoryTypeBits;

        uboOffsets[1] = memalloc.allocationSize;
        vkGetBufferMemoryRequirements(device, uboBuffers[1], &memReq);
        memalloc.allocationSize += memReq.size;
        memTypeBits |= memReq.memoryTypeBits;

        memalloc.memoryTypeIndex = VkUtil::findMemoryType(physicalDevice, memTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        err = vkAllocateMemory(device, &memalloc, nullptr, &uboMemory);
        check_vk_result(err);

        //binding buffers to memory
        vkBindBufferMemory(device, uboBuffers[0], uboMemory, uboOffsets[0]);
        vkBindBufferMemory(device, uboBuffers[1], uboMemory, uboOffsets[1]);

        //creating the descriptor set and binding all buffers to the corrsponding bindings
        VkDescriptorSet descriptorSet;
        std::vector<VkDescriptorSetLayout> layouts;
        layouts.push_back(descriptorSetLayout);
        VkUtil::createDescriptorSets(device, layouts, descriptorPool, &descriptorSet);

        VkUtil::updateDescriptorSet(device, uboBuffers[0], infoBytesSize, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet);
        VkUtil::updateDescriptorSet(device, uboBuffers[1], fracturesByteSize, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet);
        VkUtil::updateDescriptorSet(device, data, VK_WHOLE_SIZE, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet);
        VkUtil::updateDescriptorSet(device, indices, indicesSize * sizeof(uint32_t), 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet);
        VkUtil::updateTexelBufferDescriptorSet(device, activeIndices, 4, descriptorSet);

        //uploading data for brushing
        void* d;
        informations->amtOfFractureAxes = attributes.size();
        informations->amtOfFractures = fractures.size();
        informations->amtOfAttributes = amtOfAttributes;
        informations->amtOfIndices = indicesSize;
        informations->lineCount = 0;
        informations->globalLineCount = (lastBrush) ? 0 : -1;
        informations->first = first;
        informations->andOr = andy;
        uint32_t* inf = (uint32_t*)(informations + 1);
        for (int i = 0; i < attributes.size(); ++i) {
            inf[i] = attributes[i];
        }
        //#ifdef _DEBUG
        //        std::cout << "Brush informations:" << std::endl;
        //        std::cout << "amtOfBrushAxes" << informations->amtOfBrushAxes << std::endl;
        //        std::cout << "amtOfAttributes" << informations->amtOfAttributes << std::endl;
        //        std::cout << "amtOfIndices" << informations->amtOfIndices << std::endl;
        //        std::cout << "lineCount" << informations->lineCount << std::endl;
        //        std::cout << "first" << informations->first << std::endl;
        //        std::cout << "andOr" << informations->andOr << std::endl;
        //        for (int i = 0; i < brushInfos.size(); ++i) {
        //            std::cout << point[i * 4] << " " << point[i * 4 + 1] << " " << point[i * 4 + 2] << " " << point[i * 4 + 3] << std::endl;
        //        }
        //#endif
        vkMapMemory(device, uboMemory, uboOffsets[0], infoBytesSize, 0, &d);
        memcpy(d, informations, infoBytesSize);
        vkUnmapMemory(device, uboMemory);

        int offset = 0;
        float* bru = (float*)gpuFractures;
        for (auto& range : fractures) {
            for (int axis = 0; axis < attributes.size(); ++axis) {
                bru[offset++] = range[axis].first;
                bru[offset++] = range[axis].second;
            }
        }
        vkMapMemory(device, uboMemory, uboOffsets[1], fracturesByteSize, 0, &d);
        memcpy(d, bru, fracturesByteSize);
        vkUnmapMemory(device, uboMemory);

        //dispatching the command buffer to calculate active indices
        VkCommandBuffer command;
        VkUtil::createCommandBuffer(device, commandPool, &command);

        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, fracturePipelineLayout, 0, 1, &descriptorSet, 0, {});
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, fracturePipeline);
        int patchAmount = indicesSize / BRUSHERLOCALSIZE;
        patchAmount += (indicesSize % BRUSHERLOCALSIZE) ? 1 : 0;
        vkCmdDispatch(command, patchAmount, 1, 1);
        VkUtil::commitCommandBuffer(queue, command);
        err = vkQueueWaitIdle(queue);
        check_vk_result(err);

        //getting the amount of remaining lines(if only this brush would have been applied)
        VkUtil::downloadData(device, uboMemory, 0, sizeof(UBOFractureInfo), informations);
        ReturnStruct res{informations->lineCount, informations->globalLineCount};

        vkFreeCommandBuffers(device, commandPool, 1, &command);
        vkFreeDescriptorSets(device, descriptorPool, 1, &descriptorSet);
        vkFreeMemory(device, uboMemory, nullptr);
        vkDestroyBuffer(device, uboBuffers[0], nullptr);
        vkDestroyBuffer(device, uboBuffers[1], nullptr);

        free(informations);
        free(gpuFractures);

        return res;
    };

    //returns a pair containing the number of lines which would be active, would only these fractures be applied, and the number of lines that really are still active
    ReturnStruct brushIndices(std::vector<MultivariateGauss::MultivariateBrush>& fractures, std::vector<std::pair<float,float>>& bounds, std::vector<int>& attributes, uint32_t dataSize, VkBuffer data, VkBuffer indices, uint32_t indicesSize, VkBufferView activeIndices, uint32_t amtOfAttributes, bool first, bool andy, bool lastBrush, float stdDev) {
        //allocating all ubos and collection iformation about amount of brushes etc.
        uint32_t infoBytesSize = sizeof(UBOFractureInfo) + (attributes.size() * 3 + 1) * sizeof(float);
        UBOFractureInfo* informations = (UBOFractureInfo*)malloc(infoBytesSize);

        uint32_t fracturesByteSize = fractures.size() * (4 * attributes.size() + attributes.size() * attributes.size()) * sizeof(float);
        UBObrushes* gpuFractures = (UBObrushes*)malloc(fracturesByteSize);

        //allocating buffers and memory for ubos
        uboOffsets[0] = 0;
        VkUtil::createBuffer(device, infoBytesSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &uboBuffers[0]);
        VkUtil::createBuffer(device, fracturesByteSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &uboBuffers[1]);

        VkResult err;
        VkMemoryRequirements memReq;
        uint32_t memTypeBits;

        uboOffsets[0] = 0;
        vkGetBufferMemoryRequirements(device, uboBuffers[0], &memReq);
        VkMemoryAllocateInfo memalloc = {};
        memalloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memalloc.allocationSize = memReq.size;
        memTypeBits = memReq.memoryTypeBits;

        uboOffsets[1] = memalloc.allocationSize;
        vkGetBufferMemoryRequirements(device, uboBuffers[1], &memReq);
        memalloc.allocationSize += memReq.size;
        memTypeBits |= memReq.memoryTypeBits;

        memalloc.memoryTypeIndex = VkUtil::findMemoryType(physicalDevice, memTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        err = vkAllocateMemory(device, &memalloc, nullptr, &uboMemory);
        check_vk_result(err);

        //binding buffers to memory
        vkBindBufferMemory(device, uboBuffers[0], uboMemory, uboOffsets[0]);
        vkBindBufferMemory(device, uboBuffers[1], uboMemory, uboOffsets[1]);

        //creating the descriptor set and binding all buffers to the corrsponding bindings
        VkDescriptorSet descriptorSet;
        std::vector<VkDescriptorSetLayout> layouts;
        layouts.push_back(descriptorSetLayout);
        VkUtil::createDescriptorSets(device, layouts, descriptorPool, &descriptorSet);

        VkUtil::updateDescriptorSet(device, uboBuffers[0], infoBytesSize, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet);
        VkUtil::updateDescriptorSet(device, uboBuffers[1], fracturesByteSize, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet);
        VkUtil::updateDescriptorSet(device, data, VK_WHOLE_SIZE, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet);
        VkUtil::updateDescriptorSet(device, indices, indicesSize * sizeof(uint32_t), 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet);
        VkUtil::updateTexelBufferDescriptorSet(device, activeIndices, 4, descriptorSet);

        //uploading data for brushing
        void* d;
        informations->amtOfAttributes = amtOfAttributes;
        informations->amtOfFractureAxes = attributes.size();
        informations->amtOfFractures = fractures.size();
        informations->amtOfIndices = indicesSize;
        informations->lineCount = 0;
        informations->globalLineCount = (lastBrush) ? 0 : -1;
        informations->first = first;
        informations->andOr = andy;
        float* inf = (float*)(informations + 1);
        *inf++ = stdDev;
        for (int i = 0; i < attributes.size(); ++i) {
            inf[i * 3] = attributes[i];
            inf[i * 3 + 1] = bounds[i].first;
            inf[i * 3 + 2] = bounds[i].second;
        }
        //#ifdef _DEBUG
        //        std::cout << "Brush informations:" << std::endl;
        //        std::cout << "amtOfBrushAxes" << informations->amtOfBrushAxes << std::endl;
        //        std::cout << "amtOfAttributes" << informations->amtOfAttributes << std::endl;
        //        std::cout << "amtOfIndices" << informations->amtOfIndices << std::endl;
        //        std::cout << "lineCount" << informations->lineCount << std::endl;
        //        std::cout << "first" << informations->first << std::endl;
        //        std::cout << "andOr" << informations->andOr << std::endl;
        //        for (int i = 0; i < brushInfos.size(); ++i) {
        //            std::cout << point[i * 4] << " " << point[i * 4 + 1] << " " << point[i * 4 + 2] << " " << point[i * 4 + 3] << std::endl;
        //        }
        //#endif
        vkMapMemory(device, uboMemory, uboOffsets[0], infoBytesSize, 0, &d);
        memcpy(d, informations, infoBytesSize);
        vkUnmapMemory(device, uboMemory);

        int offset = 0;
        float* bru = (float*)gpuFractures;
        for (auto& mult : fractures) {
            for (int i = 0; i < attributes.size(); ++i) bru[offset++] = mult.mean[i];
            for (int i = 0; i < attributes.size(); ++i) bru[offset++] = mult.sv(i);
            int miMaInd = -1;
            for (int i = 0; i < attributes.size(); ++i) { 
                if (mult.sv(i) > 1e-20) {
                    bru[offset++] = 0;
                    bru[offset++] = 0;
                }
                else {
                    bru[offset++] = mult.pcBounds[++miMaInd].first; 
                    bru[offset++] = mult.pcBounds[miMaInd].second; 
                }
            }
            assert(miMaInd == mult.pcBounds.size() - 1);
            for (int i = 0; i < attributes.size(); ++i)
                for (int j = 0; j < attributes.size(); ++j) bru[offset++] = mult.pc(j,i);        //transposing the matrix to allow for faster memory reads on the gpu
        }
        assert(offset == fracturesByteSize / sizeof(float));
        vkMapMemory(device, uboMemory, uboOffsets[1], fracturesByteSize, 0, &d);
        memcpy(d, bru, fracturesByteSize);
        vkUnmapMemory(device, uboMemory);

        //dispatching the command buffer to calculate active indices
        VkCommandBuffer command;
        VkUtil::createCommandBuffer(device, commandPool, &command);

        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, multivariatePipelineLayout, 0, 1, &descriptorSet, 0, {});
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, multivariatePipeline);
        int patchAmount = indicesSize / BRUSHERLOCALSIZE;
        patchAmount += (indicesSize % BRUSHERLOCALSIZE) ? 1 : 0;
        vkCmdDispatch(command, patchAmount, 1, 1);
        VkUtil::commitCommandBuffer(queue, command);
        err = vkQueueWaitIdle(queue);
        check_vk_result(err);

        //getting the amount of remaining lines(if only this brush would have been applied)
        VkUtil::downloadData(device, uboMemory, 0, sizeof(UBOFractureInfo), informations);
        ReturnStruct res{informations->lineCount, informations->globalLineCount};

        vkFreeCommandBuffers(device, commandPool, 1, &command);
        vkFreeDescriptorSets(device, descriptorPool, 1, &descriptorSet);
        vkFreeMemory(device, uboMemory, nullptr);
        vkDestroyBuffer(device, uboBuffers[0], nullptr);
        vkDestroyBuffer(device, uboBuffers[1], nullptr);

        free(informations);
        free(gpuFractures);

        return res;
    };

    //returns a pair containing the number of lines which would be active, would only these fractures be applied, and the number of lines that really are still active
    ReturnStruct brushIndices(const Polygons& lassos, uint32_t dataSize, VkBuffer data, VkBuffer indices, uint32_t indicesSize, VkBufferView activeIndices, uint32_t amtOfAttributes, bool first, bool andy, bool lastBrush) {
        //allocating all ubos and collection iformation about amount of brushes etc.
        uint32_t infoBytesSize = sizeof(UBOinfo);
        UBOinfo* informations = (UBOinfo*)malloc(infoBytesSize);

        uint32_t lassosByteSize = sizeof(uint32_t) + lassos.size() * (1 + 3) * sizeof(float);
        for(const auto& lasso: lassos) lassosByteSize += sizeof(float) * 2 * lasso.borderPoints.size();
        float* lassoBytes = (float*)malloc(lassosByteSize);

        //allocating buffers and memory for ubos
        uboOffsets[0] = 0;
        VkUtil::createBuffer(device, infoBytesSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &uboBuffers[0]);
        VkUtil::createBuffer(device, lassosByteSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &uboBuffers[1]);

        VkResult err;
        VkMemoryRequirements memReq;
        uint32_t memTypeBits;

        uboOffsets[0] = 0;
        vkGetBufferMemoryRequirements(device, uboBuffers[0], &memReq);
        VkMemoryAllocateInfo memalloc = {};
        memalloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memalloc.allocationSize = memReq.size;
        memTypeBits = memReq.memoryTypeBits;

        uboOffsets[1] = memalloc.allocationSize;
        vkGetBufferMemoryRequirements(device, uboBuffers[1], &memReq);
        memalloc.allocationSize += memReq.size;
        memTypeBits |= memReq.memoryTypeBits;

        memalloc.memoryTypeIndex = VkUtil::findMemoryType(physicalDevice, memTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        err = vkAllocateMemory(device, &memalloc, nullptr, &uboMemory);
        check_vk_result(err);

        //binding buffers to memory
        vkBindBufferMemory(device, uboBuffers[0], uboMemory, uboOffsets[0]);
        vkBindBufferMemory(device, uboBuffers[1], uboMemory, uboOffsets[1]);

        //creating the descriptor set and binding all buffers to the corrsponding bindings
        VkDescriptorSet descriptorSet;
        std::vector<VkDescriptorSetLayout> layouts;
        layouts.push_back(descriptorSetLayout);
        VkUtil::createDescriptorSets(device, layouts, descriptorPool, &descriptorSet);

        VkUtil::updateDescriptorSet(device, uboBuffers[0], infoBytesSize, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet);
        VkUtil::updateDescriptorSet(device, uboBuffers[1], lassosByteSize, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet);
        VkUtil::updateDescriptorSet(device, data, VK_WHOLE_SIZE, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet);
        VkUtil::updateDescriptorSet(device, indices, indicesSize * sizeof(uint32_t), 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorSet);
        VkUtil::updateTexelBufferDescriptorSet(device, activeIndices, 4, descriptorSet);

        //uploading data for brushing
        void* d;
        //informations->amtOfBrushAxes = brushes.size();
        informations->amtOfAttributes = amtOfAttributes;
        informations->amtOfIndices = indicesSize;
        informations->lineCount = 0;
        informations->globalLineCount = (lastBrush) ? 0 : -1;
        informations->first = first;
        informations->andOr = andy;
        
        *(uint32_t*)lassoBytes = lassos.size();
        uint32_t base_index = 1 + lassos.size();
        uint32_t lassoC = 0;
        for(const auto& lasso: lassos){
            lassoBytes[1 + lassoC] = base_index - 1; //1 has to be substracted as the first element is not in the array on the gpu
            lassoBytes[base_index++] = lasso.borderPoints.size();
            lassoBytes[base_index++] = lasso.attr1;
            lassoBytes[base_index++] = lasso.attr2;
            for(const auto& p: lasso.borderPoints){
                lassoBytes[base_index++] = p.x;
                lassoBytes[base_index++] = p.y;
            }
            ++lassoC;
        }
        assert(base_index * sizeof(float) == lassosByteSize);

        vkMapMemory(device, uboMemory, uboOffsets[0], infoBytesSize, 0, &d);
        memcpy(d, informations, infoBytesSize);
        vkUnmapMemory(device, uboMemory);

        vkMapMemory(device, uboMemory, uboOffsets[1], lassosByteSize, 0, &d);
        memcpy(d, lassoBytes, lassosByteSize);
        vkUnmapMemory(device, uboMemory);

        //dispatching the command buffer to calculate active indices
        VkCommandBuffer command;
        VkUtil::createCommandBuffer(device, commandPool, &command);

        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, lassoPipelineLayout, 0, 1, &descriptorSet, 0, {});
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, lassoPipeline);
        int patchAmount = indicesSize / BRUSHERLOCALSIZE;
        patchAmount += (indicesSize % BRUSHERLOCALSIZE) ? 1 : 0;
        vkCmdDispatch(command, patchAmount, 1, 1);
        VkUtil::commitCommandBuffer(queue, command);
        err = vkQueueWaitIdle(queue);
        check_vk_result(err);

        //getting the amount of remaining lines(if only this brush would have been applied)
        VkUtil::downloadData(device, uboMemory, 0, sizeof(UBOinfo), informations);
        ReturnStruct res{informations->lineCount, informations->globalLineCount};

        vkFreeCommandBuffers(device, commandPool, 1, &command);
        vkFreeDescriptorSets(device, descriptorPool, 1, &descriptorSet);
        vkFreeMemory(device, uboMemory, nullptr);
        vkDestroyBuffer(device, uboBuffers[0], nullptr);
        vkDestroyBuffer(device, uboBuffers[1], nullptr);

        free(informations);
        free(lassoBytes);

        return res;
    };
};