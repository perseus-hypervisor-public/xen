#ifndef __ARM_IPA_H__
#define __ARM_IPA_H__

#define IPA_PAGE_SHIFT		(12)
#define IPA_PAGE_SIZE		(1 << IPA_PAGE_SHIFT)

#define IPA_PTE_STRONGORDERD	(0x0UL)
#define IPA_PTE_BUFFERABLE		(0x1UL)
#define IPA_PTE_WRITETHROUGH	(0x2UL)
#define IPA_PTE_WRITEBACK		(0x3UL)
#define IPA_PTE_DEVICE			(0x4UL)
#define IPA_PTE_WRITEALLOC		(0x7UL)

/* Shareability values for the IPA entries */
#define IPA_PTE_NONE			((0x0UL) << 8)
#define IPA_PTE_OUTER			((0x2UL) << 8)
#define IPA_PTE_INNER			((0x3UL) << 8)

#define IPA_PTE_READABLE	((0x1UL) << 6)
#define IPA_PTE_WRITABLE	((0x2UL) << 6)
#define IPA_PTE_XN			((0x1UL) << 54)

#define IPA_PTE_ACCESSED	((0x1UL) << 10)

#define IPA_PTE_VALID	((0x1UL) << 0)
#define IPA_PTE_TABLE	((0x3UL) << 0)
#define IPA_PTE_BLOCK	((0x1UL) << 0)
#define IPA_PTE_PAGE	((0x3UL) << 0)
#define IPA_PTE_MASK	((0x3UL) << 0)

#define IPA_TYPE_NORMAL (IPA_PTE_OUTER | IPA_PTE_ACCESSED | ((IPA_PTE_READABLE | IPA_PTE_WRITABLE)) | (0xF << 2))
#define IPA_TYPE_DEVICE (IPA_PTE_OUTER | IPA_PTE_ACCESSED | ((IPA_PTE_READABLE | IPA_PTE_WRITABLE)) | (0x1 << 2))

unsigned long *ipa_alloc_table(void);
void ipa_free_table(unsigned long *table);

int ipa_map_range(unsigned long *table, unsigned long vfn, unsigned long mfn, int nr, unsigned long flags);
int ipa_map_section_range(unsigned long *table, unsigned long vfn, unsigned long mfn, int nr, unsigned long flags);
int ipa_unmap_range(unsigned long* table, unsigned long vfn, int nr);
int ipa_unmap_section_range(unsigned long* table, unsigned long vfn, int nr);

int ipa_protect_range(unsigned long* table, unsigned long vfn, int nr, unsigned long flags);
int ipa_protect_section_range(unsigned long* table, unsigned long vfn, int nr, unsigned long flags);

void ipa_dump_page_maps(unsigned long *page_table, int depth);
#endif /*!__ARM_IPA_H__*/
