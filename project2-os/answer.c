
#define PAGE_SIZE       4096
#define HUGE_PAGE_SIZE  (2 * 1024 * 1024)
#define PAGES_PER_HUGE  (HUGE_PAGE_SIZE / PAGE_SIZE)   /* 512 */

void manage_huge_pages(process_t *p)
{
    vma_t *vma;
    unsigned long addr;

    for_each_vma(p, vma) {
        /*
         * Huge pages are only useful for large enough regions.
         * Small mappings should stay as normal 4 KB pages.
         */
        if (vma->size < HUGE_PAGE_SIZE)
            continue;

        /*
         * Start at the next 2 MB aligned virtual address.
         */
        addr = align_up(vma->start, HUGE_PAGE_SIZE);

        while (addr + HUGE_PAGE_SIZE <= vma->end) {
            if (should_promote_to_huge_page(p, vma, addr)) {
                promote_to_huge_page(p, vma, addr);
            }

            addr += HUGE_PAGE_SIZE;
        }
    }
}

/*
 * The OS decides whether promotion is useful by checking whether the
 * 2 MB range is valid, active, and uniform.
 */
bool should_promote_to_huge_page(process_t *p, vma_t *vma,
                                 unsigned long addr)
{
    int i;
    pte_t *pte;
    int access_count = 0;
    protection_t prot = get_protection(vma, addr);

    /*
     * The virtual address must be huge-page aligned.
     */
    if (addr % HUGE_PAGE_SIZE != 0)
        return false;

    for (i = 0; i < PAGES_PER_HUGE; i++) {
        unsigned long va = addr + i * PAGE_SIZE;

        pte = page_table_lookup(p, va);

        /*
         * All 512 regular pages must exist.
         */
        if (pte == NULL || !pte_present(pte))
            return false;

        /*
         * All pages must have the same permissions.
         * For example, they should all be read/write or all read-only.
         */
        if (pte_protection(pte) != prot)
            return false;

        /*
         * Avoid promoting cold pages.
         */
        if (pte_accessed(pte))
            access_count++;
    }

    /*
     * Example policy: promote only if most pages are actually used.
     */
    if (access_count < 400)
        return false;

    return true;
}

/*
 * If the OS decides to promote, it replaces 512 regular mappings with
 * one huge page mapping.
 */
void promote_to_huge_page(process_t *p, vma_t *vma,
                          unsigned long addr)
{
    phys_addr_t huge_frame;
    int i;

    /*
     * Try to allocate one contiguous 2 MB physical frame.
     */
    huge_frame = alloc_contiguous_physical_memory(HUGE_PAGE_SIZE);

    if (huge_frame == INVALID)
        return;

    /*
     * Copy the data from the old 512 small pages into the new huge page.
     */
    for (i = 0; i < PAGES_PER_HUGE; i++) {
        unsigned long va = addr + i * PAGE_SIZE;
        phys_addr_t old_frame = pte_to_phys(page_table_lookup(p, va));

        copy_page(huge_frame + i * PAGE_SIZE, old_frame);
        free_physical_page(old_frame);
    }

    /*
     * Remove the 512 old PTEs and install one huge page table entry.
     */
    remove_small_page_mappings(p, addr, HUGE_PAGE_SIZE);

    install_huge_pte(p,
                     addr,
                     huge_frame,
                     vma->protection);

    flush_tlb_range(p, addr, addr + HUGE_PAGE_SIZE);
}

/*
 * The OS should also be able to split huge pages back into normal pages
 * when they are no longer useful.
 */
void maybe_split_huge_page(process_t *p, unsigned long addr)
{
    huge_pte_t *hpte;

    hpte = huge_page_lookup(p, addr);
    if (hpte == NULL)
        return;

    /*
     * Split if only a small part is being used, or if part of the
     * huge page needs different permissions, swapping, or unmapping.
     */
    if (huge_page_is_cold(hpte) ||
        partial_unmap_requested(p, addr) ||
        protection_change_inside_range(p, addr)) {

        split_huge_page(p, addr);
    }
}

/*
 * Splitting process:
 *
 * Remove the huge page mapping, allocate 512 small pages, copy each 4 KB
 * piece into its own small page, install 512 normal PTEs, then free the
 * old 2 MB physical frame.
 */
void split_huge_page(process_t *p, unsigned long addr)
{
    phys_addr_t huge_frame;
    int i;

    huge_frame = huge_pte_to_phys(huge_page_lookup(p, addr));

    remove_huge_pte(p, addr);

    for (i = 0; i < PAGES_PER_HUGE; i++) {
        phys_addr_t small_frame;

        small_frame = alloc_physical_page();
        copy_page(small_frame, huge_frame + i * PAGE_SIZE);

        install_small_pte(p,
                          addr + i * PAGE_SIZE,
                          small_frame,
                          original_protection);
    }

    free_contiguous_physical_memory(huge_frame, HUGE_PAGE_SIZE);
    flush_tlb_range(p, addr, addr + HUGE_PAGE_SIZE);
}

/*
 * Overall policy:
 *
 * The OS should create huge pages for large, aligned, frequently accessed
 * regions because this reduces the number of page table entries and TLB
 * entries. One huge page replaces 512 normal pages.
 *
 * The OS should avoid or split huge pages when memory usage is sparse,
 * when permissions differ within the 2 MB range, when physical memory is
 * fragmented, or when only part of the region is being unmapped.
 *
 * This improves performance by reducing TLB misses and page table overhead,
 * but it can increase memory waste and allocation cost if used too
 * aggressively.
 */
