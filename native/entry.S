.section .text.entry,"ax",%progbits
.global loop_entry
.type loop_entry,%function
loop_entry:
    mov x0, x20
    mov x1, x19
    mov x2, x22
    mov x3, x21
    bl loop_fallback
    mov w21, w0
    b stock_cleanup
.size loop_entry, .-loop_entry
