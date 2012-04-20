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
#include "paper_fluff.h"
#include "guppi_udp.h"
#include "guppi_time.h"

#define STATUS_KEY "NETSTAT"  /* Define before guppi_threads.h */
#include "guppi_threads.h"
#include "guppi_defines.h"
#include "paper_thread.h"

#define TIMING_TEST
#define DEBUG_NET

#ifdef TIMING_TEST
static int fluffed_words = 0;
#endif

// TODO put this in a header
typedef struct {
    uint64_t count;
    int      ant_base;
    int      chan_base;
} packet_header_t;

typedef struct {
    int64_t mcnt_start;
    int64_t mcnt_offset;
    int block_i;
    int sub_block_i;
    int block_active[N_INPUT_BLOCKS];
} block_info_t;
// end TODO put this in a header

void print_block_info(block_info_t * binfo) {
    printf("mcnt_start mcnt_offset block_i sub_block_i %lu %lu %d %d\n", 
           binfo->mcnt_start, binfo->mcnt_offset, binfo->block_i, binfo->sub_block_i);
}

void print_ring_mcnts(paper_input_databuf_t *paper_input_databuf_p) {

    int i, j;

    for(i=0; i < N_INPUT_BLOCKS; i++) {
	for(j=0; j < N_SUB_BLOCKS_PER_INPUT_BLOCK; j++) {
		printf("block %d sub_block %d mcnt %lu\n", i, j, paper_input_databuf_p->block[i].header[j].mcnt);
	}
    }
}

void clear_chan_present(paper_input_databuf_t *paper_input_databuf_p, int block_i) {
    int i;
    for(i=0; i < N_SUB_BLOCKS_PER_INPUT_BLOCK; i++) {
	paper_input_databuf_p->block[block_i].header[i].chan_present[0] = 0;
	paper_input_databuf_p->block[block_i].header[i].chan_present[1] = 0;
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
    pkt_header->chan_base   = raw_header        & 0x000000000000000F;
    pkt_header->ant_base    = (raw_header >> 8) & 0x00000000000000FF;

#ifdef TIMING_TEST
    static int fake_mcnt=0;
    static int fake_ant_base=0;
    static int pkt_counter=0;

    if(pkt_counter == 8) {
	fake_mcnt++;
        fake_ant_base = 0;
	pkt_counter=0;
    } else if(pkt_counter % 8 == 0) {
	fake_ant_base += 4;
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
			// good_flag = 1;
		} else { 
			// good_flag = 0;
			printf("missing %d packets for block %d at mcnt %ld  and time %ld\n", 
	                       N_PACKETS_PER_BLOCK - block_active[i], 
	                       i, 
		               paper_input_databuf_p->block[i].header[0].mcnt,
                               time(NULL));
			//exit(0);
		}
		paper_input_databuf_set_filled(paper_input_databuf_p, i);
		block_active[i] = 0;
	}
    }
}

int calc_block_indexes(uint64_t pkt_mcnt, block_info_t *binfo) {


    if(binfo->mcnt_start < 0) {
    	if(pkt_mcnt % N_SUB_BLOCKS_PER_INPUT_BLOCK != 0) {
		return -1;				// insist that we start on a multiple of sub_blocks/block
	}
	binfo->mcnt_start = pkt_mcnt;	        // good to go
    }

    // calculate block and sub_block subscripts while taking care of count rollover
    if(pkt_mcnt >= binfo->mcnt_start) {
	binfo->mcnt_offset = pkt_mcnt - binfo->mcnt_start;
    } else {							 // we have a count rollover
	binfo->mcnt_offset = binfo->mcnt_offset + pkt_mcnt + 1;  // assumes that pkt_header.count is now very small, probably zero
    } 
    binfo->block_i     = (binfo->mcnt_offset) / N_SUB_BLOCKS_PER_INPUT_BLOCK % N_INPUT_BLOCKS; 
    binfo->sub_block_i = (binfo->mcnt_offset) % N_SUB_BLOCKS_PER_INPUT_BLOCK; 

#if 0
printf("mcnt %llu  block_i %d  sub_block_i %d  count %d\n", (long long unsigned)pkt_mcnt, binfo->block_i, binfo->sub_block_i, binfo->block_active[binfo->block_i]);
#endif

    if(pkt_mcnt < binfo->mcnt_start && binfo->block_i == 0 && binfo->sub_block_i == 0) {
	binfo->mcnt_start = pkt_mcnt;				// reset on block,sub 0
    }
    return 0;
} 

