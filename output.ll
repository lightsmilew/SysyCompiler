; ModuleID = 'test_cases/true_cases/input.cpp'
source_filename = "test_cases/true_cases/input.cpp"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@globalVar = dso_local global i32 100, align 4
@__const._Z23testInitializationCheckv.arr = private unnamed_addr constant [3 x i32] [i32 1, i32 2, i32 3], align 4
@__const._Z23testArrayInitializationv.arr1 = private unnamed_addr constant [5 x i32] [i32 1, i32 2, i32 3, i32 4, i32 5], align 16
@__const._Z23testArrayInitializationv.arr2 = private unnamed_addr constant [5 x i32] [i32 1, i32 2, i32 3, i32 0, i32 0], align 16
@__const._Z23testArrayInitializationv.arr3 = private unnamed_addr constant [5 x i32] [i32 1, i32 2, i32 3, i32 4, i32 5], align 16
@__const._Z23testArrayInitializationv.matrix1 = private unnamed_addr constant [2 x [3 x i32]] [[3 x i32] [i32 1, i32 2, i32 3], [3 x i32] [i32 4, i32 5, i32 6]], align 16
@__const._Z23testArrayInitializationv.matrix2 = private unnamed_addr constant [2 x [3 x i32]] [[3 x i32] [i32 1, i32 2, i32 0], [3 x i32] [i32 4, i32 0, i32 0]], align 16
@globalArray = dso_local global [5 x i32] [i32 1, i32 2, i32 3, i32 4, i32 5], align 16
@globalFloat = dso_local global float 0x40108F5C20000000, align 4
@__const._Z22testArrayInitTypeMatchv.intArray = private unnamed_addr constant [3 x i32] [i32 1, i32 2, i32 3], align 4
@__const._Z22testArrayInitTypeMatchv.floatArray = private unnamed_addr constant [3 x float] [float 1.000000e+00, float 2.000000e+00, float 3.000000e+00], align 4
@__const._Z22testArrayInitTypeMatchv.mixedArray = private unnamed_addr constant [3 x float] [float 1.000000e+00, float 2.000000e+00, float 3.000000e+00], align 4
@__const._Z20testArrayAsParameterv.arr1 = private unnamed_addr constant [5 x i32] [i32 1, i32 2, i32 3, i32 4, i32 5], align 16
@__const._Z20testArrayAsParameterv.matrix = private unnamed_addr constant [2 x [3 x i32]] [[3 x i32] [i32 1, i32 2, i32 3], [3 x i32] [i32 4, i32 5, i32 6]], align 16
@__const._Z11testControlv.arr1 = private unnamed_addr constant [5 x i32] [i32 1, i32 2, i32 3, i32 4, i32 5], align 16
@__const._Z11testControlv.matrix = private unnamed_addr constant [2 x [3 x i32]] [[3 x i32] [i32 1, i32 2, i32 3], [3 x i32] [i32 4, i32 5, i32 6]], align 16
@__const._Z26testArrayElementConversionv.intArray = private unnamed_addr constant [3 x i32] [i32 1, i32 2, i32 3], align 4

