/* (C) 2005,  University of Karlsruhe
 *
 * File path:     afterburn-wedge/l4-common/irq.cc
 * Description:   The irq thread for handling asynchronous events.
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
 * $Id: irq.cc,v 1.28 2006/01/11 19:01:17 stoess Exp $
 *
 ********************************************************************/

#include <l4/ipc.h>
#include <l4/kip.h>
#include <l4/schedule.h>
#include <l4/thread.h>

#include INC_ARCH(intlogic.h)
#include INC_WEDGE(console.h)
#include INC_WEDGE(vcpulocal.h)
#include INC_WEDGE(backend.h)
#include INC_WEDGE(l4privileged.h)
#include INC_WEDGE(hthread.h)
#include INC_WEDGE(user.h)
#include INC_WEDGE(irq.h)
#include INC_WEDGE(message.h)

#include <device/acpi.h>

static const bool debug_hwirq=0;
static const bool debug_timer=0;
static const bool debug_virq=1;
static const bool debug_ipi=1;
static const bool debug_preemption=0;

static unsigned char irq_stack[CONFIG_NR_VCPUS][KB(16)] ALIGNED(CONFIG_STACK_ALIGN);
static const L4_Clock_t timer_length = {raw: 10000};

L4_ThreadId_t vtimer_tid = L4_nilthread;
cpu_lock_t irq_lock VCPULOCAL("irqlock");

