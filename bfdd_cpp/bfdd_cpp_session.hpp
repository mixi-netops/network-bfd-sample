/******************************************************************************/
/*! @addtogroup bfd service on c++ boost
 @file       bfdd_session.hpp
 @brief      BFD セッション（コンテナ）
 ******************************************************************************/

#ifndef PROJECT_BFDD_SESSION_H
#define PROJECT_BFDD_SESSION_H

#include "bfdd_cpp.h"


class Conn;
// supported only BFD:rfc5880 Asynchronouse and not Demand mode.
class BfdSession{
    public:
    BfdSession(uint32_t ipaddr, uint16_t port):BfdSession(){
        memcpy(&peer_addr_.s_addr, &ipaddr, sizeof(uint32_t));
        peer_port_ = port;
    }
    BfdSession():session_state_(BFDSTATE_DOWN),remote_session_state_(BFDSTATE_DOWN),local_discr_(),remote_discr_(BFD_OFF),
            local_diag_(BFD_OFF),desired_min_tx_interval_(BFDDFLT_DESIREDMINTX),required_min_rx_interval_(BFDDFLT_REQUIREDMINRX),remote_min_rx_interval_(BFD_OFF),
            demand_mode_(BFD_OFF),remote_demand_mode_(BFD_OFF),detect_mult_(BFDDFLT_DETECTMULT),auth_type_(BFD_OFF),rcv_auth_seq_(BFD_OFF),xmit_auth_seq_(BFD_OFF),
            auth_seq_known_(BFD_OFF),must_cease_tx_echo_(BFD_OFF),must_terminate_(BFD_OFF),peer_port_(BFD_OFF),peer_addr_(),detect_time_(0),pollbit_on_(BFD_OFF),
            received_min_rx_interval_(BFD_OFF),conn_(NULL){}

    BfdSession(const BfdSession& cpy){
        session_state_          = cpy.session_state_;
        remote_session_state_   = cpy.remote_session_state_;
        local_discr_            = cpy.local_discr_;
        remote_discr_           = cpy.remote_discr_;
        local_diag_             = cpy.local_diag_;
        desired_min_tx_interval_= cpy.desired_min_tx_interval_;
        required_min_rx_interval_=cpy.required_min_rx_interval_;
        remote_min_rx_interval_ = cpy.remote_min_rx_interval_;
        demand_mode_            = cpy.demand_mode_;
        remote_demand_mode_     = cpy.remote_demand_mode_;
        detect_mult_            = cpy.detect_mult_;
        auth_type_              = cpy.auth_type_;
        rcv_auth_seq_           = cpy.rcv_auth_seq_;
        xmit_auth_seq_          = cpy.xmit_auth_seq_;
        auth_seq_known_         = cpy.auth_seq_known_;
        must_cease_tx_echo_     = cpy.must_cease_tx_echo_;
        must_terminate_         = cpy.must_terminate_;
        detect_time_            = cpy.detect_time_;
        peer_addr_              = cpy.peer_addr_;
        peer_port_              = cpy.peer_port_;
        pollbit_on_             = cpy.pollbit_on_;
        received_min_rx_interval_=cpy.received_min_rx_interval_;
        conn_                   = cpy.conn_;
    }
public:
    uint32_t    session_state_;
    uint32_t    remote_session_state_;
    uint32_t    local_discr_;
    uint32_t    remote_discr_;
    uint32_t    local_diag_;
    uint32_t    desired_min_tx_interval_;
    uint32_t    required_min_rx_interval_;
    uint32_t    remote_min_rx_interval_;
    uint32_t    demand_mode_;
    uint32_t    remote_demand_mode_;
    uint32_t    detect_mult_;
    uint32_t    auth_type_;
    uint32_t    rcv_auth_seq_;
    uint32_t    xmit_auth_seq_;
    uint32_t    auth_seq_known_;
    //
    uint32_t    must_cease_tx_echo_;
    uint32_t    must_terminate_;
    uint64_t    detect_time_;
    uint32_t    pollbit_on_;
    struct in_addr  peer_addr_;
    uint16_t    peer_port_;
    uint32_t    received_min_rx_interval_;
    class Conn* conn_;
};

#endif //PROJECT_BFDD_SESSION_H
