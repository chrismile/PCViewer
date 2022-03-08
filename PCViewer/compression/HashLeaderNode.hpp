#pragma once

#include "HierarchyCreateNode.hpp"
#include <string>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <memory>
#include <atomic>
#include "CacheManagerInterface.hpp"
#include "../robin_hood_map/robin_hood.h"

// class which represents a single node in the compression hirarchy.
class HashLeaderNode: public HierarchyCreateNode{
public:
    struct FollowerInfo{
        std::shared_ptr<HashLeaderNode> follower;
        std::vector<float> pos;
        uint32_t count;
    };

    HashLeaderNode(): eps(0), epsMul(0), depth(0), maxDepth(0){};
    HashLeaderNode(const std::vector<float>& pos, float inEps, float inEpsMul, uint32_t inDepth, uint32_t inMaxDepth);

    //Hash vector leader node stores all follower data in vectors to provide fast access
    //Further we are using a structure of arrays to provide faster access for random accesses
    //None of the below vectors deletes data when a child is being cached, except that in follower the pointer is set to a nullptr
    robin_hood::unordered_map<uint64_t, std::shared_ptr<FollowerInfo>> map;              //the hash map is the spatial index for the follower
    const float eps, epsMul;
    const uint32_t depth, maxDepth;


    // adds the data point to the hirarchy, forwards to the correct child
    void addDataPoint(const std::vector<float>& d);
    long calcCacheScore();      //returns teh score for the current node. The lower the number, the more likely it is this node is being cached
    HierarchyCreateNode* getCacheNode(long& cacheScore);                    //returns the node of the whole tree with the highest cache score which is stored in cacheScore
    void getCacheNodes(HierarchyCacheInfo& info);
    void cacheNode(const std::string_view& cachePath, const std::string& parentId, float* parentCenter, float parentEps, HierarchyCreateNode* chacheNode){throw std::runtime_error{"Standard caching not supported"};};
    void cacheNodes(CacheManagerInterface& cacheManager, const std::string& parentId, float* parentCenter, float parentEps, const std::set<HierarchyCreateNode*>& cacheNodes);
    size_t getByteSize();
    std::shared_mutex& getMutex(){return _insertLock;};

    static uint64_t getChildIndex(uint32_t dimensionality,const float* parentCenter,const float* childCenter, float parentEps, float childEps);
private:
    static std::atomic<size_t> _globalUpdateStamp;
    uint64_t _id{};
    uint32_t _updateStamp{};
    uint32_t _leaderId{};
    std::shared_mutex _insertLock;
};