/******************************************************************************/
/*! @addtogroup bfd service
 @file       bfdd_event.c
 @brief      BFDイベント実装
  ******************************************************************************/
#include "bfdd.h"

static uint64_t local_discriminator_incr = 0;
static int iterate_on_session(int sock, bfd_sess_ptr psess, void* udata);


/** *************************************************************
 * bfdパケット受信イベント\n
 * セッション情報を更新する
 *
 * @param[in]     clientside_socket   クライアントサイドソケット（送信ソケットdescriptor)
 * @param[in]     c_addr              クライアントアドレス
 * @param[in]     c_addrlen           ↑バッファサイズ
 * @param[in]     pkt                 ユーザパケット先頭アドレス
 * @param[in]     len                 ↑パケット長
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int recv_bfd_event(int clientside_socket, struct sockaddr_in * c_addr, int c_addrlen, char* pkt, int len){
    bfd_ptr bfd = (bfd_ptr)pkt;
    bfd_sess_t  sess;

    if (len != BFD_MINPKTLEN){ return(RET_NG); }
    if (bfd->u.bit.multipoint != BFD_OFF) { return(RET_NG); }  // unsupported multi point.
    if (bfd->h.head.vers != BFD_VERSION) { return(RET_NG); }
    if (bfd->u.bit.auth != BFD_OFF) { return(RET_NG); }        // unsupported auth.
    if (bfd->my_discr == BFD_OFF) { return(RET_NG); }
    if (bfd->detect_mult == BFD_OFF) { return(RET_NG); }
    if (bfd->your_discr == BFD_OFF && !(bfd->u.bit.state == BFDSTATE_DOWN || bfd->u.bit.state == BFDSTATE_ADMINDOWN)){
        return(RET_NG);
    }
    // ----------------------
    // ステートマシン処理部
    // ----------------------
    // 宛先が指定されているのにもかかわらず
    // 対象セッションがない場合、DOWNステータスで作成する
    if (bfd->your_discr != BFD_OFF){
        if (find_session(&sess, c_addr, c_addrlen) != RET_OK){
            init_session(&sess, c_addr, c_addrlen);
            //
            sess.remote_discr = bfd->my_discr;
            sess.local_discr = bfd->your_discr;
            sess.session_state = BFDSTATE_DOWN;
            //
            insert_session(&sess,c_addr, c_addrlen);
            return(iterate_on_session(clientside_socket,&sess, NULL));
        }
    }
    if (find_session(&sess, c_addr, c_addrlen) != RET_OK){
        if (bfd->u.bit.state == BFDSTATE_DOWN){
            init_session(&sess, c_addr, c_addrlen);
            insert_session(&sess,c_addr, c_addrlen);
        }
        if (find_session(&sess, c_addr, c_addrlen) != RET_OK){
            return(RET_NG);
        }
    }
    // 新規セッション
    if (sess.attached == BFD_OFF){
        sess.local_discr = htonl(++local_discriminator_incr);
        if (local_discriminator_incr > 0x7fffffff){
            local_discriminator_incr = 0;
        }
        sess.attached = BFD_ON;
    }

    sess.remote_session_state = bfd->u.bit.state;
    sess.remote_demand_mode = bfd->u.bit.demand;
    sess.remote_discr = bfd->my_discr;
    sess.remote_min_rx_interval = bfd->min_rx_int;
    sess.received_min_rx_interval = MIN(sess.received_min_rx_interval, bfd->min_rx_int);
    if (bfd->u.bit.final){
        sess.must_terminate = BFD_ON;
    }
    uint64_t curms = get_microsecond();
    sess.detect_time = curms + (bfd->detect_mult * MAX(bfd->min_rx_int,sess.desired_min_tx_interval));

    if (sess.session_state == BFDSTATE_ADMINDOWN){
        return(RET_NG);
    }
    if (sess.remote_session_state == BFDSTATE_ADMINDOWN){
        if (sess.session_state != BFDSTATE_DOWN){
            sess.local_diag = BFDDIAG_NEIGHBORSAIDDOWN;
            sess.session_state = BFDSTATE_DOWN;
        }
    }else{
        if (sess.session_state == BFDSTATE_DOWN){
            if (sess.remote_session_state == BFDSTATE_DOWN){
                sess.session_state = BFDSTATE_INIT;
            }else if (sess.remote_session_state == BFDSTATE_INIT){
                sess.session_state = BFDSTATE_UP;
            }
        }else if (sess.session_state == BFDSTATE_INIT){
            if (sess.remote_session_state == BFDSTATE_INIT || sess.remote_session_state == BFDSTATE_UP){
                sess.session_state = BFDSTATE_UP;
            }else{
                sess.session_state = BFDSTATE_DOWN;
            }
        }else{  // session_staete_ == UP
            if (sess.remote_session_state == BFDSTATE_DOWN){
                sess.local_diag = BFDDIAG_NEIGHBORSAIDDOWN;
                sess.session_state = BFDSTATE_DOWN;
            }
        }
    }
    if (bfd->u.bit.poll == BFD_ON){
        sess.pollbit_on = BFD_ON;
        // system must transmit a ctrl packet
        // with poll bit set as soon as practicable.
        iterate_on_session(clientside_socket,&sess, NULL);
    }
    return(insert_session(&sess,c_addr, c_addrlen));
}

/** *************************************************************
 * bfd送信用イベント\n
 * セッション情報から、対向機器へbfdを送信する
 *
 * @param[in]     sock                クライアントサイドソケット（送信ソケットdescriptor)
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int send_bfd_event(int sock){
    // 認識している全ての対向機器へbfdステータスパケットを
    // イテレータループ中に送出する
    iterate_all_session(sock, iterate_on_session, NULL);

    return(0);
}
/** *************************************************************
 * セッションイテレータ\n
 * 全てのセッション数分コールバック
 *
 * @param[in]     sock            クライアントサイドソケット（送信ソケットdescriptor)
 * @param[in]     psess           セッション構造体ポインタ
 * @param[in]     udata           コールバックユーザデータ
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int iterate_on_session(int sock, bfd_sess_ptr psess, void* udata){
    UNUSED(udata);
    int     ret;
    bfd_t   bfd;
    memset(&bfd, 0,sizeof(bfd));
    bfd.h.head.vers   = BFD_VERSION;
    bfd.h.head.diag   = psess->local_diag;
    if (psess->local_discr!=0){
        bfd.u.bit.state  = psess->session_state;
    }else{
        bfd.u.bit.state  = BFDSTATE_DOWN;
    }
    if (psess->attached == BFD_OFF){
        return(0);
    }
    if (psess->pollbit_on == BFD_ON){
        bfd.u.bit.poll   = BFD_OFF;
        bfd.u.bit.final  = BFD_ON;
    }else{
        bfd.u.bit.poll   = BFD_OFF;
        bfd.u.bit.final  = BFD_OFF;
    }
    bfd.u.bit.cpi = BFD_ON;
    bfd.u.bit.auth = BFD_OFF;
    bfd.u.bit.demand = BFD_OFF;
    bfd.u.bit.multipoint = BFD_OFF;
    bfd.detect_mult = psess->detect_mult;
    bfd.length = sizeof(bfd);
    bfd.my_discr = psess->local_discr;
    bfd.your_discr = psess->remote_discr;
    bfd.min_tx_int = htonl(psess->desired_min_tx_interval);
    bfd.min_rx_int = htonl(psess->required_min_rx_interval);
    bfd.min_echo_rx_int = htonl(psess->received_min_rx_interval);
    if ((bfd.your_discr == 0 || bfd.my_discr == 0) && bfd.u.bit.state != BFDSTATE_DOWN){
        return(0);
    }
    if (psess->remote_session_state != 1)
        fprintf(stderr, "my: %d / re: %d\n", psess->session_state, psess->remote_session_state);

    if ((ret = sendto(sock, &bfd, sizeof(bfd), 0, (struct sockaddr *)&psess->client_addr, sizeof(psess->client_addr)))<0){
        fprintf(stderr, "err sendto(%s:%d)", strerror(errno), ret);
    }
    //
    return(ret>0?0:-1);
}