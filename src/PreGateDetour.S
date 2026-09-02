.section __TEXT,__text,regular,pure_instructions
.p2align 2
.globl _BG3PreGateDetour

// Entered by an absolute `br`, not `bl`, from inside
// CameraSystem::UpdateGameCameraBehavior at preferred 0x10333e040 (+0x037bc),
// which dominates the gate that decides whether the rotation channel is read
// at all:
//
//   0x10333e040  ldr  x20, [x23, #0x00]   <- displaced
//   0x10333e044  ldr  x19, [x23, #0x30]   <- displaced
//   0x10333e048  ldr  x21, [x23, #0x60]   <- displaced
//   0x10333e04c  ldr  s0,  [x26, #0xe0]   <- displaced
//   ...
//   0x10333e1e4  ldrb w8,  [x26, #0xa9]   <- the gate reads the flag here
//   0x10333e1e8  tbnz w8,  #0, 0x10333e458
//
// x26 holds the ecl::GameCameraBehavior of the current frame: it is set by
// `mov x26, x19` at 0x10333e034 and is not written again anywhere between
// there and the gate, which reads the same register.
//
// This build's helper is read-only. It records what it sees and returns; it
// writes nothing to the camera and drives no rotation.
//
// Register safety, stated exactly rather than as "preserves all registers":
//
//   x0-x15, x17, x18, x30 and q0-q31 are saved and restored here, together
//   with NZCV, FPCR and FPSR. x19-x29 are callee-saved by the AArch64 ABI and
//   are not touched by the helper.
//
//   x16 is NOT preserved, and cannot be: the inline patch this detour is
//   reached through is `ldr x16, literal; br x16`, so x16 already holds the
//   detour's own address before the first instruction here runs.
//
//   That is safe only because x16 is dead at 0x10333e040 in this build. The
//   proof is specific to this address and was redone from the binary rather
//   than carried over from the pre-yaw site:
//     - after the patched window the first x16 access of any kind is a write,
//       `lsl x16, x14, #2` at 0x103343ad8; no read precedes it;
//     - the lowest address any backward branch from at or after this site
//       reaches is 0x10333bd78, and the x16 read at 0x10333ae54 lies below
//       it, so no path from here reaches that read;
//     - the one reachable read below the site, 0x10333c824, sits four bytes
//       after its own defining `ldrh w16, [x14, x12, lsl #1]`, with no branch
//       target between the two, and is not itself a branch target;
//     - the function's single indirect branch is a jump table whose targets
//       are provably at or above 0x103341f08, far past this site.
//
//   None of that carries to another game build. A future build must have its
//   x16 liveness and its branch targets proven again from its own binary; a
//   matching byte pattern is not evidence of either.
_BG3PreGateDetour:
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
    bl _BG3PreGateApply

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
    // instructions exactly once, then jumps to the original site + 16. x16 is
    // spent again here for the same reason it could not be preserved above.
    adrp x16, _BG3PreGateTrampoline@PAGE
    ldr x16, [x16, _BG3PreGateTrampoline@PAGEOFF]
    br x16
