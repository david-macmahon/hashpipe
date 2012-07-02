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
#include <errno.h>

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

#define DEBUG_NET

typedef struct {
    uint64_t mcnt;
    int      fid;	// Fengine ID
    int      xid;	// Xengine ID
} packet_header_t;

// The fields of a block_info_t structure hold (at least) two different kinds
// of data.  Some fields hold data that persist over many packets while other
// fields hold data that are only applicable to the current packet (or the
// previous packet).
typedef struct {
    int initialized;
    uint64_t mcnt_start;
    uint64_t mcnt_offset;
    uint64_t mcnt_prior;
    int out_of_seq_cnt;
    int block_i;
    // The m,x,q,f fields hold four of the six dimensional indices for
    // the first data word of the current packet (i.e. t=0 and c=0).
    int m; // formerly known as sub_block_i
    int x;
    int q;
    int f;
    int block_active[N_INPUT_BLOCKS];
} block_info_t;

static struct guppi_status *st_p;

void print_pkt_header(packet_header_t * pkt_header) {

    static long long prior_mcnt;

    printf("packet header : mcnt %012lx (diff from prior %lld) fid %d xid %d\n", 
	   pkt_header->mcnt, pkt_header->mcnt-prior_mcnt, pkt_header->fid, pkt_header->xid);

    prior_mcnt = pkt_header->mcnt;
}

void print_block_info(block_info_t * binfo) {
    printf("binfo : mcnt_start %012lx mcnt_offset %012lx block_i %d m=%02d x=%d q=%d f=%d\n",
           binfo->mcnt_start, binfo->mcnt_offset, binfo->block_i, binfo->m, binfo->x, binfo->q, binfo->f);
}

void print_block_active(block_info_t * binfo) {
    int i;
    for(i=0;i<N_INPUT_BLOCKS;i++) { 
	if(i == binfo->block_i) {
		fprintf(stdout, "*%03d ", binfo->block_active[i]);	
	} else {
		fprintf(stdout, " %03d ", binfo->block_active[i]);	
	}
    }
    fprintf(stdout, "\n");
}

void print_ring_mcnts(paper_input_databuf_t *paper_input_databuf_p) {

    int i;

    for(i=0; i < N_INPUT_BLOCKS; i++) {
	printf("block %d mcnt %012lx\n", i, paper_input_databuf_p->block[i].header.mcnt);
    }
}

inline int inc_block_i(block_i) {
    return((block_i + 1) % N_INPUT_BLOCKS);
}

inline int dec_block_i(block_i) {
    return(block_i == 0 ? N_INPUT_BLOCKS - 1 : block_i - 1);
}

#ifdef LOG_MCNTS
#define MAX_MCNT_LOG (1024*1024)
static uint64_t mcnt_log[MAX_MCNT_LOG];
static int mcnt_log_idx = 0;

void dump_mcnt_log()
{
    int i;
    FILE *f = fopen("mcnt.log","w");
    for(i=0; i<MAX_MCNT_LOG; i++) {
	if(mcnt_log[i] == 0) break;
	fprintf(f, "%012lx\n", mcnt_log[i]);
    }
    fclose(f);
}
#endif

static inline void get_header (struct guppi_udp_packet *p, packet_header_t * pkt_header)
{
#ifdef TIMING_TEST
    static int pkt_counter=0;
    pkt_header->mcnt = (pkt_counter / (Nx*Nq*Nf)) %  Nm;
    pkt_header->xid  = (pkt_counter / (   Nq*Nf)) %  Nx;
    pkt_header->fid  = (pkt_counter             ) % (Nq*Nf);
    pkt_counter++;
#else
    uint64_t raw_header;
    raw_header = be64toh(*(unsigned long long *)p->data);
    pkt_header->mcnt        = raw_header >> 16;
    pkt_header->xid         = raw_header        & 0x00000000000000FF;
    pkt_header->fid         = (raw_header >> 8) & 0x00000000000000FF;
#endif

#ifdef LOG_MCNTS
    mcnt_log[mcnt_log_idx++ % MAX_MCNT_LOG] = pkt_header->mcnt;
#endif
}

static void die(paper_input_databuf_t *paper_input_databuf_p, block_info_t *binfo)
{
    print_block_info(binfo);
    print_block_active(binfo);
    print_ring_mcnts(paper_input_databuf_p);
#ifdef LOG_MCNTS
    dump_mcnt_log();
#endif
    abort(); // End process and generate core file (if ulimit allows)
}

