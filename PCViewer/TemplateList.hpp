#pragma once
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

struct TemplateList {
	std::string name;
	VkBuffer buffer;
	std::vector<uint32_t> indices;
	std::vector<std::pair<float, float>> minMax;
	float pointRatio;		//ratio of points in the datasaet(reduced)
    std::string parentDataSetName = ""; // This only gets set for template lists used to generate brushes to identify the parent dataset correctly but only once.
};