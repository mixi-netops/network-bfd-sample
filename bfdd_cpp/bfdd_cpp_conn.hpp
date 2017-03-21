/******************************************************************************/
/*! @addtogroup bfd service on c++ boost
 @file       bfdd_conn.hpp
 @brief      udp server - connection
 ******************************************************************************/
#ifndef PROJECT_BFDD_CONN_HPP
#define PROJECT_BFDD_CONN_HPP

#include "bfdd_cpp.h"


class Conn {
    friend class Server;
    friend class BfdSessionManager;
public:
    virtual ~Conn(){
        if (client_side_sock_ != NULL){
            client_side_sock_->close();
            delete client_side_sock_;
        }
        client_side_sock_ = NULL;
    }
public:
    //
    BAIP::udp::endpoint& udpendp(void){ return(udpendp_); }
    BAIP::udp::endpoint& udpendp_remote(void){ return(udpendp_remote_); }
    char* rbuf(void){ return(readbuf_); }
    size_t rsiz(void){ return(sizeof(readbuf_)); }
    //
    int write(void* bf, size_t size){
        if (size <= sizeof(writebuf_)){
            memcpy(writebuf_, bf, size);
            sock_ref_->async_send_to(
                    boost::asio::buffer(writebuf_,size), udpendp_,
                    boost::bind(&Conn::handle_write, this, boost::asio::placeholders::error));
            return(RET_OK);
        }
        fprintf(stderr, "write failed.(%zu : %lu)\n", size, sizeof(writebuf_));
        return(RET_WARN);
    }
    void setudpendp_remote(BAIP::udp::endpoint udpendp){
        udpendp_remote_ = udpendp;
    }
private:
    Conn(boost::asio::io_service& ios, BAIP::udp::socket* sock):
            client_side_sock_(NULL),
            sock_ref_(sock),
            ioservice_(&ios){
        memset(writebuf_,0,sizeof(writebuf_));
        memset(readbuf_,0,sizeof(readbuf_));
    }
    Conn(){}
    void handle_write(const boost::system::error_code& err){}
    void handle_read(BAIP::udp::endpoint& endp, const boost::system::error_code& err, size_t size) { }
    //
    static Conn* create_server_side_connection(boost::asio::io_service& ios, BAIP::udp::socket* sock){
        Conn* pcon = new Conn(ios, sock);
        if (pcon){
            pcon->sock_ref_ = sock;
        }
        return(pcon);
    }
    static Conn* create_client_side_connection(boost::asio::io_service& ios, BAIP::udp::endpoint& endp_l, BAIP::udp::endpoint& endp_r){
        Conn* pcon = new Conn(ios, NULL);
        if (pcon){
            pcon->client_side_sock_ = new BAIP::udp::socket(ios);
            if (pcon->client_side_sock_){
                pcon->client_side_sock_->open(endp_l.protocol());
                pcon->client_side_sock_->set_option(boost::asio::ip::udp::socket::send_buffer_size(SOCKET_SENDBUFFER_SIZE));
                pcon->client_side_sock_->set_option(boost::asio::ip::udp::socket::reuse_address(true));
                pcon->client_side_sock_->set_option(boost::asio::ip::unicast::hops(BFD_1HOPTTLVALUE));
                pcon->client_side_sock_->bind(endp_l);
                pcon->sock_ref_ = pcon->client_side_sock_;
                pcon->udpendp_ = endp_r;
            }
        }
        return(pcon);
    }
private:
    //
    BAIP::udp::socket   *client_side_sock_;
    BAIP::udp::socket   *sock_ref_;
    boost::asio::io_service *ioservice_;
    BAIP::udp::endpoint udpendp_;
    BAIP::udp::endpoint udpendp_remote_;
    char                readbuf_[CONNECTION_BUFFER_SIZE];
    char                writebuf_[CONNECTION_BUFFER_SIZE];
}; // class Conn

#endif //PROJECT_BFDD_CONN_HPP
