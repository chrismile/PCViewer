#include "Nodes.hpp"

using namespace deriveData::Nodes;
std::map<std::string, Registry::Entry> Registry::nodes{};
REGISTER(DatasetInput);
REGISTER(VectorZero);
REGISTER(VectorOne);
REGISTER(VectorRandom);
REGISTER(PrintVector);
REGISTER(DatasetOutput);
REGISTER(Derivation);
REGISTER(Sum);
REGISTER(Norm);
// unary nodes
REGISTER(Inverse);
REGISTER(Negate);
REGISTER(Normalization);
REGISTER(AbsoluteValue);
REGISTER(Square);
REGISTER(Sqrt);
REGISTER(Exponential);
REGISTER(Logarithm);
REGISTER(CreateVec2);
REGISTER(SplitVec2);
REGISTER(Vec2Norm);
REGISTER(CreateVec3);
REGISTER(SplitVec3);
REGISTER(Vec3Norm);
REGISTER(CreateVec4);
REGISTER(SplitVec4);
REGISTER(Vec4Norm);
// binary nodes
REGISTER(Plus);
REGISTER(Minus);
REGISTER(Multiplication);
REGISTER(Division);
REGISTER(Pow);
REGISTER(PlusVec2);
REGISTER(MinusVec2);
REGISTER(MultiplicationVec2);
REGISTER(DivisionVec2);
