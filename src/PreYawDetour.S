.section __TEXT,__text,regular,pure_instructions
.p2align 2
.globl _BG3PreYawDetour

// Entered by an absolute `br`, not `bl`, from inside
// CameraSystem::UpdateGameCameraBehavior, immediately before the update reads
// its rotation input channel:
//
//   add   x8,  x26, #0x9c      <- displaced, this site
//   ldrb  w9,  [x26, #0xa9]    <- displaced
//   add   x10, x26, #0xa0      <- displaced
//   tst   w9,  #0x1            <- displaced
//   csel  x8,  x8, x10
//   ldr   s9,  [x8]            <- the channel is consumed here
//
// At this site x26 is the ecl::GameCameraBehavior the update is about to read
// the channel from. That is the whole reason this site was chosen over the
// post-zoom one: it is the only point that yields the current frame's object
// before the value it owns is used.
//
// Register safety, stated exactly rather than as "preserves all registers":
//
//   x0-x15, x17, x18, x30 and q0-q31 are saved and restored here, together
//   with NZCV, FPCR and FPSR. x19-x29 are callee-saved by the AArch64 ABI and
//   are not touched by the helper.
//
//   x16 is NOT preserved. It cannot be: the inline patch this detour is
//   reached through is `ldr x16, literal; br x16`, so x16 already holds the
//   detour's own address by the time the first instruction here runs. The
//   original value is gone before any code here could save it, and the value
//   restored below is the patch's, not the caller's.
//
//   That is safe only because x16 is dead at this exact site in this exact
//   build, which was established from the binary rather than assumed:
//     - every read of x16 reachable from this site is preceded on all paths
//       by a write to x16 (the nearest is `lsl x16, x14, #2` at site+0x5680,
//       and it is the first x16 access of any kind after the patch);
//     - the one x16 read below the site that backward branches can reach sits
//       four bytes after its own defining `ldrh w16, [x14, x12, lsl #1]`, with
//       no branch target between the two and none targeting the read itself;
//     - the remaining read is below the lowest address any backward branch
//       from this site reaches.
//   If a game update moves this code, that proof does not carry over. The
//   byte pattern guarding the site is deliberately wider than the patch so a
//   changed neighbourhood fails to match rather than silently re-enabling an
//   unproven assumption.
_BG3PreYawDetour:
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

    mov x0, x26
    bl _BG3PreYawApply

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
    // instructions, then jumps to the original site + 16. x16 is spent again
    // here for the same reason it could not be preserved above.
    adrp x16, _BG3PreYawTrampoline@PAGE
    ldr x16, [x16, _BG3PreYawTrampoline@PAGEOFF]
    br x16