; Function Attrs: mustprogress noinline norecurse nounwind optnone uwtable
define dso_local noundef i32 @main() #0 {
  %1 = alloca i32, align 4
  store i32 0, i32* %1, align 4
  ret i32 0
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z20testSymbolResolutionv() #1 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  store i32 10, i32* %1, align 4
  %3 = load i32, i32* %1, align 4
  store i32 %3, i32* %2, align 4
  %4 = load i32, i32* %2, align 4
  ret i32 %4
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z14helperFunctioni(i32 noundef %0) #1 {
  %2 = alloca i32, align 4
  store i32 %0, i32* %2, align 4
  %3 = load i32, i32* %2, align 4
  %4 = mul nsw i32 %3, 2
  ret i32 %4
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z16testFunctionCallv() #1 {
  %1 = call noundef i32 @_Z14helperFunctioni(i32 noundef 5)
  ret i32 %1
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z16testTypeMatchingv() #1 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  %3 = alloca float, align 4
  %4 = alloca float, align 4
  %5 = alloca float, align 4
  store i32 10, i32* %1, align 4
  %6 = load i32, i32* %1, align 4
  store i32 %6, i32* %2, align 4
  store float 0x40091EB860000000, float* %3, align 4
  %7 = load float, float* %3, align 4
  store float %7, float* %4, align 4
  %8 = load i32, i32* %1, align 4
  %9 = sitofp i32 %8 to float
  store float %9, float* %5, align 4
  ret i32 0
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z9acceptInti(i32 noundef %0) #1 {
  %2 = alloca i32, align 4
  store i32 %0, i32* %2, align 4
  %3 = load i32, i32* %2, align 4
  ret i32 %3
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef float @_Z11acceptFloatf(float noundef %0) #1 {
  %2 = alloca float, align 4
  store float %0, float* %2, align 4
  %3 = load float, float* %2, align 4
  ret float %3
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z18testParameterTypesv() #1 {
  %1 = alloca i32, align 4
  %2 = alloca float, align 4
  %3 = alloca i32, align 4
  %4 = alloca float, align 4
  %5 = alloca float, align 4
  store i32 5, i32* %1, align 4
  store float 0x40091EB860000000, float* %2, align 4
  %6 = load i32, i32* %1, align 4
  %7 = call noundef i32 @_Z9acceptInti(i32 noundef %6)
  store i32 %7, i32* %3, align 4
  %8 = load float, float* %2, align 4
  %9 = call noundef float @_Z11acceptFloatf(float noundef %8)
  store float %9, float* %4, align 4
  %10 = load i32, i32* %1, align 4
  %11 = sitofp i32 %10 to float
  %12 = call noundef float @_Z11acceptFloatf(float noundef %11)
  store float %12, float* %5, align 4
  ret i32 0
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef float @_Z17testTypePromotionv() #1 {
  %1 = alloca i32, align 4
  %2 = alloca float, align 4
  %3 = alloca float, align 4
  %4 = alloca float, align 4
  %5 = alloca float, align 4
  %6 = alloca float, align 4
  store i32 10, i32* %1, align 4
  store float 0x40091EB860000000, float* %2, align 4
  %7 = load i32, i32* %1, align 4
  %8 = sitofp i32 %7 to float
  %9 = load float, float* %2, align 4
  %10 = fadd float %8, %9
  store float %10, float* %3, align 4
  %11 = load float, float* %2, align 4
  %12 = load i32, i32* %1, align 4
  %13 = sitofp i32 %12 to float
  %14 = fadd float %11, %13
  store float %14, float* %4, align 4
  %15 = load i32, i32* %1, align 4
  %16 = sitofp i32 %15 to float
  %17 = load float, float* %2, align 4
  %18 = fmul float %16, %17
  store float %18, float* %5, align 4
  %19 = load float, float* %2, align 4
  %20 = load i32, i32* %1, align 4
  %21 = sitofp i32 %20 to float
  %22 = fdiv float %19, %21
  store float %22, float* %6, align 4
  %23 = load float, float* %3, align 4
  %24 = load float, float* %4, align 4
  %25 = fadd float %23, %24
  %26 = load float, float* %5, align 4
  %27 = fadd float %25, %26
  %28 = load float, float* %6, align 4
  %29 = fadd float %27, %28
  ret float %29
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z20testConstCorrectnessv() #1 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  %3 = alloca float, align 4
  %4 = alloca float, align 4
  store i32 10, i32* %1, align 4
  store i32 10, i32* %2, align 4
  store float 0x40091EB860000000, float* %3, align 4
  store float 0x40091EB860000000, float* %4, align 4
  %5 = load i32, i32* %2, align 4
  ret i32 %5
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z15testArrayBoundsv() #1 {
  %1 = alloca [5 x i32], align 16
  %2 = alloca [3 x [4 x i32]], align 16
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = getelementptr inbounds [5 x i32], [5 x i32]* %1, i64 0, i64 0
  store i32 10, i32* %5, align 16
  %6 = getelementptr inbounds [5 x i32], [5 x i32]* %1, i64 0, i64 4
  store i32 20, i32* %6, align 16
  %7 = getelementptr inbounds [3 x [4 x i32]], [3 x [4 x i32]]* %2, i64 0, i64 0
  %8 = getelementptr inbounds [4 x i32], [4 x i32]* %7, i64 0, i64 0
  store i32 30, i32* %8, align 16
  %9 = getelementptr inbounds [3 x [4 x i32]], [3 x [4 x i32]]* %2, i64 0, i64 2
  %10 = getelementptr inbounds [4 x i32], [4 x i32]* %9, i64 0, i64 3
  store i32 40, i32* %10, align 4
  %11 = getelementptr inbounds [5 x i32], [5 x i32]* %1, i64 0, i64 1
  %12 = load i32, i32* %11, align 4
  store i32 %12, i32* %3, align 4
  %13 = getelementptr inbounds [3 x [4 x i32]], [3 x [4 x i32]]* %2, i64 0, i64 1
  %14 = getelementptr inbounds [4 x i32], [4 x i32]* %13, i64 0, i64 2
  %15 = load i32, i32* %14, align 8
  store i32 %15, i32* %4, align 4
  %16 = load i32, i32* %3, align 4
  %17 = load i32, i32* %4, align 4
  %18 = add nsw i32 %16, %17
  ret i32 %18
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z3addii(i32 noundef %0, i32 noundef %1) #1 {
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  store i32 %0, i32* %3, align 4
  store i32 %1, i32* %4, align 4
  %5 = load i32, i32* %3, align 4
  %6 = load i32, i32* %4, align 4
  %7 = add nsw i32 %5, %6
  ret i32 %7
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef float @_Z8multiplyfff(float noundef %0, float noundef %1, float noundef %2) #1 {
  %4 = alloca float, align 4
  %5 = alloca float, align 4
  %6 = alloca float, align 4
  store float %0, float* %4, align 4
  store float %1, float* %5, align 4
  store float %2, float* %6, align 4
  %7 = load float, float* %4, align 4
  %8 = load float, float* %5, align 4
  %9 = fmul float %7, %8
  %10 = load float, float* %6, align 4
  %11 = fmul float %9, %10
  ret float %11
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z21testFunctionSignaturev() #1 {
  %1 = alloca i32, align 4
  %2 = alloca float, align 4
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca float, align 4
  %6 = call noundef i32 @_Z3addii(i32 noundef 5, i32 noundef 10)
  store i32 %6, i32* %1, align 4
  %7 = call noundef float @_Z8multiplyfff(float noundef 1.500000e+00, float noundef 2.000000e+00, float noundef 3.000000e+00)
  store float %7, float* %2, align 4
  store i32 5, i32* %3, align 4
  store i32 10, i32* %4, align 4
  %8 = load i32, i32* %3, align 4
  %9 = sitofp i32 %8 to float
  %10 = load i32, i32* %4, align 4
  %11 = sitofp i32 %10 to float
  %12 = call noundef float @_Z8multiplyfff(float noundef %9, float noundef %11, float noundef 2.000000e+00)
  store float %12, float* %5, align 4
  %13 = load i32, i32* %1, align 4
  %14 = sitofp i32 %13 to float
  %15 = load float, float* %2, align 4
  %16 = fadd float %14, %15
  %17 = load float, float* %5, align 4
  %18 = fadd float %16, %17
  %19 = fptosi float %18 to i32
  ret i32 %19
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z14testScopeRulesv() #1 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  store i32 10, i32* %1, align 4
  store i32 20, i32* %2, align 4
  %6 = load i32, i32* @globalVar, align 4
  %7 = load i32, i32* %1, align 4
  %8 = add nsw i32 %6, %7
  %9 = load i32, i32* %2, align 4
  %10 = add nsw i32 %8, %9
  store i32 %10, i32* %3, align 4
  store i32 30, i32* %4, align 4
  %11 = load i32, i32* @globalVar, align 4
  %12 = load i32, i32* %1, align 4
  %13 = add nsw i32 %11, %12
  %14 = load i32, i32* %2, align 4
  %15 = add nsw i32 %13, %14
  %16 = load i32, i32* %4, align 4
  %17 = add nsw i32 %15, %16
  store i32 %17, i32* %5, align 4
  %18 = load i32, i32* @globalVar, align 4
  %19 = load i32, i32* %1, align 4
  %20 = add nsw i32 %18, %19
  ret i32 %20
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z23testInitializationCheckv() #1 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  %3 = alloca [3 x i32], align 4
  %4 = alloca i32, align 4
  store i32 10, i32* %1, align 4
  %5 = load i32, i32* %1, align 4
  store i32 %5, i32* %2, align 4
  %6 = bitcast [3 x i32]* %3 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 4 %6, i8* align 4 bitcast ([3 x i32]* @__const._Z23testInitializationCheckv.arr to i8*), i64 12, i1 false)
  %7 = getelementptr inbounds [3 x i32], [3 x i32]* %3, i64 0, i64 0
  %8 = load i32, i32* %7, align 4
  store i32 %8, i32* %4, align 4
  %9 = load i32, i32* %2, align 4
  %10 = load i32, i32* %4, align 4
  %11 = add nsw i32 %9, %10
  ret i32 %11
}

