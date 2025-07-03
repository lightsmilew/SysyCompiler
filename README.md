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

### qemu-riscv64 模拟器运行

```bash
# qume 启动
qemu-system-riscv64 \
-machine virt -nographic -m 2048 -smp 4 \
-kernel /usr/lib/u-boot/qemu-riscv64_smode/uboot.elf \
-device virtio-net-device,netdev=eth0 \
-netdev user,id=eth0,hostfwd=tcp::2222-:22 \
-device virtio-rng-pci \
-drive file=ubuntu-24.04.2-preinstalled-server-riscv64.img,format=raw,if=virtio

# 传递文件
scp -P 端口号 本地文件路径 用户名@远程主机IP:远程目录路径


```

---

## 问题

### 一. 文法错误

1.'SysY.g4' 中的 exp 文法有误，但是目前没有修正。
2.ifelse 语句支持多分支

### 二. 语法分析

1.isConst 属性需要向上传递-->已处理  
2.visitConstDecl 没有把 const 修饰的变量设置为常量表达式-->已处理  
3.constExpr 如何处理-->已处理  
4.exp 和 stmt 的 line 设置有问题，无法用于 debug

### 三. 语义分析

1.左值引用和表达式类型获取函数中对数组维度判断重复-->已处理  
2.省略第一维度的数组第一维度默认为-1，暂时不做处理，在中间代码生成时处理-->已处理，数组作为函数参数时退化为指针

### 四. 中间代码生成

1.未实现函数多基本块时是否都有返回值-->对应语义分析  
2.表达式暂时未支持 string 类型，是否会报错-->已处理  
3.未处理库函数-->已处理  
4.phi 还未生成  
5.int 和 float 范围未检测-->不做处理  
6.数组下标未检测合法(是否大于 0)-->已处理
