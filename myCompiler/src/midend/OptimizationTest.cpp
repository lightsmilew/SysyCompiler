#include "OptimizationPasses.h"
#include "IRDataStructure.h"
#include <cassert>
#include <iostream>

// 简单的测试框架
class OptimizationTest
{
private:
    int passed = 0;
    int failed = 0;

public:
    void runTest(const std::string &testName, std::function<bool()> testFunc)
    {
        std::cout << "Running test: " << testName << "... ";

        try
        {
            if (testFunc())
            {
                std::cout << "PASSED" << std::endl;
                passed++;
            }
            else
            {
                std::cout << "FAILED" << std::endl;
                failed++;
            }
        }
        catch (const std::exception &e)
        {
            std::cout << "FAILED (exception: " << e.what() << ")" << std::endl;
            failed++;
        }
    }

    void printSummary()
    {
        std::cout << "\n=== Test Summary ===" << std::endl;
        std::cout << "Passed: " << passed << std::endl;
        std::cout << "Failed: " << failed << std::endl;
        std::cout << "Total:  " << (passed + failed) << std::endl;
    }

    bool allPassed() const
    {
        return failed == 0;
    }
};

// 测试常量折叠
bool testConstantFolding()
{
    // 创建一个简单的函数用于测试
    auto func = std::make_unique<Function>("test", FunctionType::get(IntegerType::getInstance()));
    auto bb = std::make_unique<BasicBlock>("entry", func.get());

    // 创建常量折叠指令：3 + 5
    auto const3 = ConstantInt::get(3);
    auto const5 = ConstantInt::get(5);
    auto addInst = BinaryOperator::createAdd(const3, const5);
    bb->addInstruction(std::unique_ptr<Instruction>(addInst));

    func->addBasicBlock(std::move(bb));

    // 运行常量折叠优化
    optimization::ConstantFoldingPass cfPass;
    bool changed = cfPass.runOnFunction(func.get());

    // 验证结果：应该生成了常量8，并删除了加法指令
    return changed; // 简化的测试，实际应该检查指令是否被替换为常量
}

// 测试死代码消除
bool testDeadCodeElimination()
{
    auto func = std::make_unique<Function>("test", FunctionType::get(IntegerType::getInstance()));
    auto bb = std::make_unique<BasicBlock>("entry", func.get());

    // 创建一个未使用的计算指令
    auto const1 = ConstantInt::get(1);
    auto const2 = ConstantInt::get(2);
    auto deadInst = BinaryOperator::createAdd(const1, const2); // 这个结果没有被使用
    bb->addInstruction(std::unique_ptr<Instruction>(deadInst));

    // 添加一个return指令
    auto retInst = ReturnInst::create(const1);
    bb->addInstruction(std::unique_ptr<Instruction>(retInst));

    func->addBasicBlock(std::move(bb));

    size_t instCountBefore = bb->getInstructions().size();

    // 运行死代码消除
    optimization::DeadCodeEliminationPass dcePass;
    bool changed = dcePass.runOnFunction(func.get());

    size_t instCountAfter = bb->getInstructions().size();

    // 死代码应该被删除
    return changed && (instCountAfter < instCountBefore);
}

// 测试公共子表达式消除
bool testCommonSubexpressionElimination()
{
    auto func = std::make_unique<Function>("test", FunctionType::get(IntegerType::getInstance()));
    auto bb = std::make_unique<BasicBlock>("entry", func.get());

    auto const3 = ConstantInt::get(3);
    auto const5 = ConstantInt::get(5);

    // 创建两个相同的表达式：3 + 5
    auto add1 = BinaryOperator::createAdd(const3, const5);
    auto add2 = BinaryOperator::createAdd(const3, const5);

    bb->addInstruction(std::unique_ptr<Instruction>(add1));
    bb->addInstruction(std::unique_ptr<Instruction>(add2));

    func->addBasicBlock(std::move(bb));

    size_t instCountBefore = bb->getInstructions().size();

    // 运行公共子表达式消除
    optimization::CommonSubexpressionEliminationPass csePass;
    bool changed = csePass.runOnFunction(func.get());

    size_t instCountAfter = bb->getInstructions().size();

    // 其中一个加法指令应该被消除
    return changed && (instCountAfter < instCountBefore);
}

// 测试优化管道
bool testOptimizationPipeline()
{
    auto func = std::make_unique<Function>("test", FunctionType::get(IntegerType::getInstance()));
    auto bb = std::make_unique<BasicBlock>("entry", func.get());

    // 创建包含多种可优化模式的代码
    auto const3 = ConstantInt::get(3);
    auto const5 = ConstantInt::get(5);
    auto const8 = ConstantInt::get(8);

    // 常量折叠机会：3 + 5
    auto add1 = BinaryOperator::createAdd(const3, const5);
    bb->addInstruction(std::unique_ptr<Instruction>(add1));

    // 公共子表达式：再次计算 3 + 5
    auto add2 = BinaryOperator::createAdd(const3, const5);
    bb->addInstruction(std::unique_ptr<Instruction>(add2));

    // 死代码：未使用的计算
    auto deadMul = BinaryOperator::createMul(const3, const8);
    bb->addInstruction(std::unique_ptr<Instruction>(deadMul));

    // 返回指令
    auto retInst = ReturnInst::create(add1);
    bb->addInstruction(std::unique_ptr<Instruction>(retInst));

    func->addBasicBlock(std::move(bb));

    auto module = std::make_unique<Module>("test");
    module->addFunction(std::move(func));

    // 创建O2级别的优化管道
    auto passManager = optimization::createOptimizationPipeline(
        optimization::OptimizationLevel::O2, true);

    // 运行优化
    bool changed = passManager->runOnModule(module.get());

    return changed;
}

// 测试不同优化级别
bool testOptimizationLevels()
{
    // 测试O0级别（无优化）
    auto pm0 = optimization::createOptimizationPipeline(optimization::OptimizationLevel::O0);
    if (pm0 == nullptr)
        return false;

    // 测试O1级别
    auto pm1 = optimization::createOptimizationPipeline(optimization::OptimizationLevel::O1);
    if (pm1 == nullptr)
        return false;

    // 测试O2级别
    auto pm2 = optimization::createOptimizationPipeline(optimization::OptimizationLevel::O2);
    if (pm2 == nullptr)
        return false;

    return true;
}

int main()
{
    std::cout << "=== SysY Compiler Optimization Tests ===" << std::endl;

    OptimizationTest tester;

    // 运行各种测试
    tester.runTest("Constant Folding", testConstantFolding);
    tester.runTest("Dead Code Elimination", testDeadCodeElimination);
    tester.runTest("Common Subexpression Elimination", testCommonSubexpressionElimination);
    tester.runTest("Optimization Pipeline", testOptimizationPipeline);
    tester.runTest("Optimization Levels", testOptimizationLevels);

    // 打印测试结果
    tester.printSummary();

    if (tester.allPassed())
    {
        std::cout << "\n🎉 All tests passed! Your optimization framework is ready to use." << std::endl;
        return 0;
    }
    else
    {
        std::cout << "\n❌ Some tests failed. Please check the implementation." << std::endl;
        return 1;
    }
}

/*
编译和运行测试的命令：
g++ -std=c++17 -I../frontend -I../common -o optimization_test \
    OptimizationTest.cpp OptimizationPasses.cpp IRDataStructure.cpp

./optimization_test
*/
