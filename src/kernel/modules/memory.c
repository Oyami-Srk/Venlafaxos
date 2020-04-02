/*
module:
  name: Memory
  author: Shiroko
  summary: Memory Process
  entry: Task_Memory
*/
#include "core/memory.h"
#include "core/environment.h"
#include "core/paging.h"
#include "lib/bitset.h"
#include "lib/stdlib.h"
#include "lib/string.h"
#include "lib/syscall.h"
#include "modules/systask.h"
#include "modules/tty.h"

unsigned int sys_mapping[][3] = {
    // paddr, vaddr, page size
    {0, KERN_BASE, PG_SIZE * 1024}};

#define PAGE_TYPE_USABLE   0x01
#define PAGE_TYPE_RESERVED 0x02
#define PAGE_TYPE_SYSTEM   0x04
#define PAGE_TYPE_HARDWARE 0x08
#define PAGE_TYPE_FREE     0x10
#define PAGE_TYPE_INUSE    0x20

struct page {
    unsigned int type;
    int          reference;
    void *       vaddr;
    int          reserv;
};

struct _block_list {
    struct _block_list *prev;
    struct _block_list *next;
};

typedef struct _block_list block_list;

struct memory_info {
#define MAX_ORDER 11
    block_list * free_list[MAX_ORDER]; // max block size is 2^(MAX_ORDER-1)=4MB
    size_t       free_count[MAX_ORDER];
    bitset *     buddy_map[MAX_ORDER];
    uint         memory_start;
    uint         memory_end;
    uint         usable_end; // buddy map located on the end of memory
    uint         page_count; // only memory_start to memory_end
    struct page *pages_info;
};

#define GET_PAGE_BY_ID(mem, id) (((mem)->memory_start + ((id)*PG_SIZE)))

struct memory_info mem_info;
// in pages, not in bytes
#define page_alloc(pages)                                                      \
    allocate_pages_of_power_2(&mem_info, round_up_power_2(pages))
#define page_free(p, pages)                                                    \
    free_pages_of_power_2(&mem_info, (char *)p, round_up_power_2(pages))

static inline uint round_down_power_2(uint x) {
    if (x == 0)
        return 0;
    x |= (x >> 1);
    x |= (x >> 2);
    x |= (x >> 4);
    x |= (x >> 8);
    x |= (x >> 16);
    return x - (x >> 1);
}

static inline uint round_up_power_2(uint x) {
    x--;
    x |= (x >> 1);
    x |= (x >> 2);
    x |= (x >> 4);
    x |= (x >> 8);
    x |= (x >> 16);
    return x + 1;
}

static inline uint trailing_zero(uint x) {
    uint c;
    asm volatile("bsfl %0, %0" : "=a"(c) : "0"(x));
    return c;
}

static block_list *remove_from_free_list(struct memory_info *mem, block_list *p,
                                         uint order) {
    if (p->prev) {
        p->prev->next = p->next;
        p->next->prev = p->prev;
    } else {
        if (mem->free_list[order] != p)
            panic("Block has no prev but not the first.");
        mem->free_list[order] = p->next;
        p->next->prev         = NULL;
    }
    p->next = NULL;
    p->prev = NULL;
    return p;
}

static block_list *attach_to_free_list(struct memory_info *mem, block_list *p,
                                       uint order) {
    // must attach a single node
    assert(p->prev == NULL);
    assert(p->next == NULL);
    p->next               = mem->free_list[order];
    p->next->prev         = p;
    p->prev               = NULL;
    mem->free_list[order] = p;
    return p;
}

static inline int xor_buddy_map(struct memory_info *mem, char *p, uint order) {
    uint page_idx = ((uint)p - mem->memory_start) / PG_SIZE;
    xor_bit(mem->buddy_map[order], page_idx >> (order + 1), 1);
    return check_bit(mem->buddy_map[order], page_idx >> (order + 1));
}

static char *allocate_pages_of_power_2(struct memory_info *mem, uint order) {
    if (order >= MAX_ORDER)
        return NULL;
    char *block = NULL;
    if (mem->free_count[order] == 0) {
        block = allocate_pages_of_power_2(mem, order + 1);
        attach_to_free_list(mem, (block_list *)block, order);
        mem->free_count[order]++;
        // printf("= PUT %x into order %d free list =\n", block);
        block += ((1 << order) * PG_SIZE);
        xor_buddy_map(mem, block, order); // higher half is returned
        return block;
    } else {
        block =
            (char *)remove_from_free_list(mem, mem->free_list[order], order);
        mem->free_count[order]--;
        xor_buddy_map(mem, block, order);
        return block;
    }
    return NULL;
}

