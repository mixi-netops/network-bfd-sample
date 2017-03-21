/******************************************************************************/
/*! @addtogroup bfd service
 @file       bfdd_session.c
 @brief      BFDセッション実装
 ******************************************************************************/
#include "bfdd.h"

static ipcshm_categories_ptr    CATEGORIES = (ipcshm_categories_ptr)-1;
static ipcshm_datafiles_ptr	    DATAFILES = (ipcshm_datafiles_ptr)-1;
static datafiles_on_process_t	DATAFILES_INDEX;
static categories_on_process_t	CATEGORIES_INDEX;
static tree_instance_ptr        ROOT = NULL;
static char                     INDEX_DIR[128] = {0};
static char                     DATA_DIR[128] = {0};

#define STORE_U32KEY(k,v)       store(DATAFILES,&DATAFILES_INDEX,ROOT,(const char*)&k,sizeof(k),(char*)&v,sizeof(v),0)
#define SEARCH_KTVL(k,s,v,l)    search(DATAFILES,&DATAFILES_INDEX,ROOT,(const char*)&k,s,v,l)

static int get_sess_manage(bfd_sess_manage_ptr);
static int get_sess_manage_keylist(uint32_t,bfd_sess_key_list_ptr);

/** *************************************************************
 * セッションテーブルを初期化する\n
 *
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int initialize_bfd_session_table(void){
    bfd_sess_manage_t       init_mng;
    bfd_sess_key_list_t     init_list;
    uint32_t                keyid;
    int n;

    snprintf(INDEX_DIR,sizeof(INDEX_DIR)-1,"/tmp/%u/", (unsigned)getpid());
    mkdir(INDEX_DIR,0755);
    snprintf(DATA_DIR,sizeof(DATA_DIR)-1,"/tmp/%u/db/", (unsigned)getpid());
    mkdir(DATA_DIR,0755);

    // セッション：インデクステーブル初期化
    if (create_categories_area(INDEX_DIR,&CATEGORIES,&CATEGORIES_INDEX) != RET_OK){ return(1); }
    if (init_categories_from_file(CATEGORIES,&CATEGORIES_INDEX,0) != RET_OK){ return(1); }
    if (rebuild_pages_area(&CATEGORIES_INDEX) != RET_OK){ return(1); }
    // セッション：データファイル初期化
    if (create_db_area_parent(DATA_DIR,&DATAFILES) != RET_OK){ return(1); }
    if (init_datafiles_from_file(DATAFILES,&DATAFILES_INDEX) != RET_OK){ return(1); }
    // ツリールート
    if ((ROOT = create_tree_unsafe(CATEGORIES,&CATEGORIES_INDEX)) ==NULL){ return(1); }
    //
    keyid = SESSION_KEYS_MANGE;
    memset(&init_mng, 0, sizeof(init_mng));

    // 初期値：レコードなし
    if (STORE_U32KEY(keyid,init_mng)!= RET_OK){ return(1); }
    // キーリストを空で生成
    for(n = 0;n < SESSION_KEYS_MAX;n++){
        keyid = (SESSION_KEYS_BASE+n);
        memset(&init_list,0,sizeof(init_list));
        if (STORE_U32KEY(keyid,init_list)!= RET_OK){ return(1); }
    }
    return(0);
}
/** *************************************************************
 * セッションテーブルをファイナライズ\n
 * リソース解放処理
 *
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int finalize_bfd_session_table(void){
    // セッションデータファイルクリーンアップ
    if (remove_db_area_parent(&DATAFILES,&DATAFILES_INDEX) != RET_OK){
        fprintf(stderr, "remove_db_area_parent error(%s)\n", strerror(errno));
    }
    if (remove_categories_area(&CATEGORIES,&CATEGORIES_INDEX) != RET_OK){
        fprintf(stderr, "remove_categories_area error(%s)\n", strerror(errno));
    }
    remove_safe(INDEX_DIR);
    remove_safe(DATA_DIR);

    return(0);
}
/** *************************************************************
 * セッションテーブルをイテレート\n
 *
 ************************************************************* */
