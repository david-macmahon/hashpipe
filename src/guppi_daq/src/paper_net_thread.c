/* vegas_net_thread.c
 *
 * Routine to read packets from network and put them
 * into shared memory blocks.
 */

#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>

#include <xgpu.h>

#include "fitshead.h"
#include "guppi_params.h"
#include "guppi_error.h"
#include "guppi_status.h"
#include "paper_databuf.h"
#include "guppi_udp.h"
#include "guppi_time.h"

#define STATUS_KEY "NETSTAT"  /* Define before guppi_threads.h */
#include "guppi_threads.h"
#include "guppi_defines.h"
#include "paper_thread.h"

//#define TIMING_TEST
#define DEBUG_NET

typedef struct {
    uint64_t count;
    int      fid;	// Fengine ID
    int      cid;	// channel set ID
} packet_header_t;

typedef struct {
    int initialized;
    uint64_t mcnt_start;
    uint64_t mcnt_offset;
    int block_i;
    int sub_block_i;
    int block_active[N_INPUT_BLOCKS];
} block_info_t;

#ifdef TIMING_TEST
static unsigned long fluffed_words = 0;
#endif

void print_pkt_header(packet_header_t * pkt_header) {

    printf("packet header : count %llu fid %d cid %d\n", (long long unsigned)pkt_header->count, pkt_header->fid, pkt_header->cid);
}

void print_block_info(block_info_t * binfo) {
    printf("binfo : mcnt_start %llu mcnt_offset %llu block_i %d sub_block_i %d\n", 
           (long long unsigned)binfo->mcnt_start, (long long unsigned)binfo->mcnt_offset, binfo->block_i, binfo->sub_block_i);
}

void print_ring_mcnts(paper_input_databuf_t *paper_input_databuf_p) {

    int i, j;

    for(i=0; i < N_INPUT_BLOCKS; i++) {
	for(j=0; j < N_SUB_BLOCKS_PER_INPUT_BLOCK; j++) {
		printf("block %d sub_block %d mcnt %lu\n", i, j, paper_input_databuf_p->block[i].header.mcnt[j]);
	}
    }
}

inline int inc_block_i(block_i) {
    return((block_i + 1) % N_INPUT_BLOCKS);
}

inline int dec_block_i(block_i) {
    return(block_i == 0 ? N_INPUT_BLOCKS - 1 : block_i - 1);
}

void get_header (struct guppi_udp_packet *p, packet_header_t * pkt_header) {

    uint64_t raw_header;
    raw_header = guppi_udp_packet_mcnt(p);
    pkt_header->count       = raw_header >> 16;
    pkt_header->cid         = raw_header        & 0x000000000000000F;
    pkt_header->fid         = (raw_header >> 8) & 0x00000000000000FF;

#ifdef TIMING_TEST
    static int fake_mcnt=0;
    static int fake_fid=0;
    static int pkt_counter=0;

    if(pkt_counter == 8) {
	fake_mcnt++;
        fake_fid = 0;
	pkt_counter=0;
    } else if(pkt_counter % 8 == 0) {
	fake_fid += 4;
    }
    pkt_header->count = fake_mcnt;
    pkt_counter++;
    //printf("%d %d\n", pkt_counter, fake_mcnt);
#endif
}

void set_blocks_filled(paper_input_databuf_t *paper_input_databuf_p, 
		       int block_i,   
		       int block_active[],
		       int blocks_to_set) {
// The non-exceptional call of this routine will be when a block is full.
//   In this case, block_i will be two beyond the full block and blocks_to_set 
//   will be such that the final block acted upon will be the full block. 
//   This will almost always result in only the full block being set.
// The exceptional call of this routine will be when we encounter an active,
//   but for some reason abandoned, block.  In this case, block_i will be the 
//   abandoned block and blocks_to_set will be such that the final block acted 
//   upon will be two prior to the abandoned block.

    int i;
    int blocks_set;
    int found_active_block = 0;
    for(i = block_i, blocks_set = 0; blocks_set < blocks_to_set; i = inc_block_i(i), blocks_set++) {
	if(found_active_block || block_active[i]) {
		// at first active block, all subsequent blocks are considered active
		found_active_block = 1;
		if(!block_active[i]) {
    			while(paper_input_databuf_wait_free(paper_input_databuf_p, block_i)) {	
				printf("target data block %d is not free!\n", block_i);
    			}
		}
		if(block_active[i] == N_PACKETS_PER_BLOCK) {
			paper_input_databuf_p->block[i].header.good_data = 1;
		} else { 
			paper_input_databuf_p->block[i].header.good_data = 0;		// TODO needed?
			printf("missing %d packets for block %d at mcnt %ld  and time %ld\n", 
	                       N_PACKETS_PER_BLOCK - block_active[i], 
	                       i, 
		               paper_input_databuf_p->block[i].header.mcnt[0],
                               time(NULL));
			//exit(0);
		}
		paper_input_databuf_set_filled(paper_input_databuf_p, i);
		block_active[i] = 0;
	}
    }
}

