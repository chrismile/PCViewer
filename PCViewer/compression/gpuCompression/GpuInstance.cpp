#include "GpuInstance.hpp"
#include "../cpuCompression/util.h"
#include "RunLength.hpp"
#include "Huffman.hpp"
#include "HuffmanTable.h"
#include "Histogram.hpp"
#include "PackInc.hpp"
#include "Encode.hpp"
#include "../../range.hpp"
#include "../../PCUtil.h"

namespace vkCompress
{
    using namespace cudaCompress;
    GpuInstance::GpuInstance(VkUtil::Context context, uint32_t streamCountMax, uint32_t elemCountPerStreamMax, uint32_t codingBlockSize, uint32_t log2HuffmanDistinctSymbolCountMax):
    vkContext(context),
    m_streamCountMax(streamCountMax),
    m_elemCountPerStreamMax(elemCountPerStreamMax),
    m_codingBlockSize(codingBlockSize == 0 ? 128 : codingBlockSize), // default to 128
    m_log2HuffmanDistinctSymbolCountMax(log2HuffmanDistinctSymbolCountMax == 0 ? 14 : log2HuffmanDistinctSymbolCountMax)  // default to 14 bits (which was used before this was configurable)
    {
        if(m_log2HuffmanDistinctSymbolCountMax > 24) 
            throw std::runtime_error{"WARNING: log2HuffmanDistinctSymbolCountMax must be <= 24 (provided:" + std::to_string(m_log2HuffmanDistinctSymbolCountMax) + ")\n"};

        uint offsetCountMax = (m_elemCountPerStreamMax + m_codingBlockSize - 1) / m_codingBlockSize;

        uint rowPitch = (uint) getAlignedSize(m_elemCountPerStreamMax + 1, 128 / sizeof(uint));
        m_pScanPlan = new ScanPlan(context, sizeof(uint), m_elemCountPerStreamMax + 1, m_streamCountMax, rowPitch); // "+ 1" for total
        m_pReducePlan = new ReducePlan(context, sizeof(uint), m_elemCountPerStreamMax);

        size_t sizeTier0 = 0;
        sizeTier0 = max(sizeTier0, runLengthGetRequiredMemory(this));
        sizeTier0 = max(sizeTier0, huffmanGetRequiredMemory(this));
        // HuffmanEncodeTable uses histogram...
        sizeTier0 = max(sizeTier0, HuffmanEncodeTable::getRequiredMemory(this) + histogramGetRequiredMemory(this));
        sizeTier0 = max(sizeTier0, packIncGetRequiredMemory(this));
        size_t sizeTier1 = 0;
        sizeTier1 = max(sizeTier1, encodeGetRequiredMemory(this));

        m_bufferSize = sizeTier0 + sizeTier1;

        // getting warp size from phyiscal device info
        VkPhysicalDeviceSubgroupProperties subgroupProperties;
        subgroupProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
        subgroupProperties.pNext = NULL;

        VkPhysicalDeviceProperties2 physicalDeviceProperties;
        physicalDeviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        physicalDeviceProperties.pNext = &subgroupProperties;

        vkGetPhysicalDeviceProperties2(context.physicalDevice, &physicalDeviceProperties);
        m_warpSize = subgroupProperties.subgroupSize;
        m_subgroupSize = m_warpSize;

        // creating all pipelines

        // Huffman table pipelines ---------------------------------------------------
        // huffman decode
        const std::string_view huffmanShaderPath = "shader/compressHuffman_decode.comp.spv";

        auto compBytes = PCUtil::readByteFile(huffmanShaderPath);
        auto shaderModule = VkUtil::createShaderModule(context.device, compBytes);

        std::vector<VkDescriptorSetLayoutBinding> bindings;
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0;
        b.descriptorCount = 1;
        b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings.push_back(b);

        auto& decodeHuffmanLayout = Encode.Decode[0].decodeHuffmanLong.descriptorSetLayout;
        VkUtil::createDescriptorSetLayout(context.device, bindings, &decodeHuffmanLayout);

        std::vector<VkSpecializationMapEntry> entries{VkSpecializationMapEntry{0, 0, sizeof(uint32_t)}};
        uint32_t longSymbols = 1;           // first creating the long symbols pipeline
        VkSpecializationInfo specializationInfo{};
        specializationInfo.mapEntryCount = entries.size();
        specializationInfo.pMapEntries = entries.data();
        specializationInfo.dataSize = sizeof(longSymbols);
        specializationInfo.pData = &longSymbols;
        std::vector<VkPushConstantRange> pushConstants{VkPushConstantRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t)}};
        VkUtil::createComputePipeline(context.device, shaderModule, {decodeHuffmanLayout}, &Encode.Decode[0].decodeHuffmanLong.pipelineLayout, &Encode.Decode[0].decodeHuffmanLong.pipeline, &specializationInfo, pushConstants);
        // short symbols
        longSymbols = 0;
        shaderModule = VkUtil::createShaderModule(context.device, compBytes);
        VkUtil::createComputePipeline(context.device, shaderModule, {decodeHuffmanLayout}, &Encode.Decode[0].decodeHuffmanShort.pipelineLayout, &Encode.Decode[0].decodeHuffmanShort.pipeline, &specializationInfo, pushConstants);

        // huffman transpose
        const std::string_view huffmanTransposePath = "shader/compress_decodeTranspose.comp.spv";
        compBytes = PCUtil::readByteFile(huffmanTransposePath);

        // descriptorSet layout is the same as for huffman decode -> suse decodeHuffmanLayout
        // long symbols
        longSymbols = 1;
        const std::vector<uint32_t> codingBlocks{32, 64, 128, 256};
        entries.push_back({1, sizeof(uint32_t), sizeof(uint32_t)}); // adding specialization constant 1: codingBlockSize
        entries.push_back({2, sizeof(uint32_t) * 2, sizeof(uint32_t)}); // adding specialization constant 2: subgroupSize
        entries.push_back({3, sizeof(uint32_t) * 3, sizeof(uint32_t)}); // adding specialization constant 3: localHeight
        uint32_t specializationData[4]{longSymbols, 0, m_subgroupSize, 256 / m_subgroupSize};
        specializationInfo.dataSize = sizeof(uint32_t) * 4;
        specializationInfo.pData = specializationData;
        specializationInfo.mapEntryCount = entries.size();
        specializationInfo.pMapEntries = entries.data();
        for(uint32_t i: codingBlocks){
            specializationData[1] = i;  // setting the codingBlockSize
            shaderModule = VkUtil::createShaderModule(context.device, compBytes);
            VkUtil::createComputePipeline(context.device, shaderModule, {decodeHuffmanLayout}, &Encode.Decode[0].huffmanTransposeLong[i].pipelineLayout, &Encode.Decode[0].huffmanTransposeLong[i].pipeline, &specializationInfo);
        }
        // short symbols
        specializationData[0] = 0;
        for(uint32_t i: codingBlocks){
            specializationData[1] = i;  // setting the codingBlockSize
            shaderModule = VkUtil::createShaderModule(context.device, compBytes);
            VkUtil::createComputePipeline(context.device, shaderModule, {decodeHuffmanLayout}, &Encode.Decode[0].huffmanTransposeShort[i].pipelineLayout, &Encode.Decode[0].huffmanTransposeShort[i].pipeline, &specializationInfo);
        }

        // inclusive scan
        const std::string_view inclusiveScanPath = "shader/compression_scan.comp.spv";
        compBytes = PCUtil::readByteFile(inclusiveScanPath);
        shaderModule = VkUtil::createShaderModule(context.device, compBytes);

        bindings.clear();
        // does not need descriptor set anymore
        //VkUtil::createDescriptorSetLayout(context.device, bindings, &RunLength.inclusiveScanInfo.descriptorSetLayout);
        
        entries = {{0, 0, sizeof(uint32_t)}};
        uint32_t exclusive = 0;
        specializationInfo.dataSize = sizeof(uint32_t);
        specializationInfo.pData = &exclusive;
        specializationInfo.mapEntryCount = entries.size();
        specializationInfo.pMapEntries = entries.data();
        pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4 * sizeof(uint32_t) + 3 * sizeof(uint64_t)}};
        VkUtil::createComputePipeline(context.device, shaderModule, {}, &RunLength.inclusiveScanInfo.pipelineLayout, &RunLength.inclusiveScanInfo.pipeline, &specializationInfo, pushConstants);
        shaderModule = VkUtil::createShaderModule(context.device, compBytes);
        exclusive = 1;
        VkUtil::createComputePipeline(context.device, shaderModule, {}, &RunLength.exclusiveScanInfo.pipelineLayout, &RunLength.exclusiveScanInfo.pipeline, &specializationInfo, pushConstants);

        // add pipeline (used for scan pipelines)
        const std::string_view addPath = "shader/compression_scanAdd.comp.spv";
        compBytes = PCUtil::readByteFile(addPath);
        shaderModule = VkUtil::createShaderModule(context.device, compBytes);
        
        VkUtil::createComputePipeline(context.device, shaderModule, {}, &RunLength.addInfo.pipelineLayout, &RunLength.addInfo.pipeline, {}, pushConstants);
        
        // creating the multi scatter kernel
        const std::string_view scatterPath = "shader/compression_RLDecodeScatter.comp.spv";
        compBytes = PCUtil::readByteFile(scatterPath);
        shaderModule = VkUtil::createShaderModule(context.device, compBytes);

        VkUtil::createComputePipeline(context.device, shaderModule, {}, &RunLength.scatterInfo.pipelineLayout, &RunLength.scatterInfo.pipeline, {}, pushConstants);

        // creating unquantize pipeline
        const std::string_view unquantPath = "shader/compression_unquantize.comp.spv";
        compBytes = PCUtil::readByteFile(unquantPath);
        shaderModule = VkUtil::createShaderModule(context.device, compBytes);

        pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4 * sizeof(uint32_t) + 2 * sizeof(uint64_t)}};
        VkUtil::createComputePipeline(context.device, shaderModule, {}, &Quantization.unquantizeUShortFloatInfo.pipelineLayout, &Quantization.unquantizeUShortFloatInfo.pipeline, {}, pushConstants);

        // creating dwt inverse pipelines
        const std::string_view dwtInversePath = "shader/compression_DWTInverse.comp.spv";
        compBytes = PCUtil::readByteFile(dwtInversePath);
        shaderModule = VkUtil::createShaderModule(context.device, compBytes);

        uint32_t floatOut = 1;
        specializationInfo.dataSize = sizeof(floatOut);
        specializationInfo.pData = &floatOut;
        // map entries are still the same

        VkUtil::createComputePipeline(context.device, shaderModule, {}, &DWT.floatInverseInfo.pipelineLayout, &DWT.floatInverseInfo.pipeline, &specializationInfo, pushConstants);

        shaderModule = VkUtil::createShaderModule(context.device, compBytes);
        floatOut = 0;
        VkUtil::createComputePipeline(context.device, shaderModule, {}, &DWT.floatToHalfInverseInfo.pipelineLayout, &DWT.floatToHalfInverseInfo.pipeline, &specializationInfo, pushConstants);

        // copy pipeline
        const std::string_view copyPath = "shader/compression_copy.comp.spv";
        compBytes = PCUtil::readByteFile(copyPath);
        shaderModule = VkUtil::createShaderModule(context.device, compBytes);
        pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4 * sizeof(uint32_t) + 2 * sizeof(uint64_t)}};
        VkUtil::createComputePipeline(context.device, shaderModule, {}, &Copy.pipelineInfo.pipelineLayout, &Copy.pipelineInfo.pipeline, {}, pushConstants);
        
        // resources for huffman pipeline ---------------------------------------------------------------
        // creating the buffer and descriptor sets for decoding ---------------------
        for(int i: irange(Encode.ms_decodeResourcesCount)){
            auto& d = Encode.Decode[i];
            uint infoSize = sizeof(HuffmanGPUStreamInfo);
            uint symbolZeroCountsSize = elemCountPerStreamMax * sizeof(uint16_t) * 2;   // times 2 as we need storage for symbol as well as zero counts
            VkBufferUsageFlags flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            auto [buffers, offsets, memory] = VkUtil::createMultiBufferBound(context, {infoSize, infoSize}, {flags, flags}, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
            auto [symbolZeroBuffer, offs, m] = VkUtil::createMultiBufferBound(context, {symbolZeroCountsSize}, {VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT}, 0);
            d.streamInfos = buffers[0];
            d.streamInfosOffset = offsets[0];
            d.zeroInfos = buffers[1];
            d.zeroInfosOffset = offsets[1];

            d.symbolsZeroCounts = symbolZeroBuffer[0];
            d.compactSymbolsOffset = 0;
            d.zeroCountsOffset = symbolZeroCountsSize / 2;

            d.memory = memory;
            d.nonVisMemory = m;

            VkUtil::createDescriptorSets(context.device, {Encode.Decode[0].decodeHuffmanLong.descriptorSetLayout, Encode.Decode[0].decodeHuffmanLong.descriptorSetLayout}, context.descriptorPool, &d.streamInfoSet);
            VkUtil::updateDescriptorSet(context.device, d.streamInfos, infoSize, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, d.streamInfoSet);
            VkUtil::updateDescriptorSet(context.device, d.zeroInfos, infoSize, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, d.zeroStreamInfoSet);
        }

        // run length scanned indices creation
        auto[buffers, offsets, mem] = VkUtil::createMultiBufferBound(context, {elemCountPerStreamMax * sizeof(uint32_t)}, {VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT}, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        RunLength.scannedIndices = buffers[0];
        RunLength.scannedIndicesMemory = mem;
    }
    
    GpuInstance::~GpuInstance() 
    {
        Histogram.pipelineInfo.vkDestroy(vkContext);
        HuffmanTable.pipelineInfo.vkDestroy(vkContext);
        RunLength.pipelineInfo.vkDestroy(vkContext);
        RunLength.inclusiveScanInfo.vkDestroy(vkContext);
        RunLength.exclusiveScanInfo.vkDestroy(vkContext);
        RunLength.addInfo.vkDestroy(vkContext);
        RunLength.scatterInfo.vkDestroy(vkContext);
        if(RunLength.scannedIndices)
            vkDestroyBuffer(vkContext.device, RunLength.scannedIndices, nullptr);
         if(RunLength.scannedIndicesMemory)
            vkFreeMemory(vkContext.device, RunLength.scannedIndicesMemory, nullptr);
        DWT.pipelineInfo.vkDestroy(vkContext);
        DWT.floatInverseInfo.vkDestroy(vkContext);
        DWT.floatToHalfInverseInfo.vkDestroy(vkContext);
        Quantization.pipelineInfo.vkDestroy(vkContext);
        Quantization.unquantizeUShortFloatInfo.vkDestroy(vkContext);
        Encode.Decode[0].decodeHuffmanLong.vkDestroy(vkContext);
        Encode.Decode[0].decodeHuffmanShort.vkDestroy(vkContext);
        Copy.pipelineInfo.vkDestroy(vkContext);

        for(int i: irange(Encode.ms_decodeResourcesCount)){
            auto& d = Encode.Decode[i];
            if(d.streamInfos)
                vkDestroyBuffer(vkContext.device, d.streamInfos, nullptr);
            if(d.zeroInfos)
                vkDestroyBuffer(vkContext.device, d.zeroInfos, nullptr);
            if(d.memory)
                vkFreeMemory(vkContext.device, d.memory, nullptr);
            if(d.streamInfoSet)
                vkFreeDescriptorSets(vkContext.device, vkContext.descriptorPool, 2, &d.streamInfoSet);
            if(d.symbolsZeroCounts)
                vkDestroyBuffer(vkContext.device, d.symbolsZeroCounts, nullptr);
            if(d.nonVisMemory)
                vkFreeMemory(vkContext.device, d.nonVisMemory, nullptr);
        }

        for(auto& [s, p]: Encode.Decode[0].huffmanTransposeLong){
            p.vkDestroy(vkContext);
        }
        for(auto& [s, p]: Encode.Decode[0].huffmanTransposeShort){
            p.vkDestroy(vkContext);
        }

        if(m_pScanPlan->m_buffer)
            vkDestroyBuffer(vkContext.device, m_pScanPlan->m_buffer, nullptr);
        if(m_pScanPlan->m_blockSumsMemory)
            vkFreeMemory(vkContext.device, m_pScanPlan->m_blockSumsMemory, nullptr);
        if(m_pReducePlan->m_blockSums)
            vkDestroyBuffer(vkContext.device, m_pReducePlan->m_blockSums, nullptr);
        if(m_pReducePlan->m_blockSumsMem)
            vkFreeMemory(vkContext.device, m_pReducePlan->m_blockSumsMem, nullptr);
    }
}