void iterate_all_session(int sock, iterate_session itr, void* udata){
    uint32_t keyid = 0;
    bfd_sess_manage_t       mng;
    bfd_sess_key_list_t     list;
    char *resp = NULL,*resp_sess=NULL;
    uint32_t resplen = 0,resplen_sess,n,m;

    // 管理データを取得
    if (get_sess_manage(&mng) != RET_OK){ return; }
    //
    for(n = 0;n < SESSION_KEYS_MAX;n++){
        // 利用されているテーブルを判定し、キーリストへアクセスする
        if (mng.keys_bits&(1<<n) && mng.keys_incnt[n]){
            if (get_sess_manage_keylist((SESSION_KEYS_BASE+n), &list) != RET_OK) { continue; }
            // キーリストを有効分ループ
            for(m = 0;m < mng.keys_incnt[n];m++){
                // 個別セッションを取得しあれば、イベント発行
                // なくてもエラーとはしない
                if (SEARCH_KTVL(list.keys[m],sizeof(bfd_sess_key_t),&resp_sess,&resplen_sess) == RET_OK){
                    if (resp_sess && resplen_sess && resplen_sess == sizeof(bfd_sess_t)){
                        if (itr(sock, (bfd_sess_ptr)resp_sess, udata) != 0){
                            fprintf(stderr, "iterator break(%s)\n", strerror(errno));
                            break;
                        }
                        free(resp_sess);
                        resp_sess = NULL;
                    }
                }
            }
            free(resp);
            resp = NULL;
        }
    }
}
/** *************************************************************
 * セッション情報を保存\n
 * ipアドレス＋ポート番号をキーとする
 *
 * @param[in]     psess         セッション構造体
 * @param[in]     c_addr        クライアントアドレス
 * @param[in]     c_addrlen     ↑バッファサイズ
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int insert_session(bfd_sess_ptr psess, struct sockaddr_in * c_addr, int c_addrlen){
    int ret,n,isupdate;
    uint32_t            hashed,curidx,keyid;
    bfd_sess_manage_t   mng;
    bfd_sess_key_list_t list;
    bfd_sess_key_t      keyt;
    //
    if (!psess || !c_addr || !c_addrlen){
        return(RET_NG);
    }
    memset(&keyt, 0, sizeof(keyt));
    keyt.addr = c_addr->sin_addr.s_addr;
    keyt.port = c_addr->sin_port;
    //
    hashed = (safe_hash((char*)&keyt,sizeof(keyt)) % SESSION_KEYS_MAX);
    // 管理データを取得
    if (get_sess_manage(&mng) != RET_OK){ return(RET_NG); }
    if (get_sess_manage_keylist((SESSION_KEYS_BASE+hashed), &list) != RET_OK) { return(RET_NG); }
    // 一つのハッシュグループに128個のセッションを最大数とする
    if (mng.keys_incnt[hashed] >= SESSION_KEYS_LISTCNT){
        fprintf(stderr, "session group full.(%u)\n", hashed);
        return(RET_NG);
    }
    // キーが一致は上書きである
    isupdate = 0;
    for(n = 0;n < MIN(SESSION_KEYS_LISTCNT,mng.keys_incnt[hashed]);n++){
        if (memcmp(&list.keys[n],&keyt, sizeof(keyt)) == 0){
            // 上書きである
            isupdate = 1;
            break;
        }
    }
    //
    ret = store(DATAFILES,&DATAFILES_INDEX,ROOT,(const char*)&keyt,sizeof(keyt),(char*)psess,sizeof(*psess),0);
    if (ret == RET_OK && isupdate==0){
        // 挿入に成功した、キーハッシュ + mod32した キーリスト内を走査して
        // 新規セッションの場合、管理テーブルへも反映する
        // ※ipアドレスと、ポート番号を含んだ構造体全体のハッシュであるが
        // ※特定のハッシュ値に集中する可能性も十分ある、そこは運用でカバーすることを想定
        // ※ハッシュ値の重複は最大128アイテムまで可能であり、理想値の分散状態の場合は
        // ※128 x 32 => 4096 個のセッション管理となる(連番を利用すればほぼ理想値になる)
        // 対象キーが格納されるべきリストとそのサマリ領域を確認し、新規の場合、
        // リスト数をインクリメントし、自身のキーをインクリメントしたインデクス位置にコピーする

        // データ存在ビット、データ数、キーリスト
        mng.keys_bits|=(1<<hashed);
        curidx = mng.keys_incnt[hashed];
        mng.keys_incnt[hashed]++;
        memcpy(&list.keys[curidx], &keyt, sizeof(keyt));

        // 更新後データでデータベースを更新
        keyid = SESSION_KEYS_MANGE;
        if (STORE_U32KEY(keyid,mng) != RET_OK){ return(1); }
        keyid = (SESSION_KEYS_BASE+hashed);
        if (STORE_U32KEY(keyid,list) != RET_OK){ return(1); }
    }
    return(ret);
}
/** *************************************************************
 * セッション情報を取得\n
 * ipアドレス＋ポート番号をキーとする
 *
 * @param[in/out] psess         セッション構造体
 * @param[in]     c_addr        クライアントアドレス
 * @param[in]     c_addrlen     ↑バッファサイズ
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int find_session(bfd_sess_ptr psess, struct sockaddr_in * c_addr, int c_addrlen){
    bfd_sess_key_t  keyt;
    char *resp = NULL;
    uint32_t resplen;
    if (!psess || !c_addr || !c_addrlen){
        return(RET_NG);
    }
    memset(&keyt, 0, sizeof(keyt));
    keyt.addr = c_addr->sin_addr.s_addr;
    keyt.port = c_addr->sin_port;

    if (SEARCH_KTVL(keyt,sizeof(keyt),&resp,&resplen) != RET_OK){ return(RET_NG); }
    if (!resp || !resplen){ return(RET_NG); }
    if (resplen != sizeof(*psess)){ return(RET_NG); }
    memcpy(psess, resp , sizeof(*psess));
    free(resp);

    return(RET_OK);
}
/** *************************************************************
 * セッション構造体を初期化\n
 *
 * @param[in/out] psess         セッション構造体
 * @param[in]     c_addr        クライアントアドレス
 * @param[in]     c_addrlen     ↑バッファサイズ
 ************************************************************* */
