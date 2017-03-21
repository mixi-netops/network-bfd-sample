/******************************************************************************/
/*! @addtogroup in process file mmap kvs db(shared memory map - reallocation)
    @file       smmr_database.cc
    @brief      データベースオペレーション
******************************************************************************/
#include "smmr_database.h"
#include "smmr_categories.h"
#include "smmr_pagedbuffer.h"
#include "smmr_tree.h"
#include "smmr_memory.h"
#include <assert.h>



// 検索結果コールバック（index）
static int _findindex_callback_for_search(node_key_ptr,void*);
// 更新、挿入用コールバック
static int _findindex_callback_for_update_insert(node_key_ptr,void*);

/** *************************************************************
 * 検索
 *
 *
 * @param[in]     ipc_datafiles         データファイル管理インスタンス
 * @param[in]     datafiles_on_process  データファイルアクセサ（プロセス単位）
 * @param[in]     inst                  インデックストップツリーインスタンス（btreeのルートを含む）
 * @param[in]     key                   キーデータ
 * @param[in]     keylen                ↑キー長
 * @param[out]    val                   取得データ（返却値）アロケートされ返却
 * @param[out]    vallen                ↑取得データ長
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int search(ipcshm_datafiles_ptr ipc_datafiles,datafiles_on_process_ptr datafiles_on_process,tree_instance_ptr inst,const char* key,uint32_t keylen,char** val,uint32_t* vallen){
    find_container_t		container;
    uint32_t				hashedid,n;
    int						ret = RET_NG;
    // 引数チェック
    if ((char*)(inst) == (char*)INVALID_SOCKET || !inst || !datafiles_on_process){
        return(RET_NG);
    }
    if ((char*)(ipc_datafiles) == (char*)INVALID_SOCKET || !ipc_datafiles){
        return(RET_NG);
    }
    if (!key || (*val) || !vallen){
        return(RET_NG);
    }
    // コンテナ初期化
    memset(&container,0,sizeof(find_container_t));
    // キーハッシュを準備
    hashedid = safe_hash(key,keylen);
    // コンテナー設定
    container.ipc_datafiles	= ipc_datafiles;
    container.datafiles		= datafiles_on_process;
    container.inst			= inst;
    container.hashedid		= hashedid;
    container.flag			= RET_NG;
    container.key			= (char*)malloc(keylen);
    memcpy(container.key,key,keylen);
    container.keylen		= keylen;

    // 対象カテゴリからハッシュ値が合致する全データをコールバック受信
    // ※ハッシュキーが重複するデータはひとまず全てcallback対象となる
    find_tree_only_keyhash_safe(inst,hashedid,_findindex_callback_for_search,&container);
    //
    if (container.val && container.vallen && container.flag == RET_OK){
        ret = RET_OK;
        // 検索結果のallocated-heapを返却値へ
        if (container.val){ (*val) = container.val; }
        if (container.vallen){ (*vallen) = container.vallen; }
    }else{
        if (container.val){ free(container.val); }
    }
    // container.val は、更に返却先が解放する
    //  〃.key はここで使い終わっているので解放
    if (container.key){ free(container.key); }

    // expire判定されたアイテムがある場合index-remove実施
    for(n = 0;n < REMOVE_RESERVED_COUNT;n++){
        if (!ISEMPTY_KEY(container.remove_reserved[n])){
            remove_tree_safe(inst,&(container.remove_reserved[n]));
        }else{
            // 削除予約リストは連続してセット、empty でターミネートされている
            break;
        }
    }
    //
    return(ret);
}

/** *************************************************************
 * 検索結果コールバック（index - search）\n
 *  ※ カテゴリ毎のlock内でcallbackされるので、ネストして
 *  ※ tree系の処理を行えません
 *
 * @param[in]     node       ノードアドレス
 * @param[in]     usrval     ユーザデータ
 * @result  RET_OK=コールバックLoopを継続、RET_OK!=コールバックLoopを終了
 ************************************************************* */
int _findindex_callback_for_search(node_key_ptr node,void* usrval){
    find_container_ptr			container = (find_container_ptr)usrval;
    ipcshm_datafiles_ptr		ipcdataf = NULL;
    ipcshm_dataitm_ptr			dataitm = NULL;
    datafiles_on_process_ptr	datafiles = NULL;
    char*						itemptr = NULL;
    uint32_t					curtime = time(0),n;
    int                         retcd = RET_NG;
    // ステータスチェック
    if (!node || !container){
        container->flag = RET_NG;
        return(RET_NG);
    }
    // データファイルアクセサ
    ipcdataf	= container->ipc_datafiles;
    datafiles	= container->datafiles;

    // ノードのfield 値が、有効な値である事
    if (node->data_field > IPC_DATAFILE_MAX){
        container->flag = RET_NG;
        return(RET_NG);
    }
    if (datafiles->filefd[node->data_field] == INVALID_SOCKET || datafiles->filefd[node->data_field] == 0){
        container->flag = RET_NG;
        return(RET_NG);
    }
    if (datafiles->data_pointer[node->data_field] == (char*)INVALID_SOCKET || datafiles->data_pointer[node->data_field] == NULL){
        container->flag = RET_NG;
        return(RET_NG);
    }
    // コールバックされたnodeオブジェクトにデータファイルid + オフセットが
    // 格納されているので、それで、データアクセスし、キー値が完全一致することを
    // 評価する、キー値が一致したデータが対象である
    // ---------------
    // lock順序は全てのアクセスにおいて[category (index - lock)] -> [datafile - lock]
    // の順序で実装すること（maybe with out deadlock）
    {	mutex_lock_t lock={&(ipcdataf->datafile_mutex[node->data_field]),RET_NG};
        start_lock(&lock);
        if (isvalid_lock(&lock) != RET_OK){
            container->flag = RET_NG;
            goto unlock;
        }
        if ((dataitm = (ipcshm_dataitm_ptr)(datafiles->data_pointer[node->data_field] + node->data_offset)) == NULL){
            container->flag = RET_NG;
            goto unlock;
        }
        if ((char*)dataitm == (char*)INVALID_SOCKET){
            container->flag = RET_NG;
            goto unlock;
        }
        // シグネチャチェック
        // ※signature 不正データがある時は、全体的に不正と判定
        // ※データ長 > 0 が必要
        if (dataitm->signature != DATAITEM_SIGNATURE_ID || (dataitm->keylen + dataitm->vallen + sizeof(ipcshm_dataitm_t)) != dataitm->length){
            container->flag = RET_NG;
            goto unlock;
        }
        // キー長、値長チェック
        if (!dataitm->keylen || !dataitm->vallen){
            container->flag = RET_NG;
            goto unlock;
        }
        // expire チェック
        // ※次データへ＋index-expireさせるので、RET_WARN 返却
        if (dataitm->expire && dataitm->expire < curtime){
            // indexからの削除予約リストの空きエリアにプッシュ
            for(n = 0;n < REMOVE_RESERVED_COUNT;n++){
                if (ISEMPTY_KEY(container->remove_reserved[n])){
                    container->remove_reserved[n] = (*node);
                    break;
                }
            }
            // データファイルの、expiredデータ位置を空き領域に変更
            change_free_to_datafile(ipcdataf,datafiles,node,1);
            retcd = RET_WARN;
            goto unlock;
        }
        // キー長一致チェック
        // ※次データへ
        if (container->keylen != dataitm->keylen){
            retcd = RET_OK;
            goto unlock;
        }
        // validation 通過 -> キー一致判定
        itemptr = (datafiles->data_pointer[node->data_field] + node->data_offset + sizeof(ipcshm_dataitm_t));
        if (memcmp(itemptr,container->key,container->keylen) == 0){
            // 値を返却値へセット
            // ここでallocateされたheap領域は、呼び出し元で解放すること
            container->vallen	= dataitm->vallen;
            container->val		= (char*)malloc(dataitm->vallen);
            memcpy(container->val,itemptr + dataitm->keylen,dataitm->vallen);

            // キー一致したので、ここで検索loop終了
            container->flag		= RET_OK;
            goto unlock;
        }
        retcd = RET_OK;
    unlock:
        end_lock(&lock);
    }
    // キー長は一致したが、キー値 不一致なので次データへ
    return(retcd);
}

