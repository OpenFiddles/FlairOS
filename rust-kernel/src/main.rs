#![no_std]
#![no_main]

use core::arch::{asm, global_asm};
use core::panic::PanicInfo;

global_asm!(include_str!("boot.s"));

const VGA: *mut u16 = 0xB8000 as *mut u16;
const MSG: &[u8] = b"AirOS Rust kernel: i686 build OK";

unsafe fn outb(port: u16, value: u8) {
    asm!("out dx, al", in("dx") port, in("al") value, options(nomem, nostack, preserves_flags));
}

#[no_mangle]
pub extern "C" fn kmain() -> ! {
    for (i, &c) in MSG.iter().enumerate() {
        unsafe {
            VGA.add(i).write_volatile(0x0F00 | c as u16);
            outb(0x3F8, c);
        }
    }
    loop {
        unsafe { asm!("hlt", options(nomem, nostack)) }
    }
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {
        unsafe { asm!("hlt", options(nomem, nostack)) }
    }
}
