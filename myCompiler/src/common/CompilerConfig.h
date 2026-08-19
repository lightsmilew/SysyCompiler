#pragma once

// 全局编译开关（由 main 解析命令行后设置）
namespace CompilerConfig
{
    // 启用 RISC-V Vector (RVV) 中端向量化 + 后端向量指令发射
    inline bool enableRVV = false;
    inline bool isTensorProgram=false;
}
