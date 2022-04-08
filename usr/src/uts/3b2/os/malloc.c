/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)kernel:os/malloc.c	1.10"
#include "sys/param.h"
#include "sys/types.h"
#include "sys/systm.h"
#include "sys/sysmacros.h"
#include "sys/map.h"
#include "sys/sbd.h"
#include "sys/immu.h"
#include "sys/cmn_err.h"
#include "sys/debug.h"

/*
 * Allocate 'size' units from the given map.
 * Return the base of the allocated space.
 * In a map, the addresses are increasing and the
 * list is terminated by a 0 size.
 * The swap map unit is 512 bytes.
 * Algorithm is first-fit.
 *
 * As part of the kernel evolution toward modular naming, the 
 * functions malloc and mfree are being renamed to rmalloc and rmfree.
 * Compatibility will be maintained by the following assembler code:
 * (also see mfree/rmfree below)
 */

asm("	.text"); 		/* ensure symbol refers to text section */
asm("	.align 4");		/* functions aligned on word boundaries */
asm("	.globl malloc");	/* make symbol tab storage class extern */
asm("malloc:	");		/* define label. will be same as rmalloc */

unsigned long
rmalloc(mp, size)
register struct map *mp;
register size_t size;
{
	register unsigned int a;
	register struct map *bp;

	if (size == 0)
		return(NULL);
	ASSERT(size > 0);
	for (bp = mapstart(mp); bp->m_size; bp++) {
		if (bp->m_size >= size) {
			a = bp->m_addr;
			bp->m_addr += size;
			if ((bp->m_size -= size) == 0) {
				do {
					bp++;
					(bp-1)->m_addr = bp->m_addr;
				} while ((bp-1)->m_size = bp->m_size);
				mapsize(mp)++;
			}
			ASSERT(bp->m_size < (unsigned)0x80000000);
			return(a);
		}
	}
	return(0);
}

/*
 * Free the previously allocated space a
 * of size units into the specified map.
 * Sort a into map and combine on
 * one or both ends if possible.
 *
 */

asm("	.text");
asm("	.align 4");
asm("	.globl mfree");
asm("mfree:	");

void
rmfree(mp, size, a)
register struct map *mp;
register size_t size;
register unsigned long a;
{
	register struct map *bp;
	register unsigned int t;

	if (size == 0)
		return;
	ASSERT(size > 0);
	bp = mapstart(mp);
	for (; bp->m_addr<=a && bp->m_size!=0; bp++);
	if (bp>mapstart(mp) && (bp-1)->m_addr+(bp-1)->m_size == a) {
		(bp-1)->m_size += size;
		if(bp->m_addr){		/* m_addr==0 end of map table */
			ASSERT(a+size <= bp->m_addr);
			if (a+size == bp->m_addr) { 

				/* compress adjacent map addr entries */
				(bp-1)->m_size += bp->m_size;
				while (bp->m_size) {
					bp++;
					(bp-1)->m_addr = bp->m_addr;
					(bp-1)->m_size = bp->m_size;
				}
				mapsize(mp)++;
			}
		}
	} else {
		if (a+size == bp->m_addr && bp->m_size) {
			bp->m_addr -= size;
			bp->m_size += size;
			ASSERT(bp == mapstart(mp)  ||
				((bp - 1)->m_addr + (bp - 1)->m_size) <
				 bp->m_addr);
		} else {
			ASSERT(bp == mapstart(mp)  ||
				((bp - 1)->m_addr + (bp - 1)->m_size) < a);
			ASSERT(bp->m_size == 0 || (a+size < bp->m_addr));
			if (mapsize(mp) == 0) {
				cmn_err(CE_WARN,"rmfree map overflow %x.\
	Lost %d items at %d\n",mp,size,a) ;
				return;
			}
			do {
				t = bp->m_addr;
				bp->m_addr = a;
				a = t;
				t = bp->m_size;
				bp->m_size = size;
				bp++;
			} while (size = t);
			mapsize(mp)--;
		}
	}
	if (mapwant(mp)) {
		mapwant(mp) = 0;
		wakeup((caddr_t)mp);
	}
}

