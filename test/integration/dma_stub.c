// SPDX-License-Identifier: GPL-2.0
/*
 * What dma.c needs to run here: memory for the rings, skbs for the receive
 * buffers, and a fake DMA mapping. Compiled with the kernel's flags because
 * the skbs are real `struct sk_buff`s: dma.c fills them with skb_put and
 * reads the RX header from skb->data.
 *
 * No peripheral writes into this memory. The descriptors dma.c fills stay as
 * it leaves them, and the RX buffers' header stays the poison it puts there:
 * frame_len = 0. So when the timeline delivers an RX_DONE, b43_dma_rx() reads
 * the current slot from the status register -- the oracle serves that, from
 * the capture -- walks the slots up to it, finds the length at zero, drops
 * and recycles the buffer, and writes the index back: the same register ops
 * the vendor emits for a frame, with b43's ring geometry.
 *
 * The DMA addresses are pseudo-physical, handed out by a counter starting
 * where the agcombo's capture has the first ring (0x00c18000) and aligned to
 * the size asked, as dma_alloc_coherent() does with powers of two: nobody
 * reads the value but the rings' base registers, which so carry numbers of
 * the order of the vendor's and the alignment dma.c expects from the
 * allocator.
 */
#include <bcma-next/bcmn.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <net/page_pool/types.h>
#include <net/mac80211.h>

#include "trace_out.h"

/*
 * The constants virt_to_page() and __pa() use inline in the x86 headers.
 * With all three at zero __pa(x) == x and the page is x / 64: here the
 * computation only has to touch no memory, not give a real page.
 */
unsigned long page_offset_base;
unsigned long phys_base;
unsigned long vmemmap_base;

bool is_vmalloc_addr(const void *x) { return false; }

/* --- the rings' coherent memory ------------------------------------------ */

#define SHIM_DMA_BASE	0x00c18000ull

static dma_addr_t shim_dma_next = SHIM_DMA_BASE;

void *dma_alloc_attrs(struct device *dev, size_t size, dma_addr_t *handle,
		      gfp_t gfp, unsigned long attrs)
{
	size_t align = roundup_pow_of_two(size);
	void *p = kzalloc(size + align, gfp);
	uintptr_t a;

	if (!p)
		return NULL;
	a = ((uintptr_t)p + align - 1) & ~(uintptr_t)(align - 1);
	*handle = ALIGN(shim_dma_next, align);
	shim_dma_next = *handle + align;
	return (void *)a;
}

void dma_free_attrs(struct device *dev, size_t size, void *vaddr,
		    dma_addr_t handle, unsigned long attrs)
{
		/* The base cannot be recovered from the aligned address: it stays. */
}

int dma_set_mask(struct device *dev, u64 mask) { return 0; }
int dma_set_coherent_mask(struct device *dev, u64 mask) { return 0; }

/* --- mapping dei buffer: un handle unico, mai dereferenziato ------------- */

dma_addr_t dma_map_page_attrs(struct device *dev, struct page *page,
			      size_t offset, size_t size,
			      enum dma_data_direction dir, unsigned long attrs)
{
	dma_addr_t h = shim_dma_next;

	shim_dma_next += ALIGN(size, 0x1000);
	return h;
}

void dma_unmap_page_attrs(struct device *dev, dma_addr_t addr, size_t size,
			  enum dma_data_direction dir, unsigned long attrs) { }
void __dma_sync_single_for_cpu(struct device *dev, dma_addr_t addr, size_t size,
			       enum dma_data_direction dir) { }
void __dma_sync_single_for_device(struct device *dev, dma_addr_t addr,
				  size_t size, enum dma_data_direction dir) { }

/* No syncs needed: dma.c keeps its streaming TX headers and RX buffers */
bool __dma_need_sync(struct device *dev, dma_addr_t dma_addr) { return false; }

/* --- skb: only what dma.c touches ----------------------------------------- */

struct sk_buff *__netdev_alloc_skb(struct net_device *dev, unsigned int len,
				   gfp_t gfp)
{
	struct sk_buff *skb = kzalloc(sizeof(*skb), gfp);
	u8 *data;

	if (!skb)
		return NULL;
	data = kzalloc(len, gfp);
	if (!data) {
		kfree(skb);
		return NULL;
	}
	skb->head = data;
	skb->data = data;
	skb->tail = 0;
	skb->end = len;
	skb->len = 0;
	return skb;
}

void *skb_put(struct sk_buff *skb, unsigned int len)
{
	void *p = skb->head + skb->tail;

	skb->tail += len;
	skb->len += len;
	return p;
}

void *skb_pull(struct sk_buff *skb, unsigned int len)
{
	skb->len -= len;
	skb->data += len;
	return skb->data;
}

/*
 * The RX buffers' page pool is not created: dma.c falls back on
 * __dev_alloc_skb(), above, and the buffers stay the ones the poison and the
 * trace know. The pool's other symbols are needed only by the branches with
 * a pool, and are in kernel_shim.c.
 */
struct page_pool *page_pool_create(const struct page_pool_params *params)
{
	return ERR_PTR(-EOPNOTSUPP);
}

#ifdef CONFIG_PAGE_POOL
void page_pool_destroy(struct page_pool *pool) { }
#endif

/* Read inline by virt_to_head_page() in page_pool_free_va(). */
DEFINE_STATIC_KEY_FALSE(hugetlb_optimize_vmemmap_key);

void consume_skb(struct sk_buff *skb)
{
	if (!skb)
		return;
	kfree(skb->head);
	kfree(skb);
}

void *kmemdup_noprof(const void *src, size_t len, gfp_t gfp)
{
	void *p = kmalloc(len, gfp);

	if (p)
		memcpy(p, src, len);
	return p;
}

const char *dev_driver_string(const struct device *dev) { return "b43"; }

/* mac80211's TX queues and the transmission state: no traffic here. */
void ieee80211_wake_queue(struct ieee80211_hw *hw, int queue) { }
void ieee80211_tx_status_skb(struct ieee80211_hw *hw, struct sk_buff *skb) { }
