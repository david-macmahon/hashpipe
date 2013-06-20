/* guppi_udp.h
 *
 * Functions dealing with setting up and 
 * receiving data through a UDP connection.
 */
#ifndef _GUPPI_UDP_H
#define _GUPPI_UDP_H

#include <sys/types.h>
#include <netdb.h>
#include <poll.h>

#define GUPPI_MAX_PACKET_SIZE 9600

/* Struct to hold connection parameters */
struct guppi_udp_params {

    /* Info needed from outside: */
    char sender[80];  /* Sender hostname */
    int port;         /* Receive port */
    char bindhost[80];    /* Local IP address to bind to */
    int bindport;         /* Local port to bind to */
    size_t packet_size;     /* Expected packet size, 0 = don't care */
    char packet_format[32]; /* Packet format */

    /* Derived from above: */
    int sock;                       /* Receive socket */
    struct addrinfo sender_addr;    /* Sender hostname/IP params */
    struct pollfd pfd;              /* Use to poll for avail data */
};

/* Use sender and port fields in param struct to init
 * the other values, bind socket, etc.
 */
int guppi_udp_init(struct guppi_udp_params *p);

/* Close out socket, etc */
int guppi_udp_close(struct guppi_udp_params *p);

/* Basic structure of a packet.  This struct, functions should 
 * be used to get the various components of a data packet.   The
 * internal packet structure is:
 *   1. sequence number (64-bit unsigned int)
 *   2. data bytes (typically 8kB)
 *   3. status flags (64 bits)
 *
 * Except in the case of "1SFA" packets:
 *   1. sequence number (64b uint)
 *   2. data bytes (typically 8128B)
 *   3. status flags (64b)
 *   4. blank space (16B)
 */
struct guppi_udp_packet {
    size_t packet_size;  /* packet size, bytes */
    char data[GUPPI_MAX_PACKET_SIZE] __attribute__ ((aligned(128))); /* packet data */
};

#endif
