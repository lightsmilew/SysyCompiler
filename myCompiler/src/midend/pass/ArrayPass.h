#pragma once
#include "Pass.h"
namespace optimization
{
    // 13.数组消除
    class ArrayEliminationPass : public Pass
    {
    public:
        ArrayEliminationPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "ArrayElimination"; }

    private:
        // 用于记录数组消除次数
        size_t ArrayEliminationCount = 0;
    };
    // 16.删除只写数组
    class RemoveOnlyWriteArrayPass : public Pass
    {
    public:
        RemoveOnlyWriteArrayPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "RemoveOnlyWriteArray"; }

    private:
        bool writeOnlyGlobalsProcessed = false;
        bool removeWriteOnlyGlobals(Module *module);
        bool removeWriteOnlyRootInFunction(Value *root, Function *func);
    };

    // 纯拷贝循环：dst[i]=src[i] 且无其它副作用时，函数内 dst 基址替换为 src 并删除该循环
    class ArrayCopyPropagationPass : public Pass
    {
    public:
        ArrayCopyPropagationPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "ArrayCopyPropagation"; }

    private:
        bool isPureCopyLoop(const Loop &loop, Value *&srcArray, Value *&dstArray) const;
        bool isCopyPropagationSafe(const Loop &copyLoop,
                                   Value *dstArray,
                                   const std::vector<Loop> &allLoops) const;
        void replaceArrayBaseInFunction(Function *func, Value *dstArray, Value *srcArray) const;
        void redirectAndRemoveLoop(Function *func, const Loop &loop);
    };

    // 17. 数组同元素写后读转发
    class ArrayStoreLoadForwardPass : public Pass
    {
    public:
        ArrayStoreLoadForwardPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "ArrayStoreLoadForward"; }

    private:
        std::string buildArrayIndexKey(Value *ptr) const;
        std::string getForwardingKey(Value *ptr) const;
    };

}