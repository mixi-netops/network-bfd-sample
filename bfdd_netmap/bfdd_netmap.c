/******************************************************************************/
/*! @addtogroup bfd service on c with netmap
 @file       bfdd_netmap.c
 @brief      メイン
 ******************************************************************************/
#include "bfdd_netmap.h"


static int QUIT = 0;
//
void sigint_h(int sig) {
    UNUSED(sig);
    QUIT++;
}
//
/** *************************************************************
 * メインエントリポイント
 *
 * @param[in]     argc
 * @param[in]     argv   
 * @result  0=成功、0!=エラー
 ************************************************************* */
int main(int argc, char** argv) {
    int    ret,n;
    int    delay;
    char   bf[ETHER_MAX_LEN] = {0};
    struct nmreq   base_nmd;
    struct pollfd  pfds[1];
    struct timeval tv;
    uint64_t ptime = get_microsecond();
    process_handle_t proc;
    bzero(&proc, sizeof(proc)); 
    //
    printf("bfdd_netmap started.\n");
    if (argc != 3){
        fprintf(stderr, "%s <delay(usec)> <interface/ex.netmap:eth5>\n", basename(argv[0]));
        return(1);
    }
    delay = atoi(argv[1]);

    signal(SIGINT, sigint_h);
    bzero(&base_nmd, sizeof(base_nmd));
    if ((proc.nmd = nm_open(argv[2], &base_nmd, 0 ,NULL)) == NULL){
        fprintf(stderr, "failed. nm_open(%s)\n", argv[2]);
        return(-1);
    }
    //
    while(!QUIT){
        pfds[0].fd = proc.nmd->fd;
        pfds[0].events = (POLLIN);
        ioctl(pfds[0].fd, NIOCRXSYNC, NULL);
        // wait
        ret = poll(pfds, 1, 200);
        if (ret < 0 && pfds[0].revents & POLLERR) { break; }
        if (ret > 0){
            // パケットオペレート
            if (pfds[0].revents & POLLIN){
                move(&proc, recv_packet);
            }
        }else{
            // timeout時(200usec)
            if ((get_microsecond() - ptime) > (uint64_t)delay){
                // BFDパケット送信イベント
                // 受信パケットがない時に発行
                if (send_packet(0, &proc) != 0){
                    fprintf(stderr, "send_packet error\n");
                }
                ptime = get_microsecond();
            }
        }
    }
    nm_close(proc.nmd);
    //
    printf("bfdd stopped.\n");
    return(0);
}
int recv_packet(int id, process_handle_ptr ph){
    struct ether_header *eh = (struct ether_header *) ph->req;
    struct ip *ip = (struct ip *) (ph->req + sizeof(struct ether_header) + LEN_VLAN(eh));
    struct udphdr *udp = (struct udphdr *) (ph->req + sizeof(struct ether_header) + LEN_VLAN(eh) + (ip->ip_hl * 4));
    bfd_ptr bfd =  (bfd_ptr)(ph->req + sizeof(struct ether_header) + LEN_VLAN(eh) + (ip->ip_hl * 4) + (sizeof(struct udphdr)));
    uint32_t MYDSCR = 10000;
    uint32_t TMPDSCR= 0;
    // 
    swap_mac(eh);
    swap_ip(ip, 0xff);
    TMPDSCR = bfd->your_discr;
    udp->uh_sum = 0;
    printf("state : %u/your: %u/my  : %u\n", bfd->u.bit.state, ntohl(bfd->your_discr), ntohl(bfd->my_discr));


    swap_bfd(bfd);
    // state machine.
    if (bfd->u.bit.state == BFDSTATE_DOWN){
        if (!TMPDSCR){
            bfd->u.bit.state = BFDSTATE_INIT;
            bfd->my_discr = MYDSCR;
        }else{
            bfd->u.bit.state = BFDSTATE_DOWN;
            bfd->my_discr = MYDSCR;
        }
    }else if (bfd->u.bit.state == BFDSTATE_ADMINDOWN){
        bfd->u.bit.state = BFDSTATE_DOWN;
    }else if (bfd->u.bit.state == BFDSTATE_INIT){
        bfd->u.bit.state = BFDSTATE_UP;
        bfd->my_discr = MYDSCR;
    }else if (bfd->u.bit.state == BFDSTATE_UP){
        bfd->u.bit.state = BFDSTATE_UP;
        bfd->my_discr = MYDSCR;
    }else{
        bfd->u.bit.state = BFDSTATE_DOWN;
        bfd->my_discr = MYDSCR;
        bfd->your_discr = 0;
    }
    swap_index(ph); 
 
    return(0);
}

int send_packet(int id, process_handle_ptr ph){
    // TBD: パケット受信タイミングと別のタイミングで
    //      送信したくなった場合は、ここに処理を追加
    return(0);
}
