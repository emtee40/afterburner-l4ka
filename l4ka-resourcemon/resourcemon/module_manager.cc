/*********************************************************************
 *
 * Copyright (C) 2003-2004,  Karlsruhe University
 *
 * File path:     resourcemon/module_manager.cc
 * Description:   Walks the list of modules loaded by the boot loader,
 * 		  and helps to create virtual machines for each module
 * 		  as necessary.
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

#include <l4/types.h>
#include <l4/kdebug.h>

#include <resourcemon/resourcemon.h>
#include <resourcemon/vm.h>
#include <resourcemon/module_manager.h>
#include <common/console.h>
#include <common/string.h>

module_manager_t module_manager;

bool module_manager_t::init()
{
    if( this->l4bootinfo.init() )
	this->vm_modules = &this->l4bootinfo;
#if defined(__i386__)
    if( !this->vm_modules && this->mbi.init() )
	this->vm_modules = &this->mbi;
#endif
    if( !this->vm_modules )
	return false;

    if( this->vm_modules->get_total() == 0 )
	return false;

    this->dump_modules_list();

    return true;
}

void module_manager_t::dump_modules_list()
{
    L4_Word_t mod_index;

    hout << "Boot modules:\n";

    // Search for the resourcemon module.
    for( mod_index = 0; mod_index < this->vm_modules->get_total(); mod_index++ )
    {
	L4_Word_t haddr_start, size;
	const char *cmdline;

	vm_modules->get_module_info( mod_index, cmdline, haddr_start, size );
	hout << "Module " << mod_index << " at " << (void *)haddr_start;
	hout << ", size " << (void *)size << ",\n  command line: " << cmdline;
	hout << '\n';
    }
}

bool module_manager_t::next_module()
{
    const char *cmdline;
    L4_Word_t haddr_start, size;
    bool vm_is_multi_module;

    if( this->current_module >= vm_modules->get_total() )
	return false;

    // Should we clone the current module?
    vm_modules->get_module_info( this->current_module, cmdline, haddr_start,
	    size );
    L4_Word_t max_clones = get_module_param_size( "clones=", cmdline );
    if( this->module_clones < max_clones )
    {
	this->module_clones++;
	hout << "Clone " << this->module_clones << " of " << max_clones << ".\n";
	return true;
    }

    // Search for the next valid module.
    vm_is_multi_module = cmdline_has_vmstart(cmdline);
    while( (this->current_module+1) < vm_modules->get_total() )
    {
	this->current_module++;
	vm_modules->get_module_info( this->current_module, cmdline, haddr_start,
		size );
	if( cmdline_has_vmstart(cmdline) )
	    return true;
	else if( vm_is_multi_module )
	    continue;  // The start of a VM definition has vmstart on the cmd
	if( is_valid_binary(haddr_start) )
	    return true;
    }

    return false;
}

bool module_manager_t::load_current_module()
{
    L4_Word_t haddr_start, size, vcpus;
    L4_Word_t rd_index, rd_haddr_start, rd_size;
    const char *cmdline, *rd_cmdline;
    bool rd_valid, vm_is_multi_module;
    vm_t *vm;

    vm_modules->get_module_info( this->current_module, cmdline, haddr_start, 
	    size );
    hout << "Loading module: " << cmdline << "\n";

    vm_is_multi_module = cmdline_has_vmstart(cmdline);
    if( !vm_is_multi_module )
    {
	// Is the next module the ramdisk which belongs to this module?  We
	// assume that if it isn't an elf binary, then it is a valid ramdisk.
	rd_index = this->current_module + 1;
	rd_valid = false;
	if( rd_index < vm_modules->get_total() )
	{
	    vm_modules->get_module_info( rd_index, rd_cmdline, rd_haddr_start,
	    	    rd_size );
	    if( !is_valid_binary(rd_haddr_start) )
	    {
		this->current_ramdisk_module = rd_index;
		this->ramdisk_valid = true;
		rd_valid = true;
	    }
	}
	// If the next module isn't a ramdisk, but the VM needs a ramdisk and
	// one of the prior modules is a ramdisk, then use the prior ramdisk.
	if( !rd_valid && this->ramdisk_valid && cmdline_has_ramdisk(cmdline) )
	{
	    rd_valid = true;
	    rd_index = this->current_ramdisk_module;
	}
    }

    L4_Word_t vaddr_offset = get_module_param_size( "offset=", cmdline );
    L4_Word_t vm_size = get_module_param_size( "vmsize=", cmdline );
    if( vm_size == 0 )
	vm_size = get_module_memsize(cmdline);
    if( vm_size == 0 )
	vm_size = TASK_LEN;

    L4_Word_t wedge_size = get_module_param_size( "wedgesize=", cmdline );
    L4_Word_t wedge_paddr = get_module_param_size( "wedgeinstall=", cmdline );

    if( wedge_paddr + wedge_size > vm_size ) {
	hout << "The wedge doesn't fit within the virtual machine.\n";
	goto err_leave;
    }

    vm = get_vm_allocator()->allocate_vm();
    if( vm == NULL )
    {
	hout << "Unable to allocate a new virtual machine.\n";
	goto err_leave;
    }

    if( !vm->init_mm(vm_size, vaddr_offset, true, wedge_size, wedge_paddr) )
    {
	hout << "Unable to allocate memory for the virtual machine.\n";
	goto err_out;
    }

    if( !vm->install_elf_binary(haddr_start) )
    {
	hout << "Unable to install the virtual machine's elf binary.\n";
	goto err_out;
    }

    if( cmdline_has_ddos(cmdline) )
    {
	vm->enable_device_access();
	if( cmdline_disables_client_dma(cmdline) )
	    vm->disable_client_dma_access();
    }

    vcpus = get_module_param_size( "vcpus=", cmdline );
    if (vcpus)
	vm->set_vcpu_count(vcpus);

    if( !vm->init_client_shared(cmdline_options(cmdline)) )
    {
	hout << "Unable to configure the virtual machine.\n";
	goto err_out;
    }

    if( vm_is_multi_module )
    {
	const char *mod_cmdline;
	L4_Word_t mod_haddr_start, mod_size;
	L4_Word_t ceiling = vm->get_space_size();
	L4_Word_t mod_idx = this->current_module + 1;
	while( mod_idx < vm_modules->get_total() )
	{
	    vm_modules->get_module_info( mod_idx, mod_cmdline, 
		    mod_haddr_start, mod_size );
	    if( cmdline_has_vmstart(mod_cmdline) )
		break;	// We found the next VM definition.
	    if( !vm->install_module(ceiling, mod_haddr_start, mod_haddr_start+mod_size, mod_cmdline) ) {
		hout << "Unable to install the modules.\n";
		goto err_out;
	    }
	    ceiling -= mod_size;
	    if( ceiling % PAGE_SIZE )
		ceiling &= PAGE_MASK;
	    mod_idx++;
	}
    }
    else if( rd_valid &&
	    !vm->install_ramdisk(rd_haddr_start, rd_haddr_start + rd_size) )
    {
	hout << "Unable to install the ramdisk.\n";
	goto err_out;
    }

    if( !vm->start_vm() )
    {
	hout << "Unable to start the virtual machine.\n";
	goto err_out;
    }

    return true;

err_out:
    get_vm_allocator()->deallocate_vm( vm );
err_leave:
    L4_KDB_Enter( "vm start error" );
    return false;
}

const char *module_manager_t::cmdline_options( const char *cmdline )
    // This function accepts a raw GRUB module command line, and removes
    // the module name from the parameters, and returns the parameters.
{
    // Skip the module name.
    while( *cmdline && !isspace(*cmdline) )
	cmdline++;
    // Skip white space.
    while( *cmdline && isspace(*cmdline) )
	cmdline++;

    return cmdline;
}

bool module_manager_t::cmdline_has_substring( 
	const char *cmdline, const char *substr )
    // Returns true if the raw GRUB module command line has the substring
    // specified by the substr parameter.
{
    cmdline = cmdline_options( cmdline );
    if( !*cmdline )
	return false;

    return strstr(cmdline, substr) != NULL;
}

L4_Word_t module_manager_t::parse_size_option( 
	const char *name, const char *option )
    // This function extracts the size specified by a command line parameter
    // of the form "name=val[K|M|G]".  The name parameter is the "name=" 
    // string.  The option parameter contains the entire "name=val"
    // string.  The terminating characters, K|M|G, will multiply the value
    // by a kilobyte, a megabyte, or a gigabyte, respectively.  Zero
    // is returned upon any type of error.
{
    char *next;

    L4_Word_t val = strtoul(option+strlen(name), &next, 0);

    // Look for size qualifiers.
    if( *next == 'K' ) val = KB(val);
    else if( *next == 'M' ) val = MB(val);
    else if( *next == 'G' ) val = GB(val);
    else
	next--;

    // Make sure that a white space follows.
    next++;
    if( *next && !isspace(*next) )
	return 0;

    return val;
}

L4_Word_t module_manager_t::get_module_memsize( const char *cmdline )
    // This function scans for a Linux-style mem= command line parameter.
    // This command line parameter can have a variety of values, defined
    // in Linux/Documentation/kernel-parameters.txt.  If the parameter is
    // found, it's value is returned.  Otherwise we return the 0.
{
    L4_Word_t mem = 0;

    cmdline = cmdline_options( cmdline );
    if( !*cmdline )
	return mem;

    // We can have multiple mem= parameters, which can even specify ranges.  
    // Keep going until we get the correct type (i.e. no ranges).
    while( 1 )
    {
	char *mem_str = strstr( cmdline, "mem=" );
	if( !mem_str )
	    break;

	L4_Word_t val = parse_size_option( "mem=", mem_str );
	if( val )
	    return val;

	cmdline = mem_str + strlen("mem=");
    }

    return mem;
}

L4_Word_t module_manager_t::get_module_param_size( const char *token, const char *cmdline )
    // Thus function scans for a variable on the command line which
    // uses memory-style size descriptors.  For example: offset=10G
    // The token parameter specifices the parameter name on the command
    // line, and must include the = or whatever character separates the 
    // parameter name from the value.
{
    L4_Word_t size = 0;

    cmdline = cmdline_options( cmdline );
    if( !*cmdline )
	return size;

    char *token_str = strstr( cmdline, token );
    if( NULL != token_str )
	size = parse_size_option( token, token_str );

    return size;
}
