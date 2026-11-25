#ifndef FLAIROS_HWINFO_H
#define FLAIROS_HWINFO_H
#include <stdint.h>
/* Keep the pre-PMM static inventory bounded; 80 devices covers typical PCs. */
#define HWINFO_MAX_PCI 16
#define HWINFO_MAX_MEMORY_RANGES 32
#define HWINFO_MAX_ACPI_TABLES 16
enum hwinfo_pci_kind { HWINFO_PCI_OTHER=0, HWINFO_PCI_STORAGE, HWINFO_PCI_NETWORK, HWINFO_PCI_DISPLAY, HWINFO_PCI_AUDIO, HWINFO_PCI_BRIDGE, HWINFO_PCI_USB, HWINFO_PCI_INPUT };
struct hwinfo_pci_device {
 uint16_t vendor, device; uint8_t kind, bus, slot, function, class_code, subclass, prog_if, revision, irq;
 uint32_t bar[6]; uint8_t bar_io[6];
};
struct hwinfo_memory_range { uint64_t base, length; uint32_t type, attributes; };
struct hwinfo_cpu { char vendor[13]; uint32_t max_leaf, signature, features_edx, features_ecx; uint8_t has_cpuid, has_msr, has_pae, has_apic, has_sse, has_sse2, has_sse3, has_avx; };
struct hwinfo_acpi_table { char signature[5]; uint32_t address, length; uint8_t checksum_ok; };
struct hwinfo_inventory {
 struct hwinfo_cpu cpu; uint8_t bios_drive; uint32_t pci_count, memory_count;
 uint64_t usable_memory_bytes, total_memory_bytes; uint32_t rsdp_address, rsdt_address, xsdt_address, acpi_table_count, smbios_address; uint8_t acpi_revision, acpi_valid, smbios_valid; char smbios_version[16]; struct hwinfo_acpi_table acpi[HWINFO_MAX_ACPI_TABLES];
 struct hwinfo_pci_device pci[HWINFO_MAX_PCI];
 struct hwinfo_memory_range memory[HWINFO_MAX_MEMORY_RANGES];
};
void hwinfo_init(void);
const struct hwinfo_inventory *hwinfo_get(void);
void hwinfo_report(void);
#endif