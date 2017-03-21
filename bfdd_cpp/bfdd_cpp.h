/******************************************************************************/
/*! @addtogroup bfd service on c++ boost
 @file       bfdd_cpp.h
 @brief      c++用bfd共通ヘッダ
  ******************************************************************************/

#ifndef PROJECT_BFDD_CPP_H
#define PROJECT_BFDD_CPP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <dirent.h>
#include <libgen.h>
#include <netdb.h>

#include <map>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/with_lock_guard.hpp>

namespace BAIP = boost::asio::ip;


/*! @name basic defineded... */
/* @{ */
#define		RET_OK			(0)						/*!< 処理結果：成功 */
#define		RET_NG			(-1)					/*!< 処理結果：エラー */
#define		RET_WARN	    (1)						/*!< 処理結果：ワーニング */
#define		RET_MOREDATA	(2)						/*!< 処理結果：モアデータ */


#define UNUSED(x)   (void)x
#ifndef ETHER_MAX_LEN
#define ETHER_MAX_LEN           1518
#endif
#define CONNECTION_BUFFER_SIZE      2048            /*!< コネクション毎バッファサイズ */
#define SOCKET_SENDBUFFER_SIZE      (4*1024*1024)

#define BFDCLIENT_SESSION_DELAY     1000000             /*!< BFD:送信間隔  */

#define BFDDFLT_DEMANDMODE          false
#define BFDDFLT_DETECTMULT          ((uint8_t)3)
#define BFDDFLT_DESIREDMINTX        1000000
#define BFDDFLT_REQUIREDMINRX       1000000
#define BFDDFLT_UDPPORT             ((uint16_t)3784)

#define BFD_VERSION                 1                   /*!< BFD:対応バージョン:1  */
#define BFD_MINPKTLEN               24    /* Minimum length of control packet */
#define BFD_MINPKTLEN_AUTH          26    /* Minimum length of control packet with Auth section */
#define BFD_1HOPTTLVALUE            255
#define BFD_DOWNMINTX               1000000
#define BFD_HASHSIZE                251        /* Should be prime */
#define BFD_MKHKEY(val)             ((val) % BFD_HASHSIZE)
#define BFD_SRCPORTINIT             49152               /*!< BFD:udp ソースポート  */
#define BFD_SRCPORTMAX              65536
/* @} */

/*! @name bfd state machine status.... */
#define BFDSTATE_ADMINDOWN          0                   /*!< BFD:ステート定義：管理者終了  */
#define BFDSTATE_DOWN               1                   /*!< BFD:ステート定義：ダウン、初期状態  */
#define BFDSTATE_INIT               2                   /*!< BFD:ステート定義：初期化中  */
#define BFDSTATE_UP                 3                   /*!< BFD:ステート定義：接続中  */
/* @} */

/*! @name bfd diag status.... */
#define BFD_NODIAG                  0
#define BFDDIAG_DETECTTIMEEXPIRED   1
#define BFDDIAG_ECHOFAILED          2
#define BFDDIAG_NEIGHBORSAIDDOWN    3
#define BFDDIAG_FWDPLANERESET       4
#define BFDDIAG_PATHDOWN            5
#define BFDDIAG_CONCATPATHDOWN      6
#define BFDDIAG_ADMINDOWN           7
#define BFDDIAG_RCONCATPATHDOWNW    8
/* @} */

/*! @name bfd on/off.... */
#define BFD_ON                      (1)
#define BFD_OFF                     (0)
/* @} */

#ifndef MIN
# define MIN(a,b) (a<b?a:b)
#endif
#ifndef MAX
# define MAX(a,b) (a>b?a:b)
#endif
#ifndef ULONGLONG
#define ULONGLONG unsigned long long
#endif
#ifndef LONGLONG
#define LONGLONG long long
#endif
#ifndef ULONG
#define ULONG unsigned long
#endif
#ifndef USHORT
#define USHORT unsigned short
#endif
#ifndef U8
#define U8 unsigned char
#endif

#define MAKE_ULL(hi,lo) ((((ULONGLONG)((ULONG)hi & 0xffffffff))<<32 )|((ULONGLONG)((ULONG)lo & 0xffffffff)))
#define MAKE_UL(hi,lo) ((((ULONG)((USHORT)hi & 0xffff))<<16 )|((ULONG)((USHORT)lo & 0xffff)))
#define MAKE_DIGIT(hi,lo) ((((U8)((U8)hi & 0x0f))<<4 )|((U8)((U8)lo & 0x0f)))



/*! @struct bfd
 @brief
 bfdパケット\n
 */
typedef struct bfd{
    union _h {
        struct _head{
            uint8_t  diag:5;
            uint8_t  vers:3;
        }head;
        uint8_t flags;
    }h;
    union _u {
        struct _bit{
            uint8_t  multipoint:1;
            uint8_t  demand:1;
            uint8_t  auth:1;
            uint8_t  cpi:1;
            uint8_t  final:1;
            uint8_t  poll:1;
            uint8_t  state:2;
        }bit;
        uint8_t flags;
    }u;
    uint8_t       detect_mult;
    uint8_t       length;
    uint32_t      my_discr;
    uint32_t      your_discr;
    uint32_t      min_tx_int;
    uint32_t      min_rx_int;
    uint32_t      min_echo_rx_int;
}__attribute__ ((packed)) bfd_t,*bfd_ptr;


static inline uint64_t get_microsecond(void){
    struct timeval	tv;
    gettimeofday(&tv,NULL);
    return((((uint64_t)tv.tv_sec*1000000) + ((uint64_t)tv.tv_usec)));
}

// インタフェイス
class Conn;
class BfdTrigger{
public:
    virtual void on_bfd_recieved(Conn*, bfd_ptr) = 0;
};
#endif //PROJECT_BFDD_CPP_H