static void irq_handler_thread( void *param, hthread_t *hthread )
{
    L4_Word_t tid_user_base = L4_ThreadIdUserBase(L4_GetKernelInterface());
    vcpu_t &vcpu = get_vcpu();
    
    intlogic_t &intlogic = get_intlogic();
#if defined(CONFIG_VSMP)
    local_apic_t &lapic = get_lapic();
#endif
    L4_ThreadId_t tid = L4_nilthread;
    L4_ThreadId_t ack_tid = L4_nilthread;
    L4_Error_t errcode;
    
    L4_Word_t irq, vector;
    
    L4_Word_t timeouts = default_timeouts;
    
    con << "IRQ thread, "
	<< "TID " << hthread->get_global_tid() 
	<< "\n";      

    // Set our thread's exception handler. 
    L4_Set_ExceptionHandler( get_vcpu().monitor_gtid );
        
    /*
     * Associate with virtual timing source
     * jsXXX: postpone the timing to when VAPIC timer is enabled
     */
    tid.global.X.thread_no = INTLOGIC_TIMER_IRQ;
    tid.global.X.version = 1;
    errcode = AssociateInterrupt( tid, L4_Myself() );
    if ( errcode != L4_ErrOk )
	con << "Unable to associate virtual timer interrupt: "
	    << irq << ", L4 error: " 
	    << L4_ErrString(errcode) << ".\n";
    
    vtimer_tid = resourcemon_shared.cpu[L4_ProcessorNo()].vtimer_tid;
    if ( 1 || debug_timer || intlogic.is_irq_traced(irq)) 
	con << "enable virtual timer"
	    << " irq: " << INTLOGIC_TIMER_IRQ 
	    << " tid: " << vtimer_tid
	    << "\n";

    irq_lock.init();
    
    /* 
     * Tell the monitor that we're up
     */
    msg_startup_monitor_build();
    ack_tid = vcpu.monitor_gtid;
    
   
    for (;;)
    {
	
	L4_MsgTag_t tag = L4_Ipc( ack_tid, L4_anythread, timeouts, &tid );
	
	if ( L4_IpcFailed(tag) )
	{
	    errcode = L4_ErrorCode();
	    DEBUGGER_ENTER();
	    con << "VMEXT IRQ failure "
		<< " to thread " << ack_tid  
		<< " from thread " << tid
		<< " error " << (void *) errcode
		<< "\n";
	    ack_tid = L4_nilthread;
	    continue;
	}
	
	ack_tid = L4_nilthread;
	L4_Set_TimesliceReceiver(L4_nilthread);
	timeouts = default_timeouts;
	
	// Received message.
	switch( L4_Label(tag) )
	{
	    case msg_label_hwirq:
	    {
		// Hardware IRQ.
		if (tid.global.X.thread_no < tid_user_base )
		{
		    ASSERT(CONFIG_DEVICE_PASSTHRU);
		    irq = tid.global.X.thread_no;
		    if (vcpu.cpu_id == 1 && debug_hwirq || intlogic.is_irq_traced(irq)) 
			con << "hardware irq: " << irq
			    << ", int flag: " << get_cpu().interrupts_enabled()
			    << '\n';
		    
		    /* 
		     * jsXXX: not strictly necessary here, 
		     * pic/apic will set it afterwards as well
		     */
		    intlogic.set_hwirq_mask(irq);

		}
		else
		{
		    ASSERT(tid == vtimer_tid);
		    irq = INTLOGIC_TIMER_IRQ;
		    if (debug_timer || intlogic.is_irq_traced(irq)) 
		    {
			static L4_Clock_t last_time = {raw: 0};
			L4_Clock_t current_time = L4_SystemClock();
			L4_Word64_t time_diff = (current_time - last_time).raw;
			con << "vtimer irq: " << irq
			    << " diff " << (L4_Word_t) time_diff
			    << " int flag: " << get_cpu().interrupts_enabled() 
			    << "\n";
			last_time = current_time;
		    }
		}
		
		intlogic.raise_irq( irq );
		/*
		 * If a user level thread was preempted, switch
		 * to the main thread to let him handle the 
		 * preemption IPC
		 */
		if (vcpu.in_dispatch_ipc())
		{
		    if (debug_preemption)
		    {
			con << "forward timeslice to main thread"
			    << "tid " << vcpu.main_gtid
			    << "\n";
		    }		    
		    L4_Set_TimesliceReceiver(vcpu.main_gtid);
		}
		else if (vcpu.main_info.mr_save.is_preemption_msg() &&
			!irq_lock.is_locked())
		{
		    ack_tid = vcpu.main_gtid;
		    if (debug_preemption)
			con << "propagate preemption reply to kernel (IRQ)" 
			    << " tid " << ack_tid << "\n"; 
		    
		    backend_async_irq_deliver( intlogic ); 
		    vcpu.main_info.mr_save.load_preemption_reply();
		    vcpu.main_info.mr_save.set_propagated_reply(vcpu.monitor_gtid); 	
		    vcpu.main_info.mr_save.load_mrs();
		} 
		else
		{
		    /*
		     * If main thread was preempted switch to monitor
		     */
		    if (debug_preemption)
			con << "forward timeslice to monitor thread\n";
		    if (vcpu.monitor_info.mr_save.is_preemption_msg())
		    {
			vcpu.monitor_info.mr_save.load_preemption_reply();
			vcpu.monitor_info.mr_save.load_mrs();			
			ack_tid = vcpu.monitor_gtid;
		    }
		    L4_Set_TimesliceReceiver(vcpu.monitor_gtid);
		}
		break;
	    }
	    case msg_label_hwirq_ack:
	    {
		ASSERT(CONFIG_DEVICE_PASSTHRU);
		msg_hwirq_ack_extract( &irq );
		if (1 || debug_hwirq || intlogic.is_irq_traced(irq))
		    con << "unpropoagated hardware irq ack "
			<< ", irq " << irq 
			<< "\n";
		ack_tid.global.X.thread_no = irq;
		ack_tid.global.X.version = 1;
		L4_LoadMR( 0, 0 );  // Ack msg.
		break;
	    }
	    case msg_label_virq:
	    {
		// Virtual interrupt from external source.
		msg_virq_extract( &irq );
		if ( debug_virq )
		    con << "virtual irq: " << irq 
			<< ", from TID " << tid << '\n';
		intlogic.raise_irq( irq );
		break;
	    }		    
	    case msg_label_ipi:
	    {
		L4_Word_t src_vcpu_id;		
		msg_ipi_extract( &src_vcpu_id, &vector  );
		if (debug_ipi) 
		    con << " IPI from VCPU " << src_vcpu_id 
			<< " vector " << vector
			<< '\n';
#if defined(CONFIG_VSMP)
		lapic.raise_vector(vector, INTLOGIC_INVALID_IRQ);;
#endif		
		msg_ipi_done_build();
		ack_tid = tid;
		break;
	    }
#if defined(CONFIG_DEVICE_PASSTHRU)
	    case msg_label_device_enable:
	    {
		ack_tid = tid;
		msg_device_enable_extract(&irq);
		tid.global.X.thread_no = irq;
		tid.global.X.version = 1;
		    
		if (1 || debug_hwirq || intlogic.is_irq_traced(irq)) 
		    con << "enable device irq: " << irq << '\n';
		
		errcode = AssociateInterrupt( tid, L4_Myself() );

		if ( errcode != L4_ErrOk )
		    con << "Attempt to associate an unavailable interrupt: "
			<< irq << ", L4 error: " 
			<< L4_ErrString(errcode) << ".\n";
		
		msg_device_done_build();
		break;
	    }
	    case msg_label_device_disable:
	    {
		ack_tid = tid;
		msg_device_disable_extract(&irq);
		tid.global.X.thread_no = irq;
		tid.global.X.version = 1;
		    
		if (1 || debug_hwirq || intlogic.is_irq_traced(irq)) 
		    con << "disable device irq: " << irq
			<< '\n';
		errcode = DeassociateInterrupt( tid );
		if ( errcode != L4_ErrOk )
		    con << "Attempt to deassociate an unavailable interrupt: "
			<< irq << ", L4 error: " 
			<< L4_ErrString(errcode) << ".\n";
		    
		msg_device_done_build();
		break;
	    }
#endif /* defined(CONFIG_DEVICE_PASSTHRU) */
	    case msg_label_preemption:
	    {
		ASSERT(tid == vcpu.monitor_gtid);
		L4_Set_MsgTag(L4_Niltag);
		ack_tid = tid;
		break;
	    }
	    case msg_label_preemption_yield:
	    {
		ASSERT(tid == vcpu.monitor_gtid);
		vcpu.monitor_info.mr_save.store_mrs(tag);
		L4_ThreadId_t dest = vcpu.monitor_info.mr_save.get_preempt_target();
		
		L4_ThreadId_t dest_irq_tid = L4_nilthread;
		
		/* Forward yield IPC to the  resourcemon's scheduler */
		for (word_t id=0; id < CONFIG_NR_VCPUS; id++)
		    if (id != vcpu.cpu_id && get_vcpu(id).is_vcpu_ktid(dest))
			dest_irq_tid = get_vcpu(id).irq_gtid;
		
		if (1 || debug_preemption)
		{
		    con << "monitor thread sent yield/lock IPC"
			<< " dest " << dest
			<< "\n";
		}
		
		ack_tid = vtimer_tid;
		vcpu.irq_info.mr_save.load_yield_msg(dest_irq_tid);
		vcpu.irq_info.mr_save.load_mrs();
		timeouts = vtimer_timeouts;
		break;
	    }
	    case msg_label_preemption_reply:
	    {
		ASSERT(tid == vtimer_tid);
		if (1 || debug_preemption)
		{
		    con << "vtimer thread donated time"
			<< " vtimer " << vtimer_tid
			<< " monitor tag " << (void *) vcpu.monitor_info.mr_save.get_msg_tag().raw
			<< (vcpu.monitor_info.mr_save.is_preemption_msg() ? " preempt " : " nonpreeempt ")
			<< "\n";
		}
		DEBUGGER_ENTER(0);
		vcpu.monitor_info.mr_save.load_yield_reply_msg();
		vcpu.monitor_info.mr_save.load_mrs();
		ack_tid = vcpu.monitor_gtid;
		break;
	    }
	    default:
		con << "unexpected IRQ message from " << tid << '\n';
		L4_KDB_Enter("BUG");
		break;
	}
	
    } /* for (;;) */
}

L4_ThreadId_t irq_init( L4_Word_t prio, 
	L4_ThreadId_t scheduler_tid, L4_ThreadId_t pager_tid,
	vcpu_t *vcpu )
{
    hthread_t *irq_thread =
	get_hthread_manager()->create_thread( 
	    (L4_Word_t)irq_stack[vcpu->cpu_id], sizeof(irq_stack),
	    prio, irq_handler_thread, scheduler_tid, pager_tid, vcpu);

    if ( !irq_thread )
	return L4_nilthread;
 
    vcpu->irq_info.mr_save.load_startup_reply(
	(L4_Word_t) irq_thread->start_ip, (L4_Word_t) irq_thread->start_sp);
    
    return irq_thread->get_local_tid();
}