void initialize_block(paper_input_databuf_t * paper_input_databuf_p, block_info_t * binfo, uint64_t pkt_mcnt) {
// this routine may initialize a partial block (binfo->sub_block_i != 0)   
    int i;

    paper_input_databuf_p->block[binfo->block_i].header[binfo->sub_block_i].mcnt = pkt_mcnt; 

    for(i=binfo->sub_block_i+1; i<N_SUB_BLOCKS_PER_INPUT_BLOCK; i++) {
	paper_input_databuf_p->block[binfo->block_i].header[i].mcnt = 0;
    }
}

void manage_active_blocks(paper_input_databuf_t * paper_input_databuf_p, 
			  block_info_t * binfo, uint64_t pkt_mcnt) {
    //int i;

    // is the block not curretnly active?
    if(binfo->block_active[binfo->block_i] == 0) {
	// block not currently active, so:
	// check for the "impossible" condition that we are not indexing the first sub_block
	if(binfo->sub_block_i != 0) {
		printf("starting on non-active (count %d) block[%d] but with sub_block[%d] rather than sub_block[0].  Exiting.\n", 
		      binfo->block_active[binfo->block_i], binfo->block_i, binfo->sub_block_i);
		exit(1);
	}
	// acquire ownership of the block 
    	while(paper_input_databuf_wait_free(paper_input_databuf_p, binfo->block_i)) {	
		printf("target data block %d is not free!\n", binfo->block_i);
    	}
	// initialize the block
	initialize_block(paper_input_databuf_p, binfo, pkt_mcnt);
    } else {
	// block is already active. Is it abandonded?
	// TODO : abadoned block logic is a bit dicey - check/redo
	if(paper_input_databuf_p->block[binfo->block_i].header[binfo->sub_block_i].mcnt != 0 &&
 	   paper_input_databuf_p->block[binfo->block_i].header[binfo->sub_block_i].mcnt != pkt_mcnt) {
		printf("encountered an abandoned block : block[%d].sub_block[%d].mcnt = %llu while packet mcnt = %llu\n",
			binfo->block_i, binfo->sub_block_i, 
			(long long unsigned)paper_input_databuf_p->block[binfo->block_i].header[binfo->sub_block_i].mcnt,
			(long long unsigned)pkt_mcnt);
		// yes, the block is abandonded, indicating serious trouble (cable disconnect?) - go clean up the entire ring
		set_blocks_filled(paper_input_databuf_p, binfo->block_i, binfo->block_active, N_INPUT_BLOCKS);
		// re-acquire ownership of the block and assign the packet's mcnt
    		while(paper_input_databuf_wait_free(paper_input_databuf_p, binfo->block_i)) {	
			printf("target data block %d is not free!\n", binfo->block_i);
    		}
		initialize_block(paper_input_databuf_p, binfo, pkt_mcnt);
		binfo->block_active[binfo->block_i] = 0;	// re-init packet count for this block

	// block is not abandoned. Are we starting a new sub_block?    
	} else if(paper_input_databuf_p->block[binfo->block_i].header[binfo->sub_block_i].mcnt == 0) {
		paper_input_databuf_p->block[binfo->block_i].header[binfo->sub_block_i].mcnt = pkt_mcnt;
	} 

    }	// this block is now ready to receive the packet data

    binfo->block_active[binfo->block_i] += 1;			// increment packet count for this block
}