void set_block_filled(paper_input_databuf_t *paper_input_databuf_p, block_info_t *binfo, int block_i) { 
    static int last_filled = -1;

    uint32_t block_missed_pkt_cnt, block_missed_feng, missed_pkt_cnt=0;

    if(binfo->block_active[block_i]) {

	// if all packets are accounted for, mark this block as good
	if(binfo->block_active[block_i] == N_PACKETS_PER_BLOCK) {
	    paper_input_databuf_p->block[block_i].header.good_data = 1;
	}

	last_filled = (last_filled+1) % ((struct guppi_databuf *)paper_input_databuf_p)->n_block;
	if(last_filled != block_i) {
	    printf("block %d being marked filled, but expected block %d!\n", block_i, last_filled);
	    die(paper_input_databuf_p, binfo);
	}

	if(paper_input_databuf_set_filled(paper_input_databuf_p, block_i) != GUPPI_OK) {
	    guppi_error(__FUNCTION__, "error waiting for databuf filled call");
	    run_threads=0;
	    pthread_exit(NULL);
	    return;
	}

	block_missed_pkt_cnt = N_PACKETS_PER_BLOCK - binfo->block_active[block_i];
	// If we missed more than N_PACKETS_PER_BLOCK_PER_F, then assume we
	// are missing one or more F engines.  Any missed packets beyond an
	// integer multiple of N_PACKETS_PER_BLOCK_PER_F will be considered
	// as dropped packets.
	block_missed_feng = block_missed_pkt_cnt / N_PACKETS_PER_BLOCK_PER_F;
	block_missed_pkt_cnt %= N_PACKETS_PER_BLOCK_PER_F;
	guppi_status_lock_busywait_safe(st_p);
	hputu4(st_p->buf, "NETBKOUT", block_i);
	hputu4(st_p->buf, "MISSEDFE", block_missed_feng);
	if(block_missed_pkt_cnt) {
	    // Increment MISSEDPK by number of missed packets for this block
	    hgetu4(st_p->buf, "MISSEDPK", &missed_pkt_cnt);
	    missed_pkt_cnt += block_missed_pkt_cnt;
	    hputu4(st_p->buf, "MISSEDPK", missed_pkt_cnt);
	//  fprintf(stderr, "got %d packets instead of %d\n",
	//	    binfo->block_active[block_i], N_PACKETS_PER_BLOCK);
	}
	guppi_status_unlock_safe(st_p);

    	binfo->block_active[block_i] = 0;
    } 
}

static inline int calc_block_indexes(block_info_t *binfo, packet_header_t * pkt_header) {

    if(pkt_header->mcnt < binfo->mcnt_start) {
	char msg[120];
	sprintf(msg, "current packet mcnt %012lx less than mcnt start %012lx", pkt_header->mcnt, binfo->mcnt_start);
	    guppi_error(__FUNCTION__, msg);
	    //guppi_error(__FUNCTION__, "current packet mcnt less than mcnt start");
	    //run_threads=0;
	    //pthread_exit(NULL);
	    return -1;
    } else {
	binfo->mcnt_offset = pkt_header->mcnt - binfo->mcnt_start;
    }

    binfo->block_i     = (binfo->mcnt_offset) / N_SUB_BLOCKS_PER_INPUT_BLOCK % N_INPUT_BLOCKS; 
    binfo->m = (binfo->mcnt_offset) % Nm;
    binfo->x = (pkt_header->xid) % Nx;
    binfo->q = (pkt_header->fid) / 4;
    binfo->f = (pkt_header->xid) % 4;

    return 0;
} 

#define MAX_MCNT_DIFF 64 
static inline int out_of_seq_mcnt(block_info_t * binfo, uint64_t pkt_mcnt) {
// mcnt rollovers are seen and treated like any other out of sequence mcnt

    if(abs(pkt_mcnt - binfo->mcnt_prior) <= MAX_MCNT_DIFF) {
        binfo->mcnt_prior = pkt_mcnt;
    	binfo->out_of_seq_cnt = 0;
	return 0;
    } else {
	printf("Out of seq : mcnt jumps from %012lx to %012lx\n", binfo->mcnt_prior, pkt_mcnt);
    	binfo->out_of_seq_cnt++;
	return 1;
    }
}

#define MAX_OUT_OF_SEQ 5
static inline int handle_out_of_seq_mcnt(block_info_t * binfo) {

    if(binfo->out_of_seq_cnt > MAX_OUT_OF_SEQ) {
	printf("exceeded max (%d) out of sequence mcnts - restarting\n", MAX_OUT_OF_SEQ);
	binfo->initialized = 0;
    }
    return -1;
}

static inline void initialize_block(paper_input_databuf_t * paper_input_databuf_p, block_info_t * binfo, uint64_t pkt_mcnt) {

    paper_input_databuf_p->block[binfo->block_i].header.good_data = 0; 
    // Round pkt_mcnt down to nearest multiple of Nm
    paper_input_databuf_p->block[binfo->block_i].header.mcnt = pkt_mcnt - (pkt_mcnt%Nm);
}

