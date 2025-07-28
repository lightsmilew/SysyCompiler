# 指定 CMake 的最小版本要求
cmake_minimum_required(VERSION 3.10)

# 设置项目名称和使用的语言（CXX 代表 C++）
project(my_compiler CXX)

# 添加依赖 Threads 库
find_package(Threads REQUIRED)

# 设置 C++ 标准为 C++17
set(CMAKE_CXX_STANDARD 17)

# 设置 C++ 编译器标志，这里没有额外添加，使用默认
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

# 设置调试模式下的编译器标志，开启 DEBUG 宏
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")

# 使用 GLOB_RECURSE 模式递归查找 src 目录下所有的 .cpp 文件
file(GLOB_RECURSE SRC "src/*.cpp")

set(SRC
    ${SRC}
)

# 添加项目的 src 目录到头文件搜索路径
include_directories(src)

# 添加第三方库目录 antlr4-runtime 到头文件搜索路径
include_directories(3rd_party/antlr4-runtime)

# 添加 antlr4-runtime 子目录作为子项目进行构建
add_subdirectory(3rd_party/antlr4-runtime)

# 创建名为 my_compiler 的可执行文件，将所有源文件编译链接到这个可执行文件中
add_executable(my_compiler ${SRC})

# 将 antlr4_runtime 库与 my_compiler 可执行文件链接
target_link_libraries(my_compiler antlr4_runtime Threads::Threads)