int write_paper_packet_to_blocks(paper_input_databuf_t *paper_input_databuf_p, struct guppi_udp_packet *p) {

    //static int block_active[N_INPUT_BLOCKS];
    static block_info_t binfo;
    static int first_time = 1;
    packet_header_t pkt_header;
    uint8_t *payload_p;
    //int time_i, chan_i, input_i;
    int time_i, chan_i;
    int rv;

    if(first_time) {
	binfo.mcnt_start = -1;
	first_time = 0;
    }

    get_header(p, &pkt_header);
    rv = calc_block_indexes(pkt_header.count, &binfo);
    if(rv == -1) {
	return rv;
    }
    manage_active_blocks(paper_input_databuf_p, &binfo, pkt_header.count);

    // update channels present
    //paper_input_databuf_p->block[binfo.block_i].header[binfo.sub_block_i].chan_present[pkt_header.chan/64] |= ((uint64_t)1<<(pkt_header.chan%64));

    // Unpack the packet. The inner (fastest changing) loop spans the smallest area of the databuf in order to 
    // maximize the use of write cache, which speeds throughput.
    // Packet payload size is 8192 = 256 channels * 8 inputs * 4 time samples per input.  Input varies over 8 sets of inputs.  
    // Thus, mcnt increments every 8 packets.  
    //
    // Channel starts at chan_base and proceeds through a set of 2048 channels in multiples of 8 in a given packet and this
    // subset of 256 channels (and thus chan_base) is constant for a given x-engine.  Ie, "some channels".
    //
    // Input starts at ant_base and proceeds serially through a total of 4 antennas (x2 pols) for a given packet but proceeds in multiples
    // of 4 across packets for a total of 32 antennas (x2 pols = 64 inputs) for a given x-engine.  Ie, "all antennas".
#define TIME_STRIDE N_INPUT
#define CHAN_STRIDE (N_TIME * N_INPUT)
    int       block_offset;
    int       sub_block_offset;
    uint16_t *sub_block_p;

    // pointers for holding place and addressing pairs in the packet
    uint8_t  *time_pkt_p;
    uint8_t  *chan_pkt_p;

    // pointers for holding place and addressing pairs in the input buffer
    uint8_t  *time_buf_p;
    uint8_t  *chan_buf_p;

    // Calculate starting points for this packet and sub_block.
    // One packet will never span more than one sub_block.
    block_offset     = binfo.block_i     * sizeof(paper_input_block_t);
    sub_block_offset = binfo.sub_block_i * sizeof(paper_input_sub_block_t);
    sub_block_p      = (uint16_t *)paper_input_databuf_p + block_offset + sub_block_offset;
    payload_p        = (uint8_t *)(p->data+8); 

    for(time_i=0; time_i<N_TIME; time_i++) {
	time_pkt_p = (uint8_t *)payload_p   + time_i * TIME_STRIDE;
	time_buf_p = (uint8_t *)sub_block_p + time_i*sizeof(paper_input_time_t);

	for(chan_i=0; chan_i<N_CHAN; chan_i++) {
		chan_pkt_p = time_pkt_p + chan_i * CHAN_STRIDE;
		chan_buf_p = time_buf_p + chan_i*sizeof(paper_input_chan_t);

   		*(uint64_t *)chan_buf_p = *(uint64_t *)chan_pkt_p;    // copy 8 bytes of contiguous inputs 
	}
    }

    // if all packets are accounted for, mark this block filled
    if(binfo.block_active[binfo.block_i] == N_PACKETS_PER_BLOCK) {
    	fluff_32to64((uint64_t*)&(paper_input_databuf_p->block[binfo.block_i].complexity[0]), 
		     (uint64_t*)&(paper_input_databuf_p->block[binfo.block_i].complexity[0]),
		     (uint64_t*)&(paper_input_databuf_p->block[binfo.block_i].complexity[1]),
		     N_FLUFFED_8BYTES_PER_BLOCK/2);
#ifdef TIMING_TEST
	fluffed_words += N_FLUFFED_8BYTES_PER_BLOCK/2;
#endif // TIMING_TEST
#if 0
 	// debug stuff
	int i;
	for(i=0;i<4;i++) printf("%d ", binfo.block_active[i]);	
	printf("\n");
#endif
	// set the full block filled, as well as any previous abandoned blocks.  inc(inc) so we don't touch the "next" block.
	set_blocks_filled(paper_input_databuf_p, inc_block_i(inc_block_i(binfo.block_i)), binfo.block_active, N_INPUT_BLOCKS-1);

        return paper_input_databuf_p->block[binfo.block_i].header[0].mcnt;
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

    /* Set up UDP socket */
    int rv = guppi_udp_init(&up);
    if (rv!=GUPPI_OK) {
        guppi_error("guppi_net_thread",
                "Error opening UDP socket.");
        pthread_exit(NULL);
    }
    pthread_cleanup_push((void *)guppi_udp_close, &up);

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
	if(loop_count == 1000000) {
	    printf("fluffed %d words\n", fluffed_words);
	    exit(0);
	}
	loop_count++;
#endif

        /* Will exit if thread has been cancelled */
        pthread_testcancel();
    }

    /* Have to close all push's */
    pthread_cleanup_pop(0); /* Closes push(guppi_udp_close) */
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
