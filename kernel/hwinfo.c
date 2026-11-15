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
static uint8_t classify_pci(uint8_t cl,uint8_t sub){
 if(cl==1)return HWINFO_PCI_STORAGE;if(cl==2)return HWINFO_PCI_NETWORK;if(cl==3)return HWINFO_PCI_DISPLAY;
 if(cl==4)return HWINFO_PCI_AUDIO;if(cl==6)return HWINFO_PCI_BRIDGE;if(cl==12&&sub==3)return HWINFO_PCI_USB;
 if(cl==9)return HWINFO_PCI_INPUT;return HWINFO_PCI_OTHER;
}
static const char *pci_kind_name(uint8_t k){switch(k){case HWINFO_PCI_STORAGE:return "storage";case HWINFO_PCI_NETWORK:return "network";case HWINFO_PCI_DISPLAY:return "display";case HWINFO_PCI_AUDIO:return "audio";case HWINFO_PCI_BRIDGE:return "bridge";case HWINFO_PCI_USB:return "usb";case HWINFO_PCI_INPUT:return "input";default:return "other";}}
static void scan_pci(void){
 for(uint16_t bus=0;bus<256&&inv.pci_count<HWINFO_MAX_PCI;bus++)for(uint8_t slot=0;slot<32&&inv.pci_count<HWINFO_MAX_PCI;slot++){
  uint32_t id=pci_read((uint8_t)bus,slot,0,0);uint16_t vendor=(uint16_t)id;if(vendor==0xFFFF)continue;
  uint32_t h=pci_read((uint8_t)bus,slot,0,0x0C);uint8_t funcs=((h>>23)&1)?8:1;
  for(uint8_t fn=0;fn<funcs&&inv.pci_count<HWINFO_MAX_PCI;fn++){
   id=pci_read((uint8_t)bus,slot,fn,0);vendor=(uint16_t)id;if(vendor==0xFFFF)continue;
   struct hwinfo_pci_device *p=&inv.pci[inv.pci_count++];p->vendor=vendor;p->device=(uint16_t)(id>>16);p->bus=(uint8_t)bus;p->slot=slot;p->function=fn;
   uint32_t cls=pci_read((uint8_t)bus,slot,fn,8);p->revision=(uint8_t)cls;p->prog_if=(uint8_t)(cls>>8);p->subclass=(uint8_t)(cls>>16);p->class_code=(uint8_t)(cls>>24);p->kind=classify_pci(p->class_code,p->subclass);
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
static uint8_t sum8(const volatile uint8_t *p,uint32_t n){
 uint8_t s=0;
 while(n--)s=(uint8_t)(s+*p++);
 return s;
}
static int sig_eq(const volatile char *p,const char *s,int n){
 for(int i=0;i<n;i++)if(p[i]!=s[i])return 0;
 return 1;
}

/* The kernel is currently 32-bit and identity-addresses physical memory.
   Reject impossible ranges before dereferencing firmware-supplied addresses. */
static int physical_range_valid(uint64_t address,uint32_t length){
 return address>=0x1000u && length!=0 &&
        address<=0xFFFFFFFFull &&
        address+(uint64_t)length<=0x100000000ull;
}
static int acpi_sdt_bounds(uint32_t address,uint32_t max_length,uint32_t *length){
 uint32_t n;
 if(!length || (address&3u)!=0 || !physical_range_valid(address,36u))return 0;
 n=*(volatile uint32_t*)(address+4u);
 if(n<36u || n>max_length || !physical_range_valid(address,n))return 0;
 *length=n;
 return 1;
}
static uint32_t find_rsdp_range(uint32_t start,uint32_t end){
 uint32_t a;
 if(end<=start || end-start<20u)return 0;
 a=(start+15u)&~15u;
 for(;a<=end-20u;a+=16u){
  volatile uint8_t *p=(volatile uint8_t*)a;
  if(!sig_eq((volatile char*)p,"RSD PTR ",8) || sum8(p,20u)!=0)continue;
  if(p[15]>=2u){
   uint32_t length;
   if(a>end-36u)continue;
   length=*(volatile uint32_t*)(a+20u);
   if(length<36u || length>4096u || length>end-a || sum8(p,length)!=0)continue;
  }
  return a;
 }
 return 0;
}
static void scan_acpi(void){
 volatile uint16_t *ebda_seg=(volatile uint16_t*)0x40E;
 uint32_t ebda=(uint32_t)(*ebda_seg)<<4;
 uint32_t r=0,rsdt=0,root=0,root_length=0,entry_size=4;
 uint32_t rsdt_length=0;
 if(ebda>=0x400u && ebda<0xA0000u){
  uint32_t ebda_end=ebda+1024u;
  if(ebda_end>0xA0000u)ebda_end=0xA0000u;
  r=find_rsdp_range(ebda,ebda_end);
 }
 if(!r)r=find_rsdp_range(0xE0000u,0x100000u);
 if(!r)return;

 inv.rsdp_address=r;
 inv.acpi_revision=*((volatile uint8_t*)(r+15u));
 inv.acpi_valid=1;
 rsdt=*(volatile uint32_t*)(r+16u);
 if(rsdt && acpi_sdt_bounds(rsdt,65536u,&rsdt_length) &&
    sig_eq((volatile char*)rsdt,"RSDT",4) &&
    sum8((volatile uint8_t*)rsdt,rsdt_length)==0){
  inv.rsdt_address=rsdt;
  root=rsdt;
  root_length=rsdt_length;
 }

 /* A 32-bit kernel cannot dereference an XSDT entry above 4 GiB. Keep the
    full-width value until it has been checked; never truncate it into a
    plausible-looking low address. Fall back to a valid RSDT when necessary. */
 if(inv.acpi_revision>=2u){
  uint32_t rsdp_length=*(volatile uint32_t*)(r+20u);
  if(rsdp_length>=36u && rsdp_length<=4096u &&
     sum8((volatile uint8_t*)r,rsdp_length)==0){
   uint64_t xsdt64=*(volatile uint64_t*)(r+24u);
   if(xsdt64 && xsdt64<=0xFFFFFFFFull){
    uint32_t xsdt=(uint32_t)xsdt64,xsdt_length=0;
    if(acpi_sdt_bounds(xsdt,65536u,&xsdt_length) &&
       sig_eq((volatile char*)xsdt,"XSDT",4) &&
       sum8((volatile uint8_t*)xsdt,xsdt_length)==0){
     inv.xsdt_address=xsdt;
     root=xsdt;
     root_length=xsdt_length;
     entry_size=8u;
    }
   }
  }
 }
 if(!root)return;

 {
  uint32_t entries_bytes=root_length-36u;
  uint32_t count=entries_bytes/entry_size;
  if(entries_bytes%entry_size!=0)return;
  for(uint32_t i=0;i<count && inv.acpi_table_count<HWINFO_MAX_ACPI_TABLES;i++){
   uint64_t table64;
   uint32_t address,table_length=0;
   if(entry_size==8u)
    table64=*(volatile uint64_t*)(root+36u+i*8u);
   else
    table64=*(volatile uint32_t*)(root+36u+i*4u);
   if(!table64 || table64>0xFFFFFFFFull)continue;
   address=(uint32_t)table64;
   if(!acpi_sdt_bounds(address,0x100000u,&table_length))continue;
   struct hwinfo_acpi_table *t=&inv.acpi[inv.acpi_table_count++];
   for(int j=0;j<4;j++)t->signature[j]=*((volatile char*)(address+(uint32_t)j));
   t->signature[4]=0;
   t->address=address;
   t->length=table_length;
   t->checksum_ok=(uint8_t)(sum8((volatile uint8_t*)address,table_length)==0);
  }
 }
}
static void scan_smbios(void){
 for(uint32_t a=0xF0000u;a+0x20u<=0x100000u;a+=16u){
  volatile uint8_t *p=(volatile uint8_t*)a;
  if(sig_eq((volatile char*)p,"_SM_",4)){
   uint8_t length=p[5];
   if(length<0x1Fu || length>0x40u || length>0x100000u-a)continue;
   if(sum8(p,length)!=0 || !sig_eq((volatile char*)(a+16u),"_DMI_",5) ||
      sum8((volatile uint8_t*)(a+16u),15u)!=0)continue;
   inv.smbios_address=a;
   inv.smbios_valid=1;
   {
    uint8_t major=p[6],minor=p[7];
    inv.smbios_version[0]=(char)('0'+major);
    inv.smbios_version[1]='.';
    inv.smbios_version[2]=(char)('0'+minor);
    inv.smbios_version[3]=0;
   }
   return;
  }
  if(sig_eq((volatile char*)p,"_SM3_",5)){
   uint8_t length=p[6];
   if(length<0x18u || length>0x40u || length>0x100000u-a)continue;
   if(sum8(p,length)!=0)continue;
   inv.smbios_address=a;
   inv.smbios_valid=1;
   {
    uint8_t major=p[7],minor=p[8];
    inv.smbios_version[0]=(char)('0'+major);
    inv.smbios_version[1]='.';
    inv.smbios_version[2]=(char)('0'+minor);
    inv.smbios_version[3]=0;
   }
   return;
  }
 }
}
void hwinfo_init(void){if(initialized)return;initialized=1;inv.bios_drive=*(volatile uint8_t*)BOOT_DRIVE_ADDR;detect_cpu();scan_memory();scan_pci();scan_acpi();scan_smbios();}
const struct hwinfo_inventory *hwinfo_get(void){hwinfo_init();return &inv;}
static void hex(uint32_t v){extern void serial_str(const char*);char b[9];for(int i=0;i<8;i++){uint8_t x=(v>>(28-i*4))&15;b[i]="0123456789ABCDEF"[x];}b[8]=0;serial_str(b);}
void hwinfo_report(void){
 extern void serial_str(const char*);const struct hwinfo_inventory *h=hwinfo_get();
 serial_str("hw: CPU ");serial_str(h->cpu.vendor);serial_str(" signature=");hex(h->cpu.signature);serial_str("\n");
 serial_str("hw: cpuid=");serial_str(h->cpu.has_cpuid?"yes":"no");serial_str(" PAE=");serial_str(h->cpu.has_pae?"yes":"no");serial_str(" APIC=");serial_str(h->cpu.has_apic?"yes":"no");serial_str(" SSE2=");serial_str(h->cpu.has_sse2?"yes":"no");serial_str("\n");
 serial_str("hw: BIOS disk=0x");hex(h->bios_drive);serial_str(" E820 ranges=");hex(h->memory_count);serial_str(" usable bytes=");hex((uint32_t)(h->usable_memory_bytes>>32));hex((uint32_t)h->usable_memory_bytes);serial_str("\n");
 serial_str("hw: PCI devices=");hex(h->pci_count);serial_str("\n");
 for(uint32_t i=0;i<h->pci_count;i++){const struct hwinfo_pci_device*p=&h->pci[i];serial_str(" pci ");hex(p->bus);serial_str(":");hex(p->slot);serial_str(".");hex(p->function);serial_str(" id=");hex(((uint32_t)p->vendor<<16)|p->device);serial_str(" type=");serial_str(pci_kind_name(p->kind));serial_str(" class=");hex(((uint32_t)p->class_code<<16)|((uint32_t)p->subclass<<8)|p->prog_if);serial_str(" irq=");hex(p->irq);serial_str("\n");}
}
