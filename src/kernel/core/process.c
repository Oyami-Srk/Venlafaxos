#include "core/process.h"
#include "core/environment.h"
#include "core/memory.h"
#include "core/paging.h"
#include "core/protect.h"
#include "generic/asm.h"
#include "generic/typedefs.h"
#include "lib/bitset.h"
#include "lib/stdlib.h"
#include "lib/string.h"

process *proc_running   = NULL;
process *proc_table     = NULL;
process *proc_table_end = NULL; // last item of proc table
size_t * proc_count     = 0;
bitset * proc_bitmap;
size_t   proc_bitmap_size;

// proc table setup
void core_init_proc(struct core_env *env) {
    /* proc_table           = (process *)kalloc(); */
    /* proc_bitmap          = (bitset *)kalloc(); */
    /* proc_bitmap_size     = PG_SIZE / sizeof(bitset); */
    /* uint proc_table_size = (uint)(PG_SIZE / sizeof(process)); */
    /* proc_table_end       = proc_table + proc_table_size - 1; */
    /* memset(proc_table, 0, PG_SIZE); */
    /* memset(proc_bitmap, 0, PG_SIZE); */
    /* env.proc_table  = proc_table; */
    /* env.proc_bitmap = proc_bitmap; */
    /* env.proc_count  = 0; */
    /* proc_count      = &env.proc_count; */
    /* env.proc_max    = (proc_table_end - proc_table); */
    proc_table       = env->proc_table;
    proc_bitmap      = env->proc_bitmap;
    proc_bitmap_size = env->proc_bitmap_size;
    proc_count       = &env->proc_count;
    proc_table_end   = env->proc_table + env->proc_max;
    proc_running     = proc_table;
}

// proc initialized here is running under ring1
uint init_proc(uint pid, void *entry, u32 page_dir) {
    if ((pid == PROC_INTERRUPT) || (pid == PROC_ANY) ||
        check_bit(proc_bitmap, pid))
        pid = find_first_unset_bit(proc_bitmap, proc_bitmap_size);
    process *proc = &proc_table[pid];
    if (proc > proc_table_end)
        panic("No enough slot for process.");
    set_bit(proc_bitmap, pid);
    (*proc_count)++;

    proc->stack.cs = SEL_CODE_DPL1;
    proc->stack.ds = SEL_DATA_DPL1;
    proc->stack.gs = SEL_DATA_DPL1;
    proc->stack.es = SEL_DATA_DPL1;
    proc->stack.fs = SEL_DATA_DPL1;
    proc->stack.ss = SEL_DATA_DPL1;

    proc->stack.eip    = (u32)(entry);
    proc->stack.esp    = (uint)kalloc() + 4096; // task stack
    proc->stack.eflags = 0x1202;
    proc->page_dir     = (u32)KV2P(page_dir);

    proc->status = PROC_STATUS_RUNNING | PROC_STATUS_NORMAL;
    proc->pid    = pid;
    return proc->pid;
}

#define UNRUNABLE                                                              \
    (PROC_STATUS_RECEVING | PROC_STATUS_SENDING | PROC_STATUS_SUSPEND |        \
     PROC_STATUS_STOP | PROC_STATUS_ERROR)
uint scheduler_i = 0;

void scheduler(void) {
    if (!proc_running)
        return;
    do {
        scheduler_i++;
        if (scheduler_i >= *proc_count)
            scheduler_i = 0;
    } while ((proc_table[scheduler_i].status & 0xFF) & UNRUNABLE);
    proc_running = &proc_table[scheduler_i];
}
