; TinyCPS LLVM IR backend - direct lowering from validated AST
; i64 integers, printf output, lexical scopes, counted loops
source_filename = "tinycps"

@.tinycps.print_format = private unnamed_addr constant [6 x i8] c"%lld\0A\00", align 1

declare i32 @printf(ptr, ...)

define i32 @main() {
entry:
  %var0 = alloca i64 ; let sum
  %var1 = alloca i64 ; repeat counter 0
  %var2 = alloca i64 ; let increment
  %var3 = alloca i64 ; repeat counter 1
  store i64 0, ptr %var0
  store i64 0, ptr %var1
  br label %repeat.0.header
repeat.0.header:
  %t0 = load i64, ptr %var1
  %t1 = icmp slt i64 %t0, 2
  br i1 %t1, label %repeat.0.body, label %repeat.0.end
repeat.0.body:
  store i64 3, ptr %var2
  %t2 = load i64, ptr %var2
  store i64 0, ptr %var3
  br label %repeat.1.header
repeat.1.header:
  %t3 = load i64, ptr %var3
  %t4 = icmp slt i64 %t3, %t2
  br i1 %t4, label %repeat.1.body, label %repeat.1.end
repeat.1.body:
  %t5 = load i64, ptr %var0
  %t6 = add i64 %t5, 1
  store i64 %t6, ptr %var0
  br label %repeat.1.latch
repeat.1.latch:
  %t7 = load i64, ptr %var3
  %t8 = add i64 %t7, 1
  store i64 %t8, ptr %var3
  br label %repeat.1.header
repeat.1.end:
  br label %repeat.0.latch
repeat.0.latch:
  %t9 = load i64, ptr %var1
  %t10 = add i64 %t9, 1
  store i64 %t10, ptr %var1
  br label %repeat.0.header
repeat.0.end:
  %t11 = load i64, ptr %var0
  %t12 = getelementptr inbounds [6 x i8], ptr @.tinycps.print_format, i64 0, i64 0
  call i32 (ptr, ...) @printf(ptr %t12, i64 %t11)
  ret i32 0
}
