/*********************************************************************
 *
 * Copyright (C) 2005,  University of Karlsruhe
 *
 * File path:     afterburn-wedge/include/ia32/intlogic.h
 * Description:   The IA32 interrupt logic model.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: intlogic.h,v 1.15 2006/01/11 17:53:24 stoess Exp $
 *
 ********************************************************************/
#ifndef __AFTERBURN_WEDGE__INCLUDE__IA32__INTLOGIC_H__
#define __AFTERBURN_WEDGE__INCLUDE__IA32__INTLOGIC_H__

/*
 * Systems can have multiple interrupt controllers.  The interrupt 
 * routing is hidden behind the abstract interrupt logic type.
 *
 * The interrupt logic exposes three interfaces categories:
 * 1. support for devices to raise interrupts.
 * 2. support for the front-end and back-end to query for pending 
 *    interrupt requests.
 * 3. support for the interrupt controller state machine, for 
 *    configuring, masking, acking, eoi, etc.
 *
 * Devices call get_intlogic()->raise_irq(irq) to raise an interrupt
 * request.  This operates on the irq name space.
 *
 * The wedge calls get_intlogic()->pending_vector(&vector) to retrieve
 * the next pending vector.  The vector belongs to the CPU's vector name
 * space.  The interrupt logic is responsible for translating irq names
 * into vector names.
 *
 * Legacy interrupts:
 * 0 - internal timer, Counter 0 output.
 * 1 - keyboard
 * 2 - slave controller INTR output
 * 3 - serial port A
 * 4 - serial port B
 * 5 - parallel port
 * 6 - floppy disk
 * 7 - parallel port
 * 8 - internal RTC
 * 9 - 
 * 10 -
 * 11 -
 * 12 - PS/2 mouse
 * 13 - 
 * 14 - primary IDE
 * 15 - secondary IDE
 */

#if !defined(ASSEMBLY)

#include INC_ARCH(types.h)
#include INC_ARCH(sync.h)
#include INC_WEDGE(vcpulocal.h)
#include <device/i8259a.h>

#include <device/lapic.h>
#include <device/i82093.h>

#define INTLOGIC_TIMER_IRQ	  0
#define INTLOGIC_INVALID_VECTOR	  APIC_INVALID_VECTOR
#define INTLOGIC_MAX_HWIRQS	  256
#define INTLOGIC_INVALID_IRQ	  INTLOGIC_MAX_HWIRQS	


class intlogic_t 
{
private:
    //spinlock_t intlogic_lock;

#if defined(CONFIG_DEVICE_PASSTHRU)
    volatile word_t hwirq_latch;	// Real device interrupts in use.
    volatile word_t hwirq_mask;	        // Real device interrupts mask
    word_t hwirq_squash;		// Squashed device interrupts
#endif
    word_t irq_trace;			// Tracing of irqs

    i82093_t *pin_to_ioapic[INTLOGIC_MAX_HWIRQS];
public:
    i82093_t __attribute__((aligned(4096))) virtual_ioapic[CONFIG_MAX_IOAPICS];


public:
    i8259a_t master;
    i8259a_t slave;

	    
 
#if defined(CONFIG_DEVICE_PASSTHRU)
    intlogic_t()
	{
	    hwirq_latch = 0;
	    
	    //device IRQ masked=0
	    hwirq_mask = 0;

	    // Don't mix virtual IRQs with device IRQs.  Hard coded to squash
	    // IRQs 3/4=serial port
	    hwirq_squash = ( (1<<4) | (1<<3) );

#if !defined(CONFIG_WEDGE_XEN)
	    // add IRQs 0=timer
	    hwirq_squash |= ( (1<<0) );
#endif

#if defined(CONFIG_WEDGE_L4KA)
	    hwirq_squash |= ( (1<<9) | (1<<2) );
#endif
	    
	    // add IRQs 2=cascade, APIC mode disables that if necessary
	    hwirq_squash |= ( (1<<2) );
	    
	    for (word_t i=0; i<INTLOGIC_MAX_HWIRQS; i++)
		pin_to_ioapic[i] = NULL;
	    
	    irq_trace = 0;
	    //irq_trace |= (1 << 17);

	}

    void set_hwirq_latch(const word_t hwirq)
	{ bit_set_atomic(hwirq, hwirq_latch); }
    bool test_and_set_hwirq_latch(word_t hwirq)
	{ return bit_test_and_set_atomic(hwirq, hwirq_latch); } 
    word_t get_hwirq_latch()
	{ return hwirq_latch;} 
    bool is_hwirq_latched(word_t hwirq)
	{ return (hwirq_latch & (1<<hwirq));} 

