#pragma once
#include "VkUtil.h"
#include <map>
#include <vector>
#include <string>
#include "Structures.hpp"

class CorrelationManager{
public:
    enum class CorrelationMetric{
        Pearson,
        SpearmanRank,
        KendallRank
    };
    struct AttributeCorrelation{
        int baseAttribute;
        CorrelationMetric metric{};
        std::vector<float> correlationScores{};   //all axes are places consecutively in the vector(contains correlation to itself)
    };
    struct DrawlistCorrelations{
        std::string drawlist{};
        std::map<int, AttributeCorrelation> attributeCorrelations{};
    };

    CorrelationManager(const VkUtil::Context& context);
    ~CorrelationManager();

    void calculateCorrelation(const DrawList& dl, CorrelationMetric metric = CorrelationMetric::Pearson, int baseAttribute = -1, bool useGpu = true);

    std::map<std::string, DrawlistCorrelations> correlations{};
    bool printTimings = false;
private:
    uint32_t compLocalSize = 256;
    VkUtil::Context _vkContext{};
    std::string _pearsonShader = "shader/corrPearson.comp.spv";
    std::string _spearmanShader = "shader/corrSpearman.comp.spv";
    std::string _kendallShader = "shader/corrKendall.comp.spv";
    std::string _meanShader = "shader/corrMean.comp.spv";
    VkUtil::PipelineInfo _pearsonPipeline{}, _spearmanPipeline{}, _kendallPipeline{}, _meanPipeline{};
    void _execCorrelationCPU(const DrawList& dl, CorrelationMetric metric, int baseAttribute);
    void _execCorrelationGPU(const DrawList& dl, CorrelationMetric metric, int baseAttribute);
};