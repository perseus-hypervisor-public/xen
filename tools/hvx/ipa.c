#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/gfp.h>
#include <linux/pagemap.h>
#include <linux/list.h>
#include <linux/device.h>
#include <asm-generic/cacheflush.h>

#include "ipa.h"
#include "log.h"

#define IPA_SHIFT      9
#define IPA_ENTRIES    (1 << IPA_SHIFT)
#define IPA_ENTRY_MASK (IPA_ENTRIES - 1)

#define IPA_TABLE_INDEX(addr, level) \
		(((addr) >> (((3 - level) * IPA_SHIFT) + PAGE_SHIFT)) & IPA_ENTRY_MASK)

#define BLOCK_FRAME_SIZE	(2 << (20 - IPA_PAGE_SHIFT))
#define BLOCK_FRAME_MASK	(~(BLOCK_FRAME_SIZE - 1))

#define SECTION_FRAME_SIZE	(1 << (30 - IPA_PAGE_SHIFT))
#define SECTION_FRAME_MASK	(~(SECTION_FRAME_SIZE - 1))

#define REGION_FRAME_SIZE   (1 << (39 - IPA_PAGE_SHIFT))
#define REGION_FRAME_MASK   (~(REGION_FRAME_SIZE - 1))

static inline unsigned long ipa_make_block_pte(unsigned long pfn, unsigned long type)
{
	return (pfn << IPA_PAGE_SHIFT) | type | IPA_PTE_BLOCK;
}

static inline unsigned long ipa_make_page_pte(unsigned long pfn, unsigned long type)
{
	return (pfn << IPA_PAGE_SHIFT) | type | IPA_PTE_PAGE;
}

static inline unsigned long ipa_make_table_pte(unsigned long pfn)
{
	return (pfn << IPA_PAGE_SHIFT) | IPA_PTE_TABLE;
}

static inline unsigned long ipa_pte_to_pfn(const unsigned long *pte)
{
	return *pte >> IPA_PAGE_SHIFT;
}

static inline unsigned long ipa_pte_to_int(const unsigned long *pte)
{
	return *pte;
}

static inline int ipa_pte_is_table(const unsigned long *pte)
{
	return ((*pte & IPA_PTE_MASK) == IPA_PTE_TABLE);
}

static inline int ipa_pte_is_page(const unsigned long *pte)
{
	return ((*pte & IPA_PTE_MASK) == IPA_PTE_PAGE);
}

static inline int ipa_pte_is_block(const unsigned long *pte)
{
	return ((*pte & IPA_PTE_MASK) == IPA_PTE_BLOCK);
}

static inline int ipa_pte_is_valid(const unsigned long *pte)
{
	return ((*pte & IPA_PTE_VALID) == IPA_PTE_VALID);
}

static inline bool ipa_map_is_aligned(unsigned long addr, unsigned int size)
{
	return ((addr & (size - 1)) == 0);
}

static inline void ipa_pte_update(unsigned long *p, unsigned long v)
{
	asm volatile (
		"dsb sy;"
		"str %0, [%1];"
		"dsb sy;"
		:
		: "r"(v), "r"(p)
		: "memory"
	);
}

static inline unsigned long *ipa_vfn_to_ptep(unsigned long *table, unsigned long vfn, int level)
{
	return table + IPA_TABLE_INDEX(vfn << IPA_PAGE_SHIFT, level);
}

static inline unsigned long *ipa_pte_to_ptep(unsigned long *pte)
{
	unsigned long phys = ipa_pte_to_pfn(pte) << IPA_PAGE_SHIFT;
	return (unsigned long *)phys_to_virt(phys);
}

static inline unsigned long ipa_map_next_boundary(unsigned long vfn, unsigned long last, unsigned long size)
{
	unsigned long end = (vfn + (size)) & (~(size - 1));
	return (end < last) ? end : last;
}

unsigned long *ipa_alloc_table(void)
{
	struct page *page = NULL;
	unsigned long *table;

	page = alloc_pages(GFP_KERNEL | __GFP_ZERO, 0);
	if (page == NULL)
		return NULL;

	table = page_address(page);
	return table;
}

void ipa_pte_remove(unsigned long *entry, int depth)
{
	unsigned long pte = 0;

	if (ipa_pte_is_table(entry) && depth < 3) {
		int i;
		struct page *pg;
		unsigned long pfn = ipa_pte_to_pfn(entry);
		unsigned long *child = (unsigned long *)phys_to_virt(pfn << IPA_PAGE_SHIFT);
		for (i = 0; i < IPA_ENTRIES; i++)
			ipa_pte_remove(&child[i], depth + 1);

		pg = pfn_to_page(pfn);
		hvx_info("page table freed: 0x%lx\n", pfn << IPA_PAGE_SHIFT);
		__free_pages(pg, 0);
	}

	ipa_pte_update(entry, pte);
}

