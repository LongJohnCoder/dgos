#include "apic.h"
#include "types.h"
#include "gdt.h"
#include "bios_data.h"
#include "control_regs.h"
#include "interrupts.h"
#include "irq.h"
#include "idt.h"
#include "thread_impl.h"
#include "mm.h"
#include "cpuid.h"
#include "string.h"
#include "atomic.h"
#include "printk.h"
#include "likely.h"
#include "time.h"
#include "cpuid.h"
#include "spinlock.h"
#include "assert.h"

//
// MP Tables

typedef struct mp_table_hdr_t {
    char sig[4];
    uint32_t phys_addr;
    uint8_t length;
    uint8_t spec;
    uint8_t checksum;
    uint8_t features[5];
} mp_table_hdr_t;

typedef struct mp_cfg_tbl_hdr_t {
    char sig[4];
    uint16_t base_tbl_len;
    uint8_t spec_rev;
    uint8_t checksum;
    char oem_id_str[8];
    char prod_id_str[12];
    uint32_t oem_table_ptr;
    uint16_t oem_table_size;
    uint16_t entry_count;
    uint32_t apic_addr;
    uint16_t ext_tbl_len;
    uint8_t ext_tbl_checksum;
    uint8_t reserved;
} mp_cfg_tbl_hdr_t;

// entry_type 0
typedef struct mp_cfg_cpu_t {
    uint8_t entry_type;
    uint8_t apic_id;
    uint8_t apic_ver;
    uint8_t flags;
    uint32_t signature;
    uint32_t features;
    uint32_t reserved1;
    uint32_t reserved2;
} mp_cfg_cpu_t;

#define MP_CPU_FLAGS_ENABLED_BIT    0
#define MP_CPU_FLAGS_BSP_BIT        1

#define MP_CPU_FLAGS_ENABLED        (1<<MP_CPU_FLAGS_ENABLED_BIT)
#define MP_CPU_FLAGS_BSP            (1<<MP_CPU_FLAGS_BSP_BIT)

// entry_type 1
typedef struct mp_cfg_bus_t {
    uint8_t entry_type;
    uint8_t bus_id;
    char type[6];
} mp_cfg_bus_t;

// entry_type 2
typedef struct mp_cfg_ioapic_t {
    uint8_t entry_type;
    uint8_t id;
    uint8_t ver;
    uint8_t flags;
    uint32_t addr;
} mp_cfg_ioapic_t;

#define MP_IOAPIC_FLAGS_ENABLED_BIT   0

#define MP_IOAPIC_FLAGS_ENABLED       (1<<MP_IOAPIC_FLAGS_ENABLED_BIT)

// entry_type 3 and 4 flags

#define MP_INTR_FLAGS_POLARITY_BIT    0
#define MP_INTR_FLAGS_POLARITY_BITS   2
#define MP_INTR_FLAGS_TRIGGER_BIT     2
#define MP_INTR_FLAGS_TRIGGER_BITS    2

#define MP_INTR_FLAGS_TRIGGER_MASK \
        ((1<<MP_INTR_FLAGS_TRIGGER_BITS)-1)

#define MP_INTR_FLAGS_POLARITY_MASK   \
        ((1<<MP_INTR_FLAGS_POLARITY_BITS)-1)

#define MP_INTR_FLAGS_TRIGGER \
        (MP_INTR_FLAGS_TRIGGER_MASK<<MP_INTR_FLAGS_TRIGGER_BITS)

#define MP_INTR_FLAGS_POLARITY \
        (MP_INTR_FLAGS_POLARITY_MASK << \
        MP_INTR_FLAGS_POLARITY_BITS)

#define MP_INTR_FLAGS_POLARITY_DEFAULT    0
#define MP_INTR_FLAGS_POLARITY_ACTIVEHI   1
#define MP_INTR_FLAGS_POLARITY_ACTIVELO   3

#define MP_INTR_FLAGS_TRIGGER_DEFAULT     0
#define MP_INTR_FLAGS_TRIGGER_EDGE        1
#define MP_INTR_FLAGS_TRIGGER_LEVEL       3

#define MP_INTR_FLAGS_POLARITY_n(n)   ((n)<<MP_INTR_FLAGS_POLARITY_BIT)
#define MP_INTR_FLAGS_TRIGGER_n(n)    ((n)<<MP_INTR_FLAGS_TRIGGER_BIT)

#define MP_INTR_TYPE_APIC   0
#define MP_INTR_TYPE_NMI    1
#define MP_INTR_TYPE_SMI    2
#define MP_INTR_TYPE_EXTINT 3

//
// IOAPIC registers

#define IOAPIC_IOREGSEL         0
#define IOAPIC_IOREGWIN         4

#define IOAPIC_REG_ID           0
#define IOAPIC_REG_VER          1
#define IOAPIC_REG_ARB          2

#define IOAPIC_VER_VERSION_BIT  0
#define IOAPIC_VER_VERSION_BITS 8
#define IOAPIC_VER_VERSION_MASK ((1<<IOAPIC_VER_VERSION_BITS)-1)
#define IOAPIC_VER_VERSION      \
    (IOAPIC_VER_VERSION_MASK<<IOAPIC_VER_VERSION_BIT)
#define IOAPIC_VER_VERSION_n(n) ((n)<<IOAPIC_VER_VERSION_BIT)

#define IOAPIC_VER_ENTRIES_BIT  16
#define IOAPIC_VER_ENTRIES_BITS 8
#define IOAPIC_VER_ENTRIES_MASK ((1<<IOAPIC_VER_ENTRIES_BITS)-1)
#define IOAPIC_VER_ENTRIES      \
    (IOAPIC_VER_ENTRIES_MASK<<IOAPIC_VER_ENTRIES_BIT)
#define IOAPIC_VER_ENTRIES_n(n) ((n)<<IOAPIC_VER_ENTRIES_BIT)

#define IOAPIC_RED_LO_n(n)      (0x10 + (n) * 2)
#define IOAPIC_RED_HI_n(n)      (0x10 + (n) * 2 + 1)

#define IOAPIC_REDLO_VECTOR_BIT     0
#define IOAPIC_REDLO_DELIVERY_BIT   8
#define IOAPIC_REDLO_DESTMODE_BIT   11
#define IOAPIC_REDLO_STATUS_BIT     12
#define IOAPIC_REDLO_POLARITY_BIT   13
#define IOAPIC_REDLO_REMOTEIRR_BIT  14
#define IOAPIC_REDLO_TRIGGER_BIT    15
#define IOAPIC_REDLO_MASKIRQ_BIT       16
#define IOAPIC_REDHI_DEST_BIT       (56-32)

