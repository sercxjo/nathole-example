/* Use STUN and signal servers to transmit message between 2 peers over two NAT.
   Copyright (C) 2016 Sergey Khlutchin

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published
    by the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "nathole.h"
#include <ifaddrs.h>
#include <net/if.h>

int getlocaddr(int sockfd, struct peer_addr *peer)
{
    struct ifaddrs *ifaddr, *ifa;
    int n;

    struct sockaddr_in addr;
    socklen_t addrlen= sizeof addr;
    if( getsockname(sockfd, (struct sockaddr*)&addr, &addrlen) ) fatal("getsockname");
    if( addr.sin_family != AF_INET ) die("Only IPv4 supported!\n");
    peer->port= addr.sin_port;
    debug("local addr %x %d\n", htonl(addr.sin_addr.s_addr), htons(addr.sin_port));

    if( getifaddrs(&ifaddr) == -1 ) {
        perror("getifaddrs");
        return -1;
    }
    /* Walk through linked list, maintaining head pointer so we can free list later */
    for( ifa = ifaddr, n = 0; ifa != NULL; ifa = ifa->ifa_next, n++ ) {
        if( ifa->ifa_addr == NULL || (ifa->ifa_flags&(IFF_UP|IFF_LOOPBACK|IFF_RUNNING))!=(IFF_UP|IFF_RUNNING) ) continue;
        fprintf(stderr, "%s ", ifa->ifa_name);
        switch( ifa->ifa_addr->sa_family ) {
        case AF_INET:
            peer->in_addr= ((struct sockaddr_in*)ifa->ifa_addr)->sin_addr.s_addr;
            if( peer->in_addr>>16 == (169<<8|254) ) break; // link-local address
            freeifaddrs(ifaddr);
            return 0;
        case AF_INET6: // TODO: IPv6
            fprintf(stderr, "ipv6\n"); 
            break;
        case AF_PACKET: // MAC
            fprintf(stderr, "packet\n");
            break;
       }
    }
    freeifaddrs(ifaddr);
    return -1;
}
int connect_to_host(const char *node, const char *port, int sock_type)
{
    int res;
    struct addrinfo ar_request= { 0, AF_INET, sock_type }, *ar_result= 0;
    if( (res= getaddrinfo(node, port, &ar_request, &ar_result)) != 0 )
        die("Resolving %s: %s\n", node, gai_strerror(res));
    for( int r=0; r<ar_result->ai_addrlen; r++ ) debug(ar_result->ai_addrlen<=16?"%d.":"%x:", ((unsigned char*)ar_result->ai_addr)[r]);
    debug("\b \n");
    int sockfd= socket(ar_result->ai_addr->sa_family, ar_result->ai_socktype, ar_result->ai_protocol);
    if( sockfd<0 ) fatal("Create socket");
    if( connect(sockfd, ar_result->ai_addr, ar_result->ai_addrlen)<0 ) fatal("Connect socket");
    freeaddrinfo(ar_result);
    return sockfd;
}
void reconnect_to(int sockfd, const struct peer_addr *addr) {
    struct sockaddr_in ai_addr= {AF_INET, addr->port, {addr->in_addr}};
    debug("reconnect to %x %d\n", htonl(addr->in_addr), htons(addr->port));
    if( connect(sockfd, (struct sockaddr*)&ai_addr, sizeof ai_addr)<0 ) fatal("Reconnect socket");
}
struct stun_header {
    uint16_t msg_type;
    uint16_t data_len;
    uint32_t magick;
    uint32_t id[3];
    unsigned char data[];
};
void stun_req(int sockfd, struct peer_addr *peer)
{
    struct stun_header buf= { htons(1), 0, htonl(0x2112A442) }; // 1 - binding request
    int rndfd=open("/dev/urandom", 0);
    read(rndfd, (char*)buf.id, sizeof buf.id);
    close(rndfd);
    for( int attemp=0; attemp<5; attemp++ ) {
        if( write(sockfd, &buf, sizeof buf) != sizeof buf ) fatal("Send STUN request");
        struct {
            struct stun_header hdr;
            unsigned char data[256];
        } rbuf;
        struct pollfd rfd= {sockfd, POLLIN};
        int ret=poll(&rfd, 1, 500);
        if( ret<0 ) fatal("STUN responce waiting");
        if( ret==0 ) continue;
        if( rfd.revents&(POLLERR|POLLNVAL|POLLHUP) ) die("Error during STUN responce wait\n");
        ret= read(sockfd, &rbuf, sizeof rbuf);
        if(ret<0) fatal("Read STUN response");
        if( rbuf.hdr.magick!=buf.magick || memcmp(buf.id, rbuf.hdr.id, sizeof buf.id) ) continue;
        if( ret>=20 ) debug("%x len=%u mag=%x\n", htons(rbuf.hdr.msg_type), htons(rbuf.hdr.data_len), htonl(rbuf.hdr.magick));
        for( int r=0; r<ret; r++ ) debug("%d ", ((unsigned char*)&rbuf) [r]);
        debug("\n=%d\n", ret);
        for( int i=0; i<ret-20 && i<rbuf.hdr.data_len && rbuf.data[i+2]==0; i+= ((rbuf.data[i+2]*256+rbuf.data[i+3]+7)&~3) ) {
            debug("%x %d\n", rbuf.data[i]*256+rbuf.data[i+1], rbuf.data[i+2]*256+rbuf.data[i+3]);
            if(rbuf.data[i+2] || rbuf.data[i+3]==0 || ((i+rbuf.data[i+3]+7)&~3)>ret-20) continue;
            void debugIP(const char *name) {
                debug( "%1$s IPv%2$d %4$d.%5$d.%6$d.%7$d:%3$d\n", name, rbuf.data[i+5]*2+2, rbuf.data[i+6]*256+rbuf.data[i+7],
                 rbuf.data[i+8], rbuf.data[i+9], rbuf.data[i+10], rbuf.data[i+11] );
            }
            switch( rbuf.data[i]*256+rbuf.data[i+1] ) {
            case 0x8020:
                for(int j=0; j<2; j++) rbuf.data[i+6+j]^= ((char*)&rbuf.hdr.magick)[j];
                for(int j=0; j<rbuf.data[i+3]-4; j++) rbuf.data[i+8+j]^= ((char*)&rbuf.hdr.magick)[j];
                debug("XOR-");
            case 1: debugIP("MAPPED-ADDRESS");
                if(rbuf.data[i+5]==1) { // IPv4 only
                    peer->port= *(uint16_t*)(rbuf.data+i+6);
                    peer->in_addr= *(uint32_t*)(rbuf.data+i+8);
                }
                break;
            case 4: debugIP("SOURCE-ADDRESS");  break;
            case 5: debugIP("CHANGED-ADDRESS");  break;
            }
        }
        return;
    }
    die("STUN timeout\n");
}

