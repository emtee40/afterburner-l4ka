/*********************************************************************
 *
 * Copyright (C) 2005, University of Karlsruhe
 *
 * File path:     afterburner-wedge/l4ka/l4privileged.cc
 * Description:   Wrappers for the privileged L4 system calls.  Specific
 *                to the research resource monitor.
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
 ********************************************************************/

#include <l4/schedule.h>
#include INC_WEDGE(l4privileged.h)
#include INC_WEDGE(resourcemon.h)
#include INC_WEDGE(monitor.h)

const char *L4_ErrString( L4_Error_t err )
{
    static const char *msgs[] = {
	"No error", "Unprivileged", "Invalid thread",
	"Invalid space", "Invalid scheduler", "Invalid param",
	"Error in UTCB area", "Error in KIP area", "Out of memory",
	0 };

    if( (err >= L4_ErrOk) && (err <= L4_ErrNoMem) )
	return msgs[err];
    else
	return "Unknown error";
}

INLINE L4_ThreadId_t get_thread_server_tid()
{
    return resourcemon_shared.cpu[L4_ProcessorNo()].thread_server_tid;
}

L4_Error_t ThreadControl( 
	L4_ThreadId_t dest, L4_ThreadId_t space,
	L4_ThreadId_t sched, L4_ThreadId_t pager, L4_Word_t utcb, L4_Word_t prio)
{
    CORBA_Environment ipc_env = idl4_default_environment;
    L4_Error_t result;
    
    ASSERT(dest != L4_nilthread);
    IResourcemon_ThreadControl( 
	    get_thread_server_tid(),
	    &dest, &space, &sched, &pager, utcb, prio, &ipc_env );

    if( ipc_env._major != CORBA_NO_EXCEPTION )
    {
	result = CORBA_exception_id(&ipc_env) - ex_IResourcemon_ErrOk;
	CORBA_exception_free( &ipc_env );
	return result;
    }
    else
	return L4_ErrOk;
}

L4_Error_t SpaceControl( L4_ThreadId_t dest, L4_Word_t control, 
	L4_Fpage_t kip, L4_Fpage_t utcb, L4_ThreadId_t redir )
{
    CORBA_Environment ipc_env = idl4_default_environment;
    L4_Error_t result;

    IResourcemon_SpaceControl( 
	    get_thread_server_tid(),
	    &dest, control, kip.raw, utcb.raw, &redir, &ipc_env );

    if( ipc_env._major != CORBA_NO_EXCEPTION )
    {
	result = CORBA_exception_id(&ipc_env) - ex_IResourcemon_ErrOk;
	CORBA_exception_free( &ipc_env );
	return result;
    }
    else
	return L4_ErrOk;
}

L4_Error_t AssociateInterrupt( L4_ThreadId_t irq_tid, L4_ThreadId_t handler_tid )
{
    CORBA_Environment ipc_env = idl4_default_environment;
    L4_Error_t result;
    
    IResourcemon_AssociateInterrupt(
	    get_thread_server_tid(), &irq_tid, &handler_tid, &ipc_env );

    if( ipc_env._major != CORBA_NO_EXCEPTION ) {
	result = CORBA_exception_id(&ipc_env) - ex_IResourcemon_ErrOk;
	CORBA_exception_free( &ipc_env );
	return result;
    }
    else
	return L4_ErrOk;
}

L4_Error_t DeassociateInterrupt( L4_ThreadId_t irq_tid )
{
    CORBA_Environment ipc_env = idl4_default_environment;
    L4_Error_t result;

    IResourcemon_DeassociateInterrupt(
	    get_thread_server_tid(), &irq_tid, &ipc_env );

    if( ipc_env._major != CORBA_NO_EXCEPTION ) {
	result = CORBA_exception_id(&ipc_env) - ex_IResourcemon_ErrOk;
	CORBA_exception_free( &ipc_env );
	return result;
    }
    else
	return L4_ErrOk;
}


void ThreadSwitch(L4_ThreadId_t dest, cpu_lock_t *lock)
{
#if defined(CONFIG_L4KA_VMEXTENSIONS)
    L4_ThreadId_t dest_monitor_tid = get_monitor_tid(dest);
    if (dest_monitor_tid == L4_Myself())
    {
	L4_MsgTag_t tag;
	extern vcpu_t vcpu;
	LOCK_ASSERT(vcpu.is_valid_vcpu(), '7', lock->name());
	LOCK_ASSERT(dest == vcpu.main_gtid, '8', lock->name());
	if (!vcpu.main_info.mr_save.is_preemption_msg())
	{
	    tag = L4_Receive(vcpu.main_gtid, L4_ZeroTime);
	    LOCK_ASSERT(!L4_IpcFailed(tag), '9', lock->name());
	    vcpu.main_info.mr_save.store_mrs(tag);
	    LOCK_ASSERT(vcpu.main_info.mr_save.is_preemption_msg(), 'a', lock->name());
	}
	bit_set_atomic(0, cpu_lock_t::delayed_preemption); 
	vcpu.main_info.mr_save.load();
	tag = L4_Call(vcpu.main_gtid);
	vcpu.main_info.mr_save.store_mrs(tag);
	LOCK_ASSERT(vcpu.main_info.mr_save.is_preemption_msg(), 'b', lock->name());
    }
    else 
#endif
	L4_ThreadSwitch(dest);
}
 
