#include "core/protect.h"
#include "core/memory.h"
#include "generic/asm.h"
#include "lib/string.h"

Descriptor *_GDT_start;
Descriptor *_GDT_current; // point to next empty slot;
Descriptor *_GDT_end;

// must be called after set _GDT_start and _GDT_end
void __lgdt__(BOOL has_tss, u16 selector_tss) {
    uchar gdt_ptr[6];
    u16 * gdt_limit = (u16 *)(&gdt_ptr[0]);
    u32 * gdt_base  = (u32 *)(&gdt_ptr[2]);

    *gdt_limit = (_GDT_end - _GDT_start + 1) * sizeof(Descriptor) - 1;
    *gdt_base  = (u32)_GDT_start;

    asm volatile("lgdtl (%0)" ::"r"(gdt_ptr));
    asm volatile("lgdtl (%0)" ::"r"(gdt_ptr));
    asm("\n"
        "pushl %0\n"
        "pushl $1f\n"
        "lret \n"
        "1:\n"
        "mov %1, %%eax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        :
        : "g"(0x08), "g"(0x10)
        : "eax", "memory"); // flush the cpu
    if (has_tss)
        asm volatile("ltr %%ax" ::"a"(selector_tss));
}

void core_init_gdt(Descriptor *GDT, size_t gdt_count, struct tss *tss) {
    size_t size         = sizeof(KERN_GDT) / sizeof(KERN_GDT[0]);
    BOOL   has_tss      = FALSE;
    uint   gdt_n        = 0;
    u16    selector_tss = 0;

    if (size > gdt_count) {
        magic_break();
        while (1)
            ;
    }

    _GDT_start   = GDT;
    _GDT_current = _GDT_start;
    _GDT_end     = _GDT_start + gdt_count - 1;

    for (uint i = 0; i < size; i++)
        switch (KERN_GDT[i][2]) {
        case DA_386TSS:
            has_tss      = TRUE;
            selector_tss = i << 3;
            memset(tss, 0, sizeof(struct tss));
            tss->ss0 = 2 << 3; // data selector, hard code is bad
            make_descriptor(_GDT_current, (uint)(KV2P(tss)),
                            sizeof(struct tss) - 1, DA_386TSS);
            _GDT_current++;
            break;
        case DA_LDT:
            __lgdt__(has_tss, selector_tss);
            return;
            break;
        default:
            make_descriptor(_GDT_current, KERN_GDT[i][0], KERN_GDT[i][1],
                            KERN_GDT[i][2]);
            _GDT_current++;
            break;
        }

    __lgdt__(has_tss, selector_tss);
}
