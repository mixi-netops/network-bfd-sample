/******************************************************************************/
/*! @addtogroup in process file mmap kvs db(shared memory map - reallocation)
 @file       smmr.h
 @brief      マルチプロセス：インメモリデータベース
 ******************************************************************************/

#ifndef SMMR_LIBRARY_H
#define SMMR_LIBRARY_H

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
#include <errno.h>
#include <dirent.h>
#include <libgen.h>


/*! @name basic defineded... */
/* @{ */
#define		RET_OK			(0)						/*!< 処理結果：成功 */
#define		RET_NG			(-1)					/*!< 処理結果：エラー */
#define		RET_WARN	    (1)						/*!< 処理結果：ワーニング */
#define		RET_MOREDATA	(2)						/*!< 処理結果：モアデータ */

#define		VERSION_TEXT	("1.0.0.0")
#define		BUILD_NUMBER	("20170301")

#ifndef INVALID_SOCKET
#define	INVALID_SOCKET	((int)0xffffffff)		/*!< ソケット：エラー */
#endif
#ifndef INVALID_HANDLE
#define	INVALID_HANDLE	((int)0xffffffff)		/*!< 汎用    ：エラー */
#endif
#ifndef INVALID_FILE
#define	INVALID_FILE	INVALID_HANDLE			/*!< ファイル：エラー */
#endif

#ifndef MAX_PATH
#define	MAX_PATH		(256)				    /*!< パス文字列最大長 */
#endif
/*UTIL*/
#ifndef MAX
#define MAX(a, b) (a > b ? a : b)				/*!< math::max */
#endif
#ifndef MIN
#define MIN(a, b) ((a) > (b) ? (b) : (a))		/*!< math:min */
#endif
#define IS_SET(a, b) (((a) & (b)) == (b) ? 1 : 0)	/*!< bit is on? */
#ifndef LOWORD
#define LOWORD(l)           ((WORD)(((DWORD)(l)) & 0xffff))					/*!< access low. unsigned short  */
#endif
#ifndef HIWORD
#define HIWORD(l)           ((WORD)((((DWORD)(l)) >> 16) & 0xffff))			/*!< access high. unsigned short  */
#endif
#ifndef LOBYTE
#define LOBYTE(w)           ((unsigned char)(((DWORD)(w)) & 0xff))			/*!< access low. unsigned char */
#endif
#ifndef HIBYTE
#define HIBYTE(w)           ((unsigned char)((((DWORD)(w)) >> 8) & 0xff))	/*!< access high. unsigned char */
#endif

#ifndef ULONGLONG
#define ULONGLONG unsigned long long		/*!< 64 bits ：整数値 */
#endif
#ifndef ULONG
#define ULONG unsigned long					/*!< 32 bits ：整数値 */
#endif

#ifndef FALSE
#define FALSE	(0)
#endif
#ifndef TRUE
#define TRUE	(1)
#endif

#define		FILE_MODE		(0666)			/*!< ファイル生成時：デフォルトモード */
/* @} */


//////////////////////
//

/*! @name メモリマネージャ、カテゴリ defined  */
/* @{ */
#define		INDEXFLG_INIT			(0)
#define		INDEXFLG_VALID			(1 << 31)
#define		INDEXFLG_REMOVED		(1 << 30)
#define		INDEXFLG_RECYCLED		(1 << 29)

#define		HASH_MAGICID			(1785124532)
//#define		CATEGORY_CNT		(16)
#define		CATEGORY_CNT			(4)
#define		CATEGORY_MMAPIDX		(0)
// カテゴリ数は最低２個必要（index=0/インデックス用、index=[1-n] データ）
#define		CATEGORY_PATHLEN		(128)
#define		CATEGORY_SIZE			(134217728)	// 1024 * 1024 * 64 * 2
#define		CATEGORY_SIZE_IDX		(134217728)
// 最大は、128MB（32KのBITMAPなので）
#ifdef PAGE_SIZE
#undef PAGE_SIZE
#define		PAGE_SIZE				(131072)
#else
#define		PAGE_SIZE				(131072)
#endif
#define		PAGE_CNT				(CATEGORY_SIZE/PAGE_SIZE)
#define		SYSPAGE_SIZE			(2048)

