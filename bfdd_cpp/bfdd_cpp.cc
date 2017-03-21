/******************************************************************************/
/*! @addtogroup bfd service on c++ boost
 @file       bfdd_cpp.c
 @brief      BFD実装
 ******************************************************************************/
#include "bfdd_cpp_session.hpp"
#include "bfdd_cpp_session_manager.hpp"

// main entry.
int main(int argc, char **argv){
    if (argc != 3){
        fprintf(stderr, "%s <delay(usec)> <interface/ex.127.0.0.1>\n", basename(argv[0]));
        return(1);
    }
    boost::asio::io_service ios;
    BfdSessionManager       manager(atoi(argv[1]), argv[2], &ios);
    manager.run();

    return(0);
}