void ipa_free_table(unsigned long *table)
{
	int i;

	for (i = 0; i < IPA_ENTRIES; i++) {
		if (table[i]) {
			ipa_pte_remove(&table[i], 0);
		}
	}

	__free_pages(virt_to_page(table), 0);
}

inline void ipa_table_pte_update(unsigned long *entry, unsigned long *ptr)
{
	unsigned long pte = ipa_make_table_pte(virt_to_phys(ptr) >> IPA_PAGE_SHIFT);
	ipa_pte_update(entry, pte);
}

int ipa_map_downsize_section(unsigned long *entry)
{
	int i;
	unsigned long pte, *ptr;

	BUG_ON(entry == NULL);

	pte = *entry;
	ptr = ipa_alloc_table();
	if (ptr == NULL)
		return -ENOMEM;

	if (pte) {
		for (i = 0; i < IPA_ENTRIES; i++) {
			ipa_pte_update(ptr + i, pte);
			pte += BLOCK_FRAME_SIZE << IPA_PAGE_SHIFT;
		}
	}

	ipa_table_pte_update(entry, ptr);

	return 0;
}

int ipa_map_downsize_block(unsigned long *entry)
{
	int i;
	unsigned long pte, *ptr;

	BUG_ON(entry == NULL);

	pte = *entry;
	ptr = ipa_alloc_table();
	if (ptr == NULL)
		return -ENOMEM;

	if (pte) {
		pte |= IPA_PTE_TABLE;
		for (i = 0; i < IPA_ENTRIES; i++) {
			ipa_pte_update(ptr + i, pte);
			pte += IPA_PAGE_SIZE;
		}
	}

	ipa_table_pte_update(entry, ptr);

	return 0;
}

void ipa_dump_page_maps(unsigned long *page_table, int depth)
{ 
	int index = 0;
	unsigned long *ptr = page_table;

	if (depth >= 3)
		return;

	do {
		if (ipa_pte_to_int(ptr))
			hvx_info("(%d:%d):0x%lx\n", depth, index, ipa_pte_to_int(ptr));
		if (ipa_pte_is_table(ptr)) {
			unsigned long *ptep = ipa_pte_to_ptep(ptr);
			ipa_dump_page_maps(ptep, depth + 1);
		}

		index++;
		ptr++;
	} while (index < 512);
}

int ipa_map_page_range(unsigned long *table, unsigned long vfn, unsigned long pfn, int nr, unsigned long flags)
{
	unsigned long *ptr, pte;
	unsigned long end = vfn + nr;

	BUG_ON((table == NULL) && (nr == 0));

	ptr = ipa_vfn_to_ptep(table, vfn, 3);

	do {
		pte = ipa_make_page_pte(pfn, flags);
		ipa_pte_update(ptr, pte);

		vfn++;
		pfn++;
		ptr++;
	} while (vfn < end);

	return 0;
}

int ipa_map_block_range(unsigned long *table, unsigned long vfn, unsigned long pfn, int nr, unsigned long flags)
{
	int rc = 0;
	unsigned long *submap, *ptr, pte;
	unsigned long last = vfn + nr;

	BUG_ON((table == NULL) && (nr == 0));

	ptr = ipa_vfn_to_ptep(table, vfn, 2);

	do {
		unsigned long end = ipa_map_next_boundary(vfn, last, BLOCK_FRAME_SIZE);

		if (ipa_map_is_aligned(vfn | pfn | end, BLOCK_FRAME_SIZE)) {
			pte = ipa_make_block_pte(pfn, flags);
			ipa_pte_update(ptr, pte);

			vfn += BLOCK_FRAME_SIZE;
			pfn += BLOCK_FRAME_SIZE;
		} else {
			if (!ipa_pte_is_table(ptr))
				ipa_map_downsize_block(ptr);

			submap = ipa_pte_to_ptep(ptr);
			rc = ipa_map_page_range(submap, vfn, pfn, end - vfn, flags);
			if (rc < 0)
				break;

			pfn += end - vfn;
			vfn = end;
		}

		ptr++;
	} while (vfn < last);

	return rc;
}

int ipa_map_section_range(unsigned long *table, unsigned long vfn, unsigned long pfn, int nr, unsigned long flags)
{
	int rc = 0;
	unsigned long *submap, *ptr, pte;
	unsigned long last = vfn + nr;

	BUG_ON((table == NULL) && (nr == 0));

	ptr = ipa_vfn_to_ptep(table, vfn, 1);

	do {
		unsigned long end = ipa_map_next_boundary(vfn, last, SECTION_FRAME_SIZE);

		if (ipa_map_is_aligned(vfn | pfn | end, SECTION_FRAME_SIZE)) {
			pte = ipa_make_block_pte(pfn, flags);
			ipa_pte_update(ptr, pte);

			vfn += SECTION_FRAME_SIZE;
			pfn += SECTION_FRAME_SIZE;
		} else {
			if (!ipa_pte_is_table(ptr))
				ipa_map_downsize_section(ptr);

			submap = ipa_pte_to_ptep(ptr);
			rc = ipa_map_block_range(submap, vfn, pfn, end - vfn, flags);
			if (rc < 0)
				break;

			pfn += end - vfn;
			vfn = end;
		}

		ptr++;
	} while (vfn < last);

	return rc;
}