static void free_pages_of_power_2(struct memory_info *mem, char *p,
                                  uint order) {
    if (p == NULL)
        return;
    uint buddy_bit = xor_buddy_map(mem, p, order);
    if (buddy_bit == 0 && order + 1 < MAX_ORDER) {
        char *buddy               = NULL;
        uint  page_idx            = ((uint)p - mem->memory_start) / PG_SIZE;
        uint  buddy_even_page_idx = (page_idx >> (order + 1)) << (order + 1);
        if (page_idx == buddy_even_page_idx)
            buddy = p + (1 << order) * PG_SIZE;
        else
            buddy = p - (1 << order) * PG_SIZE;
        // printf("= REMOVE %x from order %d free list =\n", buddy, order);
        remove_from_free_list(mem, (block_list *)buddy, order);
        mem->free_count[order]--;
        if (buddy < p)
            free_pages_of_power_2(mem, buddy, order + 1);
        else
            free_pages_of_power_2(mem, p, order + 1);
    } else {
        attach_to_free_list(mem, (block_list *)p, order);
        mem->free_count[order]++;
    }
}

void print_free_info(struct memory_info *mem) {
    printf("[MEM] free blocks count is\n[MEM] ");
    for (uint i = 0; i < MAX_ORDER; i++)
        printf("%04d, ", 1 << i);
    printf("\n[MEM] ");
    for (uint i = 0; i < MAX_ORDER; i++)
        printf("%04d, ", mem->free_count[i]);
    printf("\n");
}

// return NULL if cannot find.
static pte_t *get_pte(pde_t *page_dir, void *va) {
    pte_t *pg_tab = NULL;
    pde_t  pde    = page_dir[(uint)va >> 22];
    if (!(pde & PG_Present))
        return NULL;
    pg_tab = (pte_t *)(pde & ~0xFFF);
    return &pg_tab[((uint)va >> 12) & 0x3FF];
}

static pte_t *create_page_table(pde_t *page_dir, void *va) {
    pte_t *pte = get_pte(page_dir, va);
    if (pte)
        return pte;
    pde_t *pde    = &page_dir[(uint)va >> 22];
    pte_t *pg_tab = (pte_t *)page_alloc(1);
    if (pg_tab == NULL)
        return NULL;
    memset(pg_tab, 0, PG_SIZE);
    // memory proc cr3 is directly mapped, no need for V2P
    *pde = (uint)pg_tab | PG_Present | PG_Writeable | PG_User;
    return &pg_tab[((uint)va >> 12) & 0x3FF];
}

// size in bytes
static int map_pages(pde_t *page_dir, void *va, void *pa, size_t size,
                     uint pte_attr) {
    va         = (void *)PGROUNDDOWN((uint)va);
    void * end = (void *)PGROUNDDOWN(((uint)va) + size - 1);
    pte_t *pte = NULL;
    for (; (uint)va < (uint)end; va += PG_SIZE, pa += PG_SIZE) {
        if ((pte = create_page_table(page_dir, va)) == NULL)
            return -1;
        if (*pte & PG_Present)
            panic("memory remap");
        *pte = (uint)pa | PG_Present | pte_attr;
    }
    return 0;
}

static pde_t *create_page_dir(void) {
    pde_t *page_dir = (pde_t *)page_alloc(1);
    if (page_dir == NULL)
        return NULL;
    memset(page_dir, 0, PG_SIZE);
    size_t sys_map_size = sizeof(sys_mapping) / sizeof(sys_mapping[0]);
    for (uint i = 0; i < sys_map_size; i++)
        map_pages(page_dir, (void *)sys_mapping[i][1],
                  (void *)sys_mapping[i][0], sys_mapping[i][2],
                  PG_Present | PG_Writeable);
    return page_dir;
}

// page_id = (page_paddr - pstart) / PG_SIZE
// pg_count = (end-start) / PG_SIZE
// buddy bits = (pg_count / (2^order)) / 2 = page_count >> (order+1)
// total buddy bits = ((2^MAX_ORDER)-1)*pg_count/(2^MAX_ORDER)

