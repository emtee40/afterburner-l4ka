/*********************************************************************
 *                
 * Copyright (C) 2004 Joshua LeVasseur
 *
 * File path:	linuxblock/L4VMblock.h
 * Description:	Common declarations for the server and client of the
 * 		Linux block driver.
 *
 * Proprietary!  DO NOT DISTRIBUTE!
 *
 * $Id: block.h,v 1.1 2006/09/21 09:28:35 joshua Exp $
 *                
 ********************************************************************/
#ifndef __linuxblock__L4VMblock_h__
#define __linuxblock__L4VMblock_h__

#include <linux/version.h>
#include <l4/kdebug.h>

/*
#if IDL4_HEADER_REVISION < 20031207
# error "Your version of IDL4 is too old.  Please upgrade to the latest."
#endif
*/

#define TRUE	1
#define FALSE	0

#define L4_TAG_IRQ      0x100

#define RAW(a)	((void *)((a).raw))

#if defined(CONFIG_AFTERBURN_DRIVERS_BLOCK_OPTIMIZE)

#define L4VMblock_debug_level	2

#define PARANOID(a)		// Don't execute paranoid stuff.
#define ASSERT(a)		// Don't execute asserts.

#else

extern int L4VMblock_debug_level;

#define PARANOID(a)		a
#define ASSERT(a)		do { if(!(a)) { printk( PREFIX "assert failure %s:%s:%d\n", __FILE__, __FUNCTION__, __LINE__); L4_KDB_Enter("assert"); } } while(0)

#endif	/* CONFIG_AFTERBURN_DRIVERS_BLOCK_OPTIMIZE */

#define dprintk(n,a...) do { if(L4VMblock_debug_level >= (n)) printk(a); } while(0)

typedef struct 
{
    L4_Word16_t cnt;
    L4_Word16_t start_free;
    L4_Word16_t start_dirty;
} L4VMblock_ring_t;

extern inline L4_Word16_t
L4VMblock_ring_available( L4VMblock_ring_t *ring )
{
    return (ring->start_dirty + ring->cnt - (ring->start_free + 1)) % ring->cnt;
}

#define L4VMBLOCK_SERVER_IRQ_DISPATCH	(1)
#define L4VMBLOCK_CLIENT_IRQ_CLEAN	(1)

#endif	/* __linuxblock__L4VMblock_h__ */