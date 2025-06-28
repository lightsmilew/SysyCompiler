; ModuleID = 'main_module'

@global_int = constant i32 303
@global_float = constant float 0x4048F5C300000000
@space = constant i32 32
@maxN = constant i32 10000
@sorted_array = global [10000 x i32] zeroinitializer

define void @bubble_sort(i32* %arr, i32 %n) {
label0:
  %n.addr = alloca i32
  store i32 %n, i32 %n.addr
  %i = alloca i32
  store i32 0, i32 %i
  %j = alloca i32
  br label %label1
label1:
  %t0 = load i32, i32 %i
  %t1 = load i32, i32 %n.addr
  %t2 = sub i32 %t1, 1
  %t3 = icmp slt i32 %t0, %t2
  br i1 %t3, label %label2, label %label3
label2:
  store i32 0, i32 %j
  br label %label4
label3:
  ret void
label4:
  %t4 = load i32, i32 %j
  %t5 = load i32, i32 %n.addr
  %t6 = sub i32 %t5, 1
  %t7 = icmp slt i32 %t4, %t6
  br i1 %t7, label %label5, label %label6
label5:
  %t8 = load i32, i32 %j
  %t9 = getelementptr i32, i32* %arr, i32 %t8
  %t10 = load i32, i32 %t9
  %t11 = load i32, i32 %j
  %t12 = add i32 %t11, 1
  %t13 = getelementptr i32, i32* %arr, i32 %t12
  %t14 = load i32, i32 %t13
  %t15 = icmp sgt i32 %t10, %t14
  br i1 %t15, label %label7, label %label8
label6:
  %t31 = load i32, i32 %i
  %t32 = add i32 %t31, 1
  store i32 %t32, i32 %i
  br label %label1
label7:
  %temp = alloca i32
  %t16 = load i32, i32 %j
  %t17 = getelementptr i32, i32* %arr, i32 %t16
  %t18 = load i32, i32 %t17
  store i32 %t18, i32 %temp
  %t19 = load i32, i32 %j
  %t20 = getelementptr i32, i32* %arr, i32 %t19
  %t21 = load i32, i32 %j
  %t22 = add i32 %t21, 1
  %t23 = getelementptr i32, i32* %arr, i32 %t22
  %t24 = load i32, i32 %t23
  store i32 %t24, i32 %t20
  %t25 = load i32, i32 %j
  %t26 = add i32 %t25, 1
  %t27 = getelementptr i32, i32* %arr, i32 %t26
  %t28 = load i32, i32 %temp
  store i32 %t28, i32 %t27
  br label %label8
label8:
  %t29 = load i32, i32 %j
  %t30 = add i32 %t29, 1
  store i32 %t30, i32 %j
  br label %label4
}

define i32 @binary_search(i32* %arr, i32 %n, i32 %target) {
label9:
  %n.addr = alloca i32
  store i32 %n, i32 %n.addr
  %target.addr = alloca i32
  store i32 %target, i32 %target.addr
  %low = alloca i32
  store i32 0, i32 %low
  %high = alloca i32
  %t33 = load i32, i32 %n.addr
  %t34 = sub i32 %t33, 1
  store i32 %t34, i32 %high
  br label %label10
label10:
  %t35 = load i32, i32 %low
  %t36 = load i32, i32 %high
  %t37 = icmp sle i32 %t35, %t36
  br i1 %t37, label %label11, label %label12
label11:
  %mid = alloca i32
  %t38 = load i32, i32 %low
  %t39 = load i32, i32 %high
  %t40 = add i32 %t38, %t39
  %t41 = sdiv i32 %t40, 2
  store i32 %t41, i32 %mid
  %t42 = load i32, i32 %mid
  %t43 = getelementptr i32, i32* %arr, i32 %t42
  %t44 = load i32, i32 %t43
  %t45 = load i32, i32 %target.addr
  %t46 = icmp eq i32 %t44, %t45
  br i1 %t46, label %label13, label %label14
label12:
  %t57 = sub i32 0, 1
  ret i32 %t57
label13:
  %t47 = load i32, i32 %mid
  ret i32 %t47
label14:
  %t48 = load i32, i32 %mid
  %t49 = getelementptr i32, i32* %arr, i32 %t48
  %t50 = load i32, i32 %t49
  %t51 = load i32, i32 %target.addr
  %t52 = icmp slt i32 %t50, %t51
  br i1 %t52, label %label16, label %label17
label15:
  br label %label10
label16:
  %t53 = load i32, i32 %mid
  %t54 = add i32 %t53, 1
  store i32 %t54, i32 %low
  br label %label18
label17:
  %t55 = load i32, i32 %mid
  %t56 = sub i32 %t55, 1
  store i32 %t56, i32 %high
  br label %label18
label18:
  br label %label15
}

