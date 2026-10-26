// SPDX-License-Identifier: GPL-2.0
/*
 * G665: deterministically interleave an RCU-protected XArray lookup
 * with real XArray root shrink, subtree destruction and final erase.
 *
 * Test binary runs ONLY on the development host; no kernel/BOOT changes.
 * Requires liburcu headers and library from the existing radix-tree suite.
 */
#include <assert.h>
#include <stdio.h>
#include <linux/xarray.h>
#include <linux/rcupdate.h>
#include "test.h"

static void cached_node_shrink(void)
{
	DEFINE_XARRAY(xa);
	XA_STATE(xas, &xa, 0);
	int old_zero, old_one, replacement;
	void *old_slot;

	assert(xa_store(&xa, 0, &old_zero, GFP_KERNEL) == NULL);
	assert(xa_store(&xa, 1, &old_one, GFP_KERNEL) == NULL);
	rcu_read_lock();
	assert(xas_load(&xas) == &old_zero);
	assert(xas.xa_node);
	assert(xa_erase(&xa, 1) == &old_one);
	old_slot = xas_reload(&xas);
	assert(xa_is_retry(old_slot));
	assert(xas_retry(&xas, old_slot));
	assert(xas_load(&xas) == &old_zero);
	assert(xa_erase(&xa, 0) == &old_zero);
	assert(xa_store(&xa, 0, &replacement, GFP_KERNEL) == NULL);
	assert(xas_reload(&xas) == &replacement);
	rcu_read_unlock();
	assert(xa_erase(&xa, 0) == &replacement);
	xa_destroy(&xa);
}

static void cached_node_subtree_destroy(void)
{
	DEFINE_XARRAY(xa);
	XA_STATE(xas, &xa, 0);
	int one, two;

	assert(xa_store(&xa, 0, &one, GFP_KERNEL) == NULL);
	assert(xa_store(&xa, 1, &two, GFP_KERNEL) == NULL);
	rcu_read_lock();
	assert(xas_load(&xas) == &one);
	assert(xas.xa_node);
	xa_destroy(&xa);
	assert(xa_is_retry(xas_reload(&xas)));
	rcu_read_unlock();
}

static void cached_node_final_erase(void)
{
	DEFINE_XARRAY(xa);
	XA_STATE(xas, &xa, 1);
	int one, two;

	assert(xa_store(&xa, 0, &one, GFP_KERNEL) == NULL);
	assert(xa_store(&xa, 1, &two, GFP_KERNEL) == NULL);
	rcu_read_lock();
	assert(xas_load(&xas) == &two);
	assert(xas.xa_node);
	assert(xa_erase(&xa, 0) == &one);
	assert(xa_erase(&xa, 1) == &two);
	assert(xas_reload(&xas) == NULL);
	rcu_read_unlock();
	xa_destroy(&xa);
}

int main(void)
{
	rcu_register_thread();
	radix_tree_init();
	cached_node_shrink();
	cached_node_subtree_destroy();
	cached_node_final_erase();
	radix_tree_cpu_dead(1);
	rcu_barrier();
	rcu_unregister_thread();
	puts("G665 actual XArray cached-node regressions PASS");
	return 0;
}
