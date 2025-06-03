# 3rd_party/antlr4-runtime/CMakeLists.txt
file(GLOB_RECURSE ANTLR4_SRC "*.cpp")
find_package(Threads REQUIRED)
add_library(antlr4_runtime STATIC ${ANTLR4_SRC})
target_link_libraries(antlr4_runtime Threads::Threads)