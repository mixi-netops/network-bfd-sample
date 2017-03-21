/******************************************************************************/
/*! @addtogroup bfd service
 @file       bfdd_session.c
 @brief      BFDソケットユーティリティ
 ******************************************************************************/
#include "bfdd.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


/** *************************************************************
 * bfdd用ソケット生成\n
 *
 * @param[in]     port           バインドポート
 * @result  int   ソケットdiscriptor、エラー時(-1)
 ************************************************************* */
int setup_bfd_socket(const char* ipv4, unsigned short port){
    struct sockaddr_in addr;
    int sock,ttl=BFD_1HOPTTLVALUE;
    memset(&addr, 0, sizeof(addr));

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    //
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ipv4);
    addr.sin_port = htons(port);
    //
    if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0){
        fprintf(stderr, "bind error(%s)\n", strerror(errno));
        return(-1);
    }
    // TTLを0xff
    if (setsockopt(sock, IPPROTO_IP, IP_TTL,(char*)&ttl,sizeof(ttl))<0){
        fprintf(stderr, "setsockopt(%s)", strerror(errno));
        return(-1);
    }
    return(sock);
}