#define IOAPIC_REDLO_DESTMODE       (1<<IOAPIC_REDLO_DESTMODE_BIT)
#define IOAPIC_REDLO_STATUS         (1<<IOAPIC_REDLO_STATUS_BIT)
#define IOAPIC_REDLO_POLARITY       (1<<IOAPIC_REDLO_POLARITY_BIT)
#define IOAPIC_REDLO_REMOTEIRR      (1<<IOAPIC_REDLO_REMOTEIRR_BIT)
#define IOAPIC_REDLO_TRIGGER        (1<<IOAPIC_REDLO_TRIGGER_BIT)
#define IOAPIC_REDLO_MASKIRQ        (1<<IOAPIC_REDLO_MASKIRQ_BIT)

#define IOAPIC_REDLO_VECTOR_BITS    8
#define IOAPIC_REDLO_DELVERY_BITS   3
#define IOAPIC_REDHI_DEST_BITS      8

#define IOAPIC_REDLO_VECTOR_MASK    ((1<<IOAPIC_REDLO_VECTOR_BITS)-1)
#define IOAPIC_REDLO_DELVERY_MASK   ((1<<IOAPIC_REDLO_DELVERY_BITS)-1)
#define IOAPIC_REDHI_DEST_MASK      ((1<<IOAPIC_REDHI_DEST_BITS)-1)

#define IOAPIC_REDLO_VECTOR \
    (IOAPIC_REDLO_VECTOR_MASK<<IOAPIC_REDLO_VECTOR_BITS)
#define IOAPIC_REDLO_DELVERY \
    (IOAPIC_REDLO_DELVERY_MASK<<IOAPIC_REDLO_DELVERY_BITS)
#define IOAPIC_REDHI_DEST \
    (IOAPIC_REDHI_DEST_MASK<<IOAPIC_REDHI_DEST_BITS)

#define IOAPIC_REDLO_VECTOR_n(n)    ((n)<<IOAPIC_REDLO_VECTOR_BIT)
#define IOAPIC_REDLO_DELIVERY_n(n)  ((n)<<IOAPIC_REDLO_DELIVERY_BIT)
#define IOAPIC_REDLO_TRIGGER_n(n)   ((n)<<IOAPIC_REDLO_TRIGGER_BIT)
#define IOAPIC_REDHI_DEST_n(n)      ((n)<<IOAPIC_REDHI_DEST_BIT)
#define IOAPIC_REDLO_POLARITY_n(n)  ((n)<<IOAPIC_REDLO_POLARITY_BIT)

#define IOAPIC_REDLO_DELIVERY_APIC      0
#define IOAPIC_REDLO_DELIVERY_LOWPRI    1
#define IOAPIC_REDLO_DELIVERY_SMI       2
#define IOAPIC_REDLO_DELIVERY_NMI       4
#define IOAPIC_REDLO_DELIVERY_INIT      5
#define IOAPIC_REDLO_DELIVERY_EXTINT    7

#define IOAPIC_REDLO_TRIGGER_EDGE   0
#define IOAPIC_REDLO_TRIGGER_LEVEL  1

#define IOAPIC_REDLO_POLARITY_ACTIVELO  1
#define IOAPIC_REDLO_POLARITY_ACTIVEHI  0

static char const * const intr_type_text[] = {
    "APIC",
    "NMI",
    "SMI",
    "EXTINT"
};

// entry_type 3
typedef struct mp_cfg_iointr_t {
    uint8_t entry_type;
    uint8_t type;
    uint16_t flags;
    uint8_t source_bus;
    uint8_t source_bus_irq;
    uint8_t dest_ioapic_id;
    uint8_t dest_ioapic_intin;
} mp_cfg_iointr_t;

// entry_type 4
typedef struct mp_cfg_lintr_t {
    uint8_t entry_type;
    uint8_t type;
    uint16_t flags;
    uint8_t source_bus;
    uint8_t source_bus_irq;
    uint8_t dest_lapic_id;
    uint8_t dest_lapic_lintin;
} mp_cfg_lintr_t;

// entry_type 128
typedef struct mp_cfg_addrmap_t {
    uint8_t entry_type;
    uint8_t len;
    uint8_t bus_id;
    uint8_t addr_type;
    uint32_t addr_lo;
    uint32_t addr_hi;
    uint32_t len_lo;
    uint32_t len_hi;
} mp_cfg_addrmap_t;

// entry_type 129
typedef struct mp_cfg_bushier_t {
    uint8_t entry_type;
    uint8_t len;
    uint8_t bus_id;
    uint8_t info;
    uint8_t parent_bus;
    uint8_t reserved[3];
} mp_cfg_bushier_t;

// entry_type 130
typedef struct mp_cfg_buscompat_t {
    uint8_t entry_type;
    uint8_t len;
    uint8_t bus_id;
    uint8_t bus_mod;
    uint32_t predef_range_list;
} mp_cfg_buscompat_t;

typedef struct mp_bus_irq_mapping_t {
    uint8_t bus;
    uint8_t intr_type;
    uint8_t flags;
    uint8_t device;
    uint8_t irq;
    uint8_t ioapic_id;
    uint8_t intin;
} mp_bus_irq_mapping_t;

typedef struct mp_ioapic_t {
    uint8_t id;
    uint8_t base_intr;
    uint8_t vector_count;
    uint8_t base_irq;
    uint32_t addr;
    uint32_t volatile *ptr;
    spinlock_t lock;
} mp_ioapic_t;

static char *mp_tables;

#define MAX_PCI_BUSSES 16
static uint8_t mp_pci_bus_ids[MAX_PCI_BUSSES];
static uint8_t mp_pci_bus_count;
static uint16_t mp_isa_bus_id;

static mp_bus_irq_mapping_t bus_irq_list[64];
static uint8_t bus_irq_count;

static uint8_t bus_irq_to_mapping[64];

static mp_ioapic_t ioapic_list[16];
static unsigned ioapic_count;

static uint8_t ioapic_next_free_vector = INTR_APIC_SPURIOUS - 1;
static uint8_t ioapic_next_irq_base = 16;

static uint8_t isa_irq_lookup[16];

static uint8_t apic_id_list[64];
static unsigned apic_id_count;

static uint8_t topo_thread_bits;
static uint8_t topo_thread_count;
static uint8_t topo_core_bits;
static uint8_t topo_core_count;

static uint8_t topo_cpu_count;

static uintptr_t apic_base;
static uint32_t volatile *apic_ptr;

#define MP_TABLE_TYPE_CPU       0
#define MP_TABLE_TYPE_BUS       1
#define MP_TABLE_TYPE_IOAPIC    2
#define MP_TABLE_TYPE_IOINTR    3
#define MP_TABLE_TYPE_LINTR     4
#define MP_TABLE_TYPE_ADDRMAP   128
#define MP_TABLE_TYPE_BUSHIER   129
#define MP_TABLE_TYPE_BUSCOMPAT 130

//
// APIC

#define APIC_REG(n)     apic_ptr[(n)>>2]
#define APIC_BIT(r,n)   (APIC_REG((r) + (((n)>>5)<<2)) & \
                            (1<<((n)&31)))

