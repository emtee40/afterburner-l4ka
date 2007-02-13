/*********************************************************************
 *                
 * Copyright (C) 2005-2007,  Karlsruhe University
 *                
 * File path:     earm.h
 * Description:   
 *                
 * @LICENSE@
 *                
 * $Id:$
 *                
 ********************************************************************/
#ifndef __EARM_H__
#define __EARM_H__

#include <common/hthread.h>
#include "earm_idl_server.h"
typedef IEarm_energy_t energy_t;

#define EARM_ACC_DEBUG
#undef  EARM_ACC_DEBUG_EVAL
#define EARM_ACC_DEBUG_MSEC                    1000
#define EARM_ACC_DEBUG_CPU
#define EARM_ACC_DEBUG_DISK
#define EARM_ACC_DEBUG_MIN_DOMAIN                  0
#define EARM_ACC_DEBUG_MIN_RESOURCE		   0


#define EARM_ACC_MIN_DOMAIN                        0
#define EARM_ACCCPU_MSEC			  20
#define EARM_EASCPU_MSEC			 100
#define EARM_EASDISK_MSEC			 200

#define THROTTLE_DISK    // Disk throttling
#define THROTTLE_CPU    // CPU throttling

#define MIN_CPU_DOMAIN             3
#define DELTA_CPU_POWER         1000
#define INIT_CPU_POWER        100000

#define MIN_DISK_DOMAIN           4
#define DELTA_DISK_POWER         50
#define INIT_DISK_POWER        100000
#define INIT_DISK_THROTTLE     (6000)

#define DTF	4 // Disk throttle factor

#define EARM_ACC_UNLIMITED (~0ULL)

static const bool debug_earmdisk = 0;
extern L4_Word64_t debug_pmc[8];

extern void earm_init();
extern void earm_accmanager_init();
extern void earm_acccpu_init();
extern void earm_easmanager_init();

extern hthread_t *earm_accmanager_thread;


extern void earm_acccpu_collect();
extern void earm_acccpu_register( L4_ThreadId_t tid, L4_Word_t uuid_cpu, IEarm_shared_t **shared);

extern L4_Word_t max_uuid_cpu;

/* Shared resource accounting data */
typedef struct {
    L4_ThreadId_t tid;
    IEarm_shared_t *shared;
    //struct list_head list;
} resource_t;
extern resource_t resources[UUID_IEarm_AccResMax];

typedef struct earm_set
{
    IEarm_shared_t res[UUID_IEarm_AccResMax];
} earm_set_t;



/* *********** Logging */
#include <resourcemon/logging.h>


#define L4_RESOURCE_SELECTOR(cpu, domain, resource)             \
    (l4_resourceselector_base[cpu] + (domain * L4_MAX_RESOURCES) + resource)

extern log_event_control_t *l4_resourceselector_base[L4_LOGGING_MAX_CPUS];
extern logfile_control_t *l4_logfile_base[L4_LOGGING_MAX_CPUS];


/* *********** PMCs */

#include <common/ia32/msr.h>

// Some additional MSRs not found in msr.h
#define MSR_IA32_PEBS_ENABLE                    0x3f1
#define MSR_IA32_PEBS_MATRIX_VERT               0x3f2

#define CONFIG_X_EARM_PENTIUM_D
#undef CONFIG_X_EARM_PENTIUM_4

#if defined(CONFIG_X_EARM_PENTIUM_4)
#define EARM_PMC_TSC_WEIGHT		    (617)	    
#define EARM_PMC_UC_WEIGHT		    (712)
#define EARM_PMC_MQW_WEIGHT		    (475)
#define EARM_PMC_RB_WEIGHT		     (56)
#define EARM_PMC_MB_WEIGHT		  (34046)
#define EARM_PMC_MR_WEIGHT		    (173)		  
#define EARM_PMC_MLR_WEIGHT		   (2996)
#define EARM_PMC_LDM_WEIGHT		   (1355)
#elif defined(CONFIG_X_EARM_PENTIUM_D)

/* 
 * jsXXX: recalibrated TSC and UC:
 *  - ran only hypervisor, without VMs
 *  - made the throttler thread either execute "hlt" or real cycles
 *  - obtained the performance counter events, they differed in TSC/UC mostly
 *  - obtained real CPU power via labview, both 
 *  - results (10 sec runtime): 
 *    HLT   : 29999 tsc +   190 uc  ( + 3 rb + 13 mr ) = 43309
 *    NOHLT : 29981 tsc + 29981 uc  ( + 4 rb + 13 mr ) = 85934
 */
#define EARM_PMC_TSC_WEIGHT		   (1435) //recalibrated, old was (1270)   	  
#define EARM_PMC_UC_WEIGHT		   (1432) //recalibrated, old was (1660)
#define EARM_PMC_MQW_WEIGHT		   (0)
#define EARM_PMC_RB_WEIGHT		   (1007) 
#define EARM_PMC_MB_WEIGHT		   (0)
#define EARM_PMC_MR_WEIGHT		   (598)    	 
#define EARM_PMC_MLR_WEIGHT		   (0)
#define EARM_PMC_LDM_WEIGHT		   (19200)
#else
#error please define EARM PMC weights
#endif

static const L4_Word64_t pmc_weight[8] = { EARM_PMC_TSC_WEIGHT, EARM_PMC_UC_WEIGHT,  EARM_PMC_MQW_WEIGHT, EARM_PMC_RB_WEIGHT, 
					   EARM_PMC_MB_WEIGHT,  EARM_PMC_MR_WEIGHT,  EARM_PMC_MLR_WEIGHT, EARM_PMC_LDM_WEIGHT };
#endif /* !__EARM_H__ */