#define	IPC_CATEGORY_SHMNM			("/ipc_categories_smmr")
#define	IPC_CATEGORY_SHMNM_C		(IPC_CATEGORY_SHMNM)
#define	IPC_CATEGORY_SIZE			((sizeof(ipcshm_categories_t) / sysconf(_SC_PAGE_SIZE) + 1) * sysconf(_SC_PAGE_SIZE))
#define	IPC_DATAFILE_SHMNM			("/ipc_datafiles_smmr")
#define	IPC_DATAFILE_SHMNM_C		(IPC_DATAFILE_SHMNM)
#define	IPC_DATAFILE_SIZE			((sizeof(ipcshm_datafiles_t) / sysconf(_SC_PAGE_SIZE) + 1) * sysconf(_SC_PAGE_SIZE))
#ifdef __TEST__
#define	IPC_DATAFILE_MAX			(0x08)
#else
#define	IPC_DATAFILE_MAX			(0x80)
#endif
#define	IPC_DATAFILE_TTLSIZE		(134217728)
#define	IPC_DATAFILE_USRSIZE		(((IPC_DATAFILE_TTLSIZE - sizeof(db_table_header_t)) / IPC_DATAFILE_ALIGNED) * IPC_DATAFILE_ALIGNED)
#define	IPC_DATAFILE_ALIGNED		(128)
#define	IPC_DATAFILE_PAGECNT		(IPC_DATAFILE_USRSIZE / IPC_DATAFILE_ALIGNED)
#define	IPC_DATAFILE_BMPSIZE		(IPC_DATAFILE_TTLSIZE / IPC_DATAFILE_ALIGNED / 8)
#define	DATAFILE_PATHLEN			(CATEGORY_PATHLEN)

#define	DATAITEM_SIGNATURE_ID		(0xDEADC1DE)
#define	SMMR_TREEROOT_ID		    (0xDEADBEAF)


// メモリページオフセット
#define		MMPOS_USED_ROOT			(0)
#define		MMPOS_FREE_ROOT			(MMPOS_USED_ROOT + sizeof(memitem_head_t) + sizeof(memitem_root_detail_t) + sizeof(memitem_foot_t))
#define		MMPOS_BCKT_ROOT			(MMPOS_FREE_ROOT + sizeof(memitem_head_t) + sizeof(memitem_root_detail_t) + sizeof(memitem_foot_t))
#define		MMPOS_BCKT_INIT			(MMPOS_BCKT_ROOT + sizeof(memitem_head_t) + sizeof(memitem_root_detail_t) + sizeof(memitem_foot_t))

#define		MMPOS_MIN_OFST			(sizeof(memitem_head_t) + sizeof(memitem_foot_t))
#define		MMPOS_ROOT_SIZE			(sizeof(memitem_head_t) + sizeof(memitem_root_detail_t) + sizeof(memitem_foot_t))
#define		MMPOS_ROOT_FOOT			(sizeof(memitem_head_t) + sizeof(memitem_root_detail_t))
#define		MMPOS_ROOT_DTL			(sizeof(memitem_head_t))
#define		MMSIZ_HF_SIZE			(MMPOS_MIN_OFST)
#define		MMSIZ_ALL_ROOT			(MMPOS_BCKT_INIT + MMPOS_ROOT_SIZE)
#define		MMSIZ_BCKT_INIT_SIZE	(PAGE_SIZE - MMSIZ_ALL_ROOT)

// memitem_head.signature
#define		MMSIG_USED				(0xDB00C0DE)
#define		MMSIG_FREE				(0xDB55C0DE)
#define		MMSIG_BCKT				(0xDB77C0DE)
#define		MMSIG_SUFX				(0xDEADC0DE)
// memitem_head.next_offset
#define		MMFLG_CLOSE				(0xFFFFFFFF)

#define		PAGEBIT_ON(a,p)			(a[(p/8)]|=(1<<(p%8)))
#define		PAGEBIT_OFF(a,p)		(a[(p/8)]&=~(1<<(p%8)))
#define		ISPAGEBIT(a,p)			(a[(p/8)]&(1<<(p%8)))

#define		MMPOS_SYSPAGE_SYSTEM	((IPCSHM_PAGE_SYSTEM * PAGE_SIZE) + (IPCSHM_SYSPAGE_SYSTEM * SYSPAGE_SIZE))
#define		MMPOS_SYSPAGE_BMP(a)	((IPCSHM_PAGE_SYSTEM * PAGE_SIZE) + (a * SYSPAGE_SIZE))
#define		MMPSIZ_SYSPAGE_BMP		(IPCSHM_SYSPAGE_ERR * SYSPAGE_SIZE)
#define		MMPOS_USRPAGE(a)		(a * PAGE_SIZE)


