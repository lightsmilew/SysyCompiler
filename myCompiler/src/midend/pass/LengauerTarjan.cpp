#include "LengauerTarjan.h"
using namespace optimization;
void optimization::dfsLT(BasicBlock *v, LTContext &ctx)
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
            ctx.parent[w] = v;
            dfsLT(w, ctx);
        }
        ctx.pred[w].push_back(v);
    }
}

void optimization::compress(BasicBlock *v, LTContext &ctx)
{
    if (ctx.ancestor[v] && ctx.ancestor[ctx.ancestor[v]])
    {
        compress(ctx.ancestor[v], ctx);
        if (ctx.semi[ctx.label[ctx.ancestor[v]]] < ctx.semi[ctx.label[v]])
            ctx.label[v] = ctx.label[ctx.ancestor[v]];
        ctx.ancestor[v] = ctx.ancestor[ctx.ancestor[v]];
    }
}

BasicBlock *optimization::eval(BasicBlock *v, LTContext &ctx)
{
    if (!ctx.ancestor[v])
        return ctx.label[v];
    compress(v, ctx);
    if (ctx.semi[ctx.label[ctx.ancestor[v]]] < ctx.semi[ctx.label[v]])
        return ctx.label[ctx.ancestor[v]];
    else
        return ctx.label[v];
}
// 返回每个BB的直接支配者
std::unordered_map<BasicBlock *, BasicBlock *>optimization::computeIDom_LengauerTarjan(Function *func)
{
    auto &bbs = func->getBasicBlocks();
    if (bbs.empty())
        return {};

    LTContext ctx;
    BasicBlock *entry = bbs[0].get();
    ctx.N = 0;
    dfsLT(entry, ctx);

    std::vector<BasicBlock *> vertex = ctx.vertex;
    int n = vertex.size();
    std::unordered_map<BasicBlock *, BasicBlock *> idom;

    // 1. 计算semi-dominator
    for (int i = n - 1; i >= 1; --i)
    {
        BasicBlock *w = vertex[i];
        for (auto *v : ctx.pred[w])
        {
            BasicBlock *u = eval(v, ctx);
            if (ctx.semi[u] < ctx.semi[w])
                ctx.semi[w] = ctx.semi[u];
        }
        ctx.idom[w] = vertex[ctx.semi[w] - 1]; // 初始设置为 semi[w] 的节点
        ctx.bucket[vertex[ctx.semi[w] - 1]].push_back(w);
        ctx.ancestor[w] = ctx.parent[w];
        if (ctx.parent[w])
        { // 防止入口块 parent 为 nullptr
            for (auto *v : ctx.bucket[ctx.parent[w]])
            {
                BasicBlock *u = eval(v, ctx);
                ctx.idom[v] = (ctx.semi[u] < ctx.semi[v]) ? u : ctx.parent[w];
            }
            ctx.bucket[ctx.parent[w]].clear();
        }
    }
    // 2. 显式计算idom
    for (int i = 1; i < n; ++i)
    {
        BasicBlock *w = vertex[i];
        if (ctx.idom[w] != vertex[ctx.semi[w] - 1])
            ctx.idom[w] = ctx.idom[ctx.idom[w]];
    }
    ctx.idom[entry] = nullptr;

    // 3. 输出
    std::unordered_map<BasicBlock *, BasicBlock *> result;
    for (auto &p : ctx.idom)
        if (p.first)
            result[p.first] = p.second;
    return result;
}