/* SPDX-License-Identifier: AGPL-3.0-only
 *
 * FlairOS aarch64 UEFI loader.
 *
 * Runs as a UEFI application from the EFI System Partition. It asks the
 * firmware's own file system protocol for /flair.bin (the kernel plus a small
 * load header), copies the kernel to its link address, exits boot services and
 * hands over with the MMU and caches off, which is the state arch/aarch64/boot.S
 * expects. Every byte of kernel code comes off the disk through firmware
 * protocols; nothing is embedded in this loader.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef unsigned long uptr;

typedef struct {
    u64 signature;
    u32 revision;
    u32 header_size;
    u32 crc32;
    u32 reserved;
} table_header;

typedef struct {
    table_header hdr;
    char *firmware_vendor;
    u32 firmware_revision;
    void *console_in_handle;
    void *con_in;
    void *console_out_handle;
    struct efi_text_out *con_out;
    void *standard_error_handle;
    struct efi_text_out *std_err;
    void *runtime_services;
    struct efi_boot_services *boot_services;
    uptr number_of_table_entries;
    void *configuration_table;
} efi_system_table;

typedef struct efi_text_out {
    void *reset;
    u64 (*output_string)(struct efi_text_out *, u16 *);
    void *test_string;
    void *query_mode;
    void *set_mode;
    void *set_attribute;
    void *clear_screen;
    void *set_cursor_position;
    void *enable_cursor;
    void *mode;
} efi_text_out;

typedef struct efi_boot_services {
    table_header hdr;
    void *raise_tpl;
    void *restore_tpl;
    u64 (*allocate_pages)(u32, u32, u64, u64 *);
    u64 (*free_pages)(u64, u64);
    u64 (*get_memory_map)(uptr *, void *, uptr *, uptr *, u32 *);
    u64 (*allocate_pool)(u32, uptr, void **);
    u64 (*free_pool)(void *);
    void *create_event;
    void *set_timer;
    void *wait_for_event;
    void *signal_event;
    void *close_event;
    void *check_event;
    u64 (*install_protocol_interface)(void **, void *, u32, void *);
    void *reinstall_protocol_interface;
    void *uninstall_protocol_interface;
    u64 (*handle_protocol)(void *, void *, void **);
    void *reserved;
    void *register_protocol_notify;
    void *locate_handle;
    void *locate_device_path;
    void *install_configuration_table;
    void *load_image;
    void *start_image;
    void *exit;
    void *unload_image;
    u64 (*exit_boot_services)(void *, uptr);
} efi_boot_services;

typedef struct {
    u64 revision;
    u64 (*open_volume)(void *, void **);
} efi_file_system;

typedef struct {
    u64 revision;
    u64 (*open)(void *, void **, u16 *, u64, u64);
    u64 (*close)(void *);
    void *delete_file;
    u64 (*read)(void *, uptr *, void *);
    void *write;
    void *get_position;
    void *set_position;
    void *get_info;
    void *set_info;
    void *flush;
} efi_file;

typedef struct {
    u32 revision;
    u32 pad;
    void *parent_handle;
    efi_system_table *system_table;
    void *device_handle;
    void *file_path;
    void *reserved;
    u32 load_options_size;
    void *load_options;
    void *image_base;
    u64 image_size;
    u32 image_code_type;
    u32 image_data_type;
    void *unload;
} efi_loaded_image;

/* {5B1B31A1-9562-11d2-8E3F-00A0C969723B} */
static u8 loaded_image_guid[16] = {
    0xA1, 0x31, 0x1B, 0x5B, 0x62, 0x95, 0xD2, 0x11,
    0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B,
};
/* {964E5B22-6459-11d2-8E39-00A0C969723B} */
static u8 fs_guid[16] = {
    0x22, 0x5B, 0x4E, 0x96, 0x59, 0x64, 0xD2, 0x11,
    0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B,
};

#define EFI_SUCCESS 0ULL
#define EFI_BUFFER_TOO_SMALL 5ULL
#define EFI_ERROR 0x8000000000000000ULL

struct load_header {
    u32 magic;
    u32 version;
    u64 load_addr;
    u64 entry_addr;
    u64 size;
};
#define LOAD_MAGIC 0x41363446u /* "F64A" little endian */

/* No writable globals anywhere in this file: the loader is shipped as a flat
 * PE image whose .bss is never mapped by the firmware, so state is passed on
 * the stack instead. */

static void *memset_(void *dst, int value, uptr n) {
    u8 *d = (u8 *)dst;
    while (n--) *d++ = (u8)value;
    return dst;
}

static void *memcpy_(void *dst, const void *src, uptr n) {
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    while (n--) *d++ = *s++;
    return dst;
}

#define PL011_BASE 0x09000000UL
#define PL011_DR 0x00
#define PL011_FR 0x18
#define PL011_FR_TXFF (1u << 5)

/* Mirror everything to the PL011 as well: that is the serial port the presets
 * themselves use, and it keeps loader output visible regardless of how the
 * firmware wired up its console devices. */
static void uart_putc(char c) {
    volatile u32 *base = (volatile u32 *)PL011_BASE;
    int guard = 100000;
    while ((base[PL011_FR / 4] & PL011_FR_TXFF) && guard-- > 0) {
    }
    base[PL011_DR / 4] = (u32)(u8)c;
}

/* Loader diagnostics go straight to the PL011: that is the console the presets
 * use, and it stays visible even when the firmware's console chain is unhappy
 * (for example with no graphics device at all). */