// メモリアクセス：外部からは、以下のマクロを利用する
#define	SHMALLOC_ROOT(type,insti)	((type*)shmalloc(\
										insti.category_p,sizeof(type),\
										&(insti->rooti.category_id),\
										&(insti->rooti.page_id),\
										&(insti->rooti.offset)\
									))
#define	SHMALLOC(type,catep,of,r)   {   uint16_t __cateid;uint16_t __pageid;uint32_t __offset;\
                                        r=((type*)shmalloc(\
										catep,sizeof(type),\
										&(__cateid),\
										&(__pageid),\
										&(__offset)\
									));\
                                        of.category_id = __cateid;\
                                        of.page_id = __pageid;\
                                        of.offset = __offset;}
#define	SHMALLOC_NML(type,insti,of,r) {   uint16_t __cateid;uint16_t __pageid;uint32_t __offset;\
                                        r=((type*)shmalloc(\
										insti.category_p,sizeof(type),\
										&(__cateid),\
										&(__pageid),\
										&(__offset)\
									));\
                                        of.category_id = __cateid;\
                                        of.page_id = __pageid;\
                                        of.offset = __offset;}

#define	SHMFREE(catep,pointer)		shmfree(catep,pointer)

#define	ISEMPTY_NODE(n)				(!n.category_id && !n.page_id && !n.offset)
#define	REFNODE(cp,n)				(node_instance_ptr)(cp->data_pointer + MMPOS_USRPAGE(n.page_id) + n.offset + sizeof(memitem_head_t))


// Btree関連定義
#define	TREE_NODECOUNT				(4)
#define	TREE_NODEMIN				(TREE_NODECOUNT / 2)
#define	REMOVE_RESERVED_COUNT		(32)
#define	ISEMPTY_KEY(o)				(!o.keyhash && !o.data_field && !o.data_offset)

/* @} */


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
// 構造体：列挙値
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//


//////////////////////
// カテゴリ 定義 ：構造体 列挙値

// カテゴリ 管理領域 ：キーハッシュから、カテゴリハンドルを取得
// カテゴリハンドルとは、その管理化にあるページオブジェクトにアクセス
// するためのハンドル
//
/*  +-------------+-------------+-------------+-------------+-------------+-------------+-------------+    +-------------+
    | category[0] | category[1] | category[2] | category[3] | category[4] | category[5] | category[6] |... | category[15]|
    +-------------+-------------+-------------+-------------+-------------+-------------+-------------+    +-------------+
      |
 +--------+--------+   +------------+
 |page[0] |page[1] |...|page[2048]  |
 +--------+--------+   +------------+
   |
 +-------+-------+-------+   +-------+
 |mem[0] |mem[1] |mem[2] |...|mem[n] |
 +-------+-------+-------+   +-------+
 */

/*! @struct ipcshm_category
 @brief
 1 カテゴリ\n
 */
typedef struct ipcshm_category{
    pthread_mutex_t		category_mutex;
    uint32_t			filesize;
    uint32_t			category_status;
    char				category_shmnm[CATEGORY_PATHLEN];
    char				category_path[CATEGORY_PATHLEN];
}ipcshm_category_t,*ipcshm_category_ptr;


/*! @struct ipcshm_categories
 @brief
 IPC カテゴリ領域\n
 */
typedef struct ipcshm_categories{
    pthread_mutex_t		category_all_mutex;
    ipcshm_category_t	category[CATEGORY_CNT];
    int					category_shmid;
}ipcshm_categories_t,*ipcshm_categories_ptr;

/*! @struct category_on_process
 @brief
 プロセス毎 カテゴリ領域\n
 */
typedef struct category_on_process{
    uint32_t			id;
    uint32_t			filesize;
    int					filefd;
    char*				data_pointer;
}category_on_process_t,*category_on_process_ptr;

/*! @struct ipcshm_categories
 @brief
 プロセス毎 カテゴリ領域\n
 */
typedef struct categories_on_process{
    category_on_process_t	category[CATEGORY_CNT];
}categories_on_process_t,*categories_on_process_ptr;


/*! @enum IPCSHM_CATEGORY_STAT_ENUM
 @brief
 カテゴリ用ステータス
 */
typedef enum IPCSHM_CATEGORY_STAT_ENUM{
    IPCSHM_CATEGORY_STAT_DC	= 0,			/*!< 初期値 */
    IPCSHM_CATEGORY_STAT_RUNNING,			/*!< 実行中 */
    IPCSHM_CATEGORY_STAT_LOCKED,			/*!< ロック */
    IPCSHM_CATEGORY_STAT_ERROR,				/*!< エラー */
    IPCSHM_CATEGORY_STAT_MAX
}_IPCSHM_CATEGORY_STAT_ENUM;




