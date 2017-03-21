/******************************************************************************/
/*! @addtogroup bfd service
 @file       bfdd.h
 @brief      共通ヘッダ
 ******************************************************************************/
#ifndef PROJECT_BFDD_H
#define PROJECT_BFDD_H

#include "smmr_pagedbuffer.h"
#include "smmr_categories.h"
#include "smmr_memory.h"
#include "smmr_database.h"
#include "smmr_tree.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <netdb.h>

/*! @name basic defineded... */

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

/*! @name セッションデータベースアクセス.キー定義... */
#define SESSION_KEYS_MANGE  (1000)
#define SESSION_KEYS_BASE   (2000)
#define SESSION_KEYS_MIN       (0)
#define SESSION_KEYS_00        (SESSION_KEYS_MIN)
#define SESSION_KEYS_01        (1)
#define SESSION_KEYS_02        (2)
#define SESSION_KEYS_03        (3)
// ...
#define SESSION_KEYS_32       (31)
#define SESSION_KEYS_MAX      (32)
#define SESSION_KEYS_LISTCNT (128)
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

/*! @struct bfd_sess_manage
 @brief
 セッション管理テーブル：サマリ\n
 */
typedef struct bfd_sess_manage{
    uint32_t    keys_bits;  // 32ビットで、テーブル利用状況を管理
                            // (1<<31) : SESSION_KEYS_00 を利用している
                            // (1<<30) : SESSION_KEYS_01 を利用している
                            // (1<<29) : 〃
    uint32_t    keys_incnt[SESSION_KEYS_MAX];
                            // 各SESSION_KEYS_[00-31] テーブルの利用個数サマリ
}bfd_sess_manage_t,*bfd_sess_manage_ptr;
/*! @struct bfd_sess_key
 @brief
 セッション検索キー\n
 */
typedef struct bfd_sess_key{
    uint32_t    addr;
    uint16_t    port;
    uint16_t    padd;
}bfd_sess_key_t,*bfd_sess_key_ptr;
/*! @struct bfd_sess_key_list
 @brief
 セッション検索キーリスト\n
 */
typedef struct bfd_sess_key_list{
    bfd_sess_key_t  keys[SESSION_KEYS_LISTCNT];
}bfd_sess_key_list_t,*bfd_sess_key_list_ptr;

/*! @struct bfd_sess
 @brief
 セッションアイテム\n
 */
typedef struct bfd_sess{
    uint32_t    session_state;
    uint32_t    remote_session_state;
    uint32_t    local_discr;
    uint32_t    remote_discr;
    uint32_t    local_diag;
    uint32_t    desired_min_tx_interval;
    uint32_t    required_min_rx_interval;
    uint32_t    remote_min_rx_interval;
    uint32_t    demand_mode;
    uint32_t    remote_demand_mode;
    uint32_t    detect_mult;
    uint32_t    auth_type;
    uint32_t    rcv_auth_seq;
    uint32_t    xmit_auth_seq;
    uint32_t    auth_seq_known;
    //
    uint32_t    must_cease_tx_echo;
    uint32_t    must_terminate;
    uint64_t    detect_time;
    uint32_t    pollbit_on;
    uint32_t    received_min_rx_interval;
    struct sockaddr_in  client_addr;
    int                 client_addrlen;
    int         attached;
}bfd_sess_t,*bfd_sess_ptr;


typedef int(*iterate_session)(int, bfd_sess_ptr, void*);

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

//
int setup_bfd_socket(const char*,unsigned short);
int recv_bfd_event(int,struct sockaddr_in * , int , char* , int );
int send_bfd_event(int );
int initialize_bfd_session_table(void);
int finalize_bfd_session_table(void);
void init_session(bfd_sess_ptr, struct sockaddr_in *, int);
int find_session(bfd_sess_ptr, struct sockaddr_in *, int);
void iterate_all_session(int, iterate_session, void*);
int insert_session(bfd_sess_ptr , struct sockaddr_in *, int);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //PROJECT_BFDD_H
