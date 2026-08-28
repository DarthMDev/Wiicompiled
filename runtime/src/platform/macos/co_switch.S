.text
.align 2

// AArch64 AAPCS cooperative context frame (224 bytes): x19-x30, then v8-v15.
// x0 = address holding the target frame pointer; x1 = address to receive the
// current frame pointer. This is intentionally leaf-only: it never calls C++.
.globl _mkw_co_switch
_mkw_co_switch:
    sub sp, sp, #224
    stp x19, x20, [sp, #0]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    stp q8, q9, [sp, #96]
    stp q10, q11, [sp, #128]
    stp q12, q13, [sp, #160]
    stp q14, q15, [sp, #192]
    mov x2, sp
    str x2, [x1]

    ldr x2, [x0]
    mov sp, x2
    ldp x19, x20, [sp, #0]
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    ldp x29, x30, [sp, #80]
    ldp q8, q9, [sp, #96]
    ldp q10, q11, [sp, #128]
    ldp q12, q13, [sp, #160]
    ldp q14, q15, [sp, #192]
    add sp, sp, #224
    ret

// Creates a frame compatible with mkw_co_switch and returns its saved SP.
// x0 = one-past-end stack pointer, x1 = entry(void*), x2 = entry argument.
.globl _mkw_co_init
_mkw_co_init:
    bic x0, x0, #0xf
    sub x0, x0, #224
    str x1, [x0, #0]       // x19: entry
    str x2, [x0, #8]       // x20: argument
    str xzr, [x0, #80]     // x29
    adrp x3, _mkw_co_entry_trampoline@PAGE
    add x3, x3, _mkw_co_entry_trampoline@PAGEOFF
    str x3, [x0, #88]      // x30
    ret

_mkw_co_entry_trampoline:
    mov x0, x20
    blr x19
    brk #0
