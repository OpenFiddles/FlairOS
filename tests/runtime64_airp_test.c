#include <stdio.h>
#include <string.h>
#include "kmem64.h"
#include "vfs.h"
#include "apk_samples.h"
#include "jvm_sample.h"
#include "../ported/developer/openjdk/include/jvm64.h"
#include "../ported/developer/toybox/toybox_mini.h"
#include "../ported/developer/kilo/kilo_core.h"
#include "../runtime/art/art_bridge.h"
#include "../net/airp/airp_net.h"
#include "../ui/shells/shell_common.h"
#include "../kernel/pkg/elf64.h"
#include "../kernel/core/syscall64.h"
#include "../runtime/art/surface_flinger.h"
#include "../net/bridge/host_bridge.h"
#include "../ui/shells/flair_browser.h"
#include "../runtime/art/dex_interp.h"
#include "../runtime/art/android_libs.h"
#include "../runtime/art/sandbox.h"
#include "../ported/developer/libs/flair_zlib/flair_zlib.h"
#include "../net/service/net_service.h"
#include "../net/bt/bt_core.h"
#include "bt_mock_hci.h"
#include "../ui/systemui/airstin_ui.h"
#include "../ported/python/flair_py.h"

#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static char tb_out[128];
static void tb_cap(const char *line, void *ctx) {
    (void)ctx;
    if (!tb_out[0]) snprintf(tb_out, sizeof(tb_out), "%s", line);
}
static uint8_t test_heap[1024u * 1024u] __attribute__((aligned(16)));
static uint32_t test_fb[1024u * 768u];

int main(void) {
    /* 1. 64-bit aligned kmem64 allocator */
    kmem64_init(test_heap, sizeof(test_heap));
    CHECK(vfs_init() == 0);
    void *a = kmem64_alloc(128);
    void *b = kmem64_alloc(256);
    CHECK(a != 0 && b != 0);
    CHECK((((uintptr_t)a) & 15u) == 0);
    CHECK((((uintptr_t)b) & 15u) == 0);
    kfree(a);
    kfree(b);
    CHECK(kmem64_used_bytes() == 0);

    /* 2. ART Bridge + PackageManager + ActivityManager + Binder + Ashmem */
    art_bridge_init("arm64-v8a");
    CHECK(strcmp(art_bridge_abi(), "arm64-v8a") == 0);
    int p0 = art_pm_register_apk(hello_apk_bytes, (uint32_t)sizeof(hello_apk_bytes), 0xFFu);
    int p1 = art_pm_register_apk(calc_apk_bytes, (uint32_t)sizeof(calc_apk_bytes), 0xFFu);
    int p2 = art_pm_register_apk(sysinfo_apk_bytes, (uint32_t)sizeof(sysinfo_apk_bytes), 0xFFu);
    CHECK(p0 >= 0 && p1 >= 0 && p2 >= 0);
    CHECK(art_pm_count() == 3);
    CHECK(art_pm_get(p0)->info.is_user_test_app == 1);
    CHECK(art_pm_get(p2)->info.is_system_app == 1);

    CHECK(art_am_launch_intent("android.intent.action.MAIN", "com.flairos.helloworld") == 0);
    CHECK(art_am_active() != 0);
    CHECK(art_am_active()->state == ART_STATE_RESUMED);
    art_am_trigger_button(0);
    CHECK(strstr(art_am_active()->action_status, "Binder IPC:") != 0);
    art_am_pause();
    CHECK(art_am_active()->state == ART_STATE_PAUSED);
    art_am_resume();
    CHECK(art_am_active()->state == ART_STATE_RESUMED);
    CHECK(art_notif_count() >= 2);

    /* 3. Native AirP state machine + PIN + error recovery */
    airp_net_init("TestNode", AIRP_ROLE_DUPLEX);
    CHECK(airp_session_status()->state == AIRP_STATE_IDLE);
    CHECK(airp_discover_start() == 1);
    CHECK(airp_pair_peer(0, 9999u) != 0);
    CHECK(airp_session_status()->state == AIRP_STATE_ERROR);
    airp_recover();
    CHECK(airp_session_status()->state == AIRP_STATE_IDLE);
    CHECK(airp_pair_peer(0, 4242u) == 0);
    CHECK(airp_session_status()->state == AIRP_STATE_CONNECTED);
    CHECK(airp_start_stream(AIRP_PAY_SCREEN, "Casting", 4096u) == 0);
    CHECK(airp_send_frame(2048u) == 0);
    CHECK(airp_session_status()->state == AIRP_STATE_STREAMING);
    CHECK(airp_session_status()->bytes_transferred == 6144u);

    /* 4. Offscreen shell render check */
    struct ag_surface surf = { .pixels = test_fb, .width = 1024, .height = 768, .pitch = 1024 * 4, .format = AG_PIXEL_XRGB8888 };
    struct shell_boot_info binfo = { "aarch64", "desktop", 1024, 768 };
    shell_render(&surf, &binfo, 0);
    CHECK(test_fb[0] != 0);
    /* 4b. Desktop Calculator + Control Center interaction check */
    {
        struct input64_state in = {0};
        /* Click Calculator Dock icon (index 3: dx=264, ix=264+22+3*76=514, dy=678) */
        in.mouse_x = 530; in.mouse_y = 700; in.clicked = 1;
        CHECK(shell_handle_input(&in, 1024, 768) == 1);
        {
            int ticks = 0;
            while (shell_tick() && ticks < 64) ticks++;
            CHECK(ticks < 64);
        }
        in.clicked = 0; in.key = '7'; CHECK(shell_handle_input(&in, 1024, 768) == 1);
        in.key = '*'; CHECK(shell_handle_input(&in, 1024, 768) == 1);
        in.key = '6'; CHECK(shell_handle_input(&in, 1024, 768) == 1);
        in.key = '='; CHECK(shell_handle_input(&in, 1024, 768) == 1);
        /* Click top-right Control Center status cluster */
        in.key = 0; in.mouse_x = 920; in.mouse_y = 14; in.clicked = 1;
        CHECK(shell_handle_input(&in, 1024, 768) == 1);
        {
            int ticks = 0;
            while (shell_tick() && ticks < 64) ticks++;
            CHECK(ticks < 64);
        }
        /* Drill down '>' into Bluetooth sub-view, scan, connect, and go Back */
        bt_mock_hci_attach();
        in.clicked = 1; in.key = 0; in.mouse_x = 975; in.mouse_y = 180;
        CHECK(shell_handle_input(&in, 1024, 768) == 1);
        CHECK(airstin_cc_view() == CC_VIEW_BT);
        in.mouse_x = 780; in.mouse_y = 98; /* Scan Devices */
        CHECK(shell_handle_input(&in, 1024, 768) == 1);
        CHECK(bt_core_device_count() >= 4);
        in.mouse_x = 780; in.mouse_y = 160; /* Connect device 0 */
        CHECK(shell_handle_input(&in, 1024, 768) == 1);
        CHECK(bt_core_status()->state == BT_STATE_CONNECTED);
        in.mouse_x = 762; in.mouse_y = 60; /* < Back */
        CHECK(shell_handle_input(&in, 1024, 768) == 1);
        CHECK(airstin_cc_view() == CC_VIEW_ROOT);

        in.clicked = 0; in.key = 0; in.mouse_x = 780; in.mouse_y = 300; in.clicked = 1;
        CHECK(shell_handle_input(&in, 1024, 768) == 1); /* Spotlight opens from Control Center */
        {
            int ticks = 0;
            while (shell_tick() && ticks < 64) ticks++;
            CHECK(ticks < 64);
        }
        in.clicked = 0; in.key = INPUT64_KEY_ESC;
        CHECK(shell_handle_input(&in, 1024, 768) == 1); /* Spotlight closes */
        {
            int ticks = 0;
            while (shell_tick() && ticks < 64) ticks++;
            CHECK(ticks < 64);
        }
        shell_render(&surf, &binfo, 1);
    }

    /* 5. OpenJDK 21 .class (0xCAFEBABE) bytecode execution */
    {
        struct jvm_run_result jr;
        CHECK(jvm_run_class(hello_flair_class_bytes, (uint32_t)sizeof(hello_flair_class_bytes), &jr) == 0);
        CHECK(jr.major_ver == 65);
        CHECK(strcmp(jr.class_name, "HelloFlair") == 0);
        CHECK(jr.line_count == 3);
        CHECK(strcmp(jr.lines[0], "Hello from OpenJDK 21 on FlairOS!") == 0);
        CHECK(strcmp(jr.lines[2], "42") == 0);
    }

    /* 6. Toybox mini-coreutils + Kilo editor */
    {
        struct kilo_state ks;
        kilo_init(&ks);
        CHECK(kilo_open_file(&ks, "/home/KILO.TXT") == 0);
        CHECK(kilo_handle_key(&ks, 'H') == 1 && kilo_handle_key(&ks, 'i') == 1);
        CHECK(kilo_save_file(&ks) == 0);

        tb_out[0] = 0;
        const char *wc_argv[2] = { "wc", "/home/KILO.TXT" };
        CHECK(toybox_run_cmd(2, wc_argv, "/", tb_cap, 0) == 0);
        CHECK(strstr(tb_out, "bytes=2") != 0);

        tb_out[0] = 0;
        const char *gr_argv[3] = { "grep", "Hi", "/home/KILO.TXT" };
        CHECK(toybox_run_cmd(3, gr_argv, "/", tb_cap, 0) == 0);
        CHECK(strcmp(tb_out, "Hi") == 0);
    }


    /* 7. ELF64 + Syscall64 + SurfaceFlinger + HostBridge + FlairBrowser */
    {
        /* Minimal valid ELF64 ET_DYN EM_AARCH64 image with 1 PT_LOAD segment */
        uint8_t elf_buf[256];
        memset(elf_buf, 0, sizeof(elf_buf));
        elf_buf[0] = 0x7F; elf_buf[1] = 'E'; elf_buf[2] = 'L'; elf_buf[3] = 'F';
        elf_buf[4] = 2; elf_buf[5] = 1; elf_buf[6] = 1; /* ELF64 LE */
        *(uint16_t *)(elf_buf + 16) = ELF64_ET_DYN;
        *(uint16_t *)(elf_buf + 18) = ELF64_EM_AARCH64;
        *(uint32_t *)(elf_buf + 20) = 1;
        *(uint64_t *)(elf_buf + 24) = 0x1000; /* e_entry */
        *(uint64_t *)(elf_buf + 32) = 64;     /* e_phoff */
        *(uint16_t *)(elf_buf + 52) = 64;     /* e_ehsize */
        *(uint16_t *)(elf_buf + 54) = 56;     /* e_phentsize */
        *(uint16_t *)(elf_buf + 56) = 1;      /* e_phnum */
        /* PT_LOAD at offset 64 */
        *(uint32_t *)(elf_buf + 64) = 1;      /* p_type = PT_LOAD */
        *(uint32_t *)(elf_buf + 68) = 5;      /* p_flags = R|X */
        *(uint64_t *)(elf_buf + 72) = 128;    /* p_offset */
        *(uint64_t *)(elf_buf + 80) = 0x1000; /* p_vaddr */
        *(uint64_t *)(elf_buf + 88) = 0x1000; /* p_paddr */
        *(uint64_t *)(elf_buf + 96) = 32;     /* p_filesz */
        *(uint64_t *)(elf_buf + 104) = 64;    /* p_memsz */
        elf_buf[128] = 0xC0; elf_buf[129] = 0x03; elf_buf[130] = 0x5F; elf_buf[131] = 0xD6; /* ret */

        struct elf64_image eimg;
        CHECK(elf64_load_image(elf_buf, sizeof(elf_buf), &eimg) == 0);
        CHECK(eimg.loaded == 1 && eimg.machine == ELF64_EM_AARCH64);
        CHECK(elf64_lookup_runtime_sym("malloc") != 0);

        CHECK(syscall64_dispatch(FLAIR_SYS_getpid, 0, 0, 0, 0, 0, 0) == 1001);
        int64_t mp = syscall64_dispatch(FLAIR_SYS_mmap, 0, 4096, 3, 0, 0, 0);
        CHECK(mp > 0);
        CHECK(syscall64_dispatch(FLAIR_SYS_munmap, (uint64_t)mp, 4096, 0, 0, 0, 0) == 0);

        sf_init();
        int sid = sf_create_surface("chrome.surface", 64, 64);
        CHECK(sid >= 0);
        int32_t sw = 0, sh = 0;
        uint32_t *spx = sf_lock_buffer(sid, &sw, &sh);
        CHECK(spx != 0 && sw == 64 && sh == 64);
        spx[0] = 0xFF00FF00u;
        sf_unlock_and_post(sid);
        sf_composite_to(sid, &surf, 10, 40);
        CHECK(test_fb[40 * 1024 + 10] == 0xFF00FF00u);
        CHECK(sf_find_by_tag("chrome.surface") == sid);
        sf_destroy_surface(sid);

        CHECK(art_jni_register("com/flair/Browser", "nativeInit", (void *)flair_browser_init) == 0);
        CHECK(art_jni_lookup("com/flair/Browser", "nativeInit") == (void *)flair_browser_init);

        host_bridge_init();
        CHECK(host_bridge_telemetry()->battery_pct == -1);

        flair_browser_init();
        CHECK(flair_browser_node_count() >= 3);
        flair_browser_navigate("about:system");
        CHECK(strcmp(flair_browser_current_url(), "about:system") == 0);
        flair_browser_navigate("file:///home/NOTE.TXT");
        CHECK(flair_browser_node_count() >= 2);
    }

    /* 8. Dalvik DEX Interpreter + Android Native Libs + AirStinUI + NetService */
    {
        /* const/4 v0, 5; const/4 v1, 7; add-int v0, v0, v1; const-string v2, 0; invoke-static v2; return v0 */
        const uint16_t insns[] = {
            0x5012u,          /* const/4 v0, #5 */
            0x7112u,          /* const/4 v1, #7 */
            0x0090u, 0x0100u, /* add-int v0, v0, v1 */
            0x021Au, 0x0000u, /* const-string v2, string@0 */
            0x1071u, 0x0000u, 0x0002u, /* invoke-static {v2} */
            0x000Fu           /* return v0 */
        };
        const char *strs[] = { "Dalvik OK!" };
        struct dex_exec_result dr;
        CHECK(dex_interp_exec(insns, (uint32_t)(sizeof(insns) / sizeof(insns[0])), strs, 1, &dr) == 0);
        CHECK(dr.ok == 1 && dr.ret_val == 12);
        CHECK(dr.log_count == 1 && strcmp(dr.logs[0], "Dalvik OK!") == 0);

        CHECK(android_lookup_bionic_sym("__android_log_print") != 0);
        CHECK(android_log_print(4, "FlairART", "Booted") == 0);
        CHECK(android_log_count() >= 1);

        net_service_init();
        CHECK(net_service_state()->cell_bars == 0 && net_service_state()->wifi_bars == 0);
        net_service_toggle_airplane();
        CHECK(net_service_state()->airplane_mode == 1 && net_service_state()->cell_bars == 0);
        net_service_toggle_airplane();
        CHECK(net_service_state()->airplane_mode == 0 && net_service_state()->wifi_bars == 0);

        struct apk_info ainfo;
        memset(&ainfo, 0, sizeof(ainfo));
        strcpy(ainfo.package_name, "com.flair.demo");
        strcpy(ainfo.app_label, "Demo App");
        airstin_ui_begin_install("/apps/hello.apk", &ainfo);
        CHECK(airstin_ui_installer()->active == 1);
        ((struct airstin_installer *)airstin_ui_installer())->installing = 1;
        while (!airstin_ui_install_done()) (void)airstin_ui_tick();
        CHECK(airstin_ui_installer()->progress == 100);
        airstin_ui_cancel_install();
    }

    /* 9. Per-App UID Sandbox + Syscall64 Confinement + DEFLATE Inflate */
    {
        uint32_t u_uid = 0;
        CHECK(sandbox_register_app("com.user.sandboxed", 0, SANDBOX_PERM_INTERNET, &u_uid) == 0);
        CHECK(u_uid >= SANDBOX_UID_APP_BASE);
        sandbox_set_current_uid(u_uid);
        CHECK(syscall64_dispatch(FLAIR_SYS_getuid, 0, 0, 0, 0, 0, 0) == (int64_t)u_uid);

        /* Allowed inside own /data/data/<pkg>/ */
        int64_t sfd = syscall64_dispatch(FLAIR_SYS_openat, 0, (uint64_t)(uintptr_t)"/data/data/com.user.sandboxed/cfg.txt",
                                         VFS_O_CREAT | VFS_O_RDWR, 0, 0, 0);
        CHECK(sfd >= 0);
        CHECK(syscall64_dispatch(FLAIR_SYS_close, (uint64_t)sfd, 0, 0, 0, 0, 0) == 0);

        /* Denied writing to /home/ or /system/ without STORAGE/SYSTEM perms (-EACCES = -13) */
        CHECK(syscall64_dispatch(FLAIR_SYS_openat, 0, (uint64_t)(uintptr_t)"/home/NOTE.TXT",
                                 VFS_O_WRONLY, 0, 0, 0) == -13);
        CHECK(syscall64_dispatch(FLAIR_SYS_openat, 0, (uint64_t)(uintptr_t)"/system/evil.txt",
                                 VFS_O_CREAT | VFS_O_WRONLY, 0, 0, 0) == -13);
        sandbox_set_current_uid(SANDBOX_UID_SYSTEM);

        /* RFC 1951 DEFLATE raw inflate (stored block + fixed-Huffman "Hello") */
        const uint8_t deflate_hello[] = { 0xF3, 0x48, 0xCD, 0xC9, 0xC9, 0x07, 0x00 };
        uint8_t out_inf[16];
        uint32_t out_len = 0;
        CHECK(flair_inflate_raw(deflate_hello, (uint32_t)sizeof(deflate_hello), out_inf, sizeof(out_inf), &out_len) == 0);
        out_inf[out_len] = 0;
        CHECK(out_len == 5 && strcmp((const char *)out_inf, "Hello") == 0);
    }

    /* 10. Status Bar Power Button -> Double-Tap Graceful Shutdown & Forced Hold */
    {
        airstin_power_open_modal();
        CHECK(airstin_power_state()->modal_open == 1 && airstin_power_state()->taps == 0);
        airstin_power_tap_shutdown();
        CHECK(airstin_power_state()->taps == 1 && airstin_power_state()->shutting_down == 0);
        airstin_power_tap_shutdown();
        CHECK(airstin_power_state()->taps == 2 && airstin_power_state()->shutting_down == 1 && airstin_power_state()->forced == 0);
        while (!airstin_power_state()->halted) (void)airstin_ui_tick();
        CHECK(airstin_power_state()->halted == 1);

        /* Reset and verify forced hardware hold */
        airstin_power_open_modal();
        airstin_power_cancel();
        CHECK(airstin_power_state()->modal_open == 0);
        airstin_power_force_hold();
        CHECK(airstin_power_state()->forced == 1 && airstin_power_state()->halted == 1);
    }

    /* 11. 4K/90Hz Stability: ag_damage rect tracking + kfree coalescing */
    {
        size_t before = kmem64_used_bytes();
        void *tmp_buf = kmem64_alloc(65536u);
        CHECK(tmp_buf != 0 && kmem64_used_bytes() > before);
        kfree(tmp_buf);
        CHECK(kmem64_used_bytes() == before);
    }

    /* 12. Real CPython 3.16 (libpython3.16.a) + /home/hello.py VFS execution */
    {
        flair_py_init();
        tb_out[0] = 0;
        CHECK(flair_py_run_script("import sys, itertools\ndef fib(n): return n if n < 2 else fib(n-1) + fib(n-2)\nprint([fib(i) for i in range(7)], list(itertools.islice(itertools.count(10), 3)))", tb_cap, 0) == 0);
        CHECK(strcmp(tb_out, "[0, 1, 1, 2, 3, 5, 8] [10, 11, 12]") == 0);

        tb_out[0] = 0;
        CHECK(flair_py_eval_line("[x*x for x in range(4)]", tb_cap, 0) == 0);
        CHECK(strcmp(tb_out, "[0, 1, 4, 9]") == 0);

        tb_out[0] = 0;
        CHECK(flair_py_run_file("/home/hello.py", tb_cap, 0) == 0);
        CHECK(tb_out[0] != 0);
    }
    puts("Runtime64 + ART + AirP + Shell host test: all checks passed");
    return 0;
}