// APIC ID
#define APIC_ID         APIC_REG(0x20)

// APIC version
#define APIC_VER        APIC_REG(0x30)

// Task Priority Register
#define APIC_TPR        APIC_REG(0x80)

// Arbitration Priority Register
#define APIC_APR        APIC_REG(0x90)

// Processor Priority Register
#define APIC_PPR        APIC_REG(0xA0)

// End Of Interrupt register
#define APIC_EOI        APIC_REG(0xB0)

// Logical Destination Register
#define APIC_LDR        APIC_REG(0xD0)

// Destination Format Register
#define APIC_DFR        APIC_REG(0xE0)

// Spuriout Interrupt Register
#define APIC_SIR        APIC_REG(0xF0)

// In Service Registers (256 individual bits)
#define APIC_ISR_n(n)   APIC_BIT(0x100, n)

// Trigger Mode Registers (256 bits)
#define APIC_TMR_n(n)   APIC_BIT(0x180, n)

// Interrupt Request Registers (256 bits)
#define APIC_IRR_n(n)   APIC_BIT(0x200, n)

// Error Status Register
#define APIC_ESR        APIC_REG(0x280)

// Local Vector Table Corrected Machine Check Interrupt
#define APIC_LVT_CMCI   APIC_REG(0x2F0)

// Local Vector Table Interrupt Command Register Low
#define APIC_ICR_LO     APIC_REG(0x300)

// Local Vector Table Interrupt Command Register High
#define APIC_ICR_HI     APIC_REG(0x310)

// Local Vector Table Timer Register
#define APIC_LVT_TR     APIC_REG(0x320)

// Local Vector Table Thermal Sensor Register
#define APIC_LVT_TSR    APIC_REG(0x330)

// Local Vector Table Performance Monitoring Counter Register
#define APIC_LVT_PMCR   APIC_REG(0x340)

// Local Vector Table Local Interrupt 0 Register
#define APIC_LVT_LNT0   APIC_REG(0x350)

// Local Vector Table Local Interrupt 1 Register
#define APIC_LVT_LNT1   APIC_REG(0x360)

// Local Vector Table Error Register
#define APIC_LVT_ERR    APIC_REG(0x370)

// Local Vector Table Timer Initial Count Register
#define APIC_LVT_ICR    APIC_REG(0x380)

// Local Vector Table Timer Current Count Register
#define APIC_LVT_CCR    APIC_REG(0x390)

// Local Vector Table Timer Divide Configuration Register
#define APIC_LVT_DCR    APIC_REG(0x3E0)

#define APIC_CMD        APIC_ICR_LO
#define APIC_DEST       APIC_ICR_HI

#define APIC_DEST_BIT               24
#define APIC_DEST_BITS              8
#define APIC_DEST_MASK              ((1U << APIC_DEST_BITS)-1U)
#define APIC_DEST_n(n)              \
    (((n) & APIC_DEST_MASK) << APIC_DEST_BIT)

#define APIC_CMD_SIPI_PAGE_BIT      0
#define APIC_CMD_VECTOR_BIT         0
#define APIC_CMD_DEST_MODE_BIT      8
#define APIC_CMD_DEST_LOGICAL_BIT   11
#define APIC_CMD_PENDING_BIT        12
#define APIC_CMD_ILD_CLR_BIT        14
#define APIC_CMD_ILD_SET_BIT        15
#define APIC_CMD_DEST_TYPE_BIT      18

#define APIC_CMD_VECTOR_BITS        8
#define APIC_CMD_SIPI_PAGE_BITS     8
#define APIC_CMD_DEST_MODE_BITS     3
#define APIC_CMD_DEST_TYPE_BITS     2

#define APIC_CMD_VECTOR_MASK        ((1 << APIC_CMD_VECTOR_BITS)-1)
#define APIC_CMD_SIPI_PAGE_MASK     ((1 << APIC_CMD_SIPI_PAGE_BITS)-1)
#define APIC_CMD_DEST_MODE_MASK     ((1 << APIC_CMD_DEST_MODE_BITS)-1)
#define APIC_CMD_DEST_TYPE_MASK     ((1 << APIC_CMD_DEST_TYPE_BITS)-1)

#define APIC_CMD_SIPI_PAGE      \
    (APIC_CMD_SIPI_PAGE_MASK << APIC_CMD_SIPI_PAGE_BIT)
#define APIC_CMD_DEST_MODE      \
    (APIC_CMD_DEST_MODE_MASK << APIC_CMD_DEST_MODE_BIT)
#define APIC_CMD_SIPI_PAGE      \
    (APIC_CMD_SIPI_PAGE_MASK << APIC_CMD_SIPI_PAGE_BIT)

#define APIC_CMD_SIPI_PAGE_n(n) ((n) << APIC_CMD_SIPI_PAGE_BIT)
#define APIC_CMD_DEST_MODE_n(n) ((n) << APIC_CMD_DEST_MODE_BIT)
#define APIC_CMD_DEST_TYPE_n(n) ((n) << APIC_CMD_DEST_TYPE_BIT)
#define APIC_CMD_SIPI_PAGE_n(n) ((n) << APIC_CMD_SIPI_PAGE_BIT)

#define APIC_CMD_VECTOR_n(n)    \
    (((n) & APIC_CMD_VECTOR_MASK) << APIC_CMD_VECTOR_BIT)

#define APIC_CMD_VECTOR         (1U << APIC_CMD_VECTOR_BIT)
#define APIC_CMD_DEST_LOGICAL   (1U << APIC_CMD_DEST_LOGICAL_BIT)
#define APIC_CMD_PENDING        (1U << APIC_CMD_PENDING_BIT)
#define APIC_CMD_ILD_CLR        (1U << APIC_CMD_ILD_CLR_BIT)
#define APIC_CMD_ILD_SET        (1U << APIC_CMD_ILD_SET_BIT)
#define APIC_CMD_DEST_TYPE      (1U << APIC_CMD_DEST_TYPE_BIT)

#define APIC_CMD_DEST_MODE_NORMAL   APIC_CMD_DEST_MODE_n(0)
#define APIC_CMD_DEST_MODE_LOWPRI   APIC_CMD_DEST_MODE_n(1)
#define APIC_CMD_DEST_MODE_SMI      APIC_CMD_DEST_MODE_n(2)
#define APIC_CMD_DEST_MODE_NMI      APIC_CMD_DEST_MODE_n(4)
#define APIC_CMD_DEST_MODE_INIT     APIC_CMD_DEST_MODE_n(5)
#define APIC_CMD_DEST_MODE_SIPI     APIC_CMD_DEST_MODE_n(6)

#define APIC_CMD_DEST_TYPE_BYID     APIC_CMD_DEST_TYPE_n(0)
#define APIC_CMD_DEST_TYPE_SELF     APIC_CMD_DEST_TYPE_n(1)
#define APIC_CMD_DEST_TYPE_ALL      APIC_CMD_DEST_TYPE_n(2)
#define APIC_CMD_DEST_TYPE_OTHER    APIC_CMD_DEST_TYPE_n(3)

