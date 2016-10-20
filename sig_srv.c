/* Simple signal server for two clients.
   Wait info from two peers and exchange it.
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
#include <netinet/tcp.h>

int main(int argn, char **argv)
{
    int lfd= socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(lfd<0) fatal("Create socket");
    struct addrinfo hints={AI_PASSIVE, AF_INET, SOCK_STREAM, IPPROTO_TCP, 0,0,0,0}, *locaddr;
    int r;
    if( (r=getaddrinfo(0, SIGSRV_PORT_STR, &hints, &locaddr))<0 ) die("getaddrinfo(0,%s,...): %s\n", SIGSRV_PORT_STR, gai_strerror(r));
    for( r=0; r < locaddr->ai_addrlen; r++ ) debug("%d.", ((unsigned char*)locaddr->ai_addr)[r]);
    debug("\b \n");
    if( bind(lfd, locaddr->ai_addr, locaddr->ai_addrlen) ) fatal("Bind socket");
    freeaddrinfo(locaddr);
    r=1;
    setsockopt(lfd, IPPROTO_TCP, TCP_NODELAY, (void *)&r, sizeof(r));

    if( listen(lfd, 2) < 0 ) fatal("listen");

    int peers[2]= {-1,-1}, npeers=0;
    for(;;){
        int i;
        if( npeers < 2 ) {
            i= peers[0] > 0; // index 0 or 1
            debug("wait %d clients\n", 2-npeers);
            peers[i]= accept(lfd, 0, 0);
            if( peers[i]<0 ) perror("accept");
            else npeers++;
        } else {
            struct pollfd rfd[2]= {{peers[0], POLLIN}, {peers[1], POLLIN}};
            debug("wait peers info %d %d\n", peers[0], peers[1]);
            char buf[2][64];
            int len[2];
            if( poll(rfd, 2, -1)<0 ) fatal("poll");
            for( i=0;i<2;i++ ) if( rfd[i].revents & (POLLERR|POLLNVAL|POLLHUP) ) {
                close(peers[i]);
                peers[i]= -1;
                npeers--;
                debug("peer error\n");
            }
            if( npeers < 2 ) continue;
            debug("connecting peers\n");
            if( (len[0]=read(peers[0], buf[0], sizeof buf[0]))<0
             || (len[1]=read(peers[1], buf[1], sizeof buf[1]))<0 ) perror("read");
            else {
                debug("writing responces\n");
                if( write(peers[0], buf[1], len[1])<0
                 || write(peers[1], buf[0], len[0])<0 ) perror("write");
            }
            close(peers[0]);
            close(peers[1]);
            peers[0]= peers[1]= -1;
            npeers= 0;
        }
    }
}
