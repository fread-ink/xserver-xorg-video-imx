/*
 * Copyright (C) 2010-2011 Freescale Semiconductor, Inc.  All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation 
 * files (the "Software"), to deal in the Software without 
 * restriction, including without limitation the rights to use, copy, 
 * modify, merge, publish, distribute, sublicense, and/or sell copies 
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS 
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN 
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN 
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
 * SOFTWARE.
 */

/*
 * Based on xorg exa_offscreen.c
 */

/*
 * Copyright © 2003 Anders Carlsson
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Anders Carlsson not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Anders Carlsson makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * ANDERS CARLSSON DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL ANDERS CARLSSON BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/** @file
 * This allocator allocates blocks of memory by maintaining a list of areas.
 * When allocating, the contiguous block of areas with the minimum eviction
 * cost is found and evicted in order to make room for the new allocation.
 */
#include <xorg-server.h>

#include "xf86.h"
#include "exa.h"
#include "imx.h"
#include "imx_exa.h"

#include <limits.h>
#include <assert.h>
#include <stdlib.h>

#if (IMX_EXA_VERSION_COMPILED >= IMX_EXA_VERSION(2,5,0))

#define	DEBUG_OFFSCREEN		0

#if DEBUG_OFFSCREEN
#define DBG_OFFSCREEN(a) ErrorF a
#else
#define DBG_OFFSCREEN(a)
#endif

#if DEBUG_OFFSCREEN
static void
imxExaOffscreenValidate (ScreenPtr pScreen)
{
    /* Access the driver specific data. */
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    ImxPtr imxPtr = IMXPTR(pScrn);
    ImxExaPtr imxExaPtr = IMXEXAPTR(imxPtr);
    ExaOffscreenArea *prev = 0, *area;

    assert (imxExaPtr->offScreenAreas->base_offset == 
	    imxExaPtr->exaDriverPtr->offScreenBase);
    for (area = imxExaPtr->offScreenAreas; area; area = area->next)
    {
	assert (area->offset >= area->base_offset &&
		area->offset < (area->base_offset + area->size));
	if (prev)
	    assert (prev->base_offset + prev->size == area->base_offset);
	prev = area;
    }
    assert (prev->base_offset + prev->size == imxExaPtr->exaDriverPtr->memorySize);
}
#else
#define imxExaOffscreenValidate(s)
#endif

/* merge the next free area into this one */
static void
imxExaOffscreenMerge (ImxExaPtr imxExaPtr, ExaOffscreenArea *area)
{
    ExaOffscreenArea	*next = area->next;

    /* account for space */
    area->size += next->size;
    /* frob pointer */
    area->next = next->next;
    if (area->next)
	area->next->prev = area;
    else
	imxExaPtr->offScreenAreas->prev = area;
    free (next);

    imxExaPtr->numOffscreenAvailable--;
}

/**
 * imxExaOffscreenFree frees an allocation.
 *
 * @param pScreen current screen
 * @param area offscreen area to free
 *
 * imxExaOffscreenFree frees an allocation created by imxExaOffscreenAlloc.  Note that
 * the save callback of the area is not called, and it is up to the driver to
 * do any cleanup necessary as a result.
 *
 * @return pointer to the newly freed area. This behavior should not be relied
 * on.
 */
ExaOffscreenArea *
imxExaOffscreenFree (ScreenPtr pScreen, ExaOffscreenArea *area)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    ImxPtr imxPtr = IMXPTR(pScrn);
    ImxExaPtr imxExaPtr = IMXEXAPTR(imxPtr);
    ExaOffscreenArea	*next = area->next;
    ExaOffscreenArea	*prev;

    DBG_OFFSCREEN (("Freed (%u) 0x%x -> 0x%x (0x%x)\n", area->last_use,
                    area->size, area->base_offset, area->offset));
    imxExaOffscreenValidate (pScreen);

    area->state = ExaOffscreenAvail;
    area->save = NULL;
    area->last_use = 0;
    area->eviction_cost = 0;
    /*
     * Find previous area
     */
    if (area == imxExaPtr->offScreenAreas)
	prev = NULL;
    else
	prev = area->prev;

    imxExaPtr->numOffscreenAvailable++;

    /* link with next area if free */
    if (next && next->state == ExaOffscreenAvail)
	imxExaOffscreenMerge (imxExaPtr, area);

    /* link with prev area if free */
    if (prev && prev->state == ExaOffscreenAvail)
    {
	area = prev;
	imxExaOffscreenMerge (imxExaPtr, area);
    }

    imxExaOffscreenValidate (pScreen);
    return area;
}

static ExaOffscreenArea *
imxExaOffscreenKickOut (ScreenPtr pScreen, ExaOffscreenArea *area)
{
    if (area->save)
	(*area->save) (pScreen, area);
    return imxExaOffscreenFree (pScreen, area);
}

