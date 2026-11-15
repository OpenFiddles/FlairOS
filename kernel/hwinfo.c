#include "../include/hwinfo.h"
#include "../include/boot.h"
#include "../include/hw.h"
#define E820_COUNT_ADDR 0x5000u
#define E820_ENTRIES_ADDR 0x5010u
#define BOOT_DRIVE_ADDR 0x4FFCu
#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC
#define E820_USABLE 1u
static struct hwinfo_inventory inv;
static int initialized;
static uint32_t pci_read(uint8_t b,uint8_t d,uint8_t f,uint8_t r){
 hw_outl(PCI_ADDR,0x80000000u|((uint32_t)b<<16)|((uint32_t)d<<11)|((uint32_t)f<<8)|(r&0xFC));
 return hw_inl(PCI_DATA);
}
static void cpuid(uint32_t leaf,uint32_t sub,uint32_t *a,uint32_t *b,uint32_t *c,uint32_t *d){
 __asm__ volatile("cpuid":"=a"(*a),"=b"(*b),"=c"(*c),"=d"(*d):"a"(leaf),"c"(sub));
}
static void detect_cpu(void){
 uint32_t a,b,c,d,flags;
 __asm__ volatile("pushfl; popl %0":"=r"(flags)); uint32_t changed=flags^(1u<<21);
 __asm__ volatile("pushl %0; popfl"::"r"(flags^(1u<<21)):"cc");
 __asm__ volatile("pushfl; popl %0":"=r"(flags));
 __asm__ volatile("pushl %0; popfl"::"r"(flags^changed):"cc");
 inv.cpu.has_cpuid=(uint8_t)((flags&(1u<<21))!=0);
 if(!inv.cpu.has_cpuid){return;}
 cpuid(0,0,&a,&b,&c,&d); inv.cpu.max_leaf=a;
 ((uint32_t*)inv.cpu.vendor)[0]=b;((uint32_t*)inv.cpu.vendor)[1]=d;((uint32_t*)inv.cpu.vendor)[2]=c;inv.cpu.vendor[12]=0;
 cpuid(1,0,&a,&b,&c,&d);inv.cpu.signature=a;inv.cpu.features_ecx=c;inv.cpu.features_edx=d;
 inv.cpu.has_msr=(d>>5)&1;inv.cpu.has_pae=(d>>6)&1;inv.cpu.has_apic=(d>>9)&1;inv.cpu.has_sse=(d>>25)&1;inv.cpu.has_sse2=(d>>26)&1;inv.cpu.has_sse3=c&1;inv.cpu.has_avx=(c>>28)&1;
}
static uint8_t pci_kind(uint8_t cl,uint8_t sub){
 if(cl==1)return HWINFO_PCI_STORAGE;if(cl==2)return HWINFO_PCI_NETWORK;if(cl==3)return HWINFO_PCI_DISPLAY;
 if(cl==4)return HWINFO_PCI_AUDIO;if(cl==6)return HWINFO_PCI_BRIDGE;if(cl==12&&sub==3)return HWINFO_PCI_USB;
 if(cl==9)return HWINFO_PCI_INPUT;return HWINFO_PCI_OTHER;
}
static void scan_pci(void){
 for(uint16_t bus=0;bus<256&&inv.pci_count<HWINFO_MAX_PCI;bus++)for(uint8_t slot=0;slot<32&&inv.pci_count<HWINFO_MAX_PCI;slot++){
  uint32_t id=pci_read((uint8_t)bus,slot,0,0);uint16_t vendor=(uint16_t)id;if(vendor==0xFFFF)continue;
  uint32_t h=pci_read((uint8_t)bus,slot,0,0x0C);uint8_t funcs=((h>>23)&1)?8:1;
  for(uint8_t fn=0;fn<funcs&&inv.pci_count<HWINFO_MAX_PCI;fn++){
   id=pci_read((uint8_t)bus,slot,fn,0);vendor=(uint16_t)id;if(vendor==0xFFFF)continue;
   struct hwinfo_pci_device *p=&inv.pci[inv.pci_count++];p->vendor=vendor;p->device=(uint16_t)(id>>16);p->bus=(uint8_t)bus;p->slot=slot;p->function=fn;
   uint32_t cls=pci_read((uint8_t)bus,slot,fn,8);p->revision=(uint8_t)cls;p->prog_if=(uint8_t)(cls>>8);p->subclass=(uint8_t)(cls>>16);p->class_code=(uint8_t)(cls>>24);
   uint32_t irq=pci_read((uint8_t)bus,slot,fn,0x3C);p->irq=(uint8_t)(irq>>8);
   for(int k=0;k<6;k++){uint32_t bar=pci_read((uint8_t)bus,slot,fn,(uint8_t)(0x10+k*4));p->bar_io[k]=(uint8_t)(bar&1);p->bar[k]=p->bar_io[k]?(bar&~3u):(bar&~15u);}
  }
 }
}
static void scan_memory(void){
 volatile uint32_t *count=(volatile uint32_t*)E820_COUNT_ADDR;
 volatile uint8_t *raw=(volatile uint8_t*)E820_ENTRIES_ADDR;
 uint32_t n=*count;if(n>32)n=32;
 for(uint32_t i=0;i<n&&inv.memory_count<HWINFO_MAX_MEMORY_RANGES;i++){
  volatile uint32_t *e=(volatile uint32_t*)(raw+i*24);
  uint64_t base=((uint64_t)e[1]<<32)|e[0],len=((uint64_t)e[3]<<32)|e[2];uint32_t type=e[4],attr=e[5];
  if(!len)continue;struct hwinfo_memory_range *m=&inv.memory[inv.memory_count++];m->base=base;m->length=len;m->type=type;m->attributes=attr;
  inv.total_memory_bytes+=len;if(type==E820_USABLE)inv.usable_memory_bytes+=len;
 }
}
void hwinfo_init(void){if(initialized)return;initialized=1;inv.bios_drive=*(volatile uint8_t*)BOOT_DRIVE_ADDR;detect_cpu();scan_memory();scan_pci();}
const struct hwinfo_inventory *hwinfo_get(void){hwinfo_init();return &inv;}
static void hex(uint32_t v){extern void serial_str(const char*);char b[9];for(int i=0;i<8;i++){uint8_t x=(v>>(28-i*4))&15;b[i]="0123456789ABCDEF"[x];}b[8]=0;serial_str(b);}
void hwinfo_report(void){
 extern void serial_str(const char*);const struct hwinfo_inventory *h=hwinfo_get();
 serial_str("hw: CPU ");serial_str(h->cpu.vendor);serial_str(" signature=");hex(h->cpu.signature);serial_str("\n");
 serial_str("hw: cpuid=");serial_str(h->cpu.has_cpuid?"yes":"no");serial_str(" PAE=");serial_str(h->cpu.has_pae?"yes":"no");serial_str(" APIC=");serial_str(h->cpu.has_apic?"yes":"no");serial_str(" SSE2=");serial_str(h->cpu.has_sse2?"yes":"no");serial_str("\n");
 serial_str("hw: BIOS disk=0x");hex(h->bios_drive);serial_str(" E820 ranges=");hex(h->memory_count);serial_str(" usable bytes=");hex((uint32_t)(h->usable_memory_bytes>>32));hex((uint32_t)h->usable_memory_bytes);serial_str("\n");
 serial_str("hw: PCI devices=");hex(h->pci_count);serial_str("\n");
 for(uint32_t i=0;i<h->pci_count;i++){const struct hwinfo_pci_device*p=&h->pci[i];serial_str(" pci ");hex(p->bus);serial_str(":");hex(p->slot);serial_str(".");hex(p->function);serial_str(" id=");hex(((uint32_t)p->vendor<<16)|p->device);serial_str(" class=");hex(((uint32_t)p->class_code<<16)|((uint32_t)p->subclass<<8)|p->prog_if);serial_str(" irq=");hex(p->irq);serial_str("\n");}
}
