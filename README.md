# SysY 编译器

## 操作
### 生成词法语法

```bash
java -jar /path/to/antlr-4.13.2-complete.jar -Dlanguage=Cpp -no-listener -visitor -o frontend/generate SysY.g4
```

### 编译

```bash
rm -rf build
mkdir build
cd build
# 如果需要调试信息，可以使用 Debug 模式
# 如果需要优化，可以使用 Release 模式
cmake ..
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake -DCMAKE_BUILD_TYPE=Release ..

make
```

### 测试

```bash
python3 test.py
```

### 运行脚本
```bash

./run.sh   
# 只运行测试脚本  
# cmake编译后运行测试脚本  
./run.sh -build  
# cmake重构后运行测试脚本  
./run.sh -rebuild  
```

---
## 问题  

### 1. 文法错误
'SysY.g4' 中的exp文法有误，但是目前没有修正。

### 2. 语法分析
isConst属性需要向上传递  
visitConstDecl没有把const修饰的变量设置为常量表达式  
constExpr如何处理 

### 3. 语义分析
左值引用和表达式类型获取函数中对数组维度判断重复  
省略第一维度的数组第一维度默认为-1，暂时不做处理，在中间代码生成时处理  

### 4. 中间代码生成
未实现函数多基本块时是否都有返回值  
表达式暂时未支持string类型，是否会报错  