struct peer_addr mypeer[2], peer[2];
int main(int argn, char **argv)
{
    if( argn<3 || argn>4 ) die("usage: stun_cli <stun_server> <signal_server> [message]\n");
    int sockfd= connect_to_host(argv[1], "3478", SOCK_DGRAM);
    stun_req(sockfd, mypeer+1);
    getlocaddr(sockfd, mypeer);
    for( int r=0; r<sizeof mypeer; r++ ) debug("%d ", ((unsigned char*)&mypeer)[r]);
    debug("\n");
    int signaling_fd= connect_to_host(argv[2], SIGSRV_PORT_STR, SOCK_STREAM);
    if( write(signaling_fd, mypeer, sizeof mypeer)<0 ) fatal("write to signaling server");
    if( read(signaling_fd, peer, sizeof peer)<0 ) fatal("read from signaling server");
    reconnect_to(sockfd, peer);
    for( int r=0; r<sizeof peer; r++ ) debug("%d ", ((unsigned char*)&peer)[r]);
    debug("\n");
    // Wait for hole is ready
    char state=0, ibuf[256];
    struct pollfd rfd= {sockfd, POLLIN};
    for( int i=0; i<100; i++ ) {
        if( i==20 ) reconnect_to(sockfd, peer+1);
        debug("%d ",i);
        int r= write(sockfd, &state, 1);
        debug("w%d %d\n", r, state);
        switch( state ) {
        case 2:
            ;
            int msglen= strlen(argv[3])+1;
            if( msglen==1 ) msglen= 0;
            int r= write(sockfd, argv[3], msglen)>0;
            usleep(200000);
            r= write(sockfd, argv[3], msglen)>0 || r;
            usleep(200000);
            r= write(sockfd, argv[3], msglen)>0 || r;
            usleep(200000);
            r= write(sockfd, argv[3], msglen)>0 || r;
            if( !r ) fatal("Send message");
            debug("Message sent\n");
            return 0;
        }
        while( (r=poll(&rfd, 1, 300)) != 0 ) {
            if( r<0 ) { perror("wait peer packet"); usleep(200000); break;}
            r= read(sockfd, ibuf, sizeof ibuf);
            if( r<0 ) {perror("read"); usleep(150000);}
            else if( r==1 ) {
                debug("readed %d %d\n", r, ibuf[0]);
                if( state==0 ) state= 1;
                else if( ibuf[0] ) state= argn==4? 2: 3;
            } else {
                debug("readed %d\n",r);
                if( r==1 || r>1 && ibuf[r-1]!=0 ) continue;
                if( r ) write(1, ibuf, r-1);
                debug("\n");
                return 0;
            }
        }
    }
    die("Transmission fail\n");
}