static void
imxExaUpdateEvictionCost(ExaOffscreenArea *area, unsigned offScreenCounter)
{
    unsigned age;

    if (area->state == ExaOffscreenAvail)
	return;

    age = offScreenCounter - area->last_use;

    /* This is unlikely to happen, but could result in a division by zero... */
    if (age > (UINT_MAX / 2)) {
	age = UINT_MAX / 2;
	area->last_use = offScreenCounter - age;
    }

    area->eviction_cost = area->size / age;
}

static ExaOffscreenArea *
imxExaFindAreaToEvict(ImxExaPtr imxExaPtr, int size, int align)
{
    ExaOffscreenArea *begin, *end, *best;
    unsigned cost, best_cost;
    int avail, real_size;

    best_cost = UINT_MAX;
    begin = end = imxExaPtr->offScreenAreas;
    avail = 0;
    cost = 0;
    best = 0;

    while (end != NULL)
    {
	restart:
	while (begin != NULL && begin->state == ExaOffscreenLocked)
	    begin = end = begin->next;

	if (begin == NULL)
	    break;

	/* adjust size needed to account for alignment loss for this area */
	real_size = size + (begin->base_offset + begin->size - size) % align;

	while (avail < real_size && end != NULL)
	{
	    if (end->state == ExaOffscreenLocked) {
		/* Can't more room here, restart after this locked area */
		avail = 0;
		cost = 0;
		begin = end;
		goto restart;
	    }
	    avail += end->size;
	    imxExaUpdateEvictionCost(end, imxExaPtr->offScreenCounter);
	    cost += end->eviction_cost;
	    end = end->next;
	}

	/* Check the cost, update best */
	if (avail >= real_size && cost < best_cost) {
	    best = begin;
	    best_cost = cost;
	}

	avail -= begin->size;
	cost -= begin->eviction_cost;
	begin = begin->next;
    }

    return best;
}

/**
 * imxExaOffscreenAlloc allocates offscreen memory
 *
 * @param pScreen current screen
 * @param size size in bytes of the allocation
 * @param align byte alignment requirement for the offset of the allocated area
 * @param locked whether the allocated area is locked and can't be kicked out
 * @param save callback for when the area is evicted from memory
 * @param privdata private data for the save callback.
 *
 * Allocates offscreen memory from the device associated with pScreen.  size
 * and align deteremine where and how large the allocated area is, and locked
 * will mark whether it should be held in card memory.  privdata may be any
 * pointer for the save callback when the area is removed.
 *
 * Note that locked areas do get evicted on VT switch unless the driver
 * requested version 2.1 or newer behavior.  In that case, the save callback is
 * still called.
 */
ExaOffscreenArea *
imxExaOffscreenAlloc (ScreenPtr pScreen, int size, int align,
                   Bool locked,
                   ExaOffscreenSaveProc save,
                   pointer privData)
{
    ExaOffscreenArea *area;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    ImxPtr imxPtr = IMXPTR(pScrn);
    ImxExaPtr imxExaPtr = IMXEXAPTR(imxPtr);
    int real_size = 0, largest_avail = 0;

    imxExaOffscreenValidate (pScreen);
    if (!align)
	align = 1;

    if (!size)
    {
	DBG_OFFSCREEN (("Alloc 0x%x -> EMPTY\n", size));
	return NULL;
    }

    /* throw out requests that cannot fit */
    if (size > (imxExaPtr->exaDriverPtr->memorySize - imxExaPtr->exaDriverPtr->offScreenBase))
    {
	DBG_OFFSCREEN (("Alloc 0x%x vs (0x%lx) -> TOBIG\n", size,
			imxExaPtr->exaDriverPtr->memorySize -
			imxExaPtr->exaDriverPtr->offScreenBase));
	return NULL;
    }

    /* Try to find a free space that'll fit. */
    for (area = imxExaPtr->offScreenAreas; area; area = area->next)
    {
	/* skip allocated areas */
	if (area->state != ExaOffscreenAvail)
	    continue;

	/* adjust size to match alignment requirement */
	real_size = size + (area->base_offset + area->size - size) % align;

	/* does it fit? */
	if (real_size <= area->size)
	    break;

	if (area->size > largest_avail)
	    largest_avail = area->size;
    }

    if (!area)
    {
	area = imxExaFindAreaToEvict(imxExaPtr, size, align);

	if (!area)
	{
	    DBG_OFFSCREEN (("Alloc 0x%x -> NOSPACE\n", size));
	    /* Could not allocate memory */
	    imxExaOffscreenValidate (pScreen);
	    return NULL;
	}

	/* adjust size needed to account for alignment loss for this area */
	real_size = size + (area->base_offset + area->size - size) % align;

	/*
	 * Kick out first area if in use
	 */
	if (area->state != ExaOffscreenAvail)
	    area = imxExaOffscreenKickOut (pScreen, area);
	/*
	 * Now get the system to merge the other needed areas together
	 */
	while (area->size < real_size)
	{
	    assert (area->next && area->next->state == ExaOffscreenRemovable);
	    (void) imxExaOffscreenKickOut (pScreen, area->next);
	}
    }

    /* save extra space in new area */
    if (real_size < area->size)
    {
	ExaOffscreenArea   *new_area = malloc (sizeof (ExaOffscreenArea));
	if (!new_area)
	    return NULL;
	new_area->base_offset = area->base_offset;

	new_area->offset = new_area->base_offset;
	new_area->align = 0;
	new_area->size = area->size - real_size;
	new_area->state = ExaOffscreenAvail;
	new_area->save = NULL;
	new_area->last_use = 0;
	new_area->eviction_cost = 0;
	new_area->next = area;
	new_area->prev = area->prev;
	if (area->prev->next)
	    area->prev->next = new_area;
	else
	    imxExaPtr->offScreenAreas = new_area;
	area->prev = new_area;
	area->base_offset = new_area->base_offset + new_area->size;
	area->size = real_size;
    } else
	imxExaPtr->numOffscreenAvailable--;

    /*
     * Mark this area as in use
     */
    if (locked)
	area->state = ExaOffscreenLocked;
    else
	area->state = ExaOffscreenRemovable;
    area->privData = privData;
    area->save = save;
    area->last_use = imxExaPtr->offScreenCounter++;
    area->offset = (area->base_offset + align - 1);
    area->offset -= area->offset % align;
    area->align = align;

    imxExaOffscreenValidate (pScreen);

    DBG_OFFSCREEN (("Alloc (%d) 0x%x -> 0x%x (0x%x)\n", area->last_use,
                    size, area->base_offset, area->offset));
    return area;
}

