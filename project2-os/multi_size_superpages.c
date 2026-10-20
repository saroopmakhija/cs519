#define PAGE_SIZE        4096
#define MIN_SUPER_SIZE   (64 * 1024)
#define MAX_SUPER_SIZE   (1024UL * 1024 * 1024)

/*
 * A superpage can be 64 KB, 128 KB, 256 KB, ... up to 1 GB.
 * The OS tries the smallest useful size first. If a range keeps growing
 * and the smaller superpage level fills up, it can promote to the next
 * power-of-two size.
 */
struct superpage_level {
    unsigned long size;
    int pages_per_superpage;
};

struct superpage_level superpage_levels[] = {
    { 64 * 1024, PAGE_COUNT(64 * 1024) },
    { 128 * 1024, PAGE_COUNT(128 * 1024) },
    { 256 * 1024, PAGE_COUNT(256 * 1024) },
    { 512 * 1024, PAGE_COUNT(512 * 1024) },
    { 1 * 1024 * 1024, PAGE_COUNT(1 * 1024 * 1024) },
    { 2 * 1024 * 1024, PAGE_COUNT(2 * 1024 * 1024) },
    { 4 * 1024 * 1024, PAGE_COUNT(4 * 1024 * 1024) },
    { 8 * 1024 * 1024, PAGE_COUNT(8 * 1024 * 1024) },
    { 16 * 1024 * 1024, PAGE_COUNT(16 * 1024 * 1024) },
    { 32 * 1024 * 1024, PAGE_COUNT(32 * 1024 * 1024) },
    { 64 * 1024 * 1024, PAGE_COUNT(64 * 1024 * 1024) },
    { 128 * 1024 * 1024, PAGE_COUNT(128 * 1024 * 1024) },
    { 256 * 1024 * 1024, PAGE_COUNT(256 * 1024 * 1024) },
    { 512 * 1024 * 1024, PAGE_COUNT(512 * 1024 * 1024) },
    { 1024UL * 1024 * 1024, PAGE_COUNT(1024UL * 1024 * 1024) },
};

#define NR_SUPER_LEVELS  ARRAY_SIZE(superpage_levels)

void manage_multi_size_superpages(process_t *p)
{
    vma_t *vma;
    int level;

    for_each_vma(p, vma) {
        /*
         * Walk from small to large. This lets a process first get cheap
         * 64 KB superpages, then later combine them into larger superpages
         * if the whole larger range becomes hot and full.
         */
        for (level = 0; level < NR_SUPER_LEVELS; level++) {
            unsigned long size = superpage_levels[level].size;
            unsigned long addr;

            if (vma->size < size)
                continue;

            addr = align_up(vma->start, size);

            while (addr + size <= vma->end) {
                if (should_promote_to_superpage(p, vma, addr, level))
                    promote_to_superpage(p, vma, addr, level);

                addr += size;
            }
        }
    }
}

/*
 * Pick the next larger superpage size for a range that already has smaller
 * pages or smaller superpages. For example, many full 64 KB superpages can
 * become one 128 KB superpage, then 256 KB, and so on.
 */
int next_superpage_level(int current_level)
{
    if (current_level + 1 >= NR_SUPER_LEVELS)
        return current_level;

    return current_level + 1;
}

/*
 * Promotion is allowed only when the whole range is present, aligned,
 * frequently used, and has uniform permissions.
 */
bool should_promote_to_superpage(process_t *p, vma_t *vma,
                                 unsigned long addr, int level)
{
    unsigned long size = superpage_levels[level].size;
    int pages = superpage_levels[level].pages_per_superpage;
    protection_t prot = get_protection(vma, addr);
    int present_count = 0;
    int access_count = 0;
    int i;

    if (addr % size != 0)
        return false;

    /*
     * The region must be backed by normal pages or by smaller superpages
     * that exactly cover this larger range.
     */
    for (i = 0; i < pages; i++) {
        unsigned long va = addr + i * PAGE_SIZE;
        pte_t *pte = page_table_lookup(p, va);

        if (pte == NULL || !pte_present(pte))
            return false;

        if (pte_protection(pte) != prot)
            return false;

        present_count++;

        if (pte_accessed(pte))
            access_count++;
    }

    /*
     * The larger the superpage, the more conservative the OS should be.
     * This avoids wasting hundreds of MB or 1 GB for sparse memory.
     */
    if (present_count < pages)
        return false;

    if (access_count < pages * 8 / 10)
        return false;

    return true;
}