int calc_block_indexes(uint64_t pkt_mcnt, block_info_t *binfo) {

    if(!binfo->initialized) {
    	if(pkt_mcnt % N_SUB_BLOCKS_PER_INPUT_BLOCK != 0) {
		return -1;				// insist that we start on a multiple of sub_blocks/block
	}
	binfo->mcnt_start = pkt_mcnt;	        // good to go
	binfo->initialized = 1;
    }

    // calculate block and sub_block subscripts while taking care of count rollover
    if(pkt_mcnt >= binfo->mcnt_start) {
	binfo->mcnt_offset = pkt_mcnt - binfo->mcnt_start;
    } else {							 // we have a count rollover
	binfo->mcnt_offset = binfo->mcnt_offset + pkt_mcnt + 1;  // assumes that pkt_header.count is now very small, probably zero
    } 
    binfo->block_i     = (binfo->mcnt_offset) / N_SUB_BLOCKS_PER_INPUT_BLOCK % N_INPUT_BLOCKS; 
    binfo->sub_block_i = (binfo->mcnt_offset) % N_SUB_BLOCKS_PER_INPUT_BLOCK; 


    if(pkt_mcnt < binfo->mcnt_start && binfo->block_i == 0 && binfo->sub_block_i == 0) {
	binfo->mcnt_start = pkt_mcnt;				// reset on block,sub 0
    }
    return 0;
} 

void initialize_block(paper_input_databuf_t * paper_input_databuf_p, block_info_t * binfo, uint64_t pkt_mcnt) {
// this routine may initialize a partial block (binfo->sub_block_i != 0)   
    int i;

    paper_input_databuf_p->block[binfo->block_i].header.mcnt[binfo->sub_block_i] = pkt_mcnt; 

    paper_input_databuf_p->block[binfo->block_i].header.good_data = 0; 

    for(i=binfo->sub_block_i+1; i<N_SUB_BLOCKS_PER_INPUT_BLOCK; i++) {
	paper_input_databuf_p->block[binfo->block_i].header.mcnt[i] = 0;
    }
}

void manage_active_blocks(paper_input_databuf_t * paper_input_databuf_p, 
			  block_info_t * binfo, uint64_t pkt_mcnt) {

    if(binfo->block_active[binfo->block_i] == 0) {    // is the block non-active?

	// handle the non-active block
	// check for the "impossible" condition that we are not indexing the first sub_block
	if(binfo->sub_block_i != 0) {
		printf("starting on non-active (count %d) block[%d] but with sub_block[%d] rather than sub_block[0].  Exiting.\n", 
		      binfo->block_active[binfo->block_i], binfo->block_i, binfo->sub_block_i);
		exit(1);
	}
	// init the block 
    	while(paper_input_databuf_wait_free(paper_input_databuf_p, binfo->block_i)) {	
		printf("target data block %d is not free!\n", binfo->block_i);
    	}
	initialize_block(paper_input_databuf_p, binfo, pkt_mcnt);
	// this non-active block is now ready to receive new data

    } else {
	// handle the active block
	// Is it abandonded?
	if(paper_input_databuf_p->block[binfo->block_i].header.mcnt[binfo->sub_block_i] != 0 &&
 	   paper_input_databuf_p->block[binfo->block_i].header.mcnt[binfo->sub_block_i] != pkt_mcnt) {
		// yes..
		// TODO : abadoned block logic is a bit dicey - check/redo
		printf("encountered an abandoned block : block[%d].sub_block[%d].header.mcnt = %llu while packet mcnt = %llu\n",
			binfo->block_i, binfo->sub_block_i, 
			(long long unsigned)paper_input_databuf_p->block[binfo->block_i].header.mcnt[binfo->sub_block_i],
			(long long unsigned)pkt_mcnt);
		// yes, the block is abandonded, indicating serious trouble (cable disconnect?) - go clean up the entire ring
		set_blocks_filled(paper_input_databuf_p, binfo->block_i, binfo->block_active, N_INPUT_BLOCKS);
		// re-acquire ownership of the block and assign the packet's mcnt
    		while(paper_input_databuf_wait_free(paper_input_databuf_p, binfo->block_i)) {	
			printf("target data block %d is not free!\n", binfo->block_i);
    		}
		initialize_block(paper_input_databuf_p, binfo, pkt_mcnt);
		binfo->block_active[binfo->block_i] = 0;	// re-init packet count for this block
	} else {	 // block is not abandoned	
		if(paper_input_databuf_p->block[binfo->block_i].header.mcnt[binfo->sub_block_i] == 0) {	// Are we starting a new sub_block?
			paper_input_databuf_p->block[binfo->block_i].header.mcnt[binfo->sub_block_i] = pkt_mcnt;   // stamp it
		}
	} 
    }	// this active block is now ready to receive new data

    binfo->block_active[binfo->block_i] += 1;			// in all cases increment packet count for this block
}

