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

    void dfsLT(BasicBlock *v, LTContext &ctx)
    {
        ctx.N++;
        ctx.semi[v] = ctx.N;
        ctx.ancestor[v] = nullptr;
        ctx.vertex.push_back(v);
        ctx.vertexId[v] = ctx.N - 1;
        ctx.label[v] = v;
        for (auto *w : v->getSuccessors())
        {
            if (!ctx.semi.count(w))
            {
                ctx.parent[w] = v; // 修正为指针
                dfsLT(w, ctx);
            }
            ctx.pred[w].push_back(v);
        }
    }

    void compress(BasicBlock *v, LTContext &ctx)
    {
        if (ctx.ancestor[v] && ctx.ancestor[ctx.ancestor[v]])
        {
            compress(ctx.ancestor[v], ctx);
            if (ctx.semi[ctx.label[ctx.ancestor[v]]] < ctx.semi[ctx.label[v]])
                ctx.label[v] = ctx.label[ctx.ancestor[v]];
            ctx.ancestor[v] = ctx.ancestor[ctx.ancestor[v]];
        }
    }

    BasicBlock *eval(BasicBlock *v, LTContext &ctx)
    {
        if (!ctx.ancestor[v])
            return ctx.label[v];
        compress(v, ctx);
        if (ctx.semi[ctx.label[ctx.ancestor[v]]] < ctx.semi[ctx.label[v]])
            return ctx.label[ctx.ancestor[v]];
        else
            return ctx.label[v];
    }
   
} // namespace
