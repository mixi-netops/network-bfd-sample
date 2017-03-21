/******************************************************************************/
/*! @addtogroup in process file mmap kvs db(shared memory map - reallocation)
    @file       smmr_categories.c
    @brief      マルチプロセス：インメモリデータベース\n
    キャッシュデータのカテゴリテーブル
*******************************************************************************
    カテゴリとはRDB的なテーブルを意味している\n
    sharedMemoryは、別々の領域（ファイルシステム上）として実装される\n
    IPCロック制御は、このカテゴリ毎に実装することによって\n
    並列プロセスアクセス時のロック粒度を最適化する\n
    RDB的な（テーブルロック）みたいに\n
******************************************************************************/
#include "smmr_categories.h"
#include "smmr_pagedbuffer.h"
#include <assert.h>


/** *************************************************************
 * カテゴリ領域の準備
 *  ※プロセスで一度、最初に
 *
 * @param[in]     basedir         ベースディレクトリ
 * @param[in,out] ipc_categories  カテゴリリスト（筐体グローバル/named shm）
 * @param[in]     categories_on_process  カテゴリアクセサ（プロセス）
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int create_categories_area(const char* basedir,ipcshm_categories_ptr* ipc_categories,categories_on_process_ptr categories_on_process){
    int				shmfd,n;
    uint32_t		category_size = 0;
    pthread_mutexattr_t ma;

    // 既に初期化済みの場合処理しない
    if ((char*)(*ipc_categories) != (char*)INVALID_SOCKET){
        fprintf(stderr, "invalid state.(categories)\n");
        return(RET_NG);
    }
    // 先に削除しておく
    if (shm_unlink(get_category_shmnm()) != 0){
        fprintf(stdout, "shm_unlink missing.(%s)\n", get_category_shmnm());
    }
    // 共有領域
#ifdef __APPLE__
    shmfd = shm_open(get_category_shmnm(),O_CREAT|O_RDWR,(S_IRUSR|S_IWUSR));
#else
    shmfd = shm_open(get_category_shmnm(),O_CREAT|O_RDWR|O_TRUNC,(S_IRUSR|S_IWUSR));
#endif
    if (shmfd < 0){
        fprintf(stderr, "shm open(%s : %s)\n", get_category_shmnm(), strerror(errno));
        return(RET_NG);
    }
    if (ftruncate(shmfd,IPC_CATEGORY_SIZE) == -1){
        close(shmfd);
        fprintf(stderr, "ftruncate(%lu)\n", IPC_CATEGORY_SIZE);
        return(RET_NG);
    }
    (*ipc_categories) = (ipcshm_categories_ptr)mmap(0, IPC_CATEGORY_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, shmfd, 0);
    if ((char*)(*ipc_categories) == (char*)INVALID_SOCKET){
        close(shmfd);
        fprintf(stderr, "mmap(%u)\n", shmfd);
        return(RET_NG);
    }
    // 初期パラメータ設定
    memset((*ipc_categories),0,IPC_CATEGORY_SIZE);
    memset(categories_on_process,0,sizeof(categories_on_process_t));

    (*ipc_categories)->category_shmid = shmfd;
    close(shmfd);

    // ミューテックス属性設定
    if (pthread_mutexattr_init(&ma)){ assert(!"failed.pthread_mutexattr_init"); }
#ifdef PTHREAD_MUTEX_ROBUST_NP
    if (pthread_mutexattr_setrobust_np(&ma,PTHREAD_MUTEX_ROBUST_NP)){ assert(!"failed.pthread_mutexattr_setrobust_np"); }
#endif
    if (pthread_mutexattr_setpshared(&ma,PTHREAD_PROCESS_SHARED)) { assert(!"failed.pthread_mutexattr_setpshared"); }
    if (pthread_mutexattr_setprotocol(&ma,PTHREAD_PRIO_INHERIT)) { assert(!"failed.pthread_mutexattr_setprotocol"); }

    // カテゴリ全体 mutex
    pthread_mutex_init(&((*ipc_categories)->category_all_mutex),&ma);

    pthread_mutexattr_destroy(&ma);
    //
    for(n = 0;n < CATEGORY_CNT;n++){
        // indexファイルは[0] 固定
        if (n == CATEGORY_MMAPIDX){ category_size = CATEGORY_SIZE_IDX;
        }else{ category_size = CATEGORY_SIZE; }

        // ミューテックス属性設定
        if (pthread_mutexattr_init(&ma)){ assert(!"failed.pthread_mutexattr_init"); }
#ifdef PTHREAD_MUTEX_ROBUST_NP
        if (pthread_mutexattr_setrobust_np(&ma,PTHREAD_MUTEX_ROBUST_NP)){ assert(!"failed.pthread_mutexattr_setrobust_np"); }
#endif
        if (pthread_mutexattr_setpshared(&ma,PTHREAD_PROCESS_SHARED)) { assert(!"failed.pthread_mutexattr_setpshared"); }
        if (pthread_mutexattr_setprotocol(&ma,PTHREAD_PRIO_INHERIT)) { assert(!"failed.pthread_mutexattr_setprotocol"); }

        // カテゴリ毎 mutex
        pthread_mutex_init(&((*ipc_categories)->category[n].category_mutex),&ma);
        pthread_mutexattr_destroy(&ma);

        // カテゴリID、ステータスセット
        categories_on_process->category[n].id			= n;
        categories_on_process->category[n].filefd		= INVALID_SOCKET;
        categories_on_process->category[n].data_pointer	= (char*)INVALID_SOCKET;

        (*ipc_categories)->category[n].category_status	= IPCSHM_CATEGORY_STAT_DC;
        (*ipc_categories)->category[n].filesize			= category_size;
        // 共有領域名称
        safe_snprintf((*ipc_categories)->category[n].category_shmnm,CATEGORY_PATHLEN - 1,"%s.%02d",get_category_shmnm(),n);
        safe_snprintf((*ipc_categories)->category[n].category_path, CATEGORY_PATHLEN - 1,"%s%s.%02d",basedir,get_category_shmnm(),n);
    }
    //
    return(RET_OK);
}
/** *************************************************************
 * カテゴリ領域の解放
 *  ※プロセスで一度、最初に
 *
 * @param[in,out] ipc_categories  カテゴリリスト（筐体グローバル/named shm）
 * @param[in]     categories_on_process  カテゴリアクセサ（プロセス）
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int remove_categories_area(ipcshm_categories_ptr* ipc_categories,categories_on_process_ptr categories_on_process){
    int			n;

    // ステータスチェック
    if ((char*)(*ipc_categories) == (char*)INVALID_SOCKET || !categories_on_process){
        return(RET_NG);
    }
    // カテゴリ個別のデタッチ
    for(n = 0;n < CATEGORY_CNT;n++){
        if (categories_on_process->category[n].filefd != INVALID_SOCKET){
            close(categories_on_process->category[n].filefd);
        }
        categories_on_process->category[n].filefd = INVALID_SOCKET;

        if (categories_on_process->category[n].data_pointer != (char*)INVALID_SOCKET){
            munmap(categories_on_process->category[n].data_pointer,categories_on_process->category[n].filesize);
        }
        categories_on_process->category[n].data_pointer = (char*)INVALID_SOCKET;
    }
    // カテゴリ全体のデタッチ
    if ((*ipc_categories)){
        close((*ipc_categories)->category_shmid);
        munmap((*ipc_categories),IPC_CATEGORY_SIZE);
    }
    (*ipc_categories) = (ipcshm_categories_ptr)INVALID_SOCKET;
    //
    return(0);
}

/** *************************************************************
 * カテゴリ領域参照 開始 ：子プロセス用
 * forkされた子プロセス（nginx-moduleのworkerを指す）ではなく
 * 普通に別のプログラムモジュールから当該カテゴリにアクセスするための
 * ipcshm_categories_ptr インスタンスを生成する処理
 *
 * @param[in,out] ipc_categories  カテゴリリスト（未初期化のもの）
 * @param[in]     categories_on_process  カテゴリアクセサ（プロセス）
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int childinit_categories_area(ipcshm_categories_ptr* ipc_categories,categories_on_process_ptr categories_on_process){
    int				shmfd,n,filefd,retcd = RET_NG;
    char*			filemmap;

    // ステータスチェック
    // 既に初期化されているものは処理しない
    if ((char*)(*ipc_categories) != (char*)INVALID_SOCKET || !categories_on_process){
        fprintf(stderr, "filed.all-ready initialized.\n");
        return(RET_NG);
    }
    // 共有領域
    shmfd = shm_open(get_category_shmnm(),O_RDWR,0);
    if (shmfd < 0){
        fprintf(stderr, "filed.shm_open(%d:%s:%s).\n",errno,strerror(errno),get_category_shmnm());
        return(RET_NG);
    }
    (*ipc_categories) = (ipcshm_categories_ptr)mmap(0, IPC_CATEGORY_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, shmfd, 0);
    if ((char*)(*ipc_categories) == (char*)INVALID_SOCKET){
        close(shmfd);
        fprintf(stderr, "filed.mmap(%d:%s).\n",errno,strerror(errno));
        return(RET_NG);
    }
    (*ipc_categories)->category_shmid = shmfd;
    close(shmfd);
    // カテゴリ全体Mutexで同期
    {	mutex_lock_t  lock = {&((*ipc_categories)->category_all_mutex),RET_NG};
        start_lock(&lock);

        // 各カテゴリ参照を準備
        for(n = 0;n < CATEGORY_CNT;n++){
            // ファイル open -> mmap
            if ((filefd = open((*ipc_categories)->category[n].category_path,O_RDWR,S_IREAD | S_IWRITE)) == INVALID_SOCKET){
                fprintf(stderr, "filed.open(%s:%s).\n",(*ipc_categories)->category[n].category_path,strerror(errno));
                goto unlock;
            }
            if ((filemmap = (char*)mmap(NULL,
                                        (*ipc_categories)->category[n].filesize,
                                        PROT_READ | PROT_WRITE,
                                        MAP_SHARED,
                                        filefd,0)) == (char*)INVALID_SOCKET){
                close(filefd);
                fprintf(stderr, "filed.open(%s:%s).\n",(*ipc_categories)->category[n].category_path,strerror(errno));
                goto unlock;
            }
            categories_on_process->category[n].id			= n;
            categories_on_process->category[n].filesize		= (*ipc_categories)->category[n].filesize;
            categories_on_process->category[n].filefd		= filefd;
            categories_on_process->category[n].data_pointer	= filemmap;
        }
        retcd = RET_OK;
unlock:
        end_lock(&lock);
    }
    return(retcd);
}
/** *************************************************************
 * カテゴリ領域参照 終了 ：子プロセス用
 * forkされた子プロセス（nginx-moduleのworkerを指す）ではなく
 * 普通に別のプログラムモジュールから当該カテゴリにアクセスするための
 * ipcshm_categories_ptr インスタンスを解放
 *
 * @param[in,out] ipc_categories  カテゴリリスト（初期化済みのもの）
 * @param[in]     categories_on_process  カテゴリアクセサ（プロセス）
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int childuninit_categories_area(ipcshm_categories_ptr* ipc_categories,categories_on_process_ptr categories_on_process){
    int		n;
    // ステータスチェック
    if ((char*)(*ipc_categories) != (char*)INVALID_SOCKET || categories_on_process){
        {	mutex_lock_t lock = {&((*ipc_categories)->category_all_mutex), RET_NG};
            start_lock(&lock);

            // 各カテゴリ参照の終了
            for(n = 0;n < CATEGORY_CNT;n++){
                if (categories_on_process->category[n].filefd != INVALID_SOCKET){
                    close(categories_on_process->category[n].filefd);
                }
                categories_on_process->category[n].filefd = INVALID_SOCKET;

                if (categories_on_process->category[n].data_pointer != (char*)INVALID_SOCKET){
                    munmap(categories_on_process->category[n].data_pointer,categories_on_process->category[n].filesize);
                }
                categories_on_process->category[n].data_pointer = (char*)INVALID_SOCKET;
            }
            end_lock(&lock);
        }
        close((*ipc_categories)->category_shmid);
        munmap((char*)(*ipc_categories),IPC_CATEGORY_SIZE);
    }
    (*ipc_categories) = (ipcshm_categories_ptr)INVALID_SOCKET;
    //
    return(0);
}
/** *************************************************************
 * キー値より、対応するカテゴリハンドルを返却\n
 * ハッシュ化前のキー値から、対応するカテゴリアドレスを取得する\n
 *
 * @param[in]     ipc_categories  カテゴリリスト（筐体グローバル/named shm）
 * @param[in]     categories_on_process  カテゴリアクセサ（プロセス）
 * @param[in]     data                   データ値
 * @paran[in]     len                    ↑データ長
 * @result  category_on_process_ptr カテゴリ個別インスタンス
 ************************************************************* */