define void @perform_operations() {
label19:
  %a = alloca i32
  store i32 10, i32 %a
  %b = alloca i32
  store i32 5, i32 %b
  %sum = alloca i32
  %t58 = load i32, i32 %a
  %t59 = load i32, i32 %b
  %t60 = add i32 %t58, %t59
  store i32 %t60, i32 %sum
  %diff = alloca i32
  %t61 = load i32, i32 %a
  %t62 = load i32, i32 %b
  %t63 = sub i32 %t61, %t62
  store i32 %t63, i32 %diff
  %prod = alloca i32
  %t64 = load i32, i32 %a
  %t65 = load i32, i32 %b
  %t66 = mul i32 %t64, %t65
  store i32 %t66, i32 %prod
  %quot = alloca i32
  %t67 = load i32, i32 %a
  %t68 = load i32, i32 %b
  %t69 = sdiv i32 %t67, %t68
  store i32 %t69, i32 %quot
  %mod = alloca i32
  %t70 = load i32, i32 %a
  %t71 = load i32, i32 %b
  %t72 = srem i32 %t70, %t71
  store i32 %t72, i32 %mod
  %t73 = load i32, i32 %sum
  call void @putint(i32 %t73)
  %t75 = load i32, i32 %space
  call void @putch(i32 %t75)
  %t77 = load i32, i32 %diff
  call void @putint(i32 %t77)
  %t79 = load i32, i32 %space
  call void @putch(i32 %t79)
  %t81 = load i32, i32 %prod
  call void @putint(i32 %t81)
  %t83 = load i32, i32 %space
  call void @putch(i32 %t83)
  %t85 = load i32, i32 %quot
  call void @putint(i32 %t85)
  %t87 = load i32, i32 %space
  call void @putch(i32 %t87)
  %t89 = load i32, i32 %mod
  call void @putint(i32 %t89)
  %t91 = load i32, i32 %space
  call void @putch(i32 %t91)
  ret void
}

define void @array_init_and_process() {
label20:
  %i = alloca i32
  store i32 0, i32 %i
  br label %label21
label21:
  %t93 = load i32, i32 %i
  %t94 = load i32, i32 %maxN
  %t95 = icmp slt i32 %t93, %t94
  br i1 %t95, label %label22, label %label23
label22:
  %t96 = load i32, i32 %i
  %t97 = getelementptr [10000 x i32] %sorted_array, i32 0, i32 %t96
  %t98 = load i32, i32 %i
  %t99 = mul i32 %t98, 303
  %t100 = load i32, i32 %maxN
  %t101 = srem i32 %t99, %t100
  store i32 %t101, i32 %t97
  %t102 = load i32, i32 %i
  %t103 = add i32 %t102, 1
  store i32 %t103, i32 %i
  br label %label21
label23:
  %t104 = getelementptr [10000 x i32] %sorted_array, i32 0
  %t105 = load i32, i32 %maxN
  call void @bubble_sort(i32* %t104, i32 %t105)
  %target = alloca i32
  %t107 = load i32, i32 %global_int
  store i32 %t107, i32 %target
  %index = alloca i32
  %t108 = getelementptr [10000 x i32] %sorted_array, i32 0
  %t109 = load i32, i32 %maxN
  %t110 = load i32, i32 %target
  %t111 = call i32 @binary_search(i32* %t108, i32 %t109, i32 %t110)
  store i32 %t111, i32 %index
  %t112 = load i32, i32 %target
  call void @putint(i32 %t112)
  %t114 = load i32, i32 %space
  call void @putch(i32 %t114)
  %t116 = load i32, i32 %index
  call void @putint(i32 %t116)
  %t118 = load i32, i32 %space
  call void @putch(i32 %t118)
  ret void
}

define i32 @main() {
label24:
  call void @starttime()
  call void @array_init_and_process()
  call void @perform_operations()
  call void @stoptime()
  ret i32 0
}


