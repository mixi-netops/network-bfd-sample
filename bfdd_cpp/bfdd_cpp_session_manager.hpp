/******************************************************************************/
/*! @addtogroup bfd service on c++ boost
 @file       bfdd_session_manager.hpp
 @brief      BFD セッション マネージャ
 ******************************************************************************/


#ifndef PROJECT_BFDD_SESSION_MANAGER_HPP
#define PROJECT_BFDD_SESSION_MANAGER_HPP
#include "bfdd_cpp.h"

//
#include "bfdd_cpp_session.hpp"
#include "bfdd_cpp_server.hpp"
#include "bfdd_cpp_conn.hpp"

typedef std::map<ULONGLONG, BfdSession>           SESSION;
typedef std::map<ULONGLONG, BfdSession>::iterator SESSIONITR;
//
class BfdSessionManager: public BfdTrigger{
public:
    BfdSessionManager(uint64_t delay,const char* interface,boost::asio::io_service* ioservice){
        ioservice_ = ioservice;
        abort_ = 0;
        interface_ = interface;
        local_discriminator_incr_ = 0;
        threads_.create_thread(boost::bind(&BfdSessionManager::on_bfd_thread, this, delay));
    }
    virtual ~BfdSessionManager(){
        threads_.join_all();
    }
public:
    void run(void){
        Server server(*ioservice_, interface_.c_str(), BFDDFLT_UDPPORT, this);
        ioservice_->run();
    }
    void abort(void){
        abort_++;
    }
    boost::mutex* mutex(void){
        return(&mutex_);
    }
    void subscribe(uint32_t ipaddr, uint16_t port){
        boost::unique_lock<boost::mutex> lock(mutex_);
        subscribe_unsafe(ipaddr, port);
    }
    void subscribe_unsafe(uint32_t ipaddr, uint16_t port){
        bfd_sessions_[MAKE_ULL(ipaddr, port)] = BfdSession(ipaddr, port);
    }
    void unsubscribe(uint32_t ipaddr, uint16_t port){
        boost::unique_lock<boost::mutex> lock(mutex_);
        SESSIONITR  itr = bfd_sessions_.find(MAKE_ULL(ipaddr, port));
        if (itr != bfd_sessions_.end()){
            bfd_sessions_.erase(itr);
        }
    }
    BfdSession* find_unsafe(uint32_t ipaddr, uint16_t port){
        SESSIONITR  itr;
        if ((itr = bfd_sessions_.find(MAKE_ULL(ipaddr, port))) == bfd_sessions_.end()){ return(NULL); }
        return(&(itr->second));
    }
    void transmit_unsafe(BfdSession* psess){
        printf("transmit_unsafe(%p/ %p)\n", (void*)pthread_self(), psess);
        bfd_t   bfd;
        memset(&bfd, 0,sizeof(bfd));
        bfd.h.head.vers   = BFD_VERSION;
        bfd.h.head.diag   = psess->local_diag_;
        if (psess->local_discr_!=0){
            bfd.u.bit.state  = psess->session_state_;
        }else{
            bfd.u.bit.state  = BFDSTATE_DOWN;
        }

        if (psess->pollbit_on_ == BFD_ON){
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
        bfd.detect_mult = psess->detect_mult_;
        bfd.length = sizeof(bfd);
        bfd.my_discr = psess->local_discr_;
        bfd.your_discr = psess->remote_discr_;
        bfd.min_tx_int = htonl(psess->desired_min_tx_interval_);
        bfd.min_rx_int = htonl(psess->required_min_rx_interval_);
        bfd.min_echo_rx_int = htonl(psess->received_min_rx_interval_);
        if ((bfd.your_discr == 0 || bfd.my_discr == 0) && bfd.u.bit.state != BFDSTATE_DOWN){
            fprintf(stderr, "transmit skip if zero\n");
            return;
        }
        if (psess->conn_ != NULL){
            psess->conn_->write(&bfd, sizeof(bfd));
        }else{
            fprintf(stderr, "transmit skip(connection is null)\n");
        }
    }
    void on_bfd_recieved(Conn* pcon, bfd_ptr bfd){
        auto endp_l = pcon->udpendp();
        auto endp_r = pcon->udpendp_remote();
        auto ipv4 = endp_r.address().to_v4().to_ulong();
        auto port = endp_r.port();
        BfdSession *psess = NULL;
        printf("ipv4: %08x/ port : %u\n", ipv4,port);
        //
        if (bfd->your_discr != BFD_OFF){
            boost::unique_lock<boost::mutex> lock(*mutex());
            if (find_unsafe(ipv4, port)==NULL){
                subscribe_unsafe(ipv4, port);
                if ((psess = find_unsafe(ipv4,port)) != NULL){
                    psess->remote_discr_ = bfd->my_discr;
                    psess->local_discr_  = bfd->your_discr;
                    psess->session_state_= BFDSTATE_DOWN;
                    transmit_unsafe(psess);
                }
                return;
            }
        }
        boost::with_lock_guard(*mutex(), [&]{
            if ((psess = find_unsafe(ipv4, port)) == NULL){
                if (bfd->u.bit.state == BFDSTATE_DOWN){
                    subscribe_unsafe(ipv4, port);
                    psess = find_unsafe(ipv4,port);
                }
                if (psess == NULL){ return; }
            }

            // 新規セッションをローカルアドレス、リモートアドレスを指定して準備
            if (psess->conn_ == NULL){
                endp_l.port(endp_r.port());
                endp_r.port(BFDDFLT_UDPPORT);

                psess->conn_ = Conn::create_client_side_connection(*ioservice_,endp_l,endp_r);
                psess->local_discr_ = htonl(++local_discriminator_incr_);
                if (local_discriminator_incr_ > 0x7fffffff){
                    local_discriminator_incr_ = 0;
                }
            }

            psess->remote_session_state_ = bfd->u.bit.state;
            psess->remote_demand_mode_ = bfd->u.bit.demand;
            psess->remote_discr_ = bfd->my_discr;
            psess->remote_min_rx_interval_ = bfd->min_rx_int;
            psess->received_min_rx_interval_ = MIN(psess->received_min_rx_interval_, bfd->min_rx_int);
            if (psess->remote_session_state_ != 1)
                fprintf(stderr, "my: %d / re: %d\n", psess->session_state_, psess->remote_session_state_);
            if (bfd->u.bit.final){
                psess->must_terminate_ = BFD_ON;
            }
            uint64_t curms = get_microsecond();
            psess->detect_time_ = curms + (bfd->detect_mult * MAX(bfd->min_rx_int,psess->desired_min_tx_interval_));

            if (psess->session_state_ == BFDSTATE_ADMINDOWN){
                return;
            }
            if (psess->remote_session_state_ == BFDSTATE_ADMINDOWN){
                if (psess->session_state_ != BFDSTATE_DOWN){
                    psess->local_diag_ = BFDDIAG_NEIGHBORSAIDDOWN;
                    psess->session_state_ = BFDSTATE_DOWN;
                }
            }else{

                if (psess->session_state_ == BFDSTATE_DOWN){
                    if (psess->remote_session_state_ == BFDSTATE_DOWN){
                        psess->session_state_ = BFDSTATE_INIT;
                    }else if (psess->remote_session_state_ == BFDSTATE_INIT){
                        psess->session_state_ = BFDSTATE_UP;
                    }
                }else if (psess->session_state_ == BFDSTATE_INIT){
                    if (psess->remote_session_state_ == BFDSTATE_INIT || psess->remote_session_state_ == BFDSTATE_UP){
                        psess->session_state_ = BFDSTATE_UP;
                    }
                }else{  // session_staete_ == UP
                    if (psess->remote_session_state_ == BFDSTATE_DOWN){
                        psess->local_diag_ = BFDDIAG_NEIGHBORSAIDDOWN;
                        psess->session_state_ = BFDSTATE_DOWN;
                    }
                }
            }
            if (bfd->u.bit.poll == BFD_ON){
                psess->pollbit_on_ = BFD_ON;
                // system must transmit a ctrl packet
                // with poll bit set as soon as practicable.
                transmit_unsafe(psess);
            }
        });
    }
    void on_bfd_thread(uint64_t delay){
        printf("BfdSessionManager::on_bfd_thread(%u)\n", (unsigned int)delay);
        //
        while (!abort_) {
            usleep(delay);
            uint64_t curms = get_microsecond();
            {   boost::unique_lock<boost::mutex> lock(mutex_);
                SESSIONITR  itr;

                for(itr = bfd_sessions_.begin();itr != bfd_sessions_.end();++itr){
                    if ((itr->second).detect_time_ > curms){
                        if ((itr->second).must_cease_tx_echo_ == BFD_OFF &&
                            (itr->second).must_terminate_ == BFD_OFF &&
                            // (itr->second).session_state_ == BFDSTATE_UP &&
                            (itr->second).remote_discr_ != 0 &&
                            (itr->second).remote_min_rx_interval_ != 0){
                            // BFD protocol , transmit..
                            transmit_unsafe(&(itr->second));
                        }
                    }
                }
            }
        }
        printf("BfdSessionManager::on_bfd_thread. completed(%p)\n", (void*)pthread_self());
    }
private:
    boost::mutex        mutex_;
    boost::thread_group threads_;
    boost::asio::io_service *ioservice_;
    SESSION             bfd_sessions_;
    std::string         interface_;
    int                 abort_;
    uint64_t            local_discriminator_incr_;
};


#endif //PROJECT_BFDD_SESSION_MANAGER_HPP