// Divide configuration register
#define APIC_LVT_DCR_BY_2           0
#define APIC_LVT_DCR_BY_4           1
#define APIC_LVT_DCR_BY_8           2
#define APIC_LVT_DCR_BY_16          3
#define APIC_LVT_DCR_BY_32          (8+0)
#define APIC_LVT_DCR_BY_64          (8+1)
#define APIC_LVT_DCR_BY_128         (8+2)
#define APIC_LVT_DCR_BY_1           (8+3)

#define APIC_SIR_APIC_ENABLE_BIT    8
#define APIC_SIR_APIC_ENABLE        (1<<APIC_SIR_APIC_ENABLE_BIT)

#define APIC_LVT_TR_MODE_BIT        17
#define APIC_LVT_TR_MODE_BITS       2
#define APIC_LVT_TR_MODE_MASK       ((1U<<APIC_LVT_TR_MODE_BITS)-1U)
#define APIC_LVT_TR_MODE_n(n)       ((n)<<APIC_LVT_TR_MODE_BIT)

#define APIC_LVT_TR_MODE_ONESHOT    0
#define APIC_LVT_TR_MODE_PERIODIC   1
#define APIC_LVT_TR_MODE_DEADLINE   2

#define APIC_LVT_MASK_BIT       16
#define APIC_LVT_PENDING_BIT    12
#define APIC_LVT_LEVEL_BIT      15
#define APIC_LVT_REMOTEIRR_BIT  14
#define APIC_LVT_ACTIVELOW_BIT  13
#define APIC_LVT_DELIVERY_BIT   8
#define APIC_LVT_DELIVERY_BITS  3

#define APIC_LVT_MASK           (1U<<APIC_LVT_MASK_BIT)
#define APIC_LVT_PENDING        (1U<<APIC_LVT_PENDING_BIT)
#define APIC_LVT_LEVEL          (1U<<APIC_LVT_LEVEL_BIT)
#define APIC_LVT_REMOTEIRR      (1U<<APIC_LVT_REMOTEIRR_BIT)
#define APIC_LVT_ACTIVELOW      (1U<<APIC_LVT_ACTIVELOW_BIT)
#define APIC_LVT_DELIVERY_MASK  ((1U<<APIC_LVT_DELIVERY_BITS)-1)
#define APIC_LVT_DELIVERY_n(n)  ((n)<<APIC_LVT_DELIVERY_BIT)

#define APIC_LVT_DELIVERY_FIXED 0
#define APIC_LVT_DELIVERY_SMI   2
#define APIC_LVT_DELIVERY_NMI   4
#define APIC_LVT_DELIVERY_EXINT 7
#define APIC_LVT_DELIVERY_INIT  5

#define APIC_LVT_VECTOR_BIT     0
#define APIC_LVT_VECTOR_BITS    8
#define APIC_LVT_VECTOR_MASK    ((1U<<APIC_LVT_VECTOR_BITS)-1U)
#define APIC_LVT_VECTOR_n(n)    ((n)<<APIC_LVT_VECTOR_BIT)

#define APIC_BASE_MSR  0x1B

#define APIC_BASE_ADDR_BIT      12
#define APIC_BASE_ADDR_BITS     40
#define APIC_BASE_GENABLE_BIT   11
#define APIC_BASE_BSP_BIT       8

#define APIC_BASE_GENABLE       (1UL<<APIC_BASE_GENABLE_BIT)
#define APIC_BASE_BSP           (1UL<<APIC_BASE_BSP_BIT)
#define APIC_BASE_ADDR_MASK     ((1UL<<APIC_BASE_ADDR_BITS)-1)
#define APIC_BASE_ADDR          (APIC_BASE_ADDR_MASK<<APIC_BASE_ADDR_BIT)

