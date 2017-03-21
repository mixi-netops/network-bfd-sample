/******************************************************************************/
/*! @addtogroup bfd service on c++ boost
 @file       bfdd_server.hpp
 @brief      udp server
 ******************************************************************************/
#ifndef PROJECT_BFDD_SERVER_HPP
#define PROJECT_BFDD_SERVER_HPP
#include "bfdd_cpp.h"

#include "bfdd_cpp_conn.hpp"
#include "bfdd_cpp_session_manager.hpp"

class Server {
public:
    Server(boost::asio::io_service& io_service, const char* interface, int port, BfdTrigger* psessmng) :
            udpsock_(io_service),
            ios_(io_service),
            psessmng_(psessmng){
        //
        port_ = port;
        BAIP::udp::endpoint endpoint(boost::asio::ip::address::from_string(interface), port_);
        udpsock_.open(endpoint.protocol());
        udpsock_.set_option(boost::asio::ip::udp::socket::reuse_address(true));
        udpsock_.set_option(boost::asio::ip::udp::socket::receive_buffer_size(4*1024*1024));
        udpsock_.bind(endpoint);
        //
        start();
        printf("Server::Server(%u)\n",port_);
    }
private:
    void start(){
        printf("Server::start(%u)\n",port_);
        Conn* pConn =  Conn::create_server_side_connection(udpsock_.get_io_service(),&udpsock_);
        udpsock_.async_receive_from(boost::asio::buffer(pConn->rbuf(), pConn->rsiz()),
                                    udpendp_,
                                    boost::bind(&Server::recieve, this, pConn,boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred)
        );
    }
    void recieve(Conn* pcon, const boost::system::error_code& err, std::size_t size){
        printf("Server::handle_udp(%lu)\n",size);
        if (!err) {
            pcon->setudpendp_remote(udpendp_);
            bfd_ptr bfd = (bfd_ptr)pcon->rbuf();
            while(1){
                if (size != BFD_MINPKTLEN){ break; }
                if (bfd->u.bit.multipoint != BFD_OFF) { break; }  // unsupported multi point.
                if (bfd->h.head.vers != BFD_VERSION) { break; }
                if (bfd->u.bit.auth != BFD_OFF) { break; }        // unsupported auth.
                if (bfd->my_discr == BFD_OFF) { break; }
                if (bfd->detect_mult == BFD_OFF) { break; }
                if (bfd->your_discr == BFD_OFF && !(bfd->u.bit.state == BFDSTATE_DOWN || bfd->u.bit.state == BFDSTATE_ADMINDOWN)){ break; }
                //
                psessmng_->on_bfd_recieved(pcon, bfd);
                break;
            }
        }else{
            if (pcon){ delete pcon; }
        }
        start();
    }
private:
    BAIP::udp::socket           udpsock_;
    boost::asio::io_service&    ios_;
    BAIP::udp::endpoint         udpendp_;
    int                         port_;
    BfdTrigger*                 psessmng_;
};

#endif //PROJECT_BFDD_SERVER_HPP
