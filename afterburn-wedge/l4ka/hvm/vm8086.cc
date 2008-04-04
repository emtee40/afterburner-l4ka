/*********************************************************************
 *                
 * Copyright (C) 2008,  Karlsruhe University
 *                
 * File path:     vm8086.cc
 * Description:   
 *                
 * @LICENSE@
 *                
 * $Id:$
 *                
 ********************************************************************/
#include <debug.h>
#include <console.h>
#include <l4/schedule.h>
#include <l4/ipc.h>
#include <l4/ipc.h>
#include <l4/schedule.h>
#include <l4/kip.h>
#include <l4/arch.h>
#include <device/portio.h>

#include INC_ARCH(ia32.h)
#include INC_ARCH(ops.h)
#include INC_WEDGE(vcpu.h)
#include INC_WEDGE(monitor.h)
#include INC_WEDGE(l4privileged.h)
#include INC_WEDGE(backend.h)
#include INC_WEDGE(vcpulocal.h)
#include INC_WEDGE(memory.h)
#include INC_WEDGE(l4thread.h)
#include INC_WEDGE(message.h)
#include INC_WEDGE(irq.h)
#include INC_ARCH(hvm_vmx.h)

extern bool handle_cr_fault();
extern bool handle_io_fault();
extern bool handle_hlt_fault();

typedef struct
{
    u16_t ip;
    u16_t cs;
} ia32_ive_t;

bool vm8086_sync_deliver_exception( exc_info_t exc, L4_Word_t eec)
{
    u16_t *stack;
    ia32_ive_t *int_vector;
    bool ext_int = (exc.hvm.type == hvm_vmx_int_t::ext_int);
    mr_save_t *vcpu_mrs = &get_vcpu().main_info.mr_save;

    if(!(get_cpu().interrupts_enabled()) && ext_int) 
    {
	printf( "ext int injection with disabled interrupts.\n");
	DEBUGGER_ENTER("VM8086 BUG");
    }
    
    dprintf(debug_hvm_vm8086, "hvm: vm8086 deliver exception %x (t %d vec %d eecv %c), eec %d, ilen %d\n", 
	    exc.raw, exc.hvm.type, exc.hvm.vector, exc.hvm.err_code_valid ? 'y' : 'n', 
	    eec, vcpu_mrs->exc_item.regs.entry_ilen);
    
    word_t eesp;
    bool mbt = backend_async_read_eaddr(L4_CTRLXFER_SSREGS_ID, vcpu_mrs->gpr_item.regs.esp, eesp);
    ASSERT(mbt);
    stack = (u16_t *) eesp;
   
    // push eflags, cs and ip onto the stack

    *(--stack) = vcpu_mrs->gpr_item.regs.eflags & 0xffff;
    *(--stack) = vcpu_mrs->seg_item[0].regs.segreg & 0xffff;

    if (ext_int)
	*(--stack) = vcpu_mrs->gpr_item.regs.eip & 0xffff;
    else
	*(--stack) = (vcpu_mrs->gpr_item.regs.eip + vcpu_mrs->hvm.ilen) & 0xffff;

    //printf("exc stack %x %x %x @%x\n", *(stack), *(stack+1), *(stack+2), stack);

    
    // get entry in interrupt vector table from guest
    int_vector = (ia32_ive_t*) ( exc.hvm.vector * 4 );

    //printf("exc ive %x %x @%x\n", int_vector->cs, int_vector->ip, int_vector);

    if( vcpu_mrs->gpr_item.regs.esp-6 < 0 )
	DEBUGGER_ENTER("stackpointer below segment");

    vcpu_mrs->gpr_item.regs.eip = int_vector->ip;
    vcpu_mrs->gpr_item.regs.esp -= 6;
    vcpu_mrs->gpr_item.regs.eflags &= ~X86_FLAGS_IF;

    vcpu_mrs->append_gpr_item();
    vcpu_mrs->append_seg_item(L4_CTRLXFER_CSREGS_ID, int_vector->cs, (int_vector->cs << 4 ), 
			      vcpu_mrs->seg_item[0].regs.limit, vcpu_mrs->seg_item[0].regs.attr);
    
    //vcpu_mrs->dump(debug_id_t(0,0));
    //DEBUGGER_ENTER("UNTESTED");
    
    // make exc_item valid (but exc itself invalid, to prevent other exceptions
    // to be appended 
    exc.hvm.valid = 0;
    vcpu_mrs->append_exc_item(exc.raw, eec, vcpu_mrs->hvm.ilen);

    return true;
}



