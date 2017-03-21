/******************************************************************************/
/*! @addtogroup bfd service on c with netmap
 @file       bfdd_netmap.h
 @brief      c with netmap用bfd共通ヘッダ
 ******************************************************************************/

#ifndef PROJECT_BFDD_H
#define PROJECT_BFDD_H

#include <sys/types.h>
#include <sys/wait.h>
#include <netdb.h>
#include <poll.h>
#include <libgen.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>
/*! @name basic defineded... */

#define LEN_VLAN(e)             (e->ether_type==htons(ETHERTYPE_VLAN)?4:0)

#ifndef MIN
#define MIN(a,b) (a<b?a:b)
#endif

#define UNUSED(x)   (void)x
#ifndef ETHER_MAX_LEN
#define ETHER_MAX_LEN           1518
#endif

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




/*! @struct process_handle
 @brief
 プロセス構造体\n
 */
typedef struct process_handle{
    struct nm_desc* nmd;
    struct netmap_ring* rxring;
    struct netmap_ring* txring;
    struct netmap_slot *rs;
    struct netmap_slot *ts;
    char*       res;
    uint16_t    reslen;
    char*       req;
    uint16_t    reqlen;
}process_handle_t,*process_handle_ptr;

typedef int (*PACKET_EVENT)(int, process_handle_ptr);





#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
int move(process_handle_ptr,PACKET_EVENT);
int process_rings(process_handle_ptr,PACKET_EVENT);
int is_bfd_packet(process_handle_ptr , char *, int );
int recv_packet(int, process_handle_ptr);
int send_packet(int, process_handle_ptr);
void swap_index(process_handle_ptr );
void swap_mac(struct ether_header* );
void swap_ip(struct ip*, uint8_t );
void swap_bfd(bfd_ptr );
#ifdef __cplusplus
}
#endif /* __cplusplus */

static inline uint64_t get_microsecond(void){
    struct timeval	tv;
    gettimeofday(&tv,NULL);
    return((((uint64_t)tv.tv_sec*1000000) + ((uint64_t)tv.tv_usec)));
}


#endif //PROJECT_BFDD_H
