/*
FILE PATH: tests/fs_host_test.c
CONTRIBUTOR (Optional): Textile
PURPOSE: Host-side checks for the FlareOS filesystem. Compiles kernel/fs.c directly and drives it through fs_dispatch. An in-memory fake ATA disk stands in for drivers/drv_ata.c, so the checks cover RAM behaviour, write-through to disk, reload after reboot, the no-disk fallback and rollback when a disk write fails.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define putc fsk_test_putc
#define puts fsk_test_puts
#include "../kernel/fs.c"

/* The host harness tests the persistent-disk path, not LiveCD mode. */
int boot_is_live_session(void) { return 0; }

#define FAKE_SECTORS 4096

static u8 fake_disk[FAKE_SECTORS][ATA_SECTOR_SIZE];
static int fake_ready = 1;
static int fake_fail_writes;

int ata_is_ready(void) {
    return fake_ready;
}

u32 ata_sector_count(void) {
    return FAKE_SECTORS;
}

int ata_read_sector(u32 lba, void *buf) {
    if (!fake_ready || lba >= FAKE_SECTORS)
        return -1;
    memcpy(buf, fake_disk[lba], ATA_SECTOR_SIZE);
    return 0;
}

int ata_write_sector(u32 lba, const void *buf) {
    if (!fake_ready || fake_fail_writes || lba >= FAKE_SECTORS)
        return -1;
    memcpy(fake_disk[lba], buf, ATA_SECTOR_SIZE);
    return 0;
}

void *kmalloc(unsigned int size) {
    return malloc(size);
}

void kfree(void *p) {
    free(p);
}

static void seed_disk(void) {
    memset(fake_disk, 0, sizeof fake_disk);
    fake_disk[FSK_DIR_LBA][0] = 0x31;
    fake_disk[FSK_DIR_LBA][1] = 0x53;
    fake_disk[FSK_DIR_LBA][2] = 0x46;
    fake_disk[FSK_DIR_LBA][3] = 0x41;
}

static void fake_reboot(void) {
    memset(fsk_files, 0, sizeof fsk_files);
    memset(fsk_handles, 0, sizeof fsk_handles);
    fsk_loaded = 0;
}

static int failures;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        failures++;
}

static u32 sys(u32 n, u32 b, u32 c, u32 d) {
    struct saved_regs r;
    memset(&r, 0, sizeof r);
    r.eax = n;
    r.ebx = b;
    r.ecx = c;
    r.edx = d;
    if (!fs_dispatch(&r)) {
        printf("FAIL no handler for %u\n", n);
        failures++;
        return FS_FAIL;
    }
    return r.eax;
}

static u32 addr(const void *p) {
    return (u32)(unsigned long)p;
}

