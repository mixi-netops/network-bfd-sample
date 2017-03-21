/******************************************************************************/
/*! @addtogroup bfd service
 @file       bfdd.c
 @brief      メインエントリポイント
 ******************************************************************************/
#include "bfdd.h"


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
 * @param[in]     argv     カテゴリアクセサ（プロセス）
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int main(int argc, char** argv) {
    struct sockaddr_in c_addr;
    int    c_addrlen;
    int    serverside_sock;
    int    clientside_sock;
    int    ret;
    int    delay;
    char   bf[ETHER_MAX_LEN] = {0};
    fd_set readfds,fds;
    struct timeval tv;
    uint64_t ptime = get_microsecond();
    //
    printf("bfdd started.\n");
    if (argc != 3){
        fprintf(stderr, "%s <delay(usec)> <interface/ex.127.0.0.1>\n", basename(argv[0]));
        return(1);
    }
    delay = atoi(argv[1]);

    signal(SIGINT, sigint_h);
    // 受信用ソケットと、送信ソケットを生成
    serverside_sock = setup_bfd_socket(argv[2], BFDDFLT_UDPPORT);
    clientside_sock = setup_bfd_socket(argv[2], BFD_SRCPORTINIT);
    if (serverside_sock < 0 || clientside_sock < 0){
        fprintf(stderr, "socket error(%s)\n", strerror(errno));
        close(serverside_sock);
        close(clientside_sock);
        return(-1);
    }
    // 受信側ソケットをノンブロッキング設定
    if (fcntl(serverside_sock, F_SETFL, O_NONBLOCK) != 0){
        fprintf(stderr, "fcntl error(%s)\n", strerror(errno));
        close(serverside_sock);
        close(clientside_sock);
        return(-1);
    }
    // セッション管理テーブル初期化
    if (initialize_bfd_session_table()!=0){
        fprintf(stderr, "init session table error(%s)\n", strerror(errno));
        close(serverside_sock);
        close(clientside_sock);
        return(-1);
    }
    //
    FD_ZERO(&readfds);
    FD_SET(serverside_sock, &readfds);
    //
    while(!QUIT){
        memcpy(&fds, &readfds, sizeof(fd_set));

        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        //
        ret = select(serverside_sock + 1, &fds, NULL, NULL, &tv);
        if (ret > 0){
            // BFDパケット受信イベント.
            memset(bf,0,sizeof(bf));
            c_addrlen = sizeof(c_addr);
            if ((ret = recvfrom(serverside_sock, bf, sizeof(bf), 0, (struct sockaddr *)&c_addr, (socklen_t*)&c_addrlen)) < 0){
                fprintf(stderr, "recvfrom error(%s:%d)\n", strerror(errno), ret);
                break;
            }
            if (recv_bfd_event(clientside_sock, &c_addr, c_addrlen, bf, ret) != 0){
                fprintf(stderr, "recv_event error\n");
            }
        }else{
            if ((get_microsecond() - ptime) > (uint64_t)delay){
                // BFDパケット送信イベント
                // 受信パケットがない時に発行
                if (send_bfd_event(clientside_sock) != 0){
                    fprintf(stderr, "send_event error\n");
                }
                ptime = get_microsecond();
            }
        }
    }
    // セッション管理テーブル解放処理
    finalize_bfd_session_table();
    //
    printf("bfdd stopped.\n");
    return(0);
}

