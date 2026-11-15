#include "kernel.h"
#include "../include/boot.h"
#include "../include/setup.h"
#include "../include/drv_ata.h"

static int setup_wait_key(void) {
    int key;
    while ((key = kbd_read()) < 0)
        halt();
    return key;
}

static void setup_put_u32(u32 value) {
    char digits[11];
    int count = 0;
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && count < (int)sizeof(digits));
    while (count)
        putc(digits[--count]);
}

static void setup_print_disk_list(void) {
    int count = ata_device_count();
    puts("Storage devices detected: ");
    setup_put_u32((u32)count);
    putc('\n');
    if (!count) {
        puts("No supported legacy ATA disks were detected.\n");
        return;
    }
    for (int i = 0; i < count; ++i) {
        struct ata_device device;
        if (ata_get_device(i, &device) != 0)
            continue;
        puts("  ");
        puts(device.channel == 0 ? "Primary " : "Secondary ");
        puts(device.drive == 0 ? "Master: " : "Slave: ");
        puts(device.identify.model[0] ? device.identify.model : "ATA disk");
        puts("\n    Capacity (LBA28): ");
        setup_put_u32(device.identify.sectors_lba28 / 2048u);
        puts(" MiB\n");
    }
    puts("\nDisk inspection is read-only. No partition or filesystem changes were made.\n");
}

static void setup_install_status(void) {
    puts("FlairOS installation preview\n");
    puts("----------------------------\n");
    puts("The install workflow is being prepared, but this build will not\n");
    puts("partition, format, or write to a selected disk.\n\n");
    puts("Remaining prerequisites:\n");
    puts("  - Read-only MBR/GPT volume discovery and validation\n");
    puts("  - FlairFS implementation and persistence tests\n");
    puts("  - Transactional installer with explicit target confirmation\n\n");
    puts("Your existing disks remain untouched. Press any key to return.\n");
}

static void setup_diagnostics(void) {
    puts("FlairOS hardware diagnostics\n");
    puts("----------------------------\n");
    puts("ATA device count: ");
    setup_put_u32((u32)ata_device_count());
    puts("\nBoot handoff: ");
    puts(boot_handoff_valid() ? "valid\n" : "legacy/default\n");
    puts("Graphics: VGA text console active; VBE and AirGraph components are built in.\n");
    puts("Diagnostics in this build are informational and do not change hardware state.\n");
}

int setup_run(void) {
    for (;;) {
        clear_screen();
        cur_attr = ATTR_OK;
        puts("FlairOS Setup\n");
        cur_attr = ATTR_TEXT;
        puts("Welcome. Choose what you would like to do.\n\n");
        puts("  1. Try FlairOS (Live Session)\n");
        puts("  2. Inspect storage devices\n");
        puts("  3. Installation readiness\n");
        puts("  4. Hardware diagnostics\n");
        puts("\nThis setup environment is non-destructive.\n");
        int key = setup_wait_key();
        if (key == '1') {
            boot_set_mode(FLAIR_BOOT_LIVE);
            serial_str("setup: entering volatile live session\n");
            return 1;
        }
        if (key == '2' || key == '3' || key == '4') {
            clear_screen();
            if (key == '2') setup_print_disk_list();
            else if (key == '3') setup_install_status();
            else setup_diagnostics();
            puts("\nPress any key to return to Setup.\n");
            setup_wait_key();
        }
    }
}
