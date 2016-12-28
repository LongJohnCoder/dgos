#pragma once
#include "types.h"

int apic_init(int ap);
void apic_start_smp(void);
unsigned apic_get_id(void);
void apic_send_ipi(int target_apic_id, uint8_t intr);
void apic_eoi(int intr);