; Function Attrs: argmemonly nofree nounwind willreturn
declare void @llvm.memcpy.p0i8.p0i8.i64(i8* noalias nocapture writeonly, i8* noalias nocapture readonly, i64, i1 immarg) #2

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z26testExpressionRestrictionsv() #1 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  store i32 10, i32* %1, align 4
  store i32 20, i32* %2, align 4
  %7 = load i32, i32* %1, align 4
  %8 = load i32, i32* %2, align 4
  %9 = add nsw i32 %7, %8
  store i32 %9, i32* %3, align 4
  %10 = load i32, i32* %1, align 4
  %11 = load i32, i32* %2, align 4
  %12 = mul nsw i32 %10, %11
  store i32 %12, i32* %4, align 4
  %13 = load i32, i32* %1, align 4
  %14 = sub nsw i32 0, %13
  store i32 %14, i32* %5, align 4
  %15 = load i32, i32* %2, align 4
  store i32 %15, i32* %6, align 4
  %16 = load i32, i32* %3, align 4
  %17 = load i32, i32* %4, align 4
  %18 = add nsw i32 %16, %17
  %19 = load i32, i32* %5, align 4
  %20 = add nsw i32 %18, %19
  %21 = load i32, i32* %6, align 4
  %22 = add nsw i32 %20, %21
  ret i32 %22
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local void @_Z14testVoidReturnv() #1 {
  %1 = alloca i32, align 4
  store i32 10, i32* %1, align 4
  %2 = load i32, i32* %1, align 4
  %3 = icmp sgt i32 %2, 5
  br i1 %3, label %4, label %5