static int parse_mp_tables(void)
{
    void *mem_top =
            (uint16_t*)((uintptr_t)*BIOS_DATA_AREA(
                uint16_t, 0x40E) << 4);
    void *ranges[4] = {
        mem_top, (uint32_t*)0xA0000,
        0, 0
    };
    for (size_t pass = 0; !mp_tables && pass < 4; pass += 2) {
        if (pass == 2) {
            ranges[2] = mmap((void*)0xE0000, 0x20000, PROT_READ,
                             MAP_PHYSICAL, -1, 0);
            ranges[3] = (char*)ranges[2] + 0x20000;
        }
        for (mp_table_hdr_t const* sig_srch = ranges[pass];
             (void*)sig_srch < ranges[pass+1]; ++sig_srch) {
            // Check signature
            if (memcmp(sig_srch->sig, "_MP_", 4))
                continue;

            // Check checksum
            char *checked_sum_ptr = (char*)sig_srch;
            uint8_t checked_sum = 0;
            for (size_t i = 0; i < sizeof(*sig_srch); ++i)
                checked_sum += *checked_sum_ptr++;
            if (checked_sum != 0)
                continue;

            mp_tables = (char*)(uintptr_t)sig_srch->phys_addr;
            break;
        }
    }

    if (mp_tables) {
        mp_cfg_tbl_hdr_t *cth = mmap(mp_tables, sizeof(*cth),
                                     PROT_READ, MAP_PHYSICAL,
                                     -1, 0);

        uint8_t *entry = (uint8_t*)(cth + 1);

        // Reset to impossible values
        mp_isa_bus_id = -1;

        // First slot reserved for BSP
        apic_id_count = 1;

        for (uint16_t i = 0; i < cth->entry_count; ++i) {
            mp_cfg_cpu_t *entry_cpu;
            mp_cfg_bus_t *entry_bus;
            mp_cfg_ioapic_t *entry_ioapic;
            mp_cfg_iointr_t *entry_iointr;
            mp_cfg_lintr_t *entry_lintr;
            mp_cfg_addrmap_t *entry_addrmap;
            mp_cfg_bushier_t *entry_busheir;
            mp_cfg_buscompat_t *entry_buscompat;
            switch (*entry) {
            case MP_TABLE_TYPE_CPU:
                entry_cpu = (mp_cfg_cpu_t *)entry;

                printdbg("CPU package found, base apic id=%u ver=0x%x\n",
                         entry_cpu->apic_id, entry_cpu->apic_ver);

                if ((entry_cpu->flags & MP_CPU_FLAGS_ENABLED) &&
                        apic_id_count < countof(apic_id_list)) {
                    if (entry_cpu->flags & MP_CPU_FLAGS_BSP)
                        apic_id_list[0] = entry_cpu->apic_id;
                    else
                        apic_id_list[apic_id_count++] = entry_cpu->apic_id;
                }

                entry = (uint8_t*)(entry_cpu + 1);
                break;

            case MP_TABLE_TYPE_BUS:
                entry_bus = (mp_cfg_bus_t *)entry;

                printdbg("%.*s bus found, id=%u\n",
                         (int)sizeof(entry_bus->type),
                         entry_bus->type, entry_bus->bus_id);

                if (!memcmp(entry_bus->type, "PCI   ", 6)) {
                    if (mp_pci_bus_count < MAX_PCI_BUSSES) {
                        mp_pci_bus_ids[mp_pci_bus_count++] =
                                entry_bus->bus_id;
                    } else {
                        printdbg("Too many PCI busses!\n");
                    }
                } else if (!memcmp(entry_bus->type, "ISA   ", 6)) {
                    if (mp_isa_bus_id == 0xFFFF)
                        mp_isa_bus_id = entry_bus->bus_id;
                    else
                        printdbg("Too many ISA busses, only one supported\n");
                } else {
                    printdbg("Dropped! Unrecognized bus named \"%.*s\"\n",
                           (int)sizeof(entry_bus->type), entry_bus->type);
                }
                entry = (uint8_t*)(entry_bus + 1);
                break;

            case MP_TABLE_TYPE_IOAPIC:
                entry_ioapic = (mp_cfg_ioapic_t *)entry;

                if (entry_ioapic->flags & MP_IOAPIC_FLAGS_ENABLED) {
                    if (ioapic_count >= countof(ioapic_list)) {
                        printdbg("Dropped! Too many IOAPIC devices\n");
                        break;
                    }

                    printdbg("IOAPIC id=%d, addr=0x%x, flags=0x%x, ver=0x%x\n",
                             entry_ioapic->id,
                             entry_ioapic->addr,
                             entry_ioapic->flags,
                             entry_ioapic->ver);

                    mp_ioapic_t *ioapic = ioapic_list + ioapic_count++;

                    ioapic->id = entry_ioapic->id;
                    ioapic->addr = entry_ioapic->addr;

                    uint32_t volatile *ioapic_ptr = mmap(
                                (void*)(uintptr_t)entry_ioapic->addr,
                                12, PROT_READ | PROT_WRITE,
                                MAP_PHYSICAL |
                                MAP_NOCACHE |
                                MAP_WRITETHRU, -1, 0);

                    ioapic->ptr = ioapic_ptr;

                    // Read redirection table size

                    ioapic_ptr[IOAPIC_IOREGSEL] = IOAPIC_REG_VER;
                    uint32_t ioapic_ver = ioapic_ptr[IOAPIC_IOREGWIN];

                    uint8_t ioapic_intr_count =
                            (ioapic_ver >> IOAPIC_VER_ENTRIES_BIT) &
                            IOAPIC_VER_ENTRIES_MASK;

                    // Allocate virtual IRQ numbers
                    ioapic->base_irq = ioapic_next_irq_base;
                    ioapic_next_irq_base += ioapic_intr_count;

                    // Allocate vectors assign range to IOAPIC
                    ioapic_next_free_vector -= ioapic_intr_count;
                    ioapic->base_intr = ioapic_next_free_vector;
                    ioapic->vector_count = ioapic_intr_count;

                    ioapic->lock = 0;
                }
                entry = (uint8_t*)(entry_ioapic + 1);
                break;

            case MP_TABLE_TYPE_IOINTR:
                entry_iointr = (mp_cfg_iointr_t *)entry;

                if (memchr(mp_pci_bus_ids, entry_iointr->source_bus,
                            mp_pci_bus_count)) {
                    // PCI IRQ
                    uint8_t bus = entry_iointr->source_bus;
                    uint8_t intr_type = entry_iointr->type;
                    uint8_t intr_flags = entry_iointr->flags;
                    uint8_t device = entry_iointr->source_bus_irq >> 2;
                    uint8_t pci_irq = entry_iointr->source_bus_irq & 3;
                    uint8_t ioapic_id = entry_iointr->dest_ioapic_id;
                    uint8_t intin = entry_iointr->dest_ioapic_intin;

                    printdbg("PCI device %u INT_%c# ->"
                             " IOAPIC ID 0x%02x INTIN %d %s\n",
                           device, (int)(pci_irq) + 'A',
                           ioapic_id, intin,
                             intr_type < countof(intr_type_text) ?
                                 intr_type_text[intr_type] :
                                 "(invalid type!)");

                    if (bus_irq_count < countof(bus_irq_list)) {
                        bus_irq_list[bus_irq_count].bus = bus;
                        bus_irq_list[bus_irq_count].intr_type = intr_type;
                        bus_irq_list[bus_irq_count].flags = intr_flags;
                        bus_irq_list[bus_irq_count].device = device;
                        bus_irq_list[bus_irq_count].irq = pci_irq & 3;
                        bus_irq_list[bus_irq_count].ioapic_id = ioapic_id;
                        bus_irq_list[bus_irq_count].intin = intin;
                        ++bus_irq_count;
                    } else {
                        printdbg("Dropped! Too many PCI IRQ mappings\n");
                    }
                } else if (entry_iointr->source_bus == mp_isa_bus_id) {
                    // ISA IRQ

                    uint8_t bus =  entry_iointr->source_bus;
                    uint8_t intr_type = entry_iointr->type;
                    uint8_t intr_flags = entry_iointr->flags;
                    uint8_t isa_irq = entry_iointr->source_bus_irq;
                    uint8_t ioapic_id = entry_iointr->dest_ioapic_id;
                    uint8_t intin = entry_iointr->dest_ioapic_intin;

                    printdbg("ISA IRQ %d -> IOAPIC ID 0x%02x INTIN %u %s\n",
                           isa_irq, ioapic_id, intin,
                             intr_type < countof(intr_type_text) ?
                                 intr_type_text[intr_type] :
                                 "(invalid type!)");

                    if (bus_irq_count < countof(bus_irq_list)) {
                        isa_irq_lookup[isa_irq] = bus_irq_count;
                        bus_irq_list[bus_irq_count].bus = bus;
                        bus_irq_list[bus_irq_count].intr_type = intr_type;
                        bus_irq_list[bus_irq_count].flags = intr_flags;
                        bus_irq_list[bus_irq_count].device = 0;
                        bus_irq_list[bus_irq_count].irq = isa_irq;
                        bus_irq_list[bus_irq_count].ioapic_id = ioapic_id;
                        bus_irq_list[bus_irq_count].intin = intin;
                        ++bus_irq_count;
                    } else {
                        printdbg("Dropped! Too many ISA IRQ mappings\n");
                    }
                } else {
                    // Unknown bus!
                    printdbg("IRQ %d on unknown bus ->"
                             " IOAPIC ID 0x%02x INTIN %u type=%d\n",
                           entry_iointr->source_bus_irq,
                           entry_iointr->dest_ioapic_id,
                           entry_iointr->dest_ioapic_intin,
                           entry_iointr->type);
                }

                entry = (uint8_t*)(entry_iointr + 1);
                break;

            case MP_TABLE_TYPE_LINTR:
                entry_lintr = (mp_cfg_lintr_t*)entry;
                if (memchr(mp_pci_bus_ids, entry_lintr->source_bus,
                            mp_pci_bus_count)) {
                    uint8_t device = entry_lintr->source_bus_irq >> 2;
                    uint8_t pci_irq = entry_lintr->source_bus_irq;
                    uint8_t lapic_id = entry_lintr->dest_lapic_id;
                    uint8_t intin = entry_lintr->dest_lapic_lintin;
                    printdbg("PCI device %u INT_%c# -> LAPIC ID 0x%02x INTIN %d\n",
                           device, (int)(pci_irq & 3) + 'A',
                           lapic_id, intin);
                } else if (entry_lintr->source_bus == mp_isa_bus_id) {
                    uint8_t isa_irq = entry_lintr->source_bus_irq;
                    uint8_t lapic_id = entry_lintr->dest_lapic_id;
                    uint8_t intin = entry_lintr->dest_lapic_lintin;

                    printdbg("ISA IRQ %d -> LAPIC ID 0x%02x INTIN %u\n",
                           isa_irq, lapic_id, intin);
                } else {
                    // Unknown bus!
                    printdbg("IRQ %d on unknown bus -> IOAPIC ID 0x%02x INTIN %u\n",
                           entry_lintr->source_bus_irq,
                           entry_lintr->dest_lapic_id,
                           entry_lintr->dest_lapic_lintin);
                }
                entry = (uint8_t*)(entry_lintr + 1);
                break;

            case MP_TABLE_TYPE_ADDRMAP:
                entry_addrmap = (mp_cfg_addrmap_t*)entry;
                uint8_t bus = entry_addrmap->bus_id;
                uint64_t addr =  entry_addrmap->addr_lo |
                        ((uint64_t)entry_addrmap->addr_hi << 32);
                uint64_t len =  entry_addrmap->addr_lo |
                        ((uint64_t)entry_addrmap->addr_hi << 32);

                printdbg("Address map, bus=%d, addr=%lx, len=%lx\n",
                         bus, addr, len);

                entry += entry_addrmap->len;
                break;

            case MP_TABLE_TYPE_BUSHIER:
                entry_busheir = (mp_cfg_bushier_t*)entry;
                bus = entry_busheir->bus_id;
                uint8_t parent_bus = entry_busheir->parent_bus;
                uint8_t info = entry_busheir->info;

                printdbg("Bus hierarchy, bus=%d, parent=%d, info=%x\n",
                         bus, parent_bus, info);

                entry += entry_busheir->len;
                break;

            case MP_TABLE_TYPE_BUSCOMPAT:
                entry_buscompat = (mp_cfg_buscompat_t*)entry;
                bus = entry_buscompat->bus_id;
                uint8_t bus_mod = entry_buscompat->bus_mod;
                uint32_t bus_predef = entry_buscompat->predef_range_list;

                printdbg("Bus compat, bus=%d, mod=%d,"
                         " predefined_range_list=%x\n",
                         bus, bus_mod, bus_predef);

                entry += entry_buscompat->len;
                break;

            default:
                printdbg("Unknown MP table entry_type! Guessing size is 8\n");
                // Hope for the best here
                entry += 8;
                break;
            }
        }

        munmap(cth, sizeof(*cth));
    }

    if (ranges[2] != 0)
        munmap(ranges[2], 0x20000);

    return !!mp_tables;
}

