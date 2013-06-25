/* hashpipe_udp.h
 *
 * Functions dealing with setting up and 
 * receiving data through a UDP connection.
 */
#ifndef _HASHPIPE_UDP_H
#define _HASHPIPE_UDP_H

#include <sys/types.h>
#include <netdb.h>
#include <poll.h>

/* Struct to hold connection parameters */
struct hashpipe_udp_params {

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
int hashpipe_udp_init(struct hashpipe_udp_params *p);

/* Close out socket, etc */
int hashpipe_udp_close(struct hashpipe_udp_params *p);

#define HASHPIPE_MAX_PACKET_SIZE 9600

/* Basic UDP packet holder. */
struct hashpipe_udp_packet {
    size_t packet_size;  /* packet size, bytes */
    char data[HASHPIPE_MAX_PACKET_SIZE] __attribute__ ((aligned(128))); /* packet data */
};

#endif // _HASHPIPE_UDP_H
