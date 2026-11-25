#![no_std]
use core::arch::asm;
use core::panic::PanicInfo;

const TX_APP_MAGIC: u32 = 0x5450_5041;
const SYS_EXIT: u32 = 11;
const SYS_GETKEY: u32 = 9;
const SYS_SLEEP: u32 = 5;
const SYS_WIN_OPEN: u32 = 14;
const SYS_WIN_CLOSE: u32 = 15;
const SYS_WIN_TEXT: u32 = 16;
const SYS_WIN_PRESENT: u32 = 18;
const SYS_WIN_ALIVE: u32 = 20;

#[inline(always)]
unsafe fn syscall(n: u32, b: u32, c: u32, d: u32) -> u32 {
    let ret: u32;
    asm!("int 0x80", inout("eax") n => ret, in("ebx") b, in("ecx") c, in("edx") d);
    ret
}

#[no_mangle]
pub extern "C" fn app_main() {
    let title = b"Rust on FlairOS\0";
    let msg = b"Hello from #![no_std] Rust!\0";
    unsafe {
        let win = syscall(SYS_WIN_OPEN, 14 | (5 << 16), 44 | (8 << 16), title.as_ptr() as u32);
        if win != 0xFFFF_FFFF {
            syscall(SYS_WIN_TEXT, win | (0x70 << 8), 2 | (2 << 16), msg.as_ptr() as u32);
            while syscall(SYS_WIN_ALIVE, win, 0, 0) != 0 {
                syscall(SYS_WIN_PRESENT, 0, 0, 0);
                if syscall(SYS_GETKEY, 0, 0, 0) == 27 { break; }
                syscall(SYS_SLEEP, 1, 0, 0);
            }
            syscall(SYS_WIN_CLOSE, win, 0, 0);
        }
        syscall(SYS_EXIT, 0, 0, 0);
    }
}

#[link_section = ".head"]
#[no_mangle]
pub static APP_HEAD: [u32; 2] = [TX_APP_MAGIC, app_main as u32];

#[panic_handler]
fn panic(_: &PanicInfo) -> ! { loop {} }
