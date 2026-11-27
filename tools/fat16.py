#!/usr/bin/env python3
"""Minimal FAT16 writer for building an EFI System Partition image.

No external tools: the build has to work on a machine with nothing but Python,
which is the same constraint the rest of tools/ follows. Only what an ESP needs
is implemented - a boot sector + BPB, two FATs, a fixed root directory, and
8.3 file/directory entries with subdirectories.
"""
import struct

SECTOR = 512
ATTR_DIR = 0x10
ATTR_ARCHIVE = 0x20


def _name_83(name):
    stem, _, ext = name.lstrip("\\/").partition(".")
    return stem.upper().ljust(8)[:8].encode("ascii"), ext.upper().ljust(3)[:3].encode("ascii")


class Fat16:
    def __init__(self, total_sectors=32768, sectors_per_cluster=4, label=b"FLAIROS ESP"):
        self.spc = sectors_per_cluster
        self.root_entries = 512
        self.root_sectors = (self.root_entries * 32 + SECTOR - 1) // SECTOR
        self.reserved = 1
        self.total_sectors = total_sectors
        self.cluster_bytes = self.spc * SECTOR
        # fat_sectors is solved iteratively because the cluster count depends on it.
        self.fat_sectors = 1
        while True:
            data_sectors = self.total_sectors - self.reserved - 2 * self.fat_sectors - self.root_sectors
            clusters = data_sectors // self.spc
            needed = ((clusters + 2) * 2 + SECTOR - 1) // SECTOR
            if needed <= self.fat_sectors:
                break
            self.fat_sectors = needed
        self.data_start = self.reserved + 2 * self.fat_sectors + self.root_sectors
        self.clusters = (self.total_sectors - self.data_start) // self.spc
        if not (4085 <= self.clusters < 65525):
            raise ValueError(f"cluster count {self.clusters} is not FAT16")
        self.fat = [0] * (self.clusters + 2)
        self.fat[0] = 0xFFF8
        self.fat[1] = 0xFFFF
        self.next_cluster = 2
        self.entries = {}          # path -> bytes (file content)
        self.dirs = {"/": []}      # path -> list of (name, attr, first_cluster, size)
        self.label = label

    # --- allocation -------------------------------------------------------
    def _alloc(self, count):
        start = self.next_cluster
        for i in range(count):
            c = start + i
            self.fat[c] = 0xFFFF if i == count - 1 else c + 1
        self.next_cluster += count
        if self.next_cluster > self.clusters + 1:
            raise ValueError("ESP image is full")
        return start

    def mkdir(self, path):
        path = "/" + path.strip("/")
        if path in self.dirs:
            return
        parent, _, name = path.rpartition("/")
        parent = parent or "/"
        if parent not in self.dirs:
            raise ValueError(f"parent {parent} does not exist")
        cluster = self._alloc(1)
        self.dirs[path] = []
        self.dirs[parent].append((name, ATTR_DIR, cluster, 0))
        self.dirs[path].append((".", ATTR_DIR, cluster, 0))
        self.dirs[path].append(("..", ATTR_DIR, 0 if parent == "/" else
                                [e for e in self.dirs[parent] if e[0] == name and False] or 0, 0))

    def add_file(self, path, data):
        path = "/" + path.strip("/")
        parent, _, name = path.rpartition("/")
        parent = parent or "/"
        if parent not in self.dirs:
            raise ValueError(f"parent {parent} does not exist")
        clusters = max(1, (len(data) + self.cluster_bytes - 1) // self.cluster_bytes)
        start = self._alloc(clusters)
        self.dirs[parent].append((name, ATTR_ARCHIVE, start, len(data)))
        self.entries[path] = (start, data)

    # --- serialisation ----------------------------------------------------
    def _boot_sector(self):
        bs = bytearray(SECTOR)
        bs[0:3] = b"\xEB\x3C\x90"
        bs[3:11] = b"FLAIROS "
        struct.pack_into("<HBHBHHBHHHII", bs, 11,
                         SECTOR, self.spc, self.reserved, 2, self.root_entries,
                         self.total_sectors if self.total_sectors < 0x10000 else 0,
                         0xF8, self.fat_sectors, 32, 64, 0,
                         self.total_sectors if self.total_sectors >= 0x10000 else 0)
        struct.pack_into("<BBBI11s8s", bs, 36, 0x80, 0, 0x29, 0x12345678,
                         self.label.ljust(11)[:11], b"FAT16   ")
        bs[510:512] = b"\x55\xAA"
        return bytes(bs)

    def _dir_bytes(self, path):
        out = bytearray()
        for name, attr, cluster, size in self.dirs[path]:
            entry = bytearray(32)
            if name == ".":
                entry[0:11] = b".          "
            elif name == "..":
                entry[0:11] = b"..         "
                parent = path.rpartition("/")[0] or "/"
                if parent != "/":
                    for e in self.dirs[parent]:
                        if e[0] == path.rpartition("/")[2]:
                            struct.pack_into("<H", entry, 26, e[2])
            else:
                stem, ext = _name_83(name)
                entry[0:8] = stem
                entry[8:11] = ext
            entry[11] = attr
            struct.pack_into("<H", entry, 26, cluster & 0xFFFF)
            struct.pack_into("<I", entry, 28, size)
            out += entry
        return bytes(out)

    def to_bytes(self):
        img = bytearray(self.total_sectors * SECTOR)
        img[0:SECTOR] = self._boot_sector()

        fat = bytearray(self.fat_sectors * SECTOR)
        for i, value in enumerate(self.fat):
            struct.pack_into("<H", fat, i * 2, value)
        for copy in range(2):
            off = (self.reserved + copy * self.fat_sectors) * SECTOR
            img[off:off + len(fat)] = fat

        root_off = (self.reserved + 2 * self.fat_sectors) * SECTOR
        root = bytearray()
        for name, attr, cluster, size in self.dirs["/"]:
            entry = bytearray(32)
            stem, ext = _name_83(name)
            entry[0:8] = stem
            entry[8:11] = ext
            entry[11] = attr
            struct.pack_into("<H", entry, 26, cluster & 0xFFFF)
            struct.pack_into("<I", entry, 28, size)
            root += entry
        img[root_off:root_off + len(root)] = root

        for path, (start, data) in self.entries.items():
            off = (self.data_start + (start - 2) * self.spc) * SECTOR
            img[off:off + len(data)] = data

        for path, entries in self.dirs.items():
            if path == "/":
                continue
            cluster = next(e[2] for e in self.dirs[path])
            off = (self.data_start + (cluster - 2) * self.spc) * SECTOR
            blob = self._dir_bytes(path)
            img[off:off + len(blob)] = blob

        return bytes(img)
