/* hashpipe_udp.c
 *
 * UDP implementations.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <poll.h>
#include <endian.h>

#include "hashpipe_udp.h"
#include "hashpipe_databuf.h"
#include "hashpipe_error.h"

int hashpipe_udp_init(struct hashpipe_udp_params *p) {

    /* Resolve local hostname to which we will bind */
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = 0;
    int rv = getaddrinfo(p->bindhost, NULL, &hints, &result);
    if (rv!=0) { 
        hashpipe_error("hashpipe_udp_init", "getaddrinfo failed");
        freeaddrinfo(result);
        return HASHPIPE_ERR_SYS;
    }

    // getaddrinfo() returns a list of address structures.
    // Try each address until we successfully bind(2).
    // If socket(2) (or bind(2)) fails, we (close the socket
    // and) try the next address.

    for (rp = result; rp != NULL; rp = rp->ai_next) {

        // Try to create socket, skip to next on failure
        p->sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (p->sock == -1)
            continue;

        // Set port here (easier(?) than converting p->bindport to string and
        // passing it to getaddrinfo).
        ((struct sockaddr_in *)(rp->ai_addr))->sin_port = htons(p->bindport);

        // Try to bind, break out of loop on success
        if (bind(p->sock, rp->ai_addr, rp->ai_addrlen) == 0)
            break;                  /* Success */

        // Close un-bindable socket
        close(p->sock);
        p->sock = -1;
    }

    if (rp == NULL) { // No address succeeded
        hashpipe_error("hashpipe_udp_init", "Could not create/bind socket");
        freeaddrinfo(result);
        return HASHPIPE_ERR_SYS;
    }

    /* Non-blocking recv */
    fcntl(p->sock, F_SETFL, O_NONBLOCK);

    /* Increase recv buffer for this sock */
    int bufsize = 128*1024*1024;
    socklen_t ss = sizeof(int);
    rv = setsockopt(p->sock, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(int));
    if (rv<0) { 
        hashpipe_error("hashpipe_udp_init", "Error setting rcvbuf size.");
        perror("setsockopt");
    } 
    rv = getsockopt(p->sock, SOL_SOCKET, SO_RCVBUF, &bufsize, &ss); 
    if (0 && rv==0) { 
        printf("hashpipe_udp_init: SO_RCVBUF=%d\n", bufsize);
    }

    /* Poll command */
    p->pfd.fd = p->sock;
    p->pfd.events = POLLIN;

    return HASHPIPE_OK;
}

int hashpipe_udp_close(struct hashpipe_udp_params *p) {
    close(p->sock);
    return HASHPIPE_OK;
}