/*
 * Allocate one contiguous physical region for this superpage size, copy the
 * old contents into it, remove the old mappings, and install one mapping.
 */
void promote_to_superpage(process_t *p, vma_t *vma,
                          unsigned long addr, int level)
{
    unsigned long size = superpage_levels[level].size;
    int pages = superpage_levels[level].pages_per_superpage;
    phys_addr_t super_frame;
    int i;

    super_frame = alloc_contiguous_physical_memory(size);
    if (super_frame == INVALID)
        return;

    for (i = 0; i < pages; i++) {
        unsigned long va = addr + i * PAGE_SIZE;
        phys_addr_t old_frame = pte_to_phys(page_table_lookup(p, va));

        copy_page(super_frame + i * PAGE_SIZE, old_frame);
        free_physical_page(old_frame);
    }

    remove_page_mappings(p, addr, size);

    install_superpage_pte(p,
                          addr,
                          super_frame,
                          size,
                          vma->protection);

    flush_tlb_range(p, addr, addr + size);
}

/*
 * When a smaller level becomes full, try to merge it into the next size.
 * Example:
 *
 *  - first fill and promote a 64 KB range
 *  - if two adjacent 64 KB ranges are both present and hot, promote to 128 KB
 *  - keep repeating until promotion fails or the range reaches 1 GB
 */
void grow_superpage_if_full(process_t *p, vma_t *vma,
                            unsigned long addr, int current_level)
{
    int level = next_superpage_level(current_level);
    unsigned long size;
    unsigned long base;

    while (level < NR_SUPER_LEVELS) {
        size = superpage_levels[level].size;
        base = align_down(addr, size);

        if (!should_promote_to_superpage(p, vma, base, level))
            return;

        promote_to_superpage(p, vma, base, level);
        level = next_superpage_level(level);
    }
}

/*
 * Superpages must also be split when only part of the range is needed,
 * when permissions diverge, or when the memory becomes cold.
 */
void maybe_split_superpage(process_t *p, unsigned long addr)
{
    super_pte_t *spte;

    spte = superpage_lookup(p, addr);
    if (spte == NULL)
        return;

    if (superpage_is_cold(spte) ||
        partial_unmap_requested(p, addr) ||
        protection_change_inside_range(p, addr)) {

        split_superpage(p, addr);
    }
}

void split_superpage(process_t *p, unsigned long addr)
{
    super_pte_t *spte = superpage_lookup(p, addr);
    unsigned long size = super_pte_size(spte);
    int pages = size / PAGE_SIZE;
    phys_addr_t super_frame;
    int i;

    super_frame = super_pte_to_phys(spte);
    remove_superpage_pte(p, addr);

    for (i = 0; i < pages; i++) {
        phys_addr_t small_frame;

        small_frame = alloc_physical_page();
        copy_page(small_frame, super_frame + i * PAGE_SIZE);

        install_small_pte(p,
                          addr + i * PAGE_SIZE,
                          small_frame,
                          original_protection);
    }

    free_contiguous_physical_memory(super_frame, size);
    flush_tlb_range(p, addr, addr + size);
}

/*
 * Overall policy:
 *
 * Start with 64 KB because it is easier to allocate and wastes less memory
 * than a 2 MB or 1 GB mapping. If neighboring ranges are full, aligned, hot,
 * and have the same permissions, promote to the next power-of-two size.
 *
 * Stop growing when physical memory is fragmented, access is sparse,
 * permissions differ, the virtual range is not aligned, or the maximum
 * 1 GB superpage size is reached.
 */