int write_paper_packet_to_blocks(paper_input_databuf_t *paper_input_databuf_p, struct guppi_udp_packet *p) {

    static block_info_t binfo;
    static int first_time = 1;
    packet_header_t pkt_header;
    const uint64_t *payload_p;
    int i;
    int rv;
    int       block_offset;
    int       sub_block_offset;
    uint64_t *dest_p;

    // block management 
    if(first_time) {
	binfo.initialized = 0;
	first_time = 0;
    }
    get_header(p, &pkt_header);
    //print_pkt_header(&pkt_header);
    rv = calc_block_indexes(pkt_header.count, &binfo);
    if(rv == -1) {
	return rv;		// idle until we are at a good stating point
    }
    //print_block_info(&binfo);
    manage_active_blocks(paper_input_databuf_p, &binfo, pkt_header.count);
    //print_ring_mcnts(paper_input_databuf_p);
    // end block management 

    // Calculate starting points for unpacking this packet into a sub_block.
    // One packet will never span more than one sub_block.
    block_offset     = binfo.block_i     * sizeof(paper_input_block_t);
    sub_block_offset = binfo.sub_block_i * sizeof(paper_input_sub_block_t);
    dest_p           = (uint64_t *)((uint8_t *)paper_input_databuf_p +
			sizeof(struct guppi_databuf)                 + 
			sizeof(guppi_databuf_cache_alignment)        +
			block_offset                                 + 
			sizeof(paper_input_header_t)                 + 
			sub_block_offset);
    payload_p        = (uint64_t *)(p->data+8);

    // unpack the packet, fluffing as we go
    for(i=0; i<(N_TIME*N_CHAN); i++) {
        uint64_t val = payload_p[i];
	// Using complex block size (cbs) of 32
	// 4 = cbs*sizeof(int8_t)/sizeof(uint64_t)
	dest_p[2*N_FENGINES*i] =  val & 0xf0f0f0f0f0f0f0f0LL;
	dest_p[2*N_FENGINES*i+4] = (val & 0x0f0f0f0f0f0f0f0fLL) << 4;
    }  // end upacking

#ifdef TIMING_TEST
	fluffed_words += (N_TIME*N_CHAN);
#endif // TIMING_TEST

    // if all packets are accounted for, mark this block filled
    if(binfo.block_active[binfo.block_i] == N_PACKETS_PER_BLOCK) {
#if 0
 	// debug stuff
	int i;
	for(i=0;i<4;i++) fprintf(stdout, "%d ", binfo.block_active[i]);	
	fprintf(stdout, "\n");
#endif
	// set the full block filled, as well as any previous abandoned blocks.  inc(inc) so we don't touch the "next" block.
	set_blocks_filled(paper_input_databuf_p, inc_block_i(inc_block_i(binfo.block_i)), binfo.block_active, N_INPUT_BLOCKS-1);

        return paper_input_databuf_p->block[binfo.block_i].header.mcnt[0];
    }

    return -1;
}

static int init(struct guppi_thread_args *args)
{
    /* Attach to status shared mem area */
    THREAD_INIT_STATUS(STATUS_KEY);

    // Get sizing parameters
    XGPUInfo xgpu_info;
    xgpuInfo(&xgpu_info);

    /* Create paper_input_databuf for output buffer */
    THREAD_INIT_DATABUF(paper_input_databuf, 4,
        xgpu_info.vecLength*sizeof(ComplexInput),
        args->output_buffer);

    // Success!
    return 0;
}