static void *apic_timer_handler(int intr, void *ctx)
{
    apic_eoi(intr);
    return thread_schedule(ctx);
}

static void *apic_spurious_handler(int intr, void *ctx)
{
    apic_eoi(intr);
    printdbg("Spurious APIC interrupt!\n");
    return ctx;
}

unsigned apic_get_id(void)
{
    if (likely(apic_ptr))
        return APIC_ID;

    cpuid_t cpuid_info;
    cpuid(&cpuid_info, CPUID_INFO_FEATURES, 0);
    unsigned apic_id = cpuid_info.ebx >> 24;
    return apic_id;
}

static void apic_send_command(uint32_t dest, uint32_t cmd)
{
    APIC_DEST = dest;
    atomic_barrier();
    APIC_CMD = cmd;
    atomic_barrier();
    while (APIC_CMD & APIC_CMD_PENDING)
        pause();
}

// if target_apic_id is <= -2, sends to all CPUs
// if target_apic_id is == -1, sends to other CPUs
// if target_apid_id is >= 0, sends to specific APIC ID
void apic_send_ipi(int target_apic_id, uint8_t intr)
{
    if (unlikely(!apic_ptr))
        return;

    uint32_t dest_type = target_apic_id < -1
            ? APIC_CMD_DEST_TYPE_ALL
            : target_apic_id < 0
            ? APIC_CMD_DEST_TYPE_OTHER
            : APIC_CMD_DEST_TYPE_BYID;

    apic_send_command(target_apic_id >= 0
                      ? target_apic_id << 24
                      : 0,
                      APIC_CMD_VECTOR_n(intr) |
                      dest_type |
                      APIC_CMD_DEST_MODE_NORMAL);
}

void apic_eoi(int intr)
{
    APIC_EOI = intr;
}

static void apic_online(int enabled, int spurious_intr)
{
    uint32_t sir = APIC_SIR;

    if (enabled)
        sir |= APIC_SIR_APIC_ENABLE;
    else
        sir &= ~APIC_SIR_APIC_ENABLE;

    if (spurious_intr >= 32)
        sir = (sir & -256) | spurious_intr;

    APIC_SIR = sir;
}

static int apic_dump_impl(int reg)
{
    // Reserved registers:
    //  0x00, 0x10,
    return (reg & 0xF) == 0 &&
            !(reg >= 0x000 && reg <= 0x010) &&
            !(reg >= 0x040 && reg <= 0x070) &&
            !(reg >= 0x290 && reg <= 0x2E0) &&
            !(reg >= 0x3A0 && reg <= 0x3D0) &&
            !(reg == 0x3F0) &&
            (reg < 0x400) &&
            (reg >= 0) &&
            // Bochs does not like these
            (reg != 0x0C0) &&
            (reg != 0x2F0);
}

void apic_dump_regs(int ap)
{
    for (int i = 0; i < 256; i += 16) {
        printdbg("ap=%d APIC: ", ap);
        for (int x = 0; x < 16; x += 4) {
            if (apic_dump_impl((i + x) * 4)) {
                printdbg("[%3x]=%08x%s", (i + x) * 4,
                         apic_ptr[i + x],
                        x == 12 ? "\n" : " ");
            } else {
                printdbg("[%3x]=--------%s", (i + x) * 4,
                        x == 12 ? "\n" : " ");
            }
        }
    }
}

