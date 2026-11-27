/* SPDX-License-Identifier: AGPL-3.0-only
 *
 * Hand the machine to the FlairOS kernel from a UEFI application: mask
 * interrupts, give the kernel a stack, turn the MMU and caches off at EL1 and
 * branch to its entry point with a clean register state.
 */
    .section .text, "ax"
    .global efi_handoff
    .type efi_handoff, %function
efi_handoff:
    /* x0 = entry, x1 = stack top */
    msr daifset, #0xf
    mov x19, x0
    mov sp, x1

    mrs x2, sctlr_el1
    bic x2, x2, #(1 << 0)      /* M: MMU off */
    bic x2, x2, #(1 << 2)      /* C: data cache off */
    bic x2, x2, #(1 << 12)     /* I: instruction cache off */
    msr sctlr_el1, x2
    isb
    tlbi vmalle1
    dsb sy
    isb

    mov x0, #0
    mov x1, #0
    mov x2, #0
    mov x3, #0
    br x19
1:
    wfe
    b 1b
    .size efi_handoff, . - efi_handoff
