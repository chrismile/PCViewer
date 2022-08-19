#pragma once
#include <vector>
#include <array>
#include <stdexcept>
#include <string_view>
#include <list>
#include <map>
#include <memory>
#include <initializer_list>
#include <cmath>
#include "../range.hpp"
#include "../Structures.hpp"
#include "../imgui_nodes/imgui_node_editor.h"
#include "../imgui_nodes/utilities/widgets.h"

namespace deriveData{
template<class T, class Base = T>
class Creatable{
public:
    template<typename... Args>
    static std::unique_ptr<T> create(Args&&... args){
        return std::make_unique<T>(args...);
    }
    template<typename... Args>
    static std::unique_ptr<Base> createBase(Args&&... args){
        std::unique_ptr<T> ptr = std::make_unique<T>(args...);
        return std::unique_ptr<Base>(ptr.release());
    }
};

template<class T, class Base = T>
inline std::vector<std::unique_ptr<Base>> createFilledVec(uint32_t size){
    std::vector<std::unique_ptr<Base>> vec(size);
    for(int i: irange(size))
        vec[i] = T::createBase();
    return vec;
}

// writeable memory view
template<class T>
class memory_view{
    T* _data{};
    const size_t _size{};
public:
    memory_view(){};
    memory_view(T& d): _data(&d), _size(1){};
    memory_view(std::vector<T>& v): _data(v.data()), _size(v.size()){};
    template<size_t size>
    memory_view(std::array<T, size>& a): _data(a.data()), _size(size){};
    memory_view(T* data, size_t size): _data(data), _size(size){};
    template<class U>
    memory_view(memory_view<U> m): _data(reinterpret_cast<T*>(m.data())), _size(m.size() * sizeof(U) / sizeof(T)){
        assert(m.size() * sizeof(U) == _size * sizeof(T));   // debug assert to check if the memory views can be converted to each other, e.g. if the element sizes align
    }

    T* data(){return _data;};
    const T* data() const {return _data;};
    size_t size() const {return _size;};
    T& operator[](size_t i){
        assert(i < _size);   // debug assert for in bounds check
        return _data[i];
    }
    const T& operator[](size_t i) const{
        assert(i < _size);
        return _data[i];
    }
};

// ------------------------------------------------------------------------------------------
// types
// ------------------------------------------------------------------------------------------
class Type{
public:
    virtual ImVec4 color() const = 0;
    virtual memory_view<float> data() = 0;
    ax::Widgets::IconType iconType() const{return ax::Widgets::IconType::Circle;};
};

class FloatType: public Type, public Creatable<FloatType, Type>{
public:
    ImVec4 color() const override{return {1,0,0,1};};
    memory_view<float> data() override {return memory_view(_d);};
private:
    float _d;
};

class ConstantFloatType: public Type, public Creatable<ConstantFloatType, Type>{
    ImVec4 color() const override{return {1, .5, 0, 1};};
    memory_view<float> data() override{return {};};    // always returns null pointer as data is not changable
};

class VectorType: public Type,  public Creatable<VectorType, Type>{
public:
    ImVec4 color() const override{return {0,1,0,1};};
    memory_view<float> data() override{return memory_view(_d);};
private:
    std::vector<float> _d;
};

class Vec2Type: public Type,  public Creatable<Vec2Type, Type>{
public:
    ImVec4 color() const override{return {.5, .5, .5, 1};};
    memory_view<float> data() override{return memory_view(_d);};
private:
    std::array<float, 2> _d;
};

class Vec3Type: public Type,  public Creatable<Vec3Type, Type>{
public:
    ImVec4 color() const override{return {1,1,1,1};};
    memory_view<float> data() override{return memory_view(_d);};
private:
    std::array<float, 3> _d;
};

class Vec4Type: public Type,  public Creatable<Vec4Type, Type>{
public:
    ImVec4 color() const override{return {.1, .1, .1, 1};};
    memory_view<float> data() override{return memory_view(_d);};
private:
    std::array<float, 4> _d;
};

// ------------------------------------------------------------------------------------------
// nodes
// ------------------------------------------------------------------------------------------
class Node{
public:
    std::vector<std::unique_ptr<Type>> inputTypes;
    std::vector<std::string> inputNames;
    std::vector<std::unique_ptr<Type>> outputTypes;
    std::vector<std::string> outputNames;

    std::string name;
    std::string middleText;

    Node(std::vector<std::unique_ptr<Type>>&& inputTypes = {},
        std::vector<std::string>&& inputNames = {},
        std::vector<std::unique_ptr<Type>>&& outputTypes = {},
        std::vector<std::string>&& outputNames = {}, 
        std::string_view header = {}, std::string_view mt = {}):
        inputTypes(std::move(inputTypes)),
        inputNames(inputNames),
        outputTypes(std::move(outputTypes)),
        outputNames(outputNames),
        name(header),
        middleText(mt){}


    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const = 0;
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const = 0;
};

struct NodesRegistry{
    static std::map<std::string, std::function<std::unique_ptr<Node>()>> nodes;
    NodesRegistry(std::string name, std::function<std::unique_ptr<Node>()> createFunction) {if(nodes.count(name) == 0) nodes[name] = createFunction;};
};

// registers the nodes with a standard constructor
#define REGISTER_NODE(class) static NodesRegistry classReg_##class(#class , class::create<>);

// ------------------------------------------------------------------------------------------
// special nodes
// ------------------------------------------------------------------------------------------

class DatasetInputNode: public Node, public Creatable<DatasetInputNode>{
public:
    const std::string_view datasetId;
    const std::list<DataSet>& datasets;