void init_session(bfd_sess_ptr psess ,struct sockaddr_in * c_addr, int c_addrlen){
    if (psess){
        psess->session_state = BFDSTATE_DOWN;
        psess->remote_session_state  = BFDSTATE_DOWN;
        psess->local_discr = BFD_OFF;
        psess->remote_discr = BFD_OFF;
        psess->local_diag = BFD_OFF;
        psess->desired_min_tx_interval = BFDDFLT_DESIREDMINTX;
        psess->required_min_rx_interval = BFDDFLT_REQUIREDMINRX;
        psess->remote_min_rx_interval = BFD_OFF;
        psess->demand_mode = BFD_OFF;
        psess->remote_demand_mode = BFD_OFF;
        psess->detect_mult = BFDDFLT_DETECTMULT;
        psess->auth_type = BFD_OFF;
        psess->rcv_auth_seq = BFD_OFF;
        psess->xmit_auth_seq = BFD_OFF;
        psess->auth_seq_known = BFD_OFF;
        psess->must_cease_tx_echo = BFD_OFF;
        psess->must_terminate = BFD_OFF;
        psess->detect_time = 0;
        psess->pollbit_on  = BFD_OFF;
        psess->received_min_rx_interval = BFD_OFF;
        psess->attached = BFD_OFF;

        // bfdポート宛先(dst)=固定3784,ソースポート(src)=固定49152
        psess->client_addr.sin_port = htons(BFDDFLT_UDPPORT);
        psess->client_addr.sin_family = AF_INET;
        psess->client_addr.sin_addr.s_addr = c_addr->sin_addr.s_addr;
        psess->client_addrlen = c_addrlen;
    }
}
int get_sess_manage(bfd_sess_manage_ptr pmng){
    uint32_t keyid = SESSION_KEYS_MANGE;
    char *resp = NULL;
    uint32_t resplen = 0;

    // 1.管理データを取得
    if (SEARCH_KTVL(keyid,sizeof(keyid),&resp,&resplen) != RET_OK){ return(RET_NG); }
    if (!resp && !resplen){ return(RET_NG); }
    if (resplen != sizeof(*pmng)){ return(RET_NG); }
    memcpy(pmng, resp, sizeof(*pmng));
    free(resp);
    resp = NULL;
    return(RET_OK);
}

int get_sess_manage_keylist(uint32_t key,bfd_sess_key_list_ptr plist){
    char *resp = NULL;
    uint32_t resplen = 0;
    if (SEARCH_KTVL(key,sizeof(key),&resp,&resplen) != RET_OK){ return(RET_NG); }
    if (!resp && !resplen){ return(RET_NG); }
    if (resplen != sizeof(*plist)){ return(RET_NG); }
    memcpy(plist, resp, sizeof(*plist));
    free(resp);
    resp = NULL;

    return(RET_OK);
}