extern bool handle_vm8086_gp(exc_info_t exc, word_t eec, word_t cr2)
{
    word_t data_size		= 16;
    word_t addr_size		= 16;
    u8_t *linear_ip, modrm;
    word_t operand_addr		= -1UL;
    word_t operand_data		= 0UL;
    bool rep			= false;
    bool seg_ovr		= false;
    word_t seg_id		= 0;
    word_t ereg			= 0;

    vcpu_t &vcpu = get_vcpu();
    mr_save_t *vcpu_mrs = &vcpu.main_info.mr_save;
    hvm_vmx_ei_qual_t qual;

   
    bool mbt = backend_async_read_eaddr(L4_CTRLXFER_CSREGS_ID, vcpu_mrs->gpr_item.regs.eip, ereg);
    ASSERT(mbt);
    
    //dprintf(debug_hvm_vm8086, 
    //  "hvm: vm8086 exc %x (type %d vec %d eecv %c), eec %d ip %x ilen %d\n", 
    //  exc.raw, exc.hvm.type, exc.hvm.vector, exc.hvm.err_code_valid ? 'y' : 'n', 
    //    vcpu_mrs->exc_item.regs.idt_eec, ereg, vcpu_mrs->hvm.ilen);
    
    linear_ip = (u8_t *) ereg;
    
    // Read the faulting instruction.

    // Strip prefixes.
    while (*linear_ip == 0x26 
	   || *linear_ip == 0x66 
	   || *linear_ip == 0x67 
	   || *linear_ip == 0xf3
	   || *linear_ip == 0xf2)
    {
	switch (*(linear_ip++))
	{
	case 0x26:
	{
	    printf("hvm: rep vm8086 exc %x (type %d vec %d eecv %c), eec %d ip %x ilen %d\n", 
		   exc.raw, exc.hvm.type, exc.hvm.vector, exc.hvm.err_code_valid ? 'y' : 'n', 
		   vcpu_mrs->exc_item.regs.idt_eec, ereg, vcpu_mrs->hvm.ilen);
	    DEBUGGER_ENTER("UNTESTED SEGOVR");
	}
	seg_id = L4_CTRLXFER_ESREGS_ID;//vmcs->gs.es_base;
	seg_ovr = true;
	break;
	case 0x66:
	    data_size = 32;
	    break;
	case 0x67:
	    addr_size = 32;
	    break;
	case 0xf2:
	case 0xf3:
	    rep = true;
	    break;
	}
    }
    
    // Decode instruction.
    switch (*linear_ip)
    {
    case 0x0f:				// mov, etc.
	switch (*(linear_ip + 1))
	{
	case 0x00:
	    dprintf(debug_hvm_vm8086, "hvm: vm8086 lldt\n");
	    DEBUGGER_ENTER("UNTESTED");
	    return false;
	    break;

	case 0x01:			// lgdt/lidt/lmsw.
	    dprintf(debug_hvm_vm8086, "hvm: vm8086 lgdt/lidt/lmsw\n");
	    modrm = *(linear_ip + 2);

	    switch (modrm & 0xc0)
	    {
	    case 0x00:
		if (addr_size == 32)
		{
		    switch (modrm & 0x7)
		    {
		    case 0x0:
			operand_addr = vcpu_mrs->gpr_item.regs.eax;
			break;
		    case 0x1:
			operand_addr = vcpu_mrs->gpr_item.regs.ecx;
			break;
		    case 0x2:
			operand_addr = vcpu_mrs->gpr_item.regs.edx;
			break;
		    case 0x3:
			operand_addr = vcpu_mrs->gpr_item.regs.ebx;
			break;
		    case 0x6:
			operand_addr = vcpu_mrs->gpr_item.regs.esi;
			break;
		    case 0x7:
			operand_addr = vcpu_mrs->gpr_item.regs.edi;
			break;
		    case 0x5:
			operand_addr = *((u32_t *) (linear_ip + 3));
			break;
		    default:
			// Other operands not supported yet.
			return false;
		    }
		}
		else
		{
		    switch (modrm & 0x7)
		    {
		    case 0x4:
			operand_addr = vcpu_mrs->gpr_item.regs.esi;
			break;
		    case 0x5:
			operand_addr = vcpu_mrs->gpr_item.regs.edi;
			break;
		    case 0x7:
			operand_addr = vcpu_mrs->gpr_item.regs.ebx;
			break;
		    case 0x6:
			operand_addr = *((u16_t *) (linear_ip + 3));
			break;
		    default:
			// Other operands not supported yet.
			return false;
		    }

		    operand_addr &= 0xffff;
		    //operand_addr += vmcs->gs.ds_sel << 4;
		    L4_KDB_Enter("rewrite");
		}
		break;

	    case 0xc0:
	    {
		switch (modrm & 0x7)
		{
		case 0x0:
		    operand_data = vcpu_mrs->gpr_item.regs.eax;
		    break;
		case 0x1:
		    operand_data = vcpu_mrs->gpr_item.regs.ecx;
		    break;
		case 0x2:
		    operand_data = vcpu_mrs->gpr_item.regs.edx;
		    break;
		case 0x3:
		    operand_data = vcpu_mrs->gpr_item.regs.ebx;
		    break;
		case 0x4:
		    operand_data = vcpu_mrs->gpr_item.regs.esp;
		    break;
		case 0x5:
		    operand_data = vcpu_mrs->gpr_item.regs.ebp;
		    break;
		case 0x6:
		    operand_data = vcpu_mrs->gpr_item.regs.esi;
		    break;
		case 0x7:
		    operand_data = vcpu_mrs->gpr_item.regs.edi;
		    break;
		}
	    }
	    break;

	    default:
		// Other operands not supported yet.
		return false;
	    }

	    switch (modrm & 0x38)
	    {
		printf("modrm & 0x38 unimplemented\n");
#if 0
	    case 0x10:			// lgdt.
		vmcs->gs.gdtr_lim	= *((u16_t *) operand_addr);
		operand_data		= *((u32_t *) (operand_addr + 2));
		if (data_size < 32)
		    operand_data &= 0x00ffffff;
		vmcs->gs.gdtr_base	= operand_data;
		break;

	    case 0x18:			// lidt.
		vmcs->gs.idtr_lim	= *((u16_t *) operand_addr);
		operand_data		= *((u32_t *) (operand_addr + 2));
		if (data_size < 32)
		    operand_data &= 0x00ffffff;
		vmcs->gs.idtr_base	= operand_data;
		break;

	    case 0x30:			// lmsw.
		if (operand_addr != -1UL)
		    operand_data = *((u16_t *) operand_addr);

		operand.raw		= 0;
		operand.X.type		= virt_msg_operand_t::o_lmsw;
		operand.imm_value	= operand_data & 0xffff;

		msg_handler->send_register_fault
		    (virt_vcpu_t::r_cr0, true, &operand);
		return true;
#endif
	    default:
		return false;
	    }

	    DEBUGGER_ENTER("UNIMPLEMENTED");
	    //vmcs->gs.rip = guest_ip + vmcs->exitinfo.instr_len;
	    return true;

	case OP_MOV_FROMCREG:		// mov cr, x.
	case OP_MOV_TOCREG:		// mov x, cr.
	    dprintf(debug_hvm_vm8086, "hvm: mov from/to CR\n");
	    
	    modrm = *(linear_ip + 2);

	    if (modrm & 0xc0 != 0xc0)
	    {
		printf("unimplemented modrm\n");
		DEBUGGER_ENTER("UNIMPLEMENTED");
		return false;
	    }
	    qual.raw = 0;
	    qual.mov_cr.access_type = (*linear_ip == OP_MOV_FROMCREG) ?
		hvm_vmx_ei_qual_t::from_cr : hvm_vmx_ei_qual_t::to_cr;
	    qual.mov_cr.cr_num = ((modrm >> 3) & 0x7);
	    qual.mov_cr.mov_cr_gpr = (hvm_vmx_ei_qual_t::gpr_e) (modrm & 0x7);
	    vcpu_mrs->hvm.qual = qual.raw;
	    return handle_cr_fault();
	    
	case OP_MOV_FROMDREG:
	case OP_MOV_TODREG:
	    printf("mov to/from dreg in real mode\n");
	    DEBUGGER_ENTER("UNIMPLEMENTED");
	    return false;
	}

	return false;
	
    case 0x6c:				// insb	 dx, mem	
    case 0x6e:				// outsb  dx,mem      
	data_size = 8;
	// fall through
    case 0x6d:				// insw	dx, mem
    case 0x6f:				// outsw dx, mem
	qual.raw = 0;
	qual.io.rep = rep;
	qual.io.string = true;
	qual.io.port_num = vcpu_mrs->gpr_item.regs.edx & 0xffff; 
	qual.io.soa = (hvm_vmx_ei_qual_t::soa_e) ((data_size / 8)-1);
	
	if (*linear_ip >= 0x6e)
	{
	    // Write
	    qual.io.dir = hvm_vmx_ei_qual_t::out;
	    
	    if (!seg_ovr) seg_id = L4_CTRLXFER_DSREGS_ID;
	    ereg = vcpu_mrs->gpr_item.regs.esi & 0xffff;
	}
	else
	{
	    // Read
	    qual.io.dir = hvm_vmx_ei_qual_t::in;
	    
	    if (!seg_ovr) seg_id = L4_CTRLXFER_ESREGS_ID;
	    ereg = vcpu_mrs->gpr_item.regs.edi & 0xffff;
	}
	
	backend_async_read_eaddr(seg_id, ereg, (word_t &)vcpu_mrs->hvm.ai_info, true);
	printf("hvm: rep io vm8086 exc %x (type %d vec %d eecv %c), eec %d ip %x ilen %d info %x\n", 
		   exc.raw, exc.hvm.type, exc.hvm.vector, exc.hvm.err_code_valid ? 'y' : 'n', 
	       vcpu_mrs->exc_item.regs.idt_eec, ereg, vcpu_mrs->hvm.ilen, vcpu_mrs->hvm.ai_info);
	DEBUGGER_ENTER("UNTESTED REP");

	vcpu_mrs->hvm.qual = qual.raw;
	return handle_io_fault();
	
#if 0
    case 0xcc:				// int 3.
    case 0xcd:				// int n.
	return vm8086_interrupt_emulation( *linear_ip == 0xcc ? 3 : *(linear_ip + 1), false);
#endif

    case 0xe4:				// inb n.
    case 0xe6:				// outb n.
	data_size = 8;
	// fall through
    case 0xe5:				// in n.
    case 0xe7:				// out n.
	qual.raw = 0;
	qual.io.rep = rep;
	qual.io.port_num =		*(linear_ip + 1);
	qual.io.soa = (hvm_vmx_ei_qual_t::soa_e) ((data_size / 8)-1);
	qual.io.dir = (*linear_ip >= 0xe6) ? hvm_vmx_ei_qual_t::out :
	    hvm_vmx_ei_qual_t::in;
	vcpu_mrs->hvm.qual = qual.raw;
	return handle_io_fault();
    case 0xec:				// inb dx.
    case 0xee:				// outb dx.
	data_size = 8;
	// fall through
    case 0xed:				// in dx.
    case 0xef:				// out dx.
	qual.raw = 0;
	qual.io.rep = rep;
	qual.io.port_num = vcpu_mrs->gpr_item.regs.edx & 0xffff;
	qual.io.soa = (hvm_vmx_ei_qual_t::soa_e) ((data_size / 8)-1);
	qual.io.dir = (*linear_ip >= 0xee) ? hvm_vmx_ei_qual_t::out :
	    hvm_vmx_ei_qual_t::in;
	vcpu_mrs->hvm.qual = qual.raw;
	return handle_io_fault();
    case 0xf4:				// hlt
	return handle_hlt_fault();
	
    }
    
    dprintf(debug_hvm_vm8086, "hvm: vm8086 forward exc to guest\n");
    return backend_sync_deliver_exception(exc, eec);
}