    DatasetInputNode(std::string_view datasetId = {}, const std::list<DataSet>& datasets = {}, const std::vector<Attribute>& attributes = {}):
        Node(createFilledVec<FloatType, Type>(1), {std::string()}, createFilledVec<FloatType, Type>(1),{std::string()}, "", ""), datasets(datasets)
    {
        if(datasets.empty())
            return;
        const auto& d = getDataset(datasets, datasetId);
            return;
        for(int i: irange(d.data.columns.size())){
            outputTypes.push_back(FloatType::create());
            outputNames.push_back(attributes[i].originalName);
        }
        name = "Input data: " + d.name;
    }

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        // TODO implement
    };
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        // TODO implement
    };
};

class DerivationNode: public Node{
public:
    // TODO: implement later...
};

// ------------------------------------------------------------------------------------------
// unary nodes
// ------------------------------------------------------------------------------------------

template<class T>
class UnaryNode: public Node{
public:
    UnaryNode(std::string_view header, std::string_view middle):
        Node(createFilledVec<T, Type>(1), {std::string()}, createFilledVec<T, Type>(1),{std::string()}, header, middle){};
};

class MultiplicationInverseNode: public UnaryNode<FloatType>, public Creatable<MultiplicationInverseNode>{
public:
    MultiplicationInverseNode(): UnaryNode("", "1/"){};

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{     
        for(size_t i: irange(input)){
            for(size_t j: irange(input[i].size()))
                output[i][j] = 1. / input[i][j];
        }
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        for(size_t i: irange(inout)){
            for(size_t j: irange(inout[i].size()))
                inout[i][j] = 1. / inout[i][j];
        }
    }
};

class AdditionInverseNode: public UnaryNode<FloatType>, public Creatable<AdditionInverseNode>{
public:
    AdditionInverseNode(): UnaryNode("", "*-1"){};

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{        
        for(size_t i: irange(input)){
            for(size_t j: irange(input[i].size()))
                output[i][j] = -input[i][j];
        }
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        for(size_t i: irange(inout)){
            for(size_t j: irange(inout[i].size()))
                inout[i][j] = -inout[i][j];
        }
    }
};

class NormalizationNode: public UnaryNode<FloatType>, public Creatable<NormalizationNode>{
public:
    enum class NormalizationType{
        ZeroOne,
        MinusOneOne
    };
    NormalizationType normalizationType{NormalizationType::ZeroOne};

    NormalizationNode(): UnaryNode("", "norm"){};

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        // TODO implement
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        // TODO implement
    }
};

class AbsoluteValueNode: public UnaryNode<FloatType>, public Creatable<AbsoluteValueNode>{
public:
    AbsoluteValueNode(): UnaryNode("", "abs"){};

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        for(size_t i: irange(input)){
            for(size_t j: irange(input.size()))
                output[i][j] = std::abs(input[i][j]);
        }
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        for(size_t i: irange(inout)){
            for(size_t j: irange(inout[i].size()))
                inout[i][j] = std::abs(inout[i][j]);
        }
    }
};

class SquareNode: public UnaryNode<FloatType>, public Creatable<SquareNode>{
public:
    SquareNode(): UnaryNode("", "square"){};

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        for(size_t i: irange(input)){
            for(size_t j: irange(input[i].size()))
                output[i][j] = input[i][j] * input[i][j];
        }
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        for(size_t i: irange(inout)){
            for(size_t j: irange(inout[i].size()))
                inout[i][j] = inout[i][j] * inout[i][j];
        }
    }
};

class ExponentialNode: public UnaryNode<FloatType>, public Creatable<ExponentialNode>{
public:
    ExponentialNode(): UnaryNode("", "exp"){};

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        for(size_t i: irange(input)){
            for(size_t j: irange(input[i].size()))
                output[i][j] = std::exp(input[i][j]);
        }
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        for(size_t i: irange(inout)){
            for(size_t j: irange(inout[i].size()))
                inout[i][j] = std::exp(inout[i][j]);
        }
    }
};

class LogarithmNode: public UnaryNode<FloatType>, public Creatable<LogarithmNode>{
public:
    LogarithmNode(): UnaryNode("", "log"){};

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        // TODO implement
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        // TODO implement
    }
};

class UnaryVec2Node: public UnaryNode<Vec2Type>{
public:
    UnaryVec2Node(std::string_view header = "", std::string_view body = ""): UnaryNode(header, body){};

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        // TODO implement
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        // TODO implement
    }
};

