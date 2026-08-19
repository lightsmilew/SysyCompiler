#pragma once

#include "ASTNode.h"
#include"../common/CompilerConfig.h"
namespace ast
{
    // 把形如 tensor int mm(tensor int A[],tensor int B[]);
    // 在前端改为普通ir适配当前中端框架
    // 改写后会变成void mm(int ret[],tensor int A[],int A_len,tensor int B[],int B_len);
    // 把返回值作为参数传递，因为函数体内对A B运算需要知道维度信息，所以在caller显式传入最外维信息
    void lowerTensors(std::shared_ptr<CompUnitNode> compUnit);
}
