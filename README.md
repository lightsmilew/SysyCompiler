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
cmake ..
make
```