//////////////////////
// メモリマネージャ 定義 ：構造体 ：列挙値

/* -------------------------------------------
 * categories領域を、4Kbytes 毎に分割したページを管理し
 * shmalloc でページ内のFREEエリアを返却する
 * 割当際に0xDEADC0DE をシグネチャにセットし
 * 確保時、解放時にこれをチェック
 *
 * -------------------------------------------
 */


/*! @struct memitem_head
 @brief
 アロケートメモリの１アイテムヘッダ\n
 \n
 */
typedef struct memitem_head{
    uint32_t		signature;			/*!< 利用領域   ：[0xDB00C0DE]  */
                                        /*!< 空き領域   ：[0xDB55C0DE]  */
    uint32_t		prev_offset;		/*!< 前LINKオフセット           */
    uint32_t		next_offset;		/*!< 次LINKオフセット           */
    uint32_t		len;				/*!< 割当サイズ                 */
}memitem_head_t,*memitem_head_ptr;


/*! @struct memitem_foot
 @brief
 アロケートメモリの１アイテムフッタ\n
 \n
 */
typedef struct memitem_foot{
    uint32_t		created;			/*!< 割当unixtime               */
    uint32_t		suffix;				/*!< サフィックス :[0xDEADC0DE] */
    uint32_t		category_id;		/*!< カテゴリID逆引き           */
    uint32_t		padding;			/*!< 未使用：パディング         */
}memitem_foot_t,*memitem_foot_ptr;


/*! @struct memitem_root_detail
 @brief
 各ルートアイテムの詳細データ\n
 INSERT 時に利用する最終オフセットが保持される\n
 \n
 */
typedef struct memitem_root_detail{
    uint32_t		first_offset;		/*!< スタートlinkオフセット       */
    uint32_t		last_offset;		/*!< 最終linkオフセット           */
    uint32_t		free_size;			/*!< フリーサイズサマリ：ページ毎 */
    uint32_t		free_cnt;			/*!< フリー数サマリ    ：ページ毎 */
    uint32_t		use_size;			/*!< 利用サイズサマリ  ：ページ毎 */
    uint32_t		use_cnt;			/*!< 利用数サマリ      ：ページ毎 */
}memitem_root_detail_t,*memitem_root_detail_ptr;



/*! @struct mempage
 @brief
 システム用メモリページ[IPCSHM_PAGE_SYSTEM]\n
 28 bytes\n
 */
typedef struct mempage_system{
    uint32_t		idx;				/*!< カテゴリID        ：カテゴリ毎 */
    uint32_t		page_size;			/*!< ページサイズ      ：カテゴリ毎 */
    uint32_t		file_size;			/*!< ファイルサイズ最大  ：カテゴリ毎 */
    uint32_t		free_size;			/*!< フリーサイズサマリ：カテゴリ毎 */
    uint32_t		free_cnt;			/*!< フリー数サマリ    ：カテゴリ毎 */
    uint32_t		use_size;			/*!< 利用サイズサマリ  ：カテゴリ毎 */
    uint32_t		use_cnt;			/*!< 利用数サマリ      ：カテゴリ毎 */
}mempage_system_t,*mempage_system_ptr;

/*! @struct bitmap_itm
 @brief
 ビットマップLoop用\n
 \n
 */
typedef struct bitmap_itm{
    uint32_t		dc_bmp_id;
    uint32_t		free_bmp_id;
}bitmap_itm_t,*bitmap_itm_ptr;


/*! @enum IPCSHM_PAGE_ENUM
 @brief
 空き領域ビットマップ\n
 */
typedef enum IPCSHM_BMPID_ENUM{
    IPCSHM_BMPID_MIN = 0,				/*!<  一番小さいほう  */
    IPCSHM_BMPID_032,					/*!<   32 -  127      */
    IPCSHM_BMPID_128,					/*!<  128 -  511      */
    IPCSHM_BMPID_512,					/*!<  512 - 2047      */
    IPCSHM_BMPID_2K,					/*!< 2048 - MAX       */
    IPCSHM_BMPID_MAX
}_IPCSHM_BMPID_ENUM;

/*! @enum IPCSHM_PAGE_ENUM
 @brief
 メモリページ用ステータス
 */
