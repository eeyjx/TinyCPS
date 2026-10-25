; TinyCPS LLVM IR backend - direct lowering from validated AST
; i64 integers, printf output, lexical scopes, counted loops
source_filename = "tinycps"

@.tinycps.print_format = private unnamed_addr constant [6 x i8] c"%lld\0A\00", align 1

declare i32 @printf(ptr, ...)

define i32 @main() {
entry:
  %var0 = alloca i64 ; let total
  %var1 = alloca i64 ; let step
  %var2 = alloca i64 ; repeat counter 0
  %var3 = alloca i64 ; let current
  store i64 0, ptr %var0
  store i64 3, ptr %var1
  store i64 0, ptr %var2
  br label %repeat.0.header
repeat.0.header:
  %t0 = load i64, ptr %var2
  %t1 = icmp slt i64 %t0, 4
  br i1 %t1, label %repeat.0.body, label %repeat.0.end
repeat.0.body:
  %t2 = load i64, ptr %var0
  %t3 = load i64, ptr %var1
  %t4 = add i64 %t2, %t3
  store i64 %t4, ptr %var0
  %t5 = load i64, ptr %var0
  store i64 %t5, ptr %var3
  %t6 = load i64, ptr %var3
  %t7 = getelementptr inbounds [6 x i8], ptr @.tinycps.print_format, i64 0, i64 0
  call i32 (ptr, ...) @printf(ptr %t7, i64 %t6)
  br label %repeat.0.latch
repeat.0.latch:
  %t8 = load i64, ptr %var2
  %t9 = add i64 %t8, 1
  store i64 %t9, ptr %var2
  br label %repeat.0.header
repeat.0.end:
  %t10 = load i64, ptr %var0
  %t11 = mul i64 2, 5
  %t12 = add i64 %t10, %t11
  %t13 = getelementptr inbounds [6 x i8], ptr @.tinycps.print_format, i64 0, i64 0
  call i32 (ptr, ...) @printf(ptr %t13, i64 %t12)
  %t14 = load i64, ptr %var0
  %t15 = getelementptr inbounds [6 x i8], ptr @.tinycps.print_format, i64 0, i64 0
  call i32 (ptr, ...) @printf(ptr %t15, i64 %t14)
  ret i32 0
}