    void set_hwirq_mask(const word_t hwirq)
	{ bit_set_atomic(hwirq, hwirq_mask); }
    void clear_hwirq_mask(const word_t hwirq)
	{ bit_clear_atomic(hwirq, hwirq_mask); }
    bool test_and_clear_hwirq_mask(word_t hwirq)
	{ return bit_test_and_clear_atomic(hwirq, hwirq_mask);} 
    word_t get_hwirq_mask()
	{ return hwirq_mask;}

    void add_hwirq_squash(word_t hwirq)
        { hwirq_squash |= 1 << hwirq; }
    void clear_hwirq_squash(word_t hwirq)
	{ hwirq_squash &= ~(1 << hwirq); }
    bool is_hwirq_squashed(word_t hwirq)
	{ return (hwirq_squash & (1<<hwirq));} 
    word_t get_hwirq_squash()
	{ return hwirq_squash; }

#endif /* CONFIG_DEVICE_PASSTHRU */	   

    bool is_irq_traced(word_t irq)
	{ return (irq < INTLOGIC_MAX_HWIRQS) && (irq_trace & (1<<irq));} 

    void raise_irq ( word_t irq )
	{
	    if (is_irq_traced(irq))
		con << "INTLOGIC: IRQ" << irq << " VCPU  " << get_vcpu().cpu_id << "\n";
	    
	    local_apic_t &lapic = get_lapic();
	    i82093_t *ioapic;
	    ASSERT(lapic.get_id() == get_vcpu().cpu_id);
	    
	    /*
	     * Deliver timer to local APIC
	     */
	    if (irq == INTLOGIC_TIMER_IRQ)
	    {
		if (lapic.is_enabled())
		    lapic.raise_timer_irq();
	    }
	    else if ((ioapic = hwirq_get_ioapic(irq)) != NULL)
		ioapic->raise_irq(irq);
	    else
	    {
		con << "No IOAPIC for " << irq << "\n";
		DEBUGGER_ENTER(0);
	    }

	    /*
	     * Virtual wire / PIC legacy
	     */
	    if (EXPECT_FALSE(lapic.is_virtual_wire()))
	    {
		/*
		 * We currently only support BSP virtual wire
		 */
		ASSERT(lapic.get_id() == 0);
		if( irq < 8)
		    master.raise_irq(irq, 0);
		else if (irq < 16)
		    slave.raise_irq(irq, 8);
	    }
	    
	}
    
    bool pending_vector( word_t &vector, word_t &irq )
	{
	    local_apic_t &lapic = get_lapic();
	    ASSERT(lapic.get_id() == get_vcpu().cpu_id);
	    ASSERT(lapic.is_valid_lapic());

	    bool ret = false;
	    
	    if (lapic.is_enabled())
	    {
		if (lapic.pending_vector(vector, irq))
		    ret = true;
		else if(lapic.is_virtual_wire())
		{
		    /*
		     * We currently only support BSP virtual wire
		     */
		    ASSERT(lapic.get_id() == 0);
			
		    if (master.pending_vector(vector, irq, 0))
			ret = true;
		    if (slave.pending_vector(vector, irq, 8))
			ret = true;
		}
	    }
    
	    return ret;
	}

    void reraise_vector ( word_t vector, word_t irq)
	{
	    
	    local_apic_t &lapic = get_lapic();
	    
	    lapic.raise_vector(vector, irq, true);
	    
	    /*
	     * Virtual wire / PIC legacy
	     */
	    if (EXPECT_FALSE(lapic.is_virtual_wire()))
	    {
		/*
		 * We currently only support BSP virtual wire
		 */
		ASSERT(lapic.get_id() == 0);
		if( irq < 8)
		    master.raise_irq(irq, 0);
		else if (irq < 16)
		    slave.raise_irq(irq, 8);
	    }
	    
	}

    
    void hwirq_register_ioapic(word_t hwirq, i82093_t *i)
	{
	    ASSERT(hwirq < INTLOGIC_MAX_HWIRQS);
	    ASSERT(i->is_valid_ioapic());
	    pin_to_ioapic[hwirq] = i;
	}
    i82093_t *hwirq_get_ioapic(word_t hwirq)
	{
	    ASSERT(hwirq < INTLOGIC_MAX_HWIRQS);
	    i82093_t *ret = pin_to_ioapic[hwirq];
	    ASSERT(ret == NULL || ret->is_valid_ioapic());
	    return ret;
	}
    
    void init_virtual_apics(word_t real_irq_sources);

    
    void raise_synchronous_irq( word_t irq )
	{
	    raise_irq(irq);
	    deliver_synchronous_irq();
	}
 
    bool deliver_synchronous_irq();

};

INLINE intlogic_t & get_intlogic()
{
    extern intlogic_t intlogic;
    return intlogic;
}

#endif	/* !ASSEMBLY */

#endif	/* __AFTERBURN_WEDGE__INCLUDE__IA32__INTLOGIC_H__ */