static inline void initialize_block_info(paper_input_databuf_t *paper_input_databuf_p, block_info_t * binfo, uint64_t pkt_mcnt) {

    int i;

    // We might be restarting so mark all currently active blocks, with the exception
    // of block_i, as filled. We will restart at block_i.  On program startup, this loop 
    // as no functional effect as no blocks are active and all block_active elements are 0. 
    for(i = 0; i < N_INPUT_BLOCKS; i++) {
	if(i == binfo->block_i) {
		binfo->block_active[i] = 0;	
	} else {
    		if(binfo->block_active[i]) {
			paper_input_databuf_p->block[i].header.good_data = 0;   // all data are bad at this point
			set_block_filled(paper_input_databuf_p, binfo, i);
		}
	}
    }		

    // On program startup block_i will be zero.  If we are restarting,  this will set 
    // us up to restart at the beginning of block_i. 
    binfo->mcnt_start = pkt_mcnt - binfo->block_i * N_SUB_BLOCKS_PER_INPUT_BLOCK;

    binfo->mcnt_prior = pkt_mcnt;
    binfo->out_of_seq_cnt = 0;
    binfo->initialized = 1;
}

// This function returns -1 unless the given packet causes a block to be marked
// as filled in which case this function returns the marked block's first mcnt.
// Any return value other than -1 will be stored in the status memory as
// NETMCNT, so it is important that values other than -1 are returned rarely
// (i.e. when marking a block as filled)!!!
static inline uint64_t write_paper_packet_to_blocks(paper_input_databuf_t *paper_input_databuf_p, struct guppi_udp_packet *p) {

    static block_info_t binfo;
    packet_header_t pkt_header;
    const uint64_t *payload_p;
    int rv;
    int i;
    uint64_t *dest_p;
    uint64_t netmcnt = -1; // Value to store in status memory

    // housekeeping for each packet
    get_header(p, &pkt_header);
    if(! binfo.initialized) {
	// insist that we start on a multiple of sub_blocks/block
    	if(pkt_header.mcnt % N_SUB_BLOCKS_PER_INPUT_BLOCK != 0) {
		return -1;
    	}
	initialize_block_info(paper_input_databuf_p, &binfo, pkt_header.mcnt);
    }
    if(out_of_seq_mcnt(&binfo, pkt_header.mcnt)) {
    	return(handle_out_of_seq_mcnt(&binfo));
    }
    if((rv = calc_block_indexes(&binfo, &pkt_header))) {
	return rv;
    }
    if(! binfo.block_active[binfo.block_i]) {
	// new block, pass along the block for two blocks ago
	i = dec_block_i(dec_block_i(binfo.block_i));
	set_block_filled(paper_input_databuf_p, &binfo, i);
	netmcnt = paper_input_databuf_p->block[i].header.mcnt;
	// Wait (hopefully not long!) for free block for this packet
	if((rv = paper_input_databuf_busywait_free(paper_input_databuf_p, binfo.block_i)) != GUPPI_OK) {    
	    if (rv==GUPPI_TIMEOUT) {
		// run_threads is 0 (i.e. shutting down)
	        return -1;
	    } else {
	        guppi_error(__FUNCTION__, "error waiting for free databuf");
	        run_threads=0;
	        pthread_exit(NULL);
	        return -1;
	    }
	}

	initialize_block(paper_input_databuf_p, &binfo, pkt_header.mcnt); 
    }
    binfo.block_active[binfo.block_i] += 1;	// increment packet count for block
    // end housekeeping

    // Calculate starting points for unpacking this packet into block's data buffer.
    dest_p = paper_input_databuf_p->block[binfo.block_i].data
	+ paper_input_databuf_data_idx(binfo.m, binfo.x, binfo.q, binfo.f, 0, 0);
    payload_p        = (uint64_t *)(p->data+8);

    // Copy data into buffer
    memcpy(dest_p, payload_p, N_BYTES_PER_PACKET);


    return netmcnt;
}

static int init(struct guppi_thread_args *args)
{
    /* Attach to status shared mem area */
    THREAD_INIT_STATUS(args->instance_id, STATUS_KEY);

    /* Create paper_input_databuf for output buffer */
    THREAD_INIT_DATABUF(args->instance_id, paper_input_databuf, N_INPUT_BLOCKS,
        N_BYTES_PER_BLOCK,
        args->output_buffer);

    // Success!
    return 0;
}

