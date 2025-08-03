#pragma once
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "../irbuild/IRDataStructure.h"
// 支配树节点编号辅助
namespace optimization
{
    struct LTContext
    {
        std::vector<BasicBlock *> vertex; // dfs序号 -> BB
        std::unordered_map<BasicBlock *, int> semi, vertexId;
        std::unordered_map<BasicBlock *, BasicBlock *> idom, parent, ancestor, label;
        std::unordered_map<BasicBlock *, std::vector<BasicBlock *>> bucket, pred, child;
        int N = 0;
    };

    void dfsLT(BasicBlock *v, LTContext &ctx);
    void compress(BasicBlock *v, LTContext &ctx);
    BasicBlock *eval(BasicBlock *v, LTContext &ctx);
    // 返回每个BB的直接支配者
    std::unordered_map<BasicBlock *, BasicBlock *>
    computeIDom_LengauerTarjan(Function *func);
}
