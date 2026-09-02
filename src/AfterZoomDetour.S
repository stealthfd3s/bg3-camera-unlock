.section __TEXT,__text,regular,pure_instructions
.p2align 2
.globl _BG3AfterZoomDetour

// Entered by an absolute `br`, not `bl`, from the middle of
// UpdateGameCameraBehavior. Every caller-saved general/SIMD register and the
// floating-point/condition state may be live here, so preserve all of them
// around the C++ helper. x19-x29 are callee-saved by the normal AArch64 ABI.
_BG3AfterZoomDetour:
    sub sp, sp, #0x2c0

    stp x0,  x1,  [sp, #0x000]
    stp x2,  x3,  [sp, #0x010]
    stp x4,  x5,  [sp, #0x020]
    stp x6,  x7,  [sp, #0x030]
    stp x8,  x9,  [sp, #0x040]
    stp x10, x11, [sp, #0x050]
    stp x12, x13, [sp, #0x060]
    stp x14, x15, [sp, #0x070]
    stp x16, x17, [sp, #0x080]
    stp x18, x30, [sp, #0x090]

    stp q0,  q1,  [sp, #0x0a0]
    stp q2,  q3,  [sp, #0x0c0]
    stp q4,  q5,  [sp, #0x0e0]
    stp q6,  q7,  [sp, #0x100]
    stp q8,  q9,  [sp, #0x120]
    stp q10, q11, [sp, #0x140]
    stp q12, q13, [sp, #0x160]
    stp q14, q15, [sp, #0x180]
    stp q16, q17, [sp, #0x1a0]
    stp q18, q19, [sp, #0x1c0]
    stp q20, q21, [sp, #0x1e0]
    stp q22, q23, [sp, #0x200]
    stp q24, q25, [sp, #0x220]
    stp q26, q27, [sp, #0x240]
    stp q28, q29, [sp, #0x260]
    stp q30, q31, [sp, #0x280]

    mrs x9, nzcv
    mrs x10, fpcr
    mrs x11, fpsr
    add x12, sp, #0x2a0
    stp x9, x10, [x12]
    str x11, [x12, #0x10]

    // At this verified site x26 is ecl::GameCameraBehavior*.
    mov x0, x26
    bl _BG3AfterZoomApply

    add x12, sp, #0x2a0
    ldp x9, x10, [x12]
    ldr x11, [x12, #0x10]
    msr nzcv, x9
    msr fpcr, x10
    msr fpsr, x11

    ldp q0,  q1,  [sp, #0x0a0]
    ldp q2,  q3,  [sp, #0x0c0]
    ldp q4,  q5,  [sp, #0x0e0]
    ldp q6,  q7,  [sp, #0x100]
    ldp q8,  q9,  [sp, #0x120]
    ldp q10, q11, [sp, #0x140]
    ldp q12, q13, [sp, #0x160]
    ldp q14, q15, [sp, #0x180]
    ldp q16, q17, [sp, #0x1a0]
    ldp q18, q19, [sp, #0x1c0]
    ldp q20, q21, [sp, #0x1e0]
    ldp q22, q23, [sp, #0x200]
    ldp q24, q25, [sp, #0x220]
    ldp q26, q27, [sp, #0x240]
    ldp q28, q29, [sp, #0x260]
    ldp q30, q31, [sp, #0x280]

    ldp x0,  x1,  [sp, #0x000]
    ldp x2,  x3,  [sp, #0x010]
    ldp x4,  x5,  [sp, #0x020]
    ldp x6,  x7,  [sp, #0x030]
    ldp x8,  x9,  [sp, #0x040]
    ldp x10, x11, [sp, #0x050]
    ldp x12, x13, [sp, #0x060]
    ldp x14, x15, [sp, #0x070]
    ldp x16, x17, [sp, #0x080]
    ldp x18, x30, [sp, #0x090]
    add sp, sp, #0x2c0

    // The trampoline executes the four displaced position-independent
    // instructions, then jumps to the original site + 16.
    adrp x16, _BG3AfterZoomTrampoline@PAGE
    ldr x16, [x16, _BG3AfterZoomTrampoline@PAGEOFF]
    br x16