category_on_process_ptr get_category_unsafe(ipcshm_categories_ptr ipc_categories,categories_on_process_ptr categories_on_process,const char* data,uint32_t len){
    return(get_category_by_hashkey_unsafe(ipc_categories,categories_on_process,safe_hash(data,len)));
}
/** *************************************************************
 * カテゴリオペレーション用ハンドル取得（キー指定）\n
 * ハッシュ値から、対応するカテゴリアドレスを取得する\n
 *
 * @param[in]     ipc_categories  カテゴリリスト（筐体グローバル/named shm）
 * @param[in]     categories_on_process  カテゴリアクセサ（プロセス）
 * @param[in]     hashed_key             ハッシュ値
 * @result  category_on_process_ptr カテゴリ個別インスタンス
 ************************************************************* */
category_on_process_ptr get_category_by_hashkey_unsafe(ipcshm_categories_ptr ipc_categories,categories_on_process_ptr categories_on_process,uint32_t hashed_key){
    uint32_t	hashed;
    // 引数チェック
    if (!ipc_categories || !hashed_key || !categories_on_process){
        return(NULL);
    }
    // ステータスチェック
    if ((char*)ipc_categories == (char*)INVALID_SOCKET){
        return(NULL);
    }
    // ハッシュ値より、カテゴリINDEXが決定する
    hashed = (hashed_key % CATEGORY_CNT);
    if (hashed < CATEGORY_CNT){
        if (categories_on_process->category[hashed].filefd != INVALID_SOCKET){
            if (categories_on_process->category[hashed].data_pointer != (char*)INVALID_SOCKET){
                // 有効なカテゴリポインタ を返却
                return(&(categories_on_process->category[hashed]));
            }
        }
    }
    return(NULL);
}
/** *************************************************************
 * カテゴリindex取得（キー指定）\n
 * ハッシュ値から、対応するカテゴリのインでクスを取得する\n
 *
 * @param[in]     ipc_categories  カテゴリリスト（筐体グローバル/named shm）
 * @param[in]     categories_on_process  カテゴリアクセサ（プロセス）
 * @param[in]     hashed_key             ハッシュ値
 * @result  int インデックス値：エラー時 < 0
 ************************************************************* */
int get_category_id_by_hashkey_unsafe(ipcshm_categories_ptr ipc_categories,categories_on_process_ptr categories_on_process,uint32_t hashed_key){
    uint32_t	hashed;
    // 引数チェック
    if (!ipc_categories || !hashed_key || !categories_on_process){
        return(-1);
    }
    // ステータスチェック
    if ((char*)ipc_categories == (char*)INVALID_SOCKET){
        return(-1);
    }
    // ハッシュ値より、カテゴリINDEXが決定する
    hashed = (hashed_key % CATEGORY_CNT);
    if (hashed < CATEGORY_CNT){
        if (categories_on_process->category[hashed].filefd != INVALID_SOCKET){
            if (categories_on_process->category[hashed].data_pointer != (char*)INVALID_SOCKET){
                // 有効なindex
                return((int)hashed);
            }
        }
    }
    return(-1);
}
/** *************************************************************
 * プロセスアクセス：カテゴリ領域をファイルシステムより初期化\n
 *
 * @param[in]     ipc_categories  カテゴリリスト（筐体グローバル/named shm）
 * @param[in]     categories_on_process  カテゴリアクセサ（プロセス）
 * @param[in]     child           子プロセス!=0
 * @result  int 成功時：RET_OK/エラー時:!=RET_OK
 ************************************************************* */
int init_categories_from_file(ipcshm_categories_ptr ipc_categories,categories_on_process_ptr categories_on_process, int child){
    int			n,filefd,retcd = RET_NG;
    uint32_t	category_size = 0;
    char*		filemmap;
    // カテゴリ領域がアタッチされていない、、
    if (!ipc_categories || !categories_on_process){
        fprintf(stderr, "missing attached.\n");
        return(RET_NG);
    }
    // カテゴリ全体Mutexで同期
    {	mutex_lock_t lock = {&(ipc_categories->category_all_mutex),RET_NG};
        start_lock(&lock);
        if (isvalid_lock(&lock) == RET_OK){
            // 全カテゴリを準備
            for(n = 0;n < CATEGORY_CNT;n++){
                // indexファイルは[0] 固定
                if (n == CATEGORY_MMAPIDX){ category_size = CATEGORY_SIZE_IDX;
                }else{ category_size = CATEGORY_SIZE; }
                // ファイルが存在しなければ、データファイル用初期サイズで生成
                if (!child){
                    if (_create_file(ipc_categories->category[n].category_path,category_size) != RET_OK){
                        fprintf(stderr, "_create_category_file(%s/%s/%u).\n", strerror(errno), ipc_categories->category[n].category_path,category_size);
                        goto unlock;
                    }
                }
                // openして、mmap しておく
                if ((filefd = open(ipc_categories->category[n].category_path,O_RDWR,S_IREAD | S_IWRITE)) == INVALID_SOCKET){
                    fprintf(stderr, "open(%s).\n", strerror(errno));
                    goto unlock;
                }
                if ((filemmap = (char*)mmap(NULL,category_size,PROT_READ | PROT_WRITE,MAP_SHARED,filefd,0)) == (char*)INVALID_SOCKET){
                    fprintf(stderr, "mmap(%s).\n", strerror(errno));
                    close(filefd);
                    goto unlock;
                }
                categories_on_process->category[n].id			= n;
                categories_on_process->category[n].filefd		= filefd;
                categories_on_process->category[n].data_pointer	= filemmap;
                categories_on_process->category[n].filesize		= category_size;
            }
        }
        retcd = RET_OK;
unlock:
        end_lock(&lock);
    }
    return(retcd);
}