static void apic_configure_timer(
        uint32_t dcr, uint32_t icr, uint8_t timer_mode,
        uint8_t intr)
{
    APIC_LVT_DCR = dcr;
    atomic_barrier();
    APIC_LVT_TR = APIC_LVT_VECTOR_n(intr) |
            APIC_LVT_TR_MODE_n(timer_mode);
    atomic_barrier();
    APIC_LVT_ICR = icr;
}

int apic_init(int ap)
{
    if (!ap) {
        // Bootstrap CPU only

        assert(apic_base == 0);
        apic_base = msr_get(APIC_BASE_MSR);

        // Set global enable if it is clear
        if (!(apic_base & APIC_BASE_GENABLE)) {
            printdbg("APIC was globally disabled!"
                     " Enabling...\n");
            msr_set(APIC_BASE_MSR, apic_base |
                    APIC_BASE_GENABLE);
        }

        apic_base &= APIC_BASE_ADDR;

        assert(apic_ptr == 0);
        apic_ptr = mmap((void*)apic_base, 4096,
                        PROT_READ | PROT_WRITE,
                        MAP_PHYSICAL |
                        MAP_NOCACHE |
                        MAP_WRITETHRU, -1, 0);

        intr_hook(INTR_APIC_TIMER, apic_timer_handler);
        intr_hook(INTR_APIC_SPURIOUS, apic_spurious_handler);

        parse_mp_tables();
    }

    apic_online(1, INTR_APIC_SPURIOUS);

    APIC_TPR = 0;

    apic_configure_timer(APIC_LVT_DCR_BY_1,
                         ((1000000000U)/(60)),
                         APIC_LVT_TR_MODE_PERIODIC,
                         INTR_APIC_TIMER);

    assert(apic_base == (msr_get(APIC_BASE_MSR) & APIC_BASE_ADDR));

    apic_dump_regs(ap);

    return 1;
}

static void apic_detect_topology(void)
{
    cpuid_t info;

    if (!cpuid(&info, 4, 0)) {
        // Enable full CPUID
        uint64_t misc_enables = msr_get(MSR_IA32_MISC_ENABLES);
        if (misc_enables & (1L<<22)) {
            // Enable more CPUID support and retry
            misc_enables &= ~(1L<<22);
            msr_set(MSR_IA32_MISC_ENABLES, misc_enables);
        }
    }

    topo_thread_bits = 0;
    topo_core_bits = 0;
    topo_thread_count = 1;
    topo_core_count = 1;

    if (cpuid(&info, 1, 0)) {
        if ((info.edx >> 28) & 1) {
            // CPU supports hyperthreading

            // Thread count
            topo_thread_count = (info.ebx >> 16) & 0xFF;
            while ((1U << topo_thread_bits) < topo_thread_count)
                 ++topo_thread_bits;
        }

        if (cpuid(&info, 4, 0)) {
            topo_core_count = ((info.eax >> 26) & 0x3F) + 1;
            while ((1U << topo_core_bits) < topo_core_count)
                 ++topo_core_bits;
        }
    }

    if (topo_thread_bits >= topo_core_bits)
        topo_thread_bits -= topo_core_bits;
    else
        topo_thread_bits = 0;

    topo_thread_count /= topo_core_count;

    topo_cpu_count = apic_id_count *
            topo_core_count * topo_thread_count;
}

void apic_start_smp(void)
{
    printdbg("%d CPU packages\n", apic_id_count);

    apic_detect_topology();

    gdt_init_tss(topo_cpu_count);
    gdt_load_tr(0);

    // See if there are any other CPUs to start
    if (topo_thread_count * topo_core_count == 1 &&
            apic_id_count == 1)
        return;

    // Read address of MP entry trampoline from boot sector
    uint32_t *mp_trampoline_ptr = (uint32_t*)0x7c40;
    uint32_t mp_trampoline_addr = *mp_trampoline_ptr;
    uint32_t mp_trampoline_page = mp_trampoline_addr >> 12;

    // Send INIT to all other CPUs
    apic_send_command(0,
                      APIC_CMD_DEST_MODE_INIT |
                      APIC_CMD_DEST_LOGICAL |
                      APIC_CMD_DEST_TYPE_OTHER);

    sleep(10);

    printdbg("%d hyperthread bits\n", topo_thread_bits);
    printdbg("%d core bits\n", topo_core_bits);

    printdbg("%d hyperthread count\n", topo_thread_count);
    printdbg("%d core count\n", topo_core_count);

    uint32_t smp_expect = 0;
    for (unsigned pkg = 0; pkg < apic_id_count; ++pkg) {
        printdbg("Package base APIC ID = %u\n", apic_id_list[pkg]);

        uint8_t cpus = topo_core_count *
                topo_thread_count *
                apic_id_count;
        uint16_t stagger = 16666 - cpus;

        for (unsigned core = 0; core < topo_core_count; ++core) {
            for (unsigned thread = 0;
                 thread < topo_thread_count; ++thread) {
                uint8_t target = apic_id_list[pkg] +
                        (thread | (core << topo_thread_bits));

                // Don't try to start BSP
                if (target == apic_id_list[0])
                    continue;

                printdbg("Sending IPI to APIC ID %u\n", target);

                // Send SIPI to CPU
                apic_send_command(APIC_DEST_n(target),
                                  APIC_CMD_SIPI_PAGE_n(mp_trampoline_page) |
                                  APIC_CMD_DEST_MODE_SIPI |
                                  APIC_CMD_DEST_TYPE_BYID);

                usleep(stagger);

                ++smp_expect;
                while (thread_smp_running != smp_expect)
                    pause();
            }
        }
    }
}

uint32_t apic_timer_count(void)
{
    return APIC_LVT_CCR;
}

//
// IOAPIC

static void ioapic_lock(mp_ioapic_t *ioapic)
{
    spinlock_lock(&ioapic->lock);
}

static void ioapic_unlock(mp_ioapic_t *ioapic)
{
    spinlock_unlock(&ioapic->lock);
}

static uint32_t ioapic_read(mp_ioapic_t *ioapic, uint32_t reg)
{
    ioapic->ptr[IOAPIC_IOREGSEL] = reg;
    return ioapic->ptr[IOAPIC_IOREGWIN];
}

static void ioapic_write(mp_ioapic_t *ioapic,
                             uint32_t reg, uint32_t value)
{
    ioapic->ptr[IOAPIC_IOREGSEL] = reg;
    ioapic->ptr[IOAPIC_IOREGWIN] = value;
}

static mp_ioapic_t *ioapic_by_id(uint8_t id)
{
    for (unsigned i = 0; i < ioapic_count; ++i) {
        if (ioapic_list[i].id == id)
            return ioapic_list + i;
    }
    return 0;
}