4:                                                ; preds = %0
  br label %6

5:                                                ; preds = %0
  br label %6

6:                                                ; preds = %5, %4
  ret void
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z21testArraySizeConstantv() #1 {
  %1 = alloca i32, align 4
  %2 = alloca [5 x i32], align 16
  %3 = alloca [5 x i32], align 16
  %4 = alloca [5 x i32], align 16
  store i32 5, i32* %1, align 4
  %5 = getelementptr inbounds [5 x i32], [5 x i32]* %2, i64 0, i64 0
  %6 = load i32, i32* %5, align 16
  %7 = getelementptr inbounds [5 x i32], [5 x i32]* %3, i64 0, i64 0
  %8 = load i32, i32* %7, align 16
  %9 = add nsw i32 %6, %8
  %10 = getelementptr inbounds [5 x i32], [5 x i32]* %4, i64 0, i64 0
  %11 = load i32, i32* %10, align 16
  %12 = add nsw i32 %9, %11
  ret i32 %12
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z23testArrayInitializationv() #1 {
  %1 = alloca [5 x i32], align 16
  %2 = alloca [5 x i32], align 16
  %3 = alloca [5 x i32], align 16
  %4 = alloca [2 x [3 x i32]], align 16
  %5 = alloca [2 x [3 x i32]], align 16
  %6 = bitcast [5 x i32]* %1 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 16 %6, i8* align 16 bitcast ([5 x i32]* @__const._Z23testArrayInitializationv.arr1 to i8*), i64 20, i1 false)
  %7 = bitcast [5 x i32]* %2 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 16 %7, i8* align 16 bitcast ([5 x i32]* @__const._Z23testArrayInitializationv.arr2 to i8*), i64 20, i1 false)
  %8 = bitcast [5 x i32]* %3 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 16 %8, i8* align 16 bitcast ([5 x i32]* @__const._Z23testArrayInitializationv.arr3 to i8*), i64 20, i1 false)
  %9 = bitcast [2 x [3 x i32]]* %4 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 16 %9, i8* align 16 bitcast ([2 x [3 x i32]]* @__const._Z23testArrayInitializationv.matrix1 to i8*), i64 24, i1 false)
  %10 = bitcast [2 x [3 x i32]]* %5 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 16 %10, i8* align 16 bitcast ([2 x [3 x i32]]* @__const._Z23testArrayInitializationv.matrix2 to i8*), i64 24, i1 false)
  %11 = getelementptr inbounds [5 x i32], [5 x i32]* %1, i64 0, i64 0
  %12 = load i32, i32* %11, align 16
  %13 = getelementptr inbounds [5 x i32], [5 x i32]* %2, i64 0, i64 0
  %14 = load i32, i32* %13, align 16
  %15 = add nsw i32 %12, %14
  %16 = getelementptr inbounds [5 x i32], [5 x i32]* %3, i64 0, i64 0
  %17 = load i32, i32* %16, align 16
  %18 = add nsw i32 %15, %17
  %19 = getelementptr inbounds [2 x [3 x i32]], [2 x [3 x i32]]* %4, i64 0, i64 0
  %20 = getelementptr inbounds [3 x i32], [3 x i32]* %19, i64 0, i64 0
  %21 = load i32, i32* %20, align 16
  %22 = add nsw i32 %18, %21
  %23 = getelementptr inbounds [2 x [3 x i32]], [2 x [3 x i32]]* %5, i64 0, i64 0
  %24 = getelementptr inbounds [3 x i32], [3 x i32]* %23, i64 0, i64 0
  %25 = load i32, i32* %24, align 16
  %26 = add nsw i32 %22, %25
  ret i32 %26
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z22testConstantExpressionv() #1 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca float, align 4
  %5 = alloca float, align 4
  store i32 10, i32* %1, align 4
  store i32 8, i32* %2, align 4
  store i32 18, i32* %3, align 4
  store float 0x40091EB860000000, float* %4, align 4
  store float 0x40191EB860000000, float* %5, align 4
  ret i32 45
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z22testArrayInitTypeMatchv() #1 {
  %1 = alloca [3 x i32], align 4
  %2 = alloca [3 x float], align 4
  %3 = alloca [3 x float], align 4
  %4 = bitcast [3 x i32]* %1 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 4 %4, i8* align 4 bitcast ([3 x i32]* @__const._Z22testArrayInitTypeMatchv.intArray to i8*), i64 12, i1 false)
  %5 = bitcast [3 x float]* %2 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 4 %5, i8* align 4 bitcast ([3 x float]* @__const._Z22testArrayInitTypeMatchv.floatArray to i8*), i64 12, i1 false)
  %6 = bitcast [3 x float]* %3 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 4 %6, i8* align 4 bitcast ([3 x float]* @__const._Z22testArrayInitTypeMatchv.mixedArray to i8*), i64 12, i1 false)
  %7 = getelementptr inbounds [3 x i32], [3 x i32]* %1, i64 0, i64 0
  %8 = load i32, i32* %7, align 4
  %9 = sitofp i32 %8 to float
  %10 = getelementptr inbounds [3 x float], [3 x float]* %2, i64 0, i64 0
  %11 = load float, float* %10, align 4
  %12 = fadd float %9, %11
  %13 = getelementptr inbounds [3 x float], [3 x float]* %3, i64 0, i64 0
  %14 = load float, float* %13, align 4
  %15 = fadd float %12, %14
  %16 = fptosi float %15 to i32
  ret i32 %16
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z12processArrayPi(i32* noundef %0) #1 {
  %2 = alloca i32*, align 8
  store i32* %0, i32** %2, align 8
  %3 = load i32*, i32** %2, align 8
  %4 = getelementptr inbounds i32, i32* %3, i64 0
  %5 = load i32, i32* %4, align 4
  %6 = load i32*, i32** %2, align 8
  %7 = getelementptr inbounds i32, i32* %6, i64 1
  %8 = load i32, i32* %7, align 4
  %9 = add nsw i32 %5, %8
  ret i32 %9
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z14process2DArrayPA3_i([3 x i32]* noundef %0) #1 {
  %2 = alloca [3 x i32]*, align 8
  store [3 x i32]* %0, [3 x i32]** %2, align 8
  %3 = load [3 x i32]*, [3 x i32]** %2, align 8
  %4 = getelementptr inbounds [3 x i32], [3 x i32]* %3, i64 0
  %5 = getelementptr inbounds [3 x i32], [3 x i32]* %4, i64 0, i64 0
  %6 = load i32, i32* %5, align 4
  %7 = load [3 x i32]*, [3 x i32]** %2, align 8
  %8 = getelementptr inbounds [3 x i32], [3 x i32]* %7, i64 0
  %9 = getelementptr inbounds [3 x i32], [3 x i32]* %8, i64 0, i64 1
  %10 = load i32, i32* %9, align 4
  %11 = add nsw i32 %6, %10
  ret i32 %11
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z20testArrayAsParameterv() #1 {
  %1 = alloca [5 x i32], align 16
  %2 = alloca [2 x [3 x i32]], align 16
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = bitcast [5 x i32]* %1 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 16 %5, i8* align 16 bitcast ([5 x i32]* @__const._Z20testArrayAsParameterv.arr1 to i8*), i64 20, i1 false)
  %6 = bitcast [2 x [3 x i32]]* %2 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 16 %6, i8* align 16 bitcast ([2 x [3 x i32]]* @__const._Z20testArrayAsParameterv.matrix to i8*), i64 24, i1 false)
  %7 = getelementptr inbounds [5 x i32], [5 x i32]* %1, i64 0, i64 0
  %8 = call noundef i32 @_Z12processArrayPi(i32* noundef %7)
  store i32 %8, i32* %3, align 4
  %9 = getelementptr inbounds [2 x [3 x i32]], [2 x [3 x i32]]* %2, i64 0, i64 0
  %10 = call noundef i32 @_Z14process2DArrayPA3_i([3 x i32]* noundef %9)
  store i32 %10, i32* %4, align 4
  store i32 1, i32* %3, align 4
  store i32 1, i32* %4, align 4
  %11 = load i32, i32* %3, align 4
  %12 = load i32, i32* %4, align 4
  %13 = add nsw i32 %11, %12
  ret i32 %13
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local void @_Z11testControlv() #1 {
  %1 = alloca [5 x i32], align 16
  %2 = alloca [2 x [3 x i32]], align 16
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  %7 = bitcast [5 x i32]* %1 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 16 %7, i8* align 16 bitcast ([5 x i32]* @__const._Z11testControlv.arr1 to i8*), i64 20, i1 false)
  %8 = bitcast [2 x [3 x i32]]* %2 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 16 %8, i8* align 16 bitcast ([2 x [3 x i32]]* @__const._Z11testControlv.matrix to i8*), i64 24, i1 false)
  store i32 0, i32* %3, align 4
  store i32 0, i32* %4, align 4
  %9 = getelementptr inbounds [5 x i32], [5 x i32]* %1, i64 0, i64 1
  %10 = load i32, i32* %9, align 4
  %11 = getelementptr inbounds [2 x [3 x i32]], [2 x [3 x i32]]* %2, i64 0, i64 0
  %12 = getelementptr inbounds [3 x i32], [3 x i32]* %11, i64 0, i64 1
  %13 = load i32, i32* %12, align 4
  %14 = icmp sgt i32 %10, %13
  br i1 %14, label %15, label %22