static void print_sys(efi_system_table *sys, const char *text) {
    (void)sys;
    for (int i = 0; text[i]; i++) uart_putc(text[i]);
}

#define print(msg) print_sys(st, (msg))
#define print_hex(value) print_hex_sys(st, (value))

static void print_hex_sys(efi_system_table *sys, u64 value) {
    char out[19];
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 16; i++) {
        u32 nibble = (u32)((value >> (60 - i * 4)) & 0xF);
        out[2 + i] = (char)(nibble < 10 ? '0' + nibble : 'a' + (nibble - 10));
    }
    out[18] = 0;
    print_sys(sys, out);
}

static void cache_clean(void *addr, uptr len) {
    /* Clean and invalidate the region so the data is in RAM before the MMU and
     * caches are turned off for the handoff. */
    u64 line = 64;
    uptr start = (uptr)addr & ~(line - 1);
    for (uptr p = start; p < (uptr)addr + len; p += line) {
        __asm__ volatile("dc civac, %0" : : "r"(p) : "memory");
    }
    __asm__ volatile("dsb sy" : : : "memory");
}

void efi_handoff(u64 entry, u64 stack_top) __attribute__((noreturn));

u64 efi_main(void *image_handle, efi_system_table *systab) {
    efi_system_table *st = systab;
    print("FlairOS UEFI loader\r\n");

    efi_boot_services *bs = st->boot_services;
    if (!bs) {
        print("loader: no boot services table\r\n");
        for (;;) __asm__ volatile("wfi");
    }
    print("loader: ask for loaded image\r\n");
    efi_loaded_image *image = 0;
    if (bs->handle_protocol(image_handle, loaded_image_guid, (void **)&image) != EFI_SUCCESS || !image) {
        print("loader: no loaded-image protocol\r\n");
        for (;;) __asm__ volatile("wfi");
    }

    print("loader: ask for simple file system\r\n");
    efi_file_system *fs = 0;
    if (bs->handle_protocol(image->device_handle, fs_guid, (void **)&fs) != EFI_SUCCESS || !fs) {
        print("loader: no simple file system on the boot device\r\n");
        for (;;) __asm__ volatile("wfi");
    }

    print("loader: open the volume\r\n");
    efi_file *root = 0;
    if (fs->open_volume(fs, (void **)&root) != EFI_SUCCESS || !root) {
        print("loader: cannot open the volume\r\n");
        for (;;) __asm__ volatile("wfi");
    }

    print("loader: open flair.bin\r\n");
    u16 kernel_name[] = { 'f', 'l', 'a', 'i', 'r', '.', 'b', 'i', 'n', 0 };
    efi_file *kernel = 0;
    u64 status = root->open(root, (void **)&kernel, kernel_name, 1 /* read */, 0);
    if (status != EFI_SUCCESS || !kernel) {
        print("loader: flair.bin not found on the ESP (status ");
        print_hex(status);
        print(")\r\n");
        for (;;) __asm__ volatile("wfi");
    }
    print("loader: opened flair.bin, allocating buffer\r\n");

    /* Read the whole file: the header tells us how big it is, but the read is
     * one call into a buffer sized for any preset kernel. */
    uptr capacity = 8u * 1024u * 1024u;
    void *buffer = 0;
    if (bs->allocate_pool(2 /* EfiLoaderData */, capacity, &buffer) != EFI_SUCCESS || !buffer) {
        print("loader: out of memory\r\n");
        for (;;) __asm__ volatile("wfi");
    }
    memset_(buffer, 0, capacity);
    print("loader: reading\r\n");
    uptr size = capacity;
    status = kernel->read(kernel, &size, buffer);
    if (status != EFI_SUCCESS || size < sizeof(struct load_header)) {
        print("loader: read failed (status ");
        print_hex(status);
        print(")\r\n");
        for (;;) __asm__ volatile("wfi");
    }

    struct load_header *hdr = (struct load_header *)buffer;
    if (hdr->magic != LOAD_MAGIC || hdr->version != 1) {
        print("loader: bad kernel header\r\n");
        for (;;) __asm__ volatile("wfi");
    }
    if (size < sizeof(struct load_header) + hdr->size) {
        print("loader: kernel image is truncated\r\n");
        for (;;) __asm__ volatile("wfi");
    }

    print("loader: kernel size ");
    print_hex(hdr->size);
    print(" at ");
    print_hex(hdr->load_addr);
    print("\r\n");

    u8 *payload = (u8 *)buffer + sizeof(struct load_header);
    memcpy_((void *)(uptr)hdr->load_addr, payload, (uptr)hdr->size);
    cache_clean((void *)(uptr)hdr->load_addr, (uptr)hdr->size);

    /* Leave boot services so the firmware stops touching the disk and timers. */
    uptr map_size = 64u * 1024u;
    void *map = 0;
    if (bs->allocate_pool(2, map_size, &map) != EFI_SUCCESS || !map) {
        print("loader: no memory map buffer\r\n");
        for (;;) __asm__ volatile("wfi");
    }
    uptr map_key = 0, desc_size = 0;
    u32 desc_version = 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        uptr needed = map_size;
        if (bs->get_memory_map(&needed, map, &map_key, &desc_size, &desc_version) != EFI_SUCCESS) {
            continue;
        }
        if (bs->exit_boot_services(image_handle, map_key) == EFI_SUCCESS) break;
        print("loader: retrying exit-boot-services\r\n");
    }

    print("loader: handing over to kernel\r\n");
    efi_handoff(hdr->entry_addr, hdr->load_addr + hdr->size + 0x8000);
    for (;;) __asm__ volatile("wfi");
}