void
imxExaOffscreenSwapIn (ScreenPtr pScreen)
{
    imxExaOffscreenInit (pScreen);
}

/**
 * imxExaOffscreenInit initializes the offscreen memory manager.
 *
 * @param pScreen current screen
 *
 * imxExaOffscreenInit is called by exaDriverInit to set up the memory manager for
 * the screen, if any offscreen memory is available.
 */
Bool
imxExaOffscreenInit (ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    ImxPtr imxPtr = IMXPTR(pScrn);
    ImxExaPtr imxExaPtr = IMXEXAPTR(imxPtr);
    ExaOffscreenArea *area;

    /* Allocate a big free area */
    area = malloc (sizeof (ExaOffscreenArea));

    if (!area)
	return FALSE;

    area->state = ExaOffscreenAvail;
    area->base_offset = imxExaPtr->exaDriverPtr->offScreenBase;
    area->offset = area->base_offset;
    area->align = 0;
    area->size = imxExaPtr->exaDriverPtr->memorySize - area->base_offset;
    area->save = NULL;
    area->next = NULL;
    area->prev = area;
    area->last_use = 0;
    area->eviction_cost = 0;

    /* Add it to the free areas */
    imxExaPtr->offScreenAreas = area;
    imxExaPtr->offScreenCounter = 1;
    imxExaPtr->numOffscreenAvailable = 1;

    imxExaOffscreenValidate (pScreen);

    return TRUE;
}

void
imxExaOffscreenFini (ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    ImxPtr imxPtr = IMXPTR(pScrn);
    ImxExaPtr imxExaPtr = IMXEXAPTR(imxPtr);
    ExaOffscreenArea *area;

    /* just free all of the area records */
    while ((area = imxExaPtr->offScreenAreas))
    {
	imxExaPtr->offScreenAreas = area->next;
	free (area);
    }
}

/**
 * Ejects all offscreen areas, and uninitializes the offscreen memory manager.
 */
void
imxExaOffscreenSwapOut (ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    ImxPtr imxPtr = IMXPTR(pScrn);
    ImxExaPtr imxExaPtr = IMXEXAPTR(imxPtr);

    imxExaOffscreenValidate (pScreen);
    /* loop until a single free area spans the space */
    for (;;)
    {
	ExaOffscreenArea *area = imxExaPtr->offScreenAreas;

	if (!area)
	    break;
	if (area->state == ExaOffscreenAvail)
	{
	    area = area->next;
	    if (!area)
		break;
	}
	assert (area->state != ExaOffscreenAvail);
	(void) imxExaOffscreenKickOut (pScreen, area);
	imxExaOffscreenValidate (pScreen);
    }
    imxExaOffscreenValidate (pScreen);
    imxExaOffscreenFini (pScreen);
}

#endif