15:                                               ; preds = %0
  store i32 0, i32* %5, align 4
  %16 = getelementptr inbounds [5 x i32], [5 x i32]* %1, i64 0, i64 1
  %17 = load i32, i32* %16, align 4
  %18 = add nsw i32 %17, 1
  %19 = getelementptr inbounds [5 x i32], [5 x i32]* %1, i64 0, i64 1
  store i32 %18, i32* %19, align 4
  %20 = load i32, i32* %3, align 4
  %21 = add nsw i32 %20, 1
  store i32 %21, i32* %3, align 4
  br label %29

22:                                               ; preds = %0
  %23 = getelementptr inbounds [5 x i32], [5 x i32]* %1, i64 0, i64 1
  %24 = load i32, i32* %23, align 4
  %25 = sub nsw i32 %24, 1
  %26 = getelementptr inbounds [5 x i32], [5 x i32]* %1, i64 0, i64 1
  store i32 %25, i32* %26, align 4
  %27 = load i32, i32* %3, align 4
  %28 = sub nsw i32 %27, 1
  store i32 %28, i32* %3, align 4
  br label %29

29:                                               ; preds = %22, %15
  %30 = load i32, i32* %3, align 4
  store i32 %30, i32* %4, align 4
  store i32 0, i32* %6, align 4
  br label %31

31:                                               ; preds = %50, %29
  %32 = getelementptr inbounds [5 x i32], [5 x i32]* %1, i64 0, i64 4
  %33 = load i32, i32* %32, align 16
  %34 = icmp sgt i32 %33, 0
  br i1 %34, label %35, label %51

35:                                               ; preds = %31
  %36 = getelementptr inbounds [5 x i32], [5 x i32]* %1, i64 0, i64 4
  %37 = load i32, i32* %36, align 16
  %38 = sub nsw i32 %37, 1
  %39 = getelementptr inbounds [5 x i32], [5 x i32]* %1, i64 0, i64 4
  store i32 %38, i32* %39, align 16
  %40 = getelementptr inbounds [5 x i32], [5 x i32]* %1, i64 0, i64 3
  %41 = load i32, i32* %40, align 4
  %42 = add nsw i32 %41, 1
  %43 = getelementptr inbounds [5 x i32], [5 x i32]* %1, i64 0, i64 3
  store i32 %42, i32* %43, align 4
  %44 = load i32, i32* %6, align 4
  %45 = add nsw i32 %44, 1
  store i32 %45, i32* %6, align 4
  %46 = getelementptr inbounds [5 x i32], [5 x i32]* %1, i64 0, i64 3
  %47 = load i32, i32* %46, align 4
  %48 = icmp sgt i32 %47, 8
  br i1 %48, label %49, label %50

49:                                               ; preds = %35
  br label %51

50:                                               ; preds = %35
  br label %31, !llvm.loop !6

51:                                               ; preds = %49, %31
  ret void
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z19testArrayDimensionsv() #1 {
  %1 = alloca [5 x i32], align 16
  %2 = alloca [1 x i32], align 4
  %3 = alloca [3 x [4 x i32]], align 16
  %4 = getelementptr inbounds [5 x i32], [5 x i32]* %1, i64 0, i64 0
  %5 = load i32, i32* %4, align 16
  %6 = getelementptr inbounds [1 x i32], [1 x i32]* %2, i64 0, i64 0
  %7 = load i32, i32* %6, align 4
  %8 = add nsw i32 %5, %7
  %9 = getelementptr inbounds [3 x [4 x i32]], [3 x [4 x i32]]* %3, i64 0, i64 0
  %10 = getelementptr inbounds [4 x i32], [4 x i32]* %9, i64 0, i64 0
  %11 = load i32, i32* %10, align 16
  %12 = add nsw i32 %8, %11
  ret i32 %12
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z26testArrayElementConversionv() #1 {
  %1 = alloca [3 x i32], align 4
  %2 = alloca float, align 4
  %3 = bitcast [3 x i32]* %1 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 4 %3, i8* align 4 bitcast ([3 x i32]* @__const._Z26testArrayElementConversionv.intArray to i8*), i64 12, i1 false)
  %4 = getelementptr inbounds [3 x i32], [3 x i32]* %1, i64 0, i64 0
  %5 = load i32, i32* %4, align 4
  %6 = sitofp i32 %5 to float
  store float %6, float* %2, align 4
  %7 = getelementptr inbounds [3 x i32], [3 x i32]* %1, i64 0, i64 1
  %8 = load i32, i32* %7, align 4
  %9 = sitofp i32 %8 to double
  %10 = fadd double %9, 2.500000e+00
  %11 = fptrunc double %10 to float
  store float %11, float* %2, align 4
  %12 = load float, float* %2, align 4
  %13 = fptosi float %12 to i32
  ret i32 %13
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local noundef i32 @_Z11runAllTestsv() #1 {
  %1 = alloca i32, align 4
  store i32 0, i32* %1, align 4
  %2 = load i32, i32* %1, align 4
  %3 = call noundef i32 @_Z20testSymbolResolutionv()
  %4 = add nsw i32 %2, %3
  store i32 %4, i32* %1, align 4
  %5 = load i32, i32* %1, align 4
  %6 = call noundef i32 @_Z16testTypeMatchingv()
  %7 = add nsw i32 %5, %6
  store i32 %7, i32* %1, align 4
  %8 = load i32, i32* %1, align 4
  %9 = sitofp i32 %8 to float
  %10 = call noundef float @_Z17testTypePromotionv()
  %11 = fadd float %9, %10
  %12 = fptosi float %11 to i32
  store i32 %12, i32* %1, align 4
  %13 = load i32, i32* %1, align 4
  %14 = call noundef i32 @_Z20testConstCorrectnessv()
  %15 = add nsw i32 %13, %14
  store i32 %15, i32* %1, align 4
  %16 = load i32, i32* %1, align 4
  %17 = call noundef i32 @_Z15testArrayBoundsv()
  %18 = add nsw i32 %16, %17
  store i32 %18, i32* %1, align 4
  %19 = load i32, i32* %1, align 4
  %20 = call noundef i32 @_Z21testFunctionSignaturev()
  %21 = add nsw i32 %19, %20
  store i32 %21, i32* %1, align 4
  %22 = load i32, i32* %1, align 4
  %23 = call noundef i32 @_Z14testScopeRulesv()
  %24 = add nsw i32 %22, %23
  store i32 %24, i32* %1, align 4
  %25 = load i32, i32* %1, align 4
  %26 = call noundef i32 @_Z23testInitializationCheckv()
  %27 = add nsw i32 %25, %26
  store i32 %27, i32* %1, align 4
  %28 = load i32, i32* %1, align 4
  %29 = call noundef i32 @_Z26testExpressionRestrictionsv()
  %30 = add nsw i32 %28, %29
  store i32 %30, i32* %1, align 4
  call void @_Z14testVoidReturnv()
  %31 = load i32, i32* %1, align 4
  %32 = call noundef i32 @_Z21testArraySizeConstantv()
  %33 = add nsw i32 %31, %32
  store i32 %33, i32* %1, align 4
  %34 = load i32, i32* %1, align 4
  %35 = call noundef i32 @_Z23testArrayInitializationv()
  %36 = add nsw i32 %34, %35
  store i32 %36, i32* %1, align 4
  %37 = load i32, i32* %1, align 4
  %38 = call noundef i32 @_Z22testConstantExpressionv()
  %39 = add nsw i32 %37, %38
  store i32 %39, i32* %1, align 4
  %40 = load i32, i32* %1, align 4
  %41 = call noundef i32 @_Z22testArrayInitTypeMatchv()
  %42 = add nsw i32 %40, %41
  store i32 %42, i32* %1, align 4
  %43 = load i32, i32* %1, align 4
  %44 = call noundef i32 @_Z20testArrayAsParameterv()
  %45 = add nsw i32 %43, %44
  store i32 %45, i32* %1, align 4
  %46 = load i32, i32* %1, align 4
  %47 = call noundef i32 @_Z19testArrayDimensionsv()
  %48 = add nsw i32 %46, %47
  store i32 %48, i32* %1, align 4
  %49 = load i32, i32* %1, align 4
  %50 = call noundef i32 @_Z26testArrayElementConversionv()
  %51 = add nsw i32 %49, %50
  store i32 %51, i32* %1, align 4
  %52 = load i32, i32* %1, align 4
  ret i32 %52
}

attributes #0 = { mustprogress noinline norecurse nounwind optnone uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { mustprogress noinline nounwind optnone uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #2 = { argmemonly nofree nounwind willreturn }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 7, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 1}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"Ubuntu clang version 14.0.0-1ubuntu1.1"}
!6 = distinct !{!6, !7}
!7 = !{!"llvm.loop.mustprogress"}