void init_memory(struct memory_info *mem) {
    printf("[MEM] Init memory 0x%x - 0x%x\n", mem->memory_start,
           mem->memory_end);
    size_t pg_count     = (mem->memory_end - mem->memory_start) / PG_SIZE;
    size_t pg_info_size = pg_count * sizeof(struct page);
    mem->page_count     = pg_count;
    uint buddy_total_bits =
        ((1 << (MAX_ORDER)) - 1) * pg_count / (1 << MAX_ORDER);
    size_t buddy_bytes = buddy_total_bits / 8;
    mem->usable_end = PGROUNDDOWN(mem->memory_end - buddy_bytes - pg_info_size);
    mem->pages_info = (struct page *)(mem->usable_end);
    mem->buddy_map[0]  = (bitset *)(mem->memory_end - buddy_bytes);
    mem->free_list[0]  = (block_list *)NULL;
    mem->free_count[0] = 0;
    printf("[MEM] Init buddy map on 0x%x, size %d bytes.\n", mem->usable_end,
           mem->memory_end - mem->usable_end);
    memset((void *)mem->usable_end, 0, mem->memory_end - mem->usable_end);
    for (uint i = 0; i < MAX_ORDER - 1; i++) {
        mem->buddy_map[i + 1] =
            (bitset *)(((uint)mem->buddy_map[i]) + (pg_count >> (i + 1)) / 8);
        mem->free_list[i + 1]  = (block_list *)NULL;
        mem->free_count[i + 1] = 0;
    }
    uint  max_block_size = (1 << (MAX_ORDER - 1)) * PG_SIZE;
    void *pg             = NULL;
    for (pg = (void *)mem->memory_start; pg < (void *)mem->usable_end;
         pg += max_block_size) {
        // free max block first
        block_list *current = (block_list *)pg;
        attach_to_free_list(mem, current, MAX_ORDER - 1);
        mem->free_count[MAX_ORDER - 1]++;
    }
    printf("[MEM] pages info size: %d KB\n",
           pg_count * sizeof(struct page) / 1024);
    for (uint i = 0; i < pg_count; i++) {
        if (GET_PAGE_BY_ID(mem, i) < mem->usable_end)
            mem->pages_info[i].type = PAGE_TYPE_FREE | PAGE_TYPE_USABLE;
        else
            mem->pages_info[i].type = PAGE_TYPE_INUSE | PAGE_TYPE_SYSTEM;
        mem->pages_info[i].vaddr = (void *)0xFFFFFFFF;
    }
    printf("[MEM] Initialized. Total %d pages.\n", pg_count);
    print_free_info(mem);
    return;
}

void Task_Memory(void) {
    struct core_env_memory_zone zone[10];
    size_t                   zone_count = query_env(ENV_KEY_MMAP, (ubyte *)zone,
                                  sizeof(struct core_env_memory_zone) * 10);
    struct core_memory_usage core_usage;
    query_env(ENV_KEY_MEMORY_USAGE, (ubyte *)&core_usage, sizeof(core_usage));

    /* struct page *pages_info = (struct page *)core_usage.core_space_end; */

    mem_info.memory_start = (uint)KV2P(core_usage.core_space_end);
    mem_info.memory_end   = core_usage.memory_end;

    init_memory(&mem_info);
    /*
    for (uint i = 0; i < MAX_ORDER; i++)
        printf("%x, ", mem_info.free_list[i]);
    printf("\n");

    char *test = allocate_pages_of_power_2(&mem_info, trailing_zero(128));
    printf("Allocated pages at 0x%x\n", test);
    print_free_info(&mem_info);
    free_pages_of_power_2(&mem_info, test, trailing_zero(128));
    print_free_info(&mem_info);
    for (uint i = 0; i < MAX_ORDER; i++)
        printf("%x, ", mem_info.free_list[i]);
    printf("\n");
    */
    // note：每一个进程都应该有自己的中断栈，大小8K。系统初期的进程共享一个中断栈。
    // 每一个进程都有自己的普通栈，大小应为4MB。系统初期的进程只有4KB（一页大小）
    // 系统映射在0x80000000之后的4MB内，这是全局的映射，用户进程有此map但是不能访问（权限过低）

    message msg;
    while (1) {
        recv_msg(&msg, PROC_ANY);
    }
}