typedef enum IPCSHM_PAGE_ENUM{
    IPCSHM_PAGE_SYSTEM = 0,				/*!< システム利用       */
                                        /* ... */
    IPCSHM_PAGE_USRAREA,				/*!< ユーザページ開始   */
    IPCSHM_PAGE_USRAREA_MAX = PAGE_CNT,	/*!< ユーザページ最大   */
    IPCSHM_PAGE_MAX
}_IPCSHM_PAGE_ENUM;

/*! @enum IPCSHM_SYSPAGE_ENUM
 @brief
 システム用メモリページ
 */
typedef enum IPCSHM_SYSPAGE_ENUM{
    IPCSHM_SYSPAGE_SYSTEM = 0,				/*!< システム利用       */
    IPCSHM_SYSPAGE_BMP_MIN,
    IPCSHM_SYSPAGE_DC_032 = IPCSHM_SYSPAGE_BMP_MIN,
                                            /*!<  32-127 byteの未使用：bitmap */
    IPCSHM_SYSPAGE_DC_128,					/*!< 128 byte  〃     */
    IPCSHM_SYSPAGE_DC_512,					/*!< 512 byte  〃     */
    IPCSHM_SYSPAGE_DC_2K,					/*!<  2K byte  〃     */
                                            /* .. リサイクル 領域 */
    IPCSHM_SYSPAGE_FREE_000,				/*!<  00-31 byteの空き：bitmap */
    IPCSHM_SYSPAGE_FREE_032,				/*!<  32 byte 〃     */
    IPCSHM_SYSPAGE_FREE_128,				/*!< 128 byte 〃     */
    IPCSHM_SYSPAGE_FREE_512,				/*!< 512 byte 〃     */
    IPCSHM_SYSPAGE_FREE_2K,					/*!<  2K byte 〃     */
    IPCSHM_SYSPAGE_ERR,						/*!< エラーページ〃     */
                                            // 最大 [32768] / 2048 = 16 まで
    IPCSHM_SYSPAGE_MAX
}_IPCSHM_SYSPAGE_ENUM;


//////////////////////
// BTREE 定義 ：構造体 ：列挙値

#pragma pack(1)
/*! @struct node_key
 @brief
 node 実装 ノードキー\n
 keyhash + data_field + data_offset でユニーク\n
 */
typedef struct node_key{
    uint32_t	keyhash;
    uint32_t	data_field:8;
    uint32_t	data_offset:24;
}node_key_t,*node_key_ptr;

/*! @struct node_instance_offset
 @brief
 node 実装 ノードインスタンスのオフセット値\n
 \n
 */
typedef struct node_instance_offset{
    uint32_t	category_id:8;
    uint32_t	page_id:16;
    uint32_t	offset:24;
}node_instance_offset_t,*node_instance_offset_ptr;

/*! @struct node_instance
 @brief
 node 実装 ノードインスタンス\n
 \n
 */
typedef struct node_instance{
    uint8_t					count;
    uint8_t					padd;
    node_instance_offset_t	childsi[TREE_NODECOUNT + 1];
    node_key_t				keys[TREE_NODECOUNT];
}node_instance_t,*node_instance_ptr;

#pragma pack()


/*! @struct tree_instance_itm
 @brief
 tree 実装 アイテム（保存分）\n
 \n
 */
typedef struct ipcshm_tree_instance_itm{
    uint32_t				signature;
    node_instance_offset_t	rooti;
    node_instance_offset_t	newnodei;
}ipcshm_tree_instance_itm_t,*ipcshm_tree_instance_itm_ptr;

/*! @struct tree_instance_itm
 @brief
 tree 実装 アイテム（プロセスヒープ）\n
 \n
 */
typedef struct tree_instance_itm_on_process{
    ipcshm_category_ptr		category;
    category_on_process_ptr	category_p;
    node_key_t				current_item;
    int						done;
    int						deleted;
    int						undersize;
}tree_instance_itm_on_process_t,*tree_instance_itm_on_process_ptr;

/*! @struct tree_instance
 @brief
 tree 実装 メインインスタンス\n
 ipcshm_xxx の領域は、shared memory 名前付きでアタッチしている\n
 領域であり、子プロセス間で共有される\n
 xxxx_on_process ... の領域は、プロセスヒープである\n
 */
typedef struct tree_instance{
    ipcshm_categories_ptr			categories;
    categories_on_process_ptr		categories_on_process;
    uint32_t						curid;
    //
    ipcshm_tree_instance_itm_ptr	itm[CATEGORY_CNT];
    tree_instance_itm_on_process_t	itm_p[CATEGORY_CNT];
}tree_instance_t,*tree_instance_ptr;