static void *run(void * _args)
{
    // Cast _args
    struct guppi_thread_args *args = (struct guppi_thread_args *)_args;

    THREAD_RUN_BEGIN(args);

    THREAD_RUN_SET_AFFINITY_PRIORITY(args);

    THREAD_RUN_ATTACH_STATUS(st);

    /* Attach to paper_input_databuf */
    THREAD_RUN_ATTACH_DATABUF(paper_input_databuf, db, args->output_buffer);

    /* Read in general parameters */
    struct guppi_params gp;
    struct sdfits pf;
    char status_buf[GUPPI_STATUS_SIZE];
    guppi_status_lock_safe(&st);
    memcpy(status_buf, st.buf, GUPPI_STATUS_SIZE);
    guppi_status_unlock_safe(&st);
    guppi_read_obs_params(status_buf, &gp, &pf);
    pthread_cleanup_push((void *)guppi_free_sdfits, &pf);

    /* Read network params */
    struct guppi_udp_params up;
    //guppi_read_net_params(status_buf, &up);
    paper_read_net_params(status_buf, &up);

    struct guppi_udp_packet p;

    /* Give all the threads a chance to start before opening network socket */
    sleep(1);

#ifndef TIMING_TEST
    /* Set up UDP socket */
    int rv = guppi_udp_init(&up);
    if (rv!=GUPPI_OK) {
        guppi_error("guppi_net_thread",
                "Error opening UDP socket.");
        pthread_exit(NULL);
    }
    pthread_cleanup_push((void *)guppi_udp_close, &up);
#endif

    /* Main loop */
    unsigned waiting=-1;
    signal(SIGINT,cc);
    while (run_threads) {

#ifndef TIMING_TEST
        /* Wait for data */
        rv = guppi_udp_wait(&up);
        if (rv!=GUPPI_OK) {
            if (rv==GUPPI_TIMEOUT) { 
                /* Set "waiting" flag */
                if (waiting!=1) {
                    guppi_status_lock_safe(&st);
                    hputs(st.buf, STATUS_KEY, "waiting");
                    guppi_status_unlock_safe(&st);
                    waiting=1;
                }
                continue; 
            } else {
                guppi_error("guppi_net_thread", 
                        "guppi_udp_wait returned error");
                perror("guppi_udp_wait");
                pthread_exit(NULL);
            }
        }
	
        /* Read packet */
        p.packet_size = recv(up.sock, p.data, GUPPI_MAX_PACKET_SIZE, 0);
        if (up.packet_size != p.packet_size) {
            if (p.packet_size != -1) {
                #ifdef DEBUG_NET
                guppi_warn("guppi_net_thread", "Incorrect pkt size");
                #endif
                continue; 
            } else {
                guppi_error("guppi_net_thread", 
                        "guppi_udp_recv returned error");
                perror("guppi_udp_recv");
                pthread_exit(NULL);
            }
        }
	
#endif
        /* Update status if needed */
        if (waiting!=0) {
            guppi_status_lock_safe(&st);
            hputs(st.buf, STATUS_KEY, "receiving");
            guppi_status_unlock_safe(&st);
            waiting=0;
        }

        // Copy packet into any blocks where it belongs.
        const int mcnt = write_paper_packet_to_blocks((paper_input_databuf_t *)db, &p);
        if(mcnt != -1) {
            guppi_status_lock_safe(&st);
            hputi4(st.buf, "NETMCNT", mcnt);
            guppi_status_unlock_safe(&st);
        }

#ifdef TIMING_TEST
	static int loop_count=1;
	//if(loop_count == 1000000) run_threads = 0; 
	if(loop_count == 10*1000*1000) {
	    printf("fluffed %lu words\n", fluffed_words);
	    exit(0);
	}
	loop_count++;
#endif

        /* Will exit if thread has been cancelled */
        pthread_testcancel();
    }

    /* Have to close all push's */
#ifndef TIMING_TEST
    pthread_cleanup_pop(0); /* Closes push(guppi_udp_close) */
#endif
    pthread_cleanup_pop(0); /* Closes guppi_free_psrfits */
    THREAD_RUN_DETACH_DATAUF;
    THREAD_RUN_DETACH_STATUS;
    THREAD_RUN_END;

    return NULL;
}

static pipeline_thread_module_t module = {
    name: "paper_net_thread",
    type: PIPELINE_INPUT_THREAD,
    init: init,
    run:  run
};

static __attribute__((constructor)) void ctor()
{
  register_pipeline_thread_module(&module);
}

// vi: set ts=8 sw=4 noet :