class CreateVec2Node: public UnaryVec2Node, public Creatable<CreateVec2Node>{
public:
    CreateVec2Node(): UnaryVec2Node(){
        inputTypes = createFilledVec<FloatType, Type>(2);
        inputNames = {"x", "y"};
        outputNames = {"vec2"};
    }

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        // TODO implement
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        // TODO implement
    }
};

class SplitVec2: public UnaryVec2Node, public Creatable<SplitVec2>{
public:
    SplitVec2(): UnaryVec2Node(){
        inputNames = {"vec2"};
        outputTypes = createFilledVec<FloatType, Type>(2);
        outputNames = {"x", "y"};
    }

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        // TODO implement
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        // TODO implement
    }
};

class Vec2Norm: public UnaryVec2Node, public Creatable<Vec2Norm>{
public:
    Vec2Norm(): UnaryVec2Node("", "len"){outputTypes[0] = FloatType::create();};

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        // TODO implement
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        // TODO implement
    }
};

class UnaryVec3Node: public UnaryNode<Vec3Type>{
public:
    UnaryVec3Node(std::string_view header = "", std::string_view body = ""): UnaryNode(header, body){};

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        // TODO implement
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        // TODO implement
    }
};

class CreateVec3Node: public UnaryVec3Node, public Creatable<CreateVec3Node>{
public:
    CreateVec3Node(): UnaryVec3Node(){
        inputTypes = createFilledVec<FloatType, Type>(3);
        inputNames = {"x", "y", "z"};
        outputNames = {"vec3"};
    }

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        // TODO implement
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        // TODO implement
    }
};

class SplitVec3: public UnaryVec3Node, public Creatable<SplitVec3>{
public:
    SplitVec3(): UnaryVec3Node(){
        inputNames = {"vec3"};
        outputTypes = createFilledVec<FloatType, Type>(3);
        outputNames = {"x", "y", "z"};
    }

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        // TODO implement
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        // TODO implement
    }
};

class Vec3Norm: public UnaryVec3Node, public Creatable<Vec3Norm>{
public:
    Vec3Norm(): UnaryVec3Node("", "len"){outputTypes[0] = FloatType::create();};

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        // TODO implement
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        // TODO implement
    }
};

class UnaryVec4Node: public UnaryNode<Vec4Type>{
public:
    UnaryVec4Node(std::string_view header = "", std::string_view body = ""): UnaryNode(header, body){};

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        // TODO implement
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        // TODO implement
    }
};

class CreateVec4Node: public UnaryVec4Node, public Creatable<CreateVec4Node>{
public:
    CreateVec4Node(): UnaryVec4Node(){
        inputTypes = createFilledVec<FloatType, Type>(4);
        inputNames = {"x", "y", "z", "w"};
        outputNames = {"vec4"};
    }

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        // TODO implement
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        // TODO implement
    }
};

class SplitVec4: public UnaryVec4Node, public Creatable<SplitVec4>{
public:
    SplitVec4(): UnaryVec4Node(){
        inputNames = {"vec4"};
        outputTypes = createFilledVec<FloatType, Type>(4);
        outputNames = {"x", "y", "z", "w"};
    };

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        // TODO implement
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        // TODO implement
    }
};

class Vec4Norm: public UnaryVec4Node, public Creatable<Vec4Norm>{
public:
    Vec4Norm(): UnaryVec4Node("", "len"){outputTypes[0] = FloatType::create();};

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        // TODO implement
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        // TODO implement
    }
};

// ------------------------------------------------------------------------------------------
// binary nodes
// ------------------------------------------------------------------------------------------

template<class T>
class BinaryNode: public Node{
public:
    BinaryNode(std::string_view header = "", std::string_view body = ""):
        Node(createFilledVec<T, Type>(2), {std::string(), std::string()}, createFilledVec<T, Type>(1), {std::string()}, header, body){};

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        // TODO implement
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        // TODO implement
    }
};

class PlusNode: public BinaryNode<FloatType>, public Creatable<PlusNode>{
public:
    PlusNode(): BinaryNode("", "+"){};

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        // TODO implement
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        // TODO implement
    }
};

class MinusNode: public BinaryNode<FloatType>, public Creatable<MinusNode>{
public:
    MinusNode(): BinaryNode("", "-"){};

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        // TODO implement
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        // TODO implement
    }
};

class MultiplicationNode: public BinaryNode<FloatType>, public Creatable<MultiplicationNode>{
public:
    MultiplicationNode(): BinaryNode("", "*"){};

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        // TODO implement
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        // TODO implement
    }
};

class DivisionNode: public BinaryNode<FloatType>, public Creatable<DivisionNode>{
public:
    DivisionNode(): BinaryNode("", "/"){};

    virtual void applyOperationCpu(const std::vector<memory_view<float>>& input ,std::vector<memory_view<float>>& output) const override{
        // TODO implement
    }
    virtual void applyOperationInplaceCpu(std::vector<memory_view<float>>& inout) const override{
        // TODO implement
    }
};
}