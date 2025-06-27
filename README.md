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
# 只运行测试脚本 
./run.sh   
# cmake编译后运行测试脚本  
./run.sh -build  
# cmake重构后运行测试脚本  
./run.sh -rebuild  
```

---
## 问题  

### 一. 文法错误
1.'SysY.g4' 中的exp文法有误，但是目前没有修正。
2.ifelse语句支持多分支
### 二. 语法分析
1.isConst属性需要向上传递-->已处理   
2.visitConstDecl没有把const修饰的变量设置为常量表达式-->已处理   
3.constExpr如何处理-->已处理    
4.exp和stmt的line设置有问题，无法用于debug  

### 三. 语义分析
1.左值引用和表达式类型获取函数中对数组维度判断重复-->已处理  
2.省略第一维度的数组第一维度默认为-1，暂时不做处理，在中间代码生成时处理-->已处理，数组作为函数参数时退化为指针  

### 四. 中间代码生成
1.未实现函数多基本块时是否都有返回值-->对应语义分析  
2.表达式暂时未支持string类型，是否会报错-->已处理  
3.未处理库函数-->已处理  
4.phi还未生成  
5.int和float范围未检测-->不做处理  
6.数组下标未检测合法(是否大于0)-->已处理  