// Returns 1 on success
// device should be 0 for ISA IRQs
static void ioapic_map(mp_ioapic_t *ioapic,
                       mp_bus_irq_mapping_t *mapping)
{
    uint8_t delivery;

    switch (mapping->intr_type) {
    case MP_INTR_TYPE_APIC:
        delivery = IOAPIC_REDLO_DELIVERY_APIC;
        break;

    case MP_INTR_TYPE_NMI:
        delivery = IOAPIC_REDLO_DELIVERY_NMI;
        break;

    case MP_INTR_TYPE_SMI:
        delivery = IOAPIC_REDLO_DELIVERY_SMI;
        break;

    case MP_INTR_TYPE_EXTINT:
        delivery = IOAPIC_REDLO_DELIVERY_EXTINT;
        break;

    default:
        printdbg("MP: Unrecognized interrupt delivery type!"
                 " Guessing APIC\n");
        delivery = IOAPIC_REDLO_DELIVERY_APIC;
        break;
    }

    uint8_t polarity;

    switch (mapping->flags & MP_INTR_FLAGS_POLARITY) {
    default:
    case MP_INTR_FLAGS_POLARITY_n(MP_INTR_FLAGS_POLARITY_ACTIVEHI):
        polarity = IOAPIC_REDLO_POLARITY_ACTIVEHI;
        break;
    case MP_INTR_FLAGS_POLARITY_n(MP_INTR_FLAGS_POLARITY_ACTIVELO):
        polarity = IOAPIC_REDLO_POLARITY_ACTIVELO;
        break;
    }

    uint8_t trigger;

    switch (mapping->flags & MP_INTR_FLAGS_TRIGGER) {
    default:
        printdbg("MP: Unrecognized IRQ trigger type!"
                 " Guessing edge\n");
        // fall through...
    case MP_INTR_FLAGS_TRIGGER_n(MP_INTR_FLAGS_TRIGGER_DEFAULT):
    case MP_INTR_FLAGS_TRIGGER_n(MP_INTR_FLAGS_TRIGGER_EDGE):
        trigger = IOAPIC_REDLO_TRIGGER_EDGE;
        break;

    case MP_INTR_FLAGS_TRIGGER_n(MP_INTR_FLAGS_TRIGGER_LEVEL):
        trigger = IOAPIC_REDLO_TRIGGER_LEVEL;
        break;

    }

    uint8_t intr = ioapic->base_intr + mapping->intin;

    uint32_t iored_lo =
            IOAPIC_REDLO_VECTOR_n(intr) |
            IOAPIC_REDLO_DELIVERY_n(delivery) |
            IOAPIC_REDLO_POLARITY_n(polarity) |
            IOAPIC_REDLO_TRIGGER_n(trigger);

    uint32_t iored_hi = IOAPIC_REDHI_DEST_n(0);

    ioapic_lock(ioapic);

    // Write low part with mask set
    ioapic_write(ioapic, IOAPIC_RED_LO_n(mapping->intin),
                 iored_lo | IOAPIC_REDLO_MASKIRQ);

    atomic_barrier();

    // Write high part
    ioapic_write(ioapic, IOAPIC_RED_HI_n(mapping->intin), iored_hi);

    atomic_barrier();

    ioapic_unlock(ioapic);
}

//
//

static mp_ioapic_t *ioapic_from_intr(int intr)
{
    for (unsigned i = 0; i < ioapic_count; ++i) {
        mp_ioapic_t *ioapic = ioapic_list + i;
        if (intr >= ioapic->base_intr &&
                intr < ioapic->base_intr +
                ioapic->vector_count) {
            return ioapic;
        }
    }
    return 0;
}

static mp_bus_irq_mapping_t *ioapic_mapping_from_irq(int irq)
{
    return bus_irq_list + bus_irq_to_mapping[irq];
}

static void *ioapic_dispatcher(int intr, isr_context_t *ctx)
{
    mp_ioapic_t *ioapic = ioapic_from_intr(intr);
    assert(ioapic);
    if (ioapic) {
        apic_eoi(intr);

        unsigned i;
        mp_bus_irq_mapping_t *mapping = bus_irq_list;
        uint8_t intin = intr - ioapic->base_intr;
        for (i = 0; i < bus_irq_count; ++i, ++mapping) {
            if (mapping->ioapic_id == ioapic->id &&
                    mapping->intin == intin)
                break;
        }

        // Reverse map ISA IRQ
        uint8_t *isa_match = memchr(isa_irq_lookup, i,
                                    sizeof(isa_irq_lookup));

        if (isa_match)
            i = isa_match - isa_irq_lookup;
        else
            i = ioapic->base_irq + intin;

        return irq_invoke(intr, i, ctx);
    }
    return ctx;
}

static void ioapic_setmask(int irq, int unmask)
{
    mp_bus_irq_mapping_t *mapping = ioapic_mapping_from_irq(irq);
    mp_ioapic_t *ioapic = ioapic_by_id(mapping->ioapic_id);

    ioapic_lock(ioapic);

    uint32_t ent = ioapic_read(
                ioapic, IOAPIC_RED_LO_n(mapping->intin));

    if (unmask)
        ent &= ~IOAPIC_REDLO_MASKIRQ;
    else
        ent |= IOAPIC_REDLO_MASKIRQ;

    ioapic_write(ioapic, IOAPIC_RED_LO_n(mapping->intin),
                 ent);

    ioapic_unlock(ioapic);
}

static void ioapic_hook(int irq, intr_handler_t handler)
{
    mp_bus_irq_mapping_t *mapping = ioapic_mapping_from_irq(irq);
    mp_ioapic_t *ioapic = ioapic_by_id(mapping->ioapic_id);
    uint8_t intr = ioapic->base_intr + mapping->intin;
    intr_hook(intr, handler);
}

static void ioapic_unhook(int irq, intr_handler_t handler)
{
    mp_bus_irq_mapping_t *mapping = ioapic_mapping_from_irq(irq);
    mp_ioapic_t *ioapic = ioapic_by_id(mapping->ioapic_id);
    uint8_t intr = ioapic->base_intr + mapping->intin;
    intr_unhook(intr, handler);
}

static void ioapic_map_all(void)
{
    mp_bus_irq_mapping_t *mapping;

    for (unsigned i = 0; i < 16; ++i)
        bus_irq_to_mapping[i] = isa_irq_lookup[i];

    for (unsigned i = 0; i < bus_irq_count; ++i) {
        mapping = bus_irq_list + i;
        if (mapping->bus != mp_isa_bus_id) {
            mp_ioapic_t *ioapic = ioapic_by_id(mapping->ioapic_id);
            uint8_t irq = ioapic->base_irq + mapping->intin;
            bus_irq_to_mapping[irq] = i;
        }

        mp_ioapic_t *ioapic = ioapic_by_id(mapping->ioapic_id);

        ioapic_map(ioapic, mapping);
    }
}

int apic_enable(void)
{
    if (!mp_tables)
        return 0;

    ioapic_map_all();

    irq_dispatcher_set_handler(ioapic_dispatcher);
    irq_setmask_set_handler(ioapic_setmask);
    irq_hook_set_handler(ioapic_hook);
    irq_unhook_set_handler(ioapic_unhook);

    return 1;
}