#define ELAPSED_NS(start,stop) \
  (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

static void *run(void * _args)
{
    // Cast _args
    struct guppi_thread_args *args = (struct guppi_thread_args *)_args;

#ifdef DEBUG_SEMS
    fprintf(stderr, "s/tid %lu/NET/' <<.\n", pthread_self());
#endif

    THREAD_RUN_BEGIN(args);

    THREAD_RUN_SET_AFFINITY_PRIORITY(args);

    THREAD_RUN_ATTACH_STATUS(args->instance_id, st);
    st_p = &st;		// allow global (this source file) access to the status buffer

    /* Attach to paper_input_databuf */
    THREAD_RUN_ATTACH_DATABUF(args->instance_id, paper_input_databuf, db, args->output_buffer);

    /* Read in general parameters */
    struct guppi_params gp;
    struct sdfits pf;
    char status_buf[GUPPI_STATUS_SIZE];
    guppi_status_lock_busywait_safe(st_p);
    memcpy(status_buf, st_p->buf, GUPPI_STATUS_SIZE);
    guppi_status_unlock_safe(st_p);
    guppi_read_obs_params(status_buf, &gp, &pf);
    pthread_cleanup_push((void *)guppi_free_sdfits, &pf);

    /* Read network params */
    struct guppi_udp_params up;
    //guppi_read_net_params(status_buf, &up);
    paper_read_net_params(status_buf, &up);
    // Store bind host/port info etc in status buffer
    guppi_status_lock_busywait_safe(&st);
    hputs(st.buf, "BINDHOST", up.bindhost);
    hputi4(st.buf, "BINDPORT", up.bindport);
    hputu4(st.buf, "MISSEDFE", 0);
    hputu4(st.buf, "MISSEDPK", 0);
    hputs(st.buf, STATUS_KEY, "running");
    guppi_status_unlock_safe(&st);

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
    uint64_t packet_count = 0;
    uint64_t elapsed_handling_ns = 0;
    uint64_t elapsed_recv_ns = 0;
    uint64_t elapsed_proc_ns = 0;
    float ns_per_pkt = 0.0;
    float ns_per_recv = 0.0;
    float ns_per_proc = 0.0;
    struct timespec start, stop;
    struct timespec recv_start, recv_stop;
    signal(SIGINT,cc);
    while (run_threads) {

#ifndef TIMING_TEST
        /* Read packet */
	clock_gettime(CLOCK_MONOTONIC, &recv_start);
	do {
	    clock_gettime(CLOCK_MONOTONIC, &start);
	    p.packet_size = recv(up.sock, p.data, GUPPI_MAX_PACKET_SIZE, 0);
	    clock_gettime(CLOCK_MONOTONIC, &recv_stop);
	} while (p.packet_size == -1 && (errno == EAGAIN || errno == EWOULDBLOCK) && run_threads);
	if(!run_threads) break;
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
	packet_count++;

        // Copy packet into any blocks where it belongs.
        const uint64_t mcnt = write_paper_packet_to_blocks((paper_input_databuf_t *)db, &p);

	clock_gettime(CLOCK_MONOTONIC, &stop);
	elapsed_handling_ns += ELAPSED_NS(start, stop);
	elapsed_recv_ns += ELAPSED_NS(recv_start, recv_stop);
	elapsed_proc_ns += ELAPSED_NS(recv_stop, stop);

        if(mcnt != -1) {
            // Update status
            ns_per_pkt = (float)elapsed_handling_ns / packet_count;
            ns_per_recv = (float)elapsed_recv_ns / packet_count;
            ns_per_proc = (float)elapsed_proc_ns / packet_count;
            guppi_status_lock_busywait_safe(&st);
            hputu8(st.buf, "NETMCNT", mcnt);
	    // Gbps = bits_per_packet / ns_per_packet
	    // (N_BYTES_PER_PACKET excludes header, so +8 for the header)
            hputr4(st.buf, "NETGBPS", 8*(N_BYTES_PER_PACKET+8)/ns_per_pkt);
            hputr4(st.buf, "NETRECNS", ns_per_recv);
            hputr4(st.buf, "NETPKTNS", ns_per_pkt);
            hputr4(st.buf, "NETPRCNS", ns_per_proc);
            guppi_status_unlock_safe(&st);
	    // Start new average
	    elapsed_handling_ns = 0;
	    elapsed_recv_ns = 0;
	    elapsed_proc_ns = 0;
	    packet_count = 0;
        }

#if defined TIMING_TEST || defined NET_TIMING_TEST

#define END_LOOP_COUNT (1*1000*1000)
	static int loop_count=0;
	static struct timespec tt_start, tt_stop;
	if(loop_count == 0) {
	    clock_gettime(CLOCK_MONOTONIC, &tt_start);
	}
	//if(loop_count == 1000000) run_threads = 0; 
	if(loop_count == END_LOOP_COUNT) {
	    clock_gettime(CLOCK_MONOTONIC, &tt_stop);
	    int64_t elapsed = ELAPSED_NS(tt_start, tt_stop);
	    printf("processed %d packets in %.6f ms (%.3f us per packet)\n",
		    END_LOOP_COUNT, elapsed/1e6, elapsed/1e3/END_LOOP_COUNT);
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