int ipa_map_range(unsigned long *table, unsigned long vfn, unsigned long pfn, int nr, unsigned long flags)
{
	int rc = 0;
	unsigned long *ptr, last = vfn + nr;

	BUG_ON((table == NULL) && (nr == 0));

	ptr = ipa_vfn_to_ptep(table, vfn, 0);

	do {
		unsigned long *submap;
		unsigned long end = ipa_map_next_boundary(vfn, last, REGION_FRAME_SIZE);

		if (!ipa_pte_is_table(ptr)) {
			unsigned long *pgt = ipa_alloc_table();
			if (pgt == NULL) {
				return -1;
			}
			ipa_table_pte_update(ptr, pgt);
		}

		submap = ipa_pte_to_ptep(ptr);
		rc = ipa_map_section_range(submap, vfn, pfn, end - vfn, flags);
		if (rc < 0)
			 break;

		pfn += end - vfn;
		vfn = end;

		ptr++;
	} while (vfn < last);

	return rc;
}

int ipa_unmap_page_range(unsigned long *table, unsigned long vfn, int nr)
{
	unsigned long *ptr, pte = 0;
	unsigned long end = vfn + nr;

	BUG_ON((table == NULL) && (nr == 0));

	ptr = ipa_vfn_to_ptep(table, vfn, 3);

	do {
		ipa_pte_update(ptr, pte);
		vfn++;
		ptr++;
	} while (vfn < end);

	return 0;
}

int ipa_unmap_block_range(unsigned long *table, unsigned long  vfn, int nr)
{
	int rc = 0;
	unsigned long *submap, *ptr;
	unsigned long last = vfn + nr;

	BUG_ON((table == NULL) && (nr == 0));

	ptr = ipa_vfn_to_ptep(table, vfn, 2);

	do {
		unsigned long end = ipa_map_next_boundary(vfn, last, BLOCK_FRAME_SIZE);

		if (ipa_map_is_aligned(vfn | end, BLOCK_FRAME_SIZE)) {
			ipa_pte_remove(ptr, 2);
			vfn += BLOCK_FRAME_SIZE;
		} else {
			if (!ipa_pte_is_table(ptr))
				ipa_map_downsize_block(ptr);

			submap = ipa_pte_to_ptep(ptr);
			rc = ipa_unmap_page_range(submap, vfn, end - vfn);
			if (rc < 0)
				break;
			vfn = end;
		}

		ptr++;
	} while (vfn < last);

	return rc;
}

int ipa_unmap_section_range(unsigned long* table, unsigned long vfn, int nr)
{
	int rc = 0;
	unsigned long *submap, *ptr;
	unsigned long last = vfn + nr;

	BUG_ON((table == NULL) && (nr == 0));

	ptr = ipa_vfn_to_ptep(table, vfn, 1);

	do {
		unsigned long end = ipa_map_next_boundary(vfn, last, SECTION_FRAME_SIZE);

		if (ipa_map_is_aligned(vfn | end, SECTION_FRAME_SIZE)) {
			ipa_pte_remove(ptr, 1);
			vfn += SECTION_FRAME_SIZE;
		} else {
			if (!ipa_pte_is_table(ptr))
				ipa_map_downsize_section(ptr);

			submap = ipa_pte_to_ptep(ptr);
			rc = ipa_unmap_block_range(submap, vfn, end - vfn);
			if (rc < 0)
				break;
			vfn = end;
		}

		ptr++;
	} while (vfn < last);

	return rc;
}

int ipa_unmap_range(unsigned long* table, unsigned long vfn, int nr)
{
	int rc = 0;
	unsigned long *submap, *ptr;
	unsigned long last = vfn + nr;

	BUG_ON((table == NULL) && (nr == 0));

	ptr = ipa_vfn_to_ptep(table, vfn, 0);

	do {
		unsigned long end = ipa_map_next_boundary(vfn, last, REGION_FRAME_SIZE);

		if (!ipa_pte_is_table(ptr))
			return -EINVAL;

		submap = ipa_pte_to_ptep(ptr);
		rc = ipa_unmap_section_range(submap, vfn, end - vfn);
		if (rc < 0)
			 break;

		vfn = end;
		ptr++;
	} while (vfn < last);

	return rc;
}