//////////////////////
// データファイル系 ：構造体 ：列挙値

/*! @struct db_table_header
 @brief
 データベーステーブル：ヘッダ定義\n
 128 bytes\n
 */
typedef struct db_table_header{
    uint64_t		first_offset;		/*!< 最初のデータのオフセット */
    uint64_t		last_offset;		/*!< 最後のデータのオフセット */
    uint64_t		used_length;		/*!< 使用中サイズ */
    uint64_t		used_count;			/*!< 使用中アイテム個数 */
    uint64_t		table_length;		/*!< ファイルサイズ（全体） */
    uint64_t		remain_length;		/*!< ファイル残りサイズ */
    // ----------------------
    uint64_t		last_updated;		/*!< 最終更新日付 */
    uint32_t		signature;			/*!< 初期判定シグネチャ   ：[0xDEADC1DE]  */
    uint32_t		padding;
    char			table_name[64];		/*!< テーブル名 */
    char			bitmap[IPC_DATAFILE_BMPSIZE];
    /*!< 空き領域bitmap */
}db_table_header_t,*db_table_header_ptr;


/*! @struct ipcshm_dataitm
 @brief
 データ１アイテムヘッダ\n
 アクセスイメージ\n
 */
typedef struct ipcshm_dataitm{
    uint32_t			signature;
    uint32_t			length;
    uint32_t			flags;
    uint32_t			expire;
    uint32_t			keylen;
    uint32_t			vallen;
//	char*				key;
//	char*				val;
}ipcshm_dataitm_t,*ipcshm_dataitm_ptr;

/*! @struct ipcshm_datafiles
 @brief
 データファイル\n
 \n
 */
typedef struct ipcshm_datafiles{
    pthread_mutex_t		datafile_mutex[IPC_DATAFILE_MAX];
    int					shmid;
    char				path[IPC_DATAFILE_MAX][DATAFILE_PATHLEN];
    char				shmnm[IPC_DATAFILE_MAX][DATAFILE_PATHLEN];
    uint32_t			use_cnt;
}ipcshm_datafiles_t,*ipcshm_datafiles_ptr;

/*! @struct datafiles_on_process
 @brief
 データファイル プロセス毎\n
 \n
 */
typedef struct datafiles_on_process{
    int					filefd[IPC_DATAFILE_MAX];
    char*				data_pointer[IPC_DATAFILE_MAX];
}datafiles_on_process_t,*datafiles_on_process_ptr;


/*! @struct find_container
  @brief
  検索コールバック用コンテナ\n
  \n
  */
typedef struct find_container{
    uint32_t					hashedid;
    ipcshm_datafiles_ptr		ipc_datafiles;
    datafiles_on_process_ptr	datafiles;
    tree_instance_ptr			inst;
    node_key_t					resultnode;
    node_key_t					remove_reserved[REMOVE_RESERVED_COUNT];
    int							flag;
    char*						key;
    uint32_t					keylen;
    char*						val;
    uint32_t					vallen;
    uint32_t					expire;
}find_container_t,*find_container_ptr;

/*! @struct paged_buffer
  @brief
  ページアラインドバッファ\n
  \n
  */
typedef struct paged_buffer{
    char*	    data;
    size_t	    end_offset;
    size_t	    allocated_size;
}paged_buffer_t,*paged_buffer_ptr;

/*! @struct mutex_lock
  @brief
  mutex lock\n
  \n
  */
typedef struct mutex_lock{
    pthread_mutex_t*	obj;
    int					result;
}mutex_lock_t,*mutex_lock_ptr;

/*! @struct mempage_summary
 @brief
 システムメモリサマリ\n
 \n
 */
typedef struct mempage_summary{
    mempage_system_t        sys;
    memitem_root_detail_t   used[IPCSHM_PAGE_USRAREA_MAX];
    memitem_root_detail_t   free[IPCSHM_PAGE_USRAREA_MAX];
    char                    bitmap[IPCSHM_SYSPAGE_MAX][PAGE_CNT];
}mempage_summary_t,*mempage_summary_ptr;


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


// snprintf thread safe.
extern pthread_mutex_t global_str_safe_muex;
void set_prefix(const char*);
const char* get_prefix(void);
const char* get_category_shmnm(void);
const char* get_datafile_shmnm(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#include "smmr.inl"

#endif