int main(void) {
    char buf[32], big[600], name[8];
    u32 h, n, ok;

    seed_disk();
    check(fs_format() == 0, "format fake disk");

    h = sys(SYS_FS_OPEN, addr("notes"), FS_WRITE, 0);
    check(h != FS_FAIL, "open notes for write");
    check(sys(SYS_FS_WRITE, h, addr("hello"), 5) == 5, "write hello");
    sys(SYS_FS_CLOSE, h, 0, 0);

    h = sys(SYS_FS_OPEN, addr("notes"), FS_APPEND, 0);
    check(h != FS_FAIL, "open notes for append");
    check(sys(SYS_FS_WRITE, h, addr(" world"), 6) == 6, "append world");
    sys(SYS_FS_CLOSE, h, 0, 0);

    h = sys(SYS_FS_OPEN, addr("notes"), FS_READ, 0);
    check(sys(SYS_FS_SIZE, h, 0, 0) == 11, "size is 11");
    memset(buf, 0, sizeof buf);
    n = sys(SYS_FS_READ, h, addr(buf), 31);
    check(n == 11 && strcmp(buf, "hello world") == 0, "read back hello world");
    check(sys(SYS_FS_SEEK, h, 6, 0) == 6, "seek to 6");
    memset(buf, 0, sizeof buf);
    n = sys(SYS_FS_READ, h, addr(buf), 5);
    check(n == 5 && strcmp(buf, "world") == 0, "read world after seek");
    check(sys(SYS_FS_SEEK, h, 12, 0) == FS_FAIL, "seek past end fails");
    check(sys(SYS_FS_OPEN, addr("notes"), FS_READ, 0) == FS_FAIL, "second open of a busy file fails");
    sys(SYS_FS_CLOSE, h, 0, 0);

    h = sys(SYS_FS_OPEN, addr("notes"), FS_WRITE, 0);
    check(h != FS_FAIL && sys(SYS_FS_SIZE, h, 0, 0) == 0, "write mode truncates");
    sys(SYS_FS_CLOSE, h, 0, 0);

    memset(big, 'x', sizeof big);
    h = sys(SYS_FS_OPEN, addr("big"), FS_WRITE, 0);
    check(sys(SYS_FS_WRITE, h, addr(big), 600) == 600, "write 600 bytes");
    sys(SYS_FS_CLOSE, h, 0, 0);
    check(sys(SYS_FS_DELETE, addr("big"), 0, 0) == 0, "delete big");

    check(sys(SYS_FS_DELETE, addr("notes"), 0, 0) == 0, "delete notes");
    check(sys(SYS_FS_OPEN, addr("notes"), FS_READ, 0) == FS_FAIL, "notes is gone");
    check(sys(SYS_FS_OPEN, addr("waytoolongname123"), FS_WRITE, 0) == FS_FAIL, "over-long name rejected");

    ok = 1;
    for (int i = 0; i < FS_FILES; i++) {
        name[0] = 'f';
        name[1] = (char)('0' + i);
        name[2] = 0;
        h = sys(SYS_FS_OPEN, addr(name), FS_WRITE, 0);
        if (h == FS_FAIL)
            ok = 0;
        sys(SYS_FS_CLOSE, h, 0, 0);
    }
    check(ok, "all file slots fit");
    check(sys(SYS_FS_OPEN, addr("extra"), FS_WRITE, 0) == FS_FAIL, "file past the slot limit is refused");
    check(sys(SYS_FS_LIST, 0, addr(buf), 0) == 0 && strcmp(buf, "f0") == 0, "list index 0 is f0");
    check(sys(SYS_FS_LIST, FS_FILES, addr(buf), 0) == FS_FAIL, "list past the last file fails");

    check(sys(SYS_FS_DELETE, addr("f0"), 0, 0) == 0, "delete f0");
    check(sys(SYS_FS_SLOTS, 0, 0, 0) == 1, "one free slot after delete");
    h = sys(SYS_FS_OPEN, addr("newfile"), FS_APPEND, 0);
    check(h != FS_FAIL, "append creates a new file");
    sys(SYS_FS_CLOSE, h, 0, 0);
    check(sys(SYS_FS_SLOTS, 0, 0, 0) == 0, "no free slots when full");

    check(sys(SYS_FS_EXISTS, addr("newfile"), 0, 0) == 1, "exists finds newfile");
    check(sys(SYS_FS_EXISTS, addr("ghost"), 0, 0) == 0, "exists misses ghost");

    check(sys(SYS_FS_RENAME, addr("f1"), addr("renamed"), 0) == 0, "rename f1 to renamed");
    check(sys(SYS_FS_EXISTS, addr("f1"), 0, 0) == 0, "old name is gone after rename");
    check(sys(SYS_FS_EXISTS, addr("renamed"), 0, 0) == 1, "new name exists after rename");
    check(sys(SYS_FS_RENAME, addr("renamed"), addr("f2"), 0) == FS_FAIL, "rename onto a taken name fails");
    check(sys(SYS_FS_EXISTS, addr("renamed"), 0, 0) == 1, "failed rename keeps the source name");
    check(sys(SYS_FS_RENAME, addr("ghost"), addr("x"), 0) == FS_FAIL, "rename of a missing file fails");
    check(sys(SYS_FS_RENAME, addr("renamed"), addr("waytoolongname123"), 0) == FS_FAIL, "rename to an over-long name fails");

    h = sys(SYS_FS_OPEN, addr("f3"), FS_READ, 0);
    check(h != FS_FAIL, "open f3 for rename check");
    check(sys(SYS_FS_RENAME, addr("f3"), addr("other"), 0) == FS_FAIL, "rename of an open file fails");
    sys(SYS_FS_CLOSE, h, 0, 0);
    check(sys(SYS_FS_RENAME, addr("f3"), addr("other"), 0) == 0, "rename works after close");
    check(sys(SYS_FS_EXISTS, addr("other"), 0, 0) == 1, "other exists after rename");

    check(sys(SYS_FS_DELETE, addr("newfile"), 0, 0) == 0, "delete newfile to free a slot");
    h = sys(SYS_FS_OPEN, addr("keep"), FS_WRITE, 0);
    check(h != FS_FAIL && sys(SYS_FS_WRITE, h, addr("persist me"), 10) == 10, "write keep to the disk");
    sys(SYS_FS_CLOSE, h, 0, 0);

    fake_reboot();
    h = sys(SYS_FS_OPEN, addr("keep"), FS_READ, 0);
    memset(buf, 0, sizeof buf);
    n = h != FS_FAIL ? sys(SYS_FS_READ, h, addr(buf), 31) : 0;
    check(n == 10 && strcmp(buf, "persist me") == 0, "keep survives a reboot from disk");
    sys(SYS_FS_CLOSE, h, 0, 0);

    fake_ready = 0;
    fake_reboot();
    check(sys(SYS_FS_OPEN, addr("keep"), FS_READ, 0) == FS_FAIL, "no disk: keep is not loaded");
    h = sys(SYS_FS_OPEN, addr("ram"), FS_WRITE, 0);
    check(h != FS_FAIL && sys(SYS_FS_WRITE, h, addr("ram only"), 8) == 8, "no disk: RAM write works");
    sys(SYS_FS_CLOSE, h, 0, 0);
    fake_ready = 1;

    fake_reboot();
    check(sys(SYS_FS_EXISTS, addr("keep"), 0, 0) == 1, "disk back: keep is loaded again");
    check(sys(SYS_FS_EXISTS, addr("ram"), 0, 0) == 0, "disk back: RAM-only file is gone");

    fake_fail_writes = 1;
    check(sys(SYS_FS_OPEN, addr("nope"), FS_WRITE, 0) == FS_FAIL, "create fails when the disk write fails");
    check(sys(SYS_FS_EXISTS, addr("nope"), 0, 0) == 0, "failed create is rolled back");
    h = sys(SYS_FS_OPEN, addr("keep"), FS_APPEND, 0);
    check(h != FS_FAIL && sys(SYS_FS_WRITE, h, addr("!"), 1) == FS_FAIL, "failed disk write returns an error");
    sys(SYS_FS_CLOSE, h, 0, 0);
    fake_fail_writes = 0;

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
