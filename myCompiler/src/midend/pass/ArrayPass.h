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
        bool moduleClosedFormProcessed = false;
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

    // 纯拷贝循环：dst[i]=src[i] 或 dst[i]=src[off+i]（off 为循环不变量）时，
    // 删除拷贝循环并将 dst 的访问重写为带偏移的 src 访问
    class ArrayCopyPropagationPass : public Pass
    {
    public:
        ArrayCopyPropagationPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "ArrayCopyPropagation"; }

    private:
        struct CopyLoopPattern
        {
            Value *srcArray = nullptr;
            Value *dstArray = nullptr;
            Value *indexOffset = nullptr; // nullptr 表示同下标 identity copy
            size_t storeIvIndexPos = 0;
            bool srcFlatIndex = false;
            bool valid = false;
        };

        bool analyzeCopyLoop(const Loop &loop, CopyLoopPattern &pattern) const;
        bool isCopyPropagationSafe(const Loop &copyLoop,
                                   Value *dstArray,
                                   const std::vector<Loop> &allLoops) const;
        void applyCopyPropagation(Function *func, const CopyLoopPattern &pattern) const;
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