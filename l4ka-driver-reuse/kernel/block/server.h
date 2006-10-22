/*********************************************************************
 *                
 * Copyright (C) 2004 Joshua LeVasseur
 *
 * File path:	linuxblock/L4VMblock_server.h
 * Description:	Declarations for the Linux block driver server.
 *
 * Proprietary!  DO NOT DISTRIBUTE!
 *
 * $Id: server.h,v 1.1 2006/09/21 09:28:35 joshua Exp $
 *                
 ********************************************************************/
#ifndef __linuxblock__L4VMblock_server_h__
#define __linuxblock__L4VMblock_server_h__

#include <glue/thread.h>
#include <glue/bottomhalf.h>
#include <glue/vmirq.h>
#include <glue/vmmemory.h>
#include <glue/vmserver.h>
#include <glue/wedge.h>

#include "L4VMblock_idl_server.h"
#include "L4VMblock_idl_reply.h"
#include "block.h"

#define L4VMBLOCK_IRQ_BOTTOM_HALF_CMD	(1)
#define L4VMBLOCK_IRQ_TOP_HALF_CMD	(2)
#define L4VMBLOCK_IRQ_DISPATCH		(4)

#define L4VMBLOCK_MAX_DEVICES		(16)
#define L4VMBLOCK_MAX_CLIENTS		(8)

#define L4VMBLOCK_INVALID_HANDLE	(~0UL)

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#define L4VMBLOCK_DO_IRQ_DISPATCH
#endif

typedef union
{
    struct {
	IVMblock_devid_t devid;
    } probe;

    struct {
	IVMblock_handle_t client_handle;
	IVMblock_devid_t devid;
	unsigned rw;
    } attach;

    struct {
	IVMblock_handle_t handle;
    } detach;

    struct {
	IVMblock_handle_t handle;
    } reattach;

} L4VM_server_cmd_params_t;


typedef struct
{
    IVMblock_handle_t handle;
    IVMblock_client_shared_t *client_shared;
    L4VM_alligned_alloc_t client_alloc_info;

    L4VMblock_ring_t ring_info;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
    struct buffer_head bh_ring_storage[ IVMblock_descriptor_ring_size ];
    struct buffer_head *bh_ring[ IVMblock_descriptor_ring_size ];
#else
    struct bio *bio_ring[ IVMblock_descriptor_ring_size ];
#endif

    L4VM_client_space_info_t *client_space;
} L4VMblock_client_info_t;


typedef struct
{
    IVMblock_handle_t handle;
    L4VMblock_client_info_t *client;
    struct block_device *blkdev;
    L4_Word_t block_size;
    kdev_t kdev;
} L4VMblock_conn_info_t;


typedef struct L4VMblock_server
{
    L4_ThreadId_t server_tid;

    L4VM_irq_t irq;
    struct tq_struct bottom_half_task;
#if !defined(L4VMBLOCK_DO_IRQ_DISPATCH)
    struct tq_struct dispatch_task;
#endif

    IVMblock_server_shared_t *server_info;
    L4VM_alligned_alloc_t server_alloc_info;

    L4VM_server_cmd_ring_t top_half_cmds;
    L4VM_server_cmd_params_t top_half_params[L4VM_SERVER_CMD_RING_LEN];
    L4VM_server_cmd_ring_t bottom_half_cmds;
    L4VM_server_cmd_params_t bottom_half_params[L4VM_SERVER_CMD_RING_LEN];

    L4VMblock_conn_info_t connections[L4VMBLOCK_MAX_DEVICES];
    L4VMblock_client_info_t clients[L4VMBLOCK_MAX_CLIENTS];

    spinlock_t ring_lock;

} L4VMblock_server_t;


extern inline L4VMblock_conn_info_t * L4VMblock_conn_lookup( 
	L4VMblock_server_t *server, IVMblock_handle_t handle )
{
    if( (handle < L4VMBLOCK_MAX_DEVICES) && 
	    (server->connections[handle].handle == handle) )
	return &server->connections[handle];
    return NULL;
}

extern inline void L4VMblock_conn_release( L4VMblock_conn_info_t *conn )
{
    conn->handle = L4VMBLOCK_INVALID_HANDLE;
}

extern inline L4VMblock_client_info_t * L4VMblock_client_lookup(
	L4VMblock_server_t *server, IVMblock_handle_t handle )
{
    if( (handle < L4VMBLOCK_MAX_CLIENTS) && 
	    (server->clients[handle].handle == handle) )
	return &server->clients[handle];
    return NULL;
}

extern inline void L4VMblock_client_release( L4VMblock_client_info_t *client )
{
    client->handle = L4VMBLOCK_INVALID_HANDLE;
}

#endif	/* __linuxblock__L4VMblock_h__ */