/** *************************************************************
 * 格納（キーバリューで文字列をセット）
 *
 *
 * @param[in]     ipc_datafiles         データファイル管理インスタンス
 * @param[in]     datafiles_on_process  データファイルアクセサ（プロセス単位）
 * @param[in]     inst                  インデックストップツリーインスタンス（btreeのルートを含む）
 * @param[in]     key                   キーデータ
 * @param[in]     keylen                ↑キー長
 * @param[in]     val                   値データ：設定値
 * @param[in]     vallen                ↑データ長
 * @param[in]     expire                unix time
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int store(ipcshm_datafiles_ptr ipc_datafiles,datafiles_on_process_ptr datafiles_on_process,tree_instance_ptr inst,const char* key,uint32_t keylen,char* val,uint32_t vallen,uint32_t expire){
    find_container_t		container;
    uint32_t				hashedid;
    // 引数チェック
    if ((char*)(inst) == (char*)INVALID_SOCKET || !inst){
        fprintf(stderr, "invalid instance pointer\n");
        return(RET_NG);
    }
    if ((char*)(ipc_datafiles) == (char*)INVALID_SOCKET || !ipc_datafiles){
        fprintf(stderr, "invalid datafiles \n");
        return(RET_NG);
    }
    if (!key || !keylen || !val || !vallen){
        fprintf(stderr, "invalid arguments.\n");
        return(RET_NG);
    }
    // コンテナ初期化
    memset(&container,0,sizeof(find_container_t));
    // キーハッシュを準備
    hashedid				= safe_hash(key,keylen);
    // コンテナー設定
    container.ipc_datafiles	= ipc_datafiles;
    container.datafiles		= datafiles_on_process;
    container.inst			= inst;
    container.hashedid		= hashedid;
    container.flag			= RET_MOREDATA;
    container.key			= (char*)malloc(keylen);
    memcpy(container.key,key,keylen);
    container.keylen		= keylen;
    container.val			= (char*)malloc(vallen);
    memcpy(container.val,val,vallen);
    container.vallen		= vallen;
    container.expire		= expire;

    // 対象カテゴリからハッシュ値が合致する全データをコールバック受信
    // ※ハッシュキーが重複するデータはひとまず全てcallback対象となる
    find_tree_only_keyhash_safe(inst,hashedid,_findindex_callback_for_update_insert,&container);
    //
    if (container.flag == RET_WARN){
        // キー値は存在したが、データ上書きが不可能だったケース
        // カレントIndexは削除して、新たなindexとして新規indexを挿入する
        // データファイルは、最大空きのあるデータファイルを選択して
        // そこを利用する
        container.flag = RET_NG;
        while(1){
            // まず、カレントindexノードを削除し
            if (remove_tree_safe(inst,&(container.resultnode)) != RET_OK){ break; }
            // データファイルの、元データ位置を空き領域に変更
            if (change_free_to_datafile(ipc_datafiles,datafiles_on_process,&(container.resultnode),0) != RET_OK){ break; }
            // 先に空き領域が有れば、それを利用
            if (insert_to_datafile_at_freepage(&container) != RET_OK){
                // 空き領域が無ければ、データファイルに今回データを挿入し
                if (insert_to_datafile(&container) != RET_OK){ break; }
            }
            // ↑で挿入できたアドレスで新規indexを挿入
            if (add_tree_safe(inst,&(container.resultnode)) != RET_OK){ break; }
            // 全て処理成功
            container.flag = RET_OK;
            break;
        }
    }else if (container.flag == RET_MOREDATA){
        // キー値が存在しなかったので、普通に挿入
        container.flag = RET_NG;
        while(1){
            // 先に空き領域が有れば、それを利用
            if (insert_to_datafile_at_freepage(&container) != RET_OK){
                // 空き領域が無ければ、データファイルに今回データを挿入し
                if (insert_to_datafile(&container) != RET_OK){ break; }
            }
            // ↑で挿入できたアドレスで新規indexを挿入
            if (add_tree_safe(inst,&(container.resultnode)) != RET_OK){ break; }
            // 全て処理成功
            container.flag = RET_OK;
            break;
        }
    }
    // container.val
    //  〃.key はここで使い終わっているので解放
    if (container.val){ free(container.val); }
    if (container.key){ free(container.key); }
    //
    if (container.flag != RET_OK){
        fprintf(stderr, "enough data file..??\n");
    }
    //
    return(container.flag==RET_OK?RET_OK:RET_NG);
}

/** *************************************************************
 * 検索結果コールバック（index - search）\n
 * コールバックIndex値より、update なのか、insertなのか判定し\n
 * 旧データバッファがサイズ充分で、上書き可能な場合\n
 * 該当領域にデータコピーして完了\n
 * データバッファサイズが足りない場合、RET_WARN を返却ステータスにセットする\n
 * その場合、呼び出し元は、ロック外でDELETE -> INSERT処理を実施する\n
 * \n
 *  ※ カテゴリ毎のlock内でcallbackされるので、ネストして\n
 *  ※ tree系の処理を行えません\n
 *
 * @param[in]     node       ノードアドレス
 * @param[in]     usrval     ユーザデータ
 * @result  RET_OK=コールバックLoopを継続、RET_OK!=コールバックLoopを終了
 ************************************************************* */
int _findindex_callback_for_update_insert(node_key_ptr node,void* usrval){
    find_container_ptr			container = (find_container_ptr)usrval;
    ipcshm_datafiles_ptr		ipcdataf = NULL;
    ipcshm_dataitm_ptr			dataitm = NULL;
    datafiles_on_process_ptr	datafiles = NULL;
    char*						itemptr = NULL;
    uint32_t					curtime = time(0);
    uint32_t					aligned_size,aligned_size_old,item_len;
    int                         retcd = RET_NG;

    // ステータスチェック
    if (!node || !container){
        return(RET_NG);
    }
    // データファイルアクセサ
    ipcdataf	= container->ipc_datafiles;
    datafiles	= container->datafiles;

    // ノードのfield 値が、有効な値である事
    if (node->data_field > IPC_DATAFILE_MAX){
        container->flag = RET_NG;
        return(RET_NG);
    }
    if (datafiles->filefd[node->data_field] == INVALID_SOCKET || datafiles->filefd[node->data_field] == 0){
        container->flag = RET_NG;
        return(RET_NG);
    }
    if (datafiles->data_pointer[node->data_field] == (char*)INVALID_SOCKET || datafiles->data_pointer[node->data_field] == NULL){
        container->flag = RET_NG;
        return(RET_NG);
    }
    // コールバックされたnodeオブジェクトにデータファイルid + オフセットが
    // 格納されているので、それで、データアクセスし、キー値が完全一致することを
    // 評価する、キー値が一致したデータが対象である
    // ---------------
    // lock順序は全てのアクセスにおいて[category (index - lock)] -> [datafile - lock]
    // の順序で実装すること
    {	mutex_lock_t lock = {&(ipcdataf->datafile_mutex[node->data_field]), RET_NG};
        start_lock(&lock);
        if (isvalid_lock(&lock) != RET_OK){
            container->flag = RET_NG;
            goto unlock;
        }
        if ((dataitm = (ipcshm_dataitm_ptr)(datafiles->data_pointer[node->data_field] + node->data_offset)) == NULL){
            container->flag = RET_NG;
            goto unlock;
        }
        if ((char*)dataitm == (char*)INVALID_SOCKET){
            container->flag = RET_NG;
            goto unlock;
        }
        // シグネチャチェック
        // ※signature 不正データがある時は、全体的に不正と判定
        // ※データ長 > 0 が必要
        if (dataitm->signature != DATAITEM_SIGNATURE_ID || (dataitm->keylen + dataitm->vallen + sizeof(ipcshm_dataitm_t)) != dataitm->length){
            container->flag = RET_NG;
            goto unlock;
        }
        // キー長、値長チェック
        if (!dataitm->keylen || !dataitm->vallen){
            container->flag = RET_NG;
            goto unlock;
        }
        // expire チェック
        // ※次データをみるので、RET_OK 返却
        if (dataitm->expire && dataitm->expire < curtime){
            retcd = RET_OK;
            goto unlock;
        }
        // キー長一致チェック
        // ※次データへ
        if (container->keylen != dataitm->keylen){
            retcd = RET_OK;
            goto unlock;
        }
        // validation 通過 -> キー一致判定
        itemptr = (datafiles->data_pointer[node->data_field] + node->data_offset + sizeof(ipcshm_dataitm_t));
        if (memcmp(itemptr,container->key,container->keylen) == 0){

            // 格納サイズは、128byteにアライメントされている
            aligned_size_old	= (dataitm->length % IPC_DATAFILE_ALIGNED)==0?
                                  (dataitm->length):
                                  (((dataitm->length / IPC_DATAFILE_ALIGNED) + 1) * IPC_DATAFILE_ALIGNED);
            // 新しい値でのアライメントサイズを計算
            item_len			= (container->keylen + container->vallen + sizeof(ipcshm_dataitm_t));
            aligned_size		= (item_len % IPC_DATAFILE_ALIGNED)==0?
                                  (item_len):
                                  (((item_len / IPC_DATAFILE_ALIGNED) + 1) * IPC_DATAFILE_ALIGNED);
            // キーが一致し且つ
            // hitした前回データ長のアライメント後領域が、今回データ長のアライメント領域 に足りる場合
            // 同一データ領域に、今回データを上書きして終了
            if (aligned_size_old >= aligned_size){
                memcpy((itemptr + dataitm->keylen),container->val,container->vallen);
                container->flag		= RET_OK;

                // データアイテム長が異なる場合、データ長＋全体長調整
                if (dataitm->vallen != container->vallen){
                    dataitm->vallen = container->vallen;
                    dataitm->length = (dataitm->keylen + dataitm->vallen + sizeof(ipcshm_dataitm_t));
                }
                goto unlock;
            }
            // 前回データが、今回データに不足している場合
            // 現在indexは削除状態とし、新たにindexを挿入する
            // これによって、index領域においては、[usedアイテム + 1/ free アイテム +1] となり
            // 次回のupdate/insert処理で前述 [freeアイテム] が再利用される
            container->flag						= RET_WARN;
            container->resultnode.keyhash		= node->keyhash;
            container->resultnode.data_field	= node->data_field;
            container->resultnode.data_offset	= node->data_offset;
            goto unlock;
        }
        retcd = RET_OK;
    unlock:
        end_lock(&lock);
    }
    // キー長は一致したが、キー値 不一致なので次データへ
    container->flag		= RET_MOREDATA;
    //
    return(retcd);
}

/** *************************************************************
 * メモリ状況表示
 *
 *
 * @param[in]     inst                  インデックスツリーインスタンス（btreeのルートを含む）
 * @param[out]    val                   値データ：返却値
 * @param[out]    vallen                ↑データ長
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int print(tree_instance_ptr inst,char** val,uint32_t* vallen){
    int				n;
    uint32_t		allocated_txt_len = 0;
    char*			allocated_txt_buffer = NULL;
    paged_buffer_ptr text_buffer = NULL;
    // 引数チェック
    if ((char*)(inst) == (char*)INVALID_SOCKET || !inst){
        return(RET_NG);
    }
    if (pagedbuffer_create(&text_buffer) != RET_OK){
        return(RET_NG);
    }
    // 全カテゴリ をプリントする
    for(n = 0;n < CATEGORY_CNT;n++){
        inst->curid = n;
        // カテゴリ別ロック
        {	mutex_lock_t	lock = {&(inst->itm_p[inst->curid].category->category_mutex), RET_NG};
            start_lock(&lock);
            if (isvalid_lock(&lock) == RET_OK){
                // カテゴリ別ページ状況をフラッシュ
                rebuild_page_statics(&(inst->categories_on_process->category[inst->curid]));
                // バッファ準備
                if (makebuffer_memory_status_unsafe(&(inst->categories_on_process->category[inst->curid]),&allocated_txt_buffer,&allocated_txt_len) == RET_OK){
                    if (allocated_txt_buffer && allocated_txt_len){
                        pagedbuffer_append(text_buffer, allocated_txt_buffer,allocated_txt_len);
                    }
                    if (allocated_txt_buffer){ free(allocated_txt_buffer); }
                }
            }
            end_lock(&lock);
        }
    }
    (*val)		= pagedbuffer_dup(text_buffer);
    (*vallen)	= pagedbuffer_current_size(text_buffer);
    //
    pagedbuffer_remove(&text_buffer);
    //
    return(RET_OK);
}


/** *************************************************************
 * データファイル領域の空き領域に、データ設定（replace）\n
 * ※データファイルは、128 bytes/pageとして利用し、各データファイルヘッダには\n
 * ※ページ利用状況が保管される\n
 *
 * @param[in]     container         コンテナインスタンス
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int insert_to_datafile_at_freepage(find_container_ptr container){
    uint32_t					n,m,offset_bk,aligned_size,remain_size,shift_size;
    //uint32_t					n,m,offset_bk,aligned_size,shift_size;
    uint32_t					findbits,finded_offset,bitmap_pageno;
    ipcshm_datafiles_ptr		ipcdataf = NULL;
    db_table_header_ptr			dbfileheader = NULL;
    ipcshm_dataitm_t			curitm;
    datafiles_on_process_ptr	datafiles = NULL;
    int 						completed = 0;
    uint32_t                    distid[IPC_DATAFILE_MAX] = {0};

    // 引数チェック
    if (!container){
        return(RET_NG);
    }
    if (!container->ipc_datafiles || !container->inst){
        return(RET_NG);
    }
    if ((char*)(container->ipc_datafiles) == (char*)INVALID_SOCKET){
        return(RET_NG);
    }
    memset(&curitm,0,sizeof(ipcshm_dataitm_t));
    curitm.signature	= DATAITEM_SIGNATURE_ID;
    curitm.length		= (container->keylen + container->vallen + sizeof(ipcshm_dataitm_t));
    curitm.flags		= 0;
    curitm.expire		= container->expire;
    curitm.keylen		= container->keylen;
    curitm.vallen		= container->vallen;

    // 格納サイズは、128byteにアライメントしておく
    aligned_size		= (curitm.length % IPC_DATAFILE_ALIGNED)==0?
                          (curitm.length):
                          (((curitm.length / IPC_DATAFILE_ALIGNED) + 1) * IPC_DATAFILE_ALIGNED);
    remain_size			= (aligned_size - curitm.length);
    shift_size			= (aligned_size / IPC_DATAFILE_ALIGNED);
    //
    ipcdataf			= container->ipc_datafiles;
    datafiles			= container->datafiles;
    // 新規投入データが、キーハッシュによってデータファイル分散するよう
    // データファイルオペレーションスタートポジションを調整
    m = safe_hash(container->key, container->keylen)%IPC_DATAFILE_MAX;
    for(n = 0;n < IPC_DATAFILE_MAX;n++){
        distid[n] = (m++);
        if (m >= IPC_DATAFILE_MAX){
            m=0;
        }
    }
    //
    for(n = 0;n < IPC_DATAFILE_MAX;n++){
        // データファイル用個別mutex - lock
        {	mutex_lock_t lock = {&(ipcdataf->datafile_mutex[distid[n]]), RET_NG};
            start_lock(&lock);
            if (isvalid_lock(&lock) != RET_OK){
                end_lock(&lock);
                break;
            }
            //
            if (datafiles->data_pointer[distid[n]] != (char*)INVALID_SOCKET){
                // 有効なデータポインタ の場合、空き領域をチェック
                dbfileheader = (db_table_header_ptr)datafiles->data_pointer[distid[n]];
                // データアイテム設定
                if (dbfileheader->remain_length <= aligned_size){
                    end_lock(&lock);
                    continue;
                }
                // free bitmapから、shift_size * IPC_DATAFILE_ALIGNED 分連続した
                // 空き領域を検索してそこを利用する
                for(finded_offset = findbits = m = 0;m < (IPC_DATAFILE_PAGECNT - 1);m++){
                    if (!(dbfileheader->bitmap[(m + 1) / 8] & (1<<((m + 1)%8)))){
                        if ((++findbits) >= shift_size){
                            // 連続した空き領域が見つかっている
                            finded_offset = (((m + 1) - (shift_size - 1)) * IPC_DATAFILE_ALIGNED);
                            break;
                        }
                    }else{
                        findbits = 0;
                    }
                }
                // shift_size == 1（最少ページサイズ）＋ finded_offset == 0（空きがない）
                // ケースは、当該データファイルがFULL状態であることを意味する
                // ファイルヘッダ：空き容量 <= 0を設定する
                if (shift_size == 1 && finded_offset == 0){
                    dbfileheader->remain_length = 0;
                }
                // 新規データ格納に充分な空き領域を持つ 「データファイル」の空き領域に
                // データアイテム設定
                if (finded_offset != 0){
                    // 空き領域を利用中にセット
                    bitmap_pageno = (finded_offset % IPC_DATAFILE_ALIGNED)==0?(finded_offset / IPC_DATAFILE_ALIGNED):((finded_offset / IPC_DATAFILE_ALIGNED) + 1);
                    for(m = bitmap_pageno;m < (bitmap_pageno + (aligned_size / IPC_DATAFILE_ALIGNED));m++){
                        dbfileheader->bitmap[m/8]|=(1<<(m%8));
                    }
                    // データファイルヘッダ分進める
                    finded_offset += sizeof(db_table_header_t);
                    if ((finded_offset + aligned_size) > IPC_DATAFILE_TTLSIZE){
                        fprintf(stderr, "data buffer is overflow(%d,%d)",finded_offset,aligned_size);
                    }
                    // 空き領域の先頭から、今回のデータをコピーする
                    offset_bk = finded_offset;
                    // アイテムヘッダ
                    memcpy(datafiles->data_pointer[distid[n]] + finded_offset,
                           &curitm,
                           sizeof(curitm));
                    finded_offset += sizeof(curitm);
                    // キーデータ
                    memcpy(datafiles->data_pointer[distid[n]] + finded_offset,
                           container->key,
                           container->keylen);
                    finded_offset += container->keylen;
                    // バリューデータ
                    memcpy(datafiles->data_pointer[distid[n]] + finded_offset,
                           container->val,
                           container->vallen);
                    finded_offset += container->vallen;
                    finded_offset += remain_size;
                    // 最終更新をupdate
                    dbfileheader->last_updated = time(0);
                    // bitmapから検索できた空き領域が
                    // データファイルのカレント利用領域を超える場合
                    // 利用済み領域を更新する、また利用個数サマリもインクリメント
                    if (finded_offset > dbfileheader->used_length){
                        dbfileheader->used_length = finded_offset;
                        dbfileheader->used_count ++;
                    }
                    // データファイル残量を更新
                    if (dbfileheader->remain_length > (finded_offset - offset_bk)){
                        dbfileheader->remain_length -= (finded_offset - offset_bk);
                    }else{
                        fprintf(stderr, "invalid datafile header(remain) %llu -> to zero. \n",dbfileheader->remain_length);
                        dbfileheader->remain_length = 0;
                    }
                    // データ挿入完了
                    completed = 1;
                    // 今回追加したデータアイテムのオフセット値を返却
                    container->resultnode.keyhash		= container->hashedid;
                    container->resultnode.data_field	= (uint8_t)distid[n];
                    container->resultnode.data_offset	= offset_bk;
                    //
                    end_lock(&lock);
                    break;
                }
            }
            end_lock(&lock);
        }
    }
    return(completed?RET_OK:RET_NG);
}

/** *************************************************************
 * データファイル領域の最後に、データを追加\n
 * ※データファイルは、128 bytes/pageとして利用し、各データファイルヘッダには\n
 * ※ページ利用状況が保管される\n
 *
 * @param[in]     container         コンテナインスタンス
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int insert_to_datafile(find_container_ptr container){
    uint32_t					n,m,offset_bk,aligned_size,remain_size,bitmap_pageno;
    ipcshm_datafiles_ptr		ipcdataf = NULL;
    db_table_header_ptr			dbfileheader = NULL;
    datafiles_on_process_ptr	datafiles = NULL;
    ipcshm_dataitm_t			curitm;
    int 						completed = 0;
    uint32_t                    distid[IPC_DATAFILE_MAX] = {0};

    // 引数チェック
    if (!container){
        return(RET_NG);
    }
    if (!container->ipc_datafiles || !container->inst){
        return(RET_NG);
    }
    if ((char*)(container->ipc_datafiles) == (char*)INVALID_SOCKET){
        return(RET_NG);
    }
    memset(&curitm,0,sizeof(ipcshm_dataitm_t));
    curitm.signature	= DATAITEM_SIGNATURE_ID;
    curitm.length		= (container->keylen + container->vallen + sizeof(ipcshm_dataitm_t));
    curitm.flags		= 0;
    curitm.expire		= container->expire;
    curitm.keylen		= container->keylen;
    curitm.vallen		= container->vallen;

    // 格納サイズは、128byteにアライメントしておく
    aligned_size		= (curitm.length % IPC_DATAFILE_ALIGNED)==0?
                          (curitm.length):
                          (((curitm.length / IPC_DATAFILE_ALIGNED) + 1) * IPC_DATAFILE_ALIGNED);
    remain_size			= (aligned_size - curitm.length);
    //
    ipcdataf			= container->ipc_datafiles;
    datafiles			= container->datafiles;
    // データファイルが、キーハッシュによってデータファイル分散するよう
    // データファイルオペレーションスタートポジションを調整
    m = safe_hash(container->key, container->keylen)%IPC_DATAFILE_MAX;
    for(n = 0;n < IPC_DATAFILE_MAX;n++){
        distid[n] = (m++);
        if (m >= IPC_DATAFILE_MAX){
            m=0;
        }
    }

    //
    for(n = 0;n < IPC_DATAFILE_MAX;n++){
        // データファイル用個別mutex - lock
        {	mutex_lock_t lock = {&(ipcdataf->datafile_mutex[distid[n]]), RET_NG};
            start_lock(&lock);
            if (isvalid_lock(&lock) != RET_OK){
                end_lock(&lock);
                break;
            }
            //
            if (datafiles->data_pointer[distid[n]] != (char*)INVALID_SOCKET){
                // 有効なデータポインタ の場合、空き領域をチェック
                dbfileheader = (db_table_header_ptr)datafiles->data_pointer[distid[n]];
                // 新規データ格納に充分な空き領域を持つ 「データファイル」の最終アイテムとして
                // データアイテム設定
                if (dbfileheader->remain_length > aligned_size){
                    // ページ管理bitmapを利用中に設定
                    bitmap_pageno = (dbfileheader->used_length % IPC_DATAFILE_ALIGNED)==0?
                                    (dbfileheader->used_length / IPC_DATAFILE_ALIGNED):
                                    ((dbfileheader->used_length / IPC_DATAFILE_ALIGNED) + 1);
                    for(m = bitmap_pageno;m < (bitmap_pageno + (aligned_size / IPC_DATAFILE_ALIGNED));m++){
                        dbfileheader->bitmap[m/8]|=(1<<(m%8));
                    }
                    // 空き領域の先頭から、今回のデータをコピーする
                    offset_bk = dbfileheader->used_length;
                    // アイテムヘッダ
                    memcpy(datafiles->data_pointer[distid[n]] + dbfileheader->used_length,
                           &curitm,
                           sizeof(curitm));
                    dbfileheader->used_length += sizeof(curitm);
                    // キーデータ
                    memcpy(datafiles->data_pointer[distid[n]] + dbfileheader->used_length,
                           container->key,
                           container->keylen);
                    dbfileheader->used_length += container->keylen;
                    // バリューデータ
                    memcpy(datafiles->data_pointer[distid[n]] + dbfileheader->used_length,
                           container->val,
                           container->vallen);
                    dbfileheader->used_length += container->vallen;
                    // アライメントパディング 分を進める
                    dbfileheader->used_length += remain_size;
                    // 使用件数をincrement
                    dbfileheader->used_count ++;
                    dbfileheader->last_updated = time(0);
                    if (dbfileheader->remain_length > (dbfileheader->used_length - offset_bk)){
                        dbfileheader->remain_length -= (dbfileheader->used_length - offset_bk);
                    }else{
                        printf("invalid datafile header(remain) %llu -> to zero. \n",dbfileheader->remain_length);
                        dbfileheader->remain_length = 0;
                    }
                    // データ挿入完了
                    completed = 1;
                    // 今回追加したデータアイテムのオフセット値を返却
                    container->resultnode.keyhash		= container->hashedid;
                    container->resultnode.data_field	= (uint8_t)distid[n];
                    container->resultnode.data_offset	= offset_bk;
                    end_lock(&lock);
                    break;
                }
            }
            end_lock(&lock);
        }
    }
    return(completed?RET_OK:RET_NG);
}
/** *************************************************************
 * データファイル指定データアイテムを空き領域としてマーク\n
 *
 * @param[in]     ipcdataf              データファイル管理インスタンス（筐体グローバルshm）
 * @param[in]     datafiles_on_process  データファイルアクセサ（プロセス毎）
 * @param[in]     node                  ノードインスタンス
 * @param[in]     nolock                ロック必要・不要フラグ
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int change_free_to_datafile(ipcshm_datafiles_ptr ipcdataf,datafiles_on_process_ptr datafiles_on_process,node_key_ptr node,int nolock){
    uint32_t				m,aligned_size;
    uint32_t				data_field,data_offset,data_length,bitmap_pageno;
    db_table_header_ptr		dbfileheader = NULL;
    ipcshm_dataitm_ptr		dataitm = NULL;
    mutex_lock_t            lock;
    int                     retcd = RET_NG;

    // 引数チェック
    if (!datafiles_on_process || !ipcdataf || (char*)(ipcdataf) == (char*)INVALID_SOCKET){
        return(RET_NG);
    }
    // 今回空きにマークする、データアイテム参照値
    data_field	= node->data_field;
    data_offset	= node->data_offset;
    //
    if (data_field >= IPC_DATAFILE_MAX){
        fprintf(stderr, "too bigger data_field(%d)",data_field);
        return(RET_NG);
    }
    if (data_offset >= IPC_DATAFILE_TTLSIZE){
        fprintf(stderr, "too bigger data_offset(%d)",data_offset);
        return(RET_NG);
    }
    if (data_offset <= sizeof(db_table_header_t)){
        printf("too small data_offset(%d)",data_offset);
        return(RET_NG);
    }
    //
    if (!nolock){
        lock.obj = &(ipcdataf->datafile_mutex[data_field]);
        lock.result = RET_NG;
        start_lock(&lock);
        if (isvalid_lock(&lock) != RET_OK){
            goto cleanup_lock;
        }
    }
    if ((dataitm = (ipcshm_dataitm_ptr)(datafiles_on_process->data_pointer[data_field] + data_offset)) == NULL){
        goto cleanup_lock;
    }
    if ((char*)dataitm == (char*)INVALID_SOCKET){
        goto cleanup_lock;
    }
    // シグネチャチェック
    // ※signature 不正データがある時は、全体的に不正と判定
    // ※データ長 > 0 が必要
    if (dataitm->signature != DATAITEM_SIGNATURE_ID || (dataitm->keylen + dataitm->vallen + sizeof(ipcshm_dataitm_t)) != dataitm->length){
        goto cleanup_lock;
    }
    data_length = dataitm->length;
    // 格納サイズは、128byteにアライメントされているので、unpackして
    // ページ数を算出しておく
    aligned_size = (data_length % IPC_DATAFILE_ALIGNED)==0?(data_length):(((data_length / IPC_DATAFILE_ALIGNED) + 1) * IPC_DATAFILE_ALIGNED);
    // 空きbmpを調整
    dbfileheader = (db_table_header_ptr)datafiles_on_process->data_pointer[data_field];

    // データオフセットから先頭ページ番号を算出して
    // ページを未使用状態に設定
    // ※オフセット値には、ファイルヘッダも含まれるので
    data_offset -= (sizeof(db_table_header_t));
    //
    bitmap_pageno = (data_offset % IPC_DATAFILE_ALIGNED)==0?(data_offset / IPC_DATAFILE_ALIGNED):((data_offset / IPC_DATAFILE_ALIGNED) + 1);
    for(m = bitmap_pageno;m < (bitmap_pageno + (aligned_size / IPC_DATAFILE_ALIGNED));m++){
        dbfileheader->bitmap[m/8]&=~(1<<(m%8));
    }
    // 空きサイズを更新
    dbfileheader->remain_length += (aligned_size * IPC_DATAFILE_ALIGNED);
    retcd = RET_OK;
cleanup_lock:
    // ロックオブジェクト終了
    if (!nolock && isvalid_lock(&lock)==RET_OK){ end_lock(&lock); }
    return(retcd);
}
/** *************************************************************
 * データファイル生成とアタッチ   ：親プロセス用\n
 *
 * @param[in]     basedir         ベースディレクトリ
 * @param[in,out] ipc_datafiles   データファイル管理インスタンス（筐体グローバル/named shm）
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int create_db_area_parent(const char* basedir,ipcshm_datafiles_ptr* ipc_datafiles){
    int				shmfd,n;
    pthread_mutexattr_t ma;
    // 既に初期化済みの場合処理しない
    if ((char*)(*ipc_datafiles) != (char*)INVALID_SOCKET){
        return(RET_NG);
    }
    // 先に削除しておく
    if (shm_unlink(get_datafile_shmnm()) != 0){
        fprintf(stdout, "shm_unlink missing.(%s)\n", get_datafile_shmnm());
    }
    // 共有領域
#ifdef __APPLE__
    shmfd = shm_open(get_datafile_shmnm(),O_CREAT|O_EXCL|O_RDWR, S_IRUSR|S_IWUSR);
#else
    shmfd = shm_open(get_datafile_shmnm(),O_CREAT|O_EXCL|O_RDWR|O_TRUNC, S_IRUSR|S_IWUSR);
#endif
    if (shmfd < 0){
        return(RET_NG);
    }
    if (ftruncate(shmfd,IPC_DATAFILE_SIZE) == -1){
        close(shmfd);
        return(RET_NG);
    }
    (*ipc_datafiles) = (ipcshm_datafiles_ptr)mmap(0, IPC_DATAFILE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, shmfd, 0);
    if ((char*)(*ipc_datafiles) == (char*)INVALID_SOCKET){
        close(shmfd);
        return(RET_NG);
    }
    // 初期パラメータ設定
    memset((*ipc_datafiles),0,IPC_DATAFILE_SIZE);

    // shmfdはロギング用途で保持しておく
    (*ipc_datafiles)->shmid = shmfd;
    close(shmfd);
    //
    for(n = 0;n < IPC_DATAFILE_MAX;n++){
        // ミューテックス属性設定
        if (pthread_mutexattr_init(&ma)){ fprintf(stderr, "failed.pthread_mutexattr_init\n"); }
#ifdef PTHREAD_MUTEX_ROBUST_NP
        if (pthread_mutexattr_setrobust_np(&ma,PTHREAD_MUTEX_ROBUST_NP)){ printf("failed.pthread_mutexattr_setrobust_np\n"); }
#endif
        if (pthread_mutexattr_setpshared(&ma,PTHREAD_PROCESS_SHARED)) { fprintf(stderr, "failed.pthread_mutexattr_setpshared\n"); }
        if (pthread_mutexattr_setprotocol(&ma,PTHREAD_PRIO_INHERIT)) { fprintf(stderr, "failed.pthread_mutexattr_setprotocol\n"); }

        // データファイル毎 mutex
        pthread_mutex_init(&((*ipc_datafiles)->datafile_mutex[n]),&ma);
        pthread_mutexattr_destroy(&ma);

        // 共有領域名称
        safe_snprintf((*ipc_datafiles)->shmnm[n],DATAFILE_PATHLEN - 1,"%s.%02d",get_datafile_shmnm(),n);
        safe_snprintf((*ipc_datafiles)->path[n], DATAFILE_PATHLEN - 1,"%s/%s.%02d",basedir,get_datafile_shmnm(),n);
        if (_create_file((*ipc_datafiles)->path[n],IPC_DATAFILE_TTLSIZE) != RET_OK){
            fprintf(stderr, "failed._create_file(%s)\n", (*ipc_datafiles)->path[n]);
        }
    }
    //
    return(RET_OK);
}
/** *************************************************************
 *  データファイル デタッチ       ：親プロセス用\n
 *
 * @param[in,out] ipc_datafiles         データファイル管理インスタンス（筐体グローバル/named shm）
 * @param[in]     datafiles_on_process  データファイルアクセサ：プロセス毎
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int remove_db_area_parent(ipcshm_datafiles_ptr* ipc_datafiles,datafiles_on_process_ptr datafiles_on_process){
    int			n;
    // ステータスチェック
    if ((char*)(*ipc_datafiles) == (char*)INVALID_SOCKET || !datafiles_on_process){
        return(RET_NG);
    }
    // データファイル個別のデタッチ
    for(n = 0;n < IPC_DATAFILE_MAX;n++){
        if (datafiles_on_process->filefd[n] != INVALID_SOCKET && datafiles_on_process->filefd[n]){
            close(datafiles_on_process->filefd[n]);
        }
        datafiles_on_process->filefd[n] = INVALID_SOCKET;

        if (datafiles_on_process->data_pointer[n] != (char*)INVALID_SOCKET && datafiles_on_process->data_pointer[n]){
            munmap(datafiles_on_process->data_pointer[n],IPC_DATAFILE_TTLSIZE);
        }
        datafiles_on_process->data_pointer[n] = (char*)INVALID_SOCKET;
    }
    // カテゴリ全体のデタッチ
    if ((char*)(*ipc_datafiles) == (char*)INVALID_SOCKET && (*ipc_datafiles)){
        close((*ipc_datafiles)->shmid);
        munmap((*ipc_datafiles),IPC_DATAFILE_SIZE);
    }
    (*ipc_datafiles) = (ipcshm_datafiles_ptr)INVALID_SOCKET;
    //
    return(0);
}

/** *************************************************************
 *  データファイル  参照初期化 :子プロセス用\n
 *  ※切断されているプロセス用※\n
 *  ※forkして切断していない子プロセスでは管理領域 ipcshm_xxx は※\n
 *  ※fork前に準備して利用すること※\n
 *
 * @param[in,out] ipc_datafiles         データファイル管理インスタンス（筐体グローバル/named shm）
 * @param[in]     datafiles_on_process  データファイルアクセサ：プロセス毎
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int childinit_db_area(ipcshm_datafiles_ptr* ipc_datafiles,datafiles_on_process_ptr datafiles_on_process){
    int				shmfd,n,filefd;
    char*			filemmap;

    // ステータスチェック
    // 既に初期化されているものは処理しない
    if ((char*)(*ipc_datafiles) != (char*)INVALID_SOCKET || !datafiles_on_process){
        return(RET_NG);
    }
    // 共有領域
    shmfd = shm_open(get_datafile_shmnm(),O_RDWR,0);
    if (shmfd < 0){
        return(RET_NG);
    }
    (*ipc_datafiles) = (ipcshm_datafiles_ptr)mmap(0, IPC_DATAFILE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, shmfd, 0);
    if ((char*)(*ipc_datafiles) == (char*)INVALID_SOCKET){
        close(shmfd);
        return(RET_NG);
    }
    (*ipc_datafiles)->shmid = shmfd;
    close(shmfd);

    // 各カテゴリ参照を準備
    for(n = 0;n < IPC_DATAFILE_MAX;n++){
        datafiles_on_process->filefd[n]			= INVALID_SOCKET;
        datafiles_on_process->data_pointer[n]	= (char*)INVALID_SOCKET;

        // ファイル open -> mmap
        if ((filefd = open((*ipc_datafiles)->path[n],O_RDWR,S_IREAD | S_IWRITE)) != INVALID_SOCKET){
            if ((filemmap = (char*)mmap(NULL,
                                        IPC_DATAFILE_TTLSIZE,
                                        PROT_READ | PROT_WRITE,
                                        MAP_SHARED,
                                        filefd,0)) == (char*)INVALID_SOCKET){
                close(filefd);
            }else{
                // 有効なデータファイルをセット
                datafiles_on_process->filefd[n]			= filefd;
                datafiles_on_process->data_pointer[n]	= filemmap;
            }
        }
    }
    return(RET_OK);
}

/** *************************************************************
 *  データファイル  参照終了 :子プロセス用\n
 *  ※切断されているプロセス用※\n
 *
 * @param[in,out] ipc_datafiles         データファイル管理インスタンス（筐体グローバル/named shm）
 * @param[in]     datafiles_on_process  データファイルアクセサ：プロセス毎
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int childuninit_db_area(ipcshm_datafiles_ptr* ipc_datafiles,datafiles_on_process_ptr datafiles_on_process){
    int		n;
    // ステータスチェック
    if ((char*)(*ipc_datafiles) != (char*)INVALID_SOCKET){
        // 各カテゴリ参照の終了
        for(n = 0;n < IPC_DATAFILE_MAX;n++){
            if (datafiles_on_process->filefd[n] != INVALID_SOCKET){
                close(datafiles_on_process->filefd[n]);
            }
            datafiles_on_process->filefd[n] = INVALID_SOCKET;

            if (datafiles_on_process->data_pointer[n] != (char*)INVALID_SOCKET){
                munmap(datafiles_on_process->data_pointer[n],IPC_DATAFILE_TTLSIZE);
            }
            datafiles_on_process->data_pointer[n] = (char*)INVALID_SOCKET;
        }
        close((*ipc_datafiles)->shmid);
        munmap((char*)(*ipc_datafiles),IPC_CATEGORY_SIZE);
    }
    (*ipc_datafiles) = (ipcshm_datafiles_ptr)INVALID_SOCKET;
    //
    return(0);
}
/** *************************************************************
 *  プロセスでの初期処理\n
 *  file open と、mmapまで\n
 *
 * @param[in]     ipc_datafiles         データファイル管理インスタンス（筐体グローバル/named shm）
 * @param[in]     datafiles_on_process  データファイルアクセサ：プロセス毎
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int init_datafiles_from_file(ipcshm_datafiles_ptr ipc_datafiles,datafiles_on_process_ptr datafiles_on_process){
    int					n,filefd;
    char*				filemmap;
    ULONGLONG			filesize;
    db_table_header_ptr	dbfileheader = NULL;

    // カテゴリ領域がアタッチされていない、、
    if (!ipc_datafiles || !datafiles_on_process){ return(RET_NG); }
    // 全カテゴリを準備
    for(n = 0;n < IPC_DATAFILE_MAX;n++){
        // ファイルシステムに、当該ファイルが存在しない場合
        // そのデータファイル領域は利用しない
        // ※つまり、空の64MB のファイルが存在すれば
        // ※その領域が利用されるイメージ
        filesize = 0;
        datafiles_on_process->data_pointer[n]	= (char*)INVALID_SOCKET;
        datafiles_on_process->filefd[n]			= INVALID_SOCKET;

        if (is_exists(ipc_datafiles->path[n],&filesize) == RET_OK){
            if (filesize == IPC_DATAFILE_TTLSIZE){
                // openして、mmap しておく
                if ((filefd = open(ipc_datafiles->path[n],O_RDWR,S_IREAD | S_IWRITE)) != INVALID_SOCKET){
                    if ((filemmap = (char*)mmap(NULL,IPC_DATAFILE_TTLSIZE,PROT_READ | PROT_WRITE,MAP_SHARED,filefd,0)) == (char*)INVALID_SOCKET){
                        close(filefd);
                    }else{
                        dbfileheader = (db_table_header_ptr)filemmap;
                        if (dbfileheader->signature != DATAITEM_SIGNATURE_ID){
                            // シグネチャが一致しない場合初期情報を書き込む
                            dbfileheader->signature		= DATAITEM_SIGNATURE_ID;
                            dbfileheader->first_offset	= sizeof(db_table_header_t);
                            dbfileheader->last_offset	= sizeof(db_table_header_t);
                            dbfileheader->used_length	= sizeof(db_table_header_t);
                            dbfileheader->used_count	= 0;
                            dbfileheader->table_length	= IPC_DATAFILE_TTLSIZE;
                            dbfileheader->remain_length	= (IPC_DATAFILE_TTLSIZE - sizeof(db_table_header_t));
                            dbfileheader->last_updated	= time(0);
                            safe_snprintf(dbfileheader->table_name,64 - 1,"%s.%02d",get_datafile_shmnm(),n);
                            // 初期bitmapはall bit-off
                            memset(dbfileheader->bitmap,0,IPC_DATAFILE_BMPSIZE);
                        }
                        datafiles_on_process->filefd[n]			= filefd;
                        datafiles_on_process->data_pointer[n]	= filemmap;
                    }
                }
            }
        }
    }
    return(RET_OK);
}



