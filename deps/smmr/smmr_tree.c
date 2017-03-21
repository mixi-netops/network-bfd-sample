/******************************************************************************/
/*! @addtogroup in process file mmap kvs db(shared memory map - reallocation)
 @file       smmr_tree.cc
 @brief      2-3-4 tree implimentation
 ******************************************************************************/
#include "smmr_tree.h"
#include "smmr_categories.h"
#include "smmr_memory.h"



// 再帰解放
static void _free_tree_node_unsafe(category_on_process_ptr,node_instance_offset_t);
// 再帰挿入
static int _insert_item_recurcive(tree_instance_ptr,node_instance_offset_t);
// 再帰検索（ユニーク）
static int _search_item_recurcive(tree_instance_ptr,node_instance_offset_t,node_key_ptr,on_finded_callback,void*,int*);
// 再帰検索（重複あり：キーハッシュのみ判定）
static int _search_item_only_keyhash_recurcive(tree_instance_ptr,node_instance_offset_t,uint32_t,on_finded_callback,void*,int*);
// 再帰プリント
static int _print_item_recurcive(tree_instance_ptr,node_instance_offset_t,uint32_t);

// 削除
static int _remove_item_recursive(tree_instance_ptr,node_instance_offset_t);
// 削除時のリストア
static int _remove_item_restore(tree_instance_ptr,node_instance_offset_t,uint32_t);
// 削除時のマージ
static int _remove_item_combine(tree_instance_ptr,node_instance_offset_t,uint32_t);
// 削除時の右シフト
static int _remove_item_moveright(tree_instance_ptr,node_instance_offset_t,uint32_t);
// 削除時の左シフト
static int _remove_item_moveleft(tree_instance_ptr,node_instance_offset_t,uint32_t);
// 削除個別
static int _remove_item_trgt(tree_instance_ptr,node_instance_offset_t,uint32_t);


// 空き領域にデータセット
static int _add_item_to_free_node(tree_instance_ptr,node_instance_offset_t,uint32_t);
// 子ノードを再帰分割
static int _split_node(tree_instance_ptr,node_instance_offset_t,uint32_t);
// ノード個数を再帰カウント
static uint32_t _count_node_recursive_unsafe(node_instance_offset_t,category_on_process_ptr);
// 一致判定（ユニーク）
static int _compare_node_key_unsafe(node_key_ptr,node_key_ptr);
// ノード新規生成
static node_instance_offset_t _calloc_new_node(tree_instance_ptr);

/** *************************************************************
 * ツリーインスタンス生成\n
 * ※このインスタンスはheapに配置される
 * ※tree_instance_ptr->itm[n] はカテゴリインスタンスであり
 * ※このカテゴリインスタンスは、プロセス間で共有される
 *
 * @param[in]     categories             カテゴリ管理インスタンス（筐体グローバル）
 * @param[in]     categories_on_process  カテゴリアクセサ：プロセス毎
 * @result  tree_instance_ptr not null=成功、エラー/null
 ************************************************************* */
tree_instance_ptr create_tree_unsafe(ipcshm_categories_ptr categories,categories_on_process_ptr categories_on_process){
    uint32_t						n;
    tree_instance_ptr				inst = NULL;
    ipcshm_category_ptr				category = NULL;
    category_on_process_ptr			category_p = NULL;
    ipcshm_tree_instance_itm_ptr	inst_itm = NULL;
    node_instance_offset_t			dummy;

    // 引数チェック
    if (!categories || !categories_on_process){ return(NULL); }

    // インスタンスの入れ物自体は、heapで確保（プロセス間非共有）
    if ((inst = (tree_instance_ptr)malloc(sizeof(tree_instance_t))) == NULL){
        return(NULL);
    }
    // 初期設定
    memset(inst,0,sizeof(tree_instance_t));
    inst->categories			= categories;
    inst->categories_on_process	= categories_on_process;
    inst->curid					= 0;
    // カテゴリ全体Mutexで同期
    {	mutex_lock_t lock={&(categories->category_all_mutex),RET_NG};
        start_lock(&lock);
        if (isvalid_lock(&lock) == RET_OK){
            // 各カテゴリ毎にtree を準備
            for(n = 0;n < CATEGORY_CNT;n++){
                category	= &(categories->category[n]);
                category_p	= &(categories_on_process->category[n]);
                inst->curid	= n;

                // tree item 領域が有れば、それをアタッチ
                inst_itm = (ipcshm_tree_instance_itm_ptr)attach(category_p,sizeof(ipcshm_tree_instance_itm_t),SMMR_TREEROOT_ID);
                if (inst_itm == NULL){
                    // 無ければ、新規allocate
                    SHMALLOC(ipcshm_tree_instance_itm_t,category_p,dummy,inst_itm);
                }
                if (!inst_itm){
                    free(inst);
                    end_lock(&lock);
                    return(NULL);
                }
                // 本インスタンス[inst_itm]はプロセス間を共有する
                // ルートオブジェクト等がプロセス間で共有する必要がある
                inst->itm[inst->curid] = inst_itm;
                // シグネチャにユニークidをセットすることで
                // アタッチが可能となる
                inst->itm[inst->curid]->signature	= SMMR_TREEROOT_ID;
                // アタッチしたrootノードが利用される
                // カテゴリはmmapアドレスなので、起動毎に異なる
                inst->itm_p[inst->curid].category	= category;
                inst->itm_p[inst->curid].category_p	= category_p;
            }
        }
        end_lock(&lock);
    }
    //
    return(inst);
}
/** *************************************************************
 * ツリー count\n
 * ノード数サマリを返却\n
 *
 * @param[in]     inst      ツリーインスタンス
 * @result  0 = エラー or 0個、!=0 アイテム個数
 ************************************************************* */
uint32_t count_tree_safe(tree_instance_ptr inst){
    uint32_t	count = 0,n;

    // 引数チェック
    if (!inst){ return(0); }
    if (!inst->categories){ return(0); }

    // 全カテゴリのカウント
    for(n = 0;n < CATEGORY_CNT;n++){
        inst->curid = n;
        // カテゴリ別ロック
        {	mutex_lock_t lock = {&(inst->itm_p[n].category->category_mutex), RET_NG};
            start_lock(&lock);
            if (isvalid_lock(&lock) == RET_OK){
                // カウント
                count += _count_node_recursive_unsafe(inst->itm[n]->rooti,inst->itm_p[n].category_p);
            }
            end_lock(&lock);
        }
    }
    return(count);
}
/** *************************************************************
 * ノード個数を再帰カウント\n
 * ※count_tree_safe からのみ実行される
 * @param[in]     nodei     初回はrootノード、後は再帰
 * @param[in]     category  カテゴリアクセサ
 * @result  アイテム個数
 ************************************************************* */
uint32_t _count_node_recursive_unsafe(node_instance_offset_t nodei,category_on_process_ptr category){
    uint32_t			n = 0;
    uint32_t			count = 0;
    node_instance_ptr	node = NULL;
    // エンプティノードは終了
    if (!ISEMPTY_NODE(nodei) || !category || !category->data_pointer){
        // ノードオフセットからデータ参照
        node = REFNODE(category,nodei);

        // 全子ノードをLoop
        for(n = 0;n < node->count;n++){
            count += _count_node_recursive_unsafe(node->childsi[n],category);
            count ++;
        }
        // 最後のリンク
        count += _count_node_recursive_unsafe(node->childsi[node->count],category);
    }
    return(count);
}
/** *************************************************************
 * ツリー解放\n
 *
 * @param[in]     inst      ツリーインスタンス
 * @result  成功:RET_OK/エラー：!=RET_OK
 ************************************************************* */
int free_tree_unsafe(tree_instance_ptr inst){
    uint32_t	n;
    // 引数チェック
    if (!inst){ return(RET_NG); }
    if (!inst->categories){ return(RET_NG); }
    //
    for(n = 0;n < CATEGORY_CNT;n++){
        inst->curid = n;
        _free_tree_node_unsafe(inst->itm_p[inst->curid].category_p,inst->itm[inst->curid]->rooti);
    }
    free(inst);
    //
    return(RET_OK);
}
/** *************************************************************
 * ノード解放を再帰処理\n
 * ※free_tree_unsafe からのみ実行される
 * @param[in]     category  カテゴリアクセサ
 * @param[in]     nodei     初回はrootノード、後は再帰
 * @result  void
 ************************************************************* */
void _free_tree_node_unsafe(category_on_process_ptr category,node_instance_offset_t nodei) {
    int	n;
    node_instance_ptr	node;

    if (ISEMPTY_NODE(nodei) || !category){ return; }
    // ノードオフセットからデータ参照
    node = REFNODE(category,nodei);
    //
    for(n = 0;n < TREE_NODECOUNT; n++){
        _free_tree_node_unsafe(category,node->childsi[n]);
    }
    // ノードオフセットからデータ参照
    node = REFNODE(category,nodei);
    //
    SHMFREE(category,node);
}

/** *************************************************************
 * アイテム追加\n
 *
 * @param[in]     inst      ツリーインスタンス
 * @param[in]     itm       追加対象のアイテム
 * @result  成功:RET_OK/エラー：!=RET_OK
 ************************************************************* */
int add_tree_safe(tree_instance_ptr inst,node_key_ptr itm){
    int		category_id;

    // 引数チェック
    if (!itm || !inst){ return(RET_NG); }
    if (!inst->categories){ return(RET_NG); }

    // ツリーIndexに利用するカテゴリをkeyhashから求める
    category_id = get_category_id_by_hashkey_unsafe(inst->categories,inst->categories_on_process,itm->keyhash);
    if (category_id < 0){
        return(RET_NG);
    }
    inst->itm_p[category_id].current_item = (*itm);
    inst->curid = category_id;

    // カテゴリ別ロック
    {	mutex_lock_t lock={&(inst->itm_p[category_id].category->category_mutex),RET_NG};
        start_lock(&lock);
        if (isvalid_lock(&lock) == RET_OK){
            // 新規アイテムを挿入
            _insert_item_recurcive(inst,inst->itm[category_id]->rooti);
            if (!inst->itm_p[category_id].done){
                inst->itm[category_id]->rooti = _calloc_new_node(inst);
            }
            end_lock(&lock);
            return(RET_OK);
        }
        end_lock(&lock);
    }
    return(RET_NG);
}
/** *************************************************************
 * 再帰挿入\n
 * 挿入すべきアイテムは\n
 * inst->itm_p[inst->curid].current_item に配置されている\n
 *
 * @param[in]     inst      ツリーインスタンス
 * @param[in]     nodei     初回はrootノード、後は再帰
 * @result  成功:RET_OK/エラー：!=RET_OK
 ************************************************************* */
int _insert_item_recurcive(tree_instance_ptr inst,node_instance_offset_t nodei){
    uint32_t			pos;
    node_instance_ptr	node = NULL;

    // root の場合
    if (ISEMPTY_NODE(nodei)){
        inst->itm_p[inst->curid].done	= FALSE;
        memset(&(inst->itm[inst->curid]->newnodei),0,sizeof(node_instance_offset_t));
        return(RET_OK);
    }
    // ノードオフセットからデータ参照
    node = REFNODE(inst->itm_p[inst->curid].category_p,nodei);

    // 検索ノードを進めて
    pos = 0;
    while(pos < node->count && (_compare_node_key_unsafe(&(node->keys[pos]),
                                                         &(inst->itm_p[inst->curid].current_item)) < 0)){
        pos++;
    }
    if (pos < node->count && _compare_node_key_unsafe(&(node->keys[pos]),
                                                      &(inst->itm_p[inst->curid].current_item)) == 0){
        printf("\texists.\n");
        inst->itm_p[inst->curid].done = TRUE;
        return(RET_OK);
    }
    // 再帰的に自身を呼び返す
    _insert_item_recurcive(inst,node->childsi[pos]);
    if (inst->itm_p[inst->curid].done){
        return(RET_OK);
    }
    if (node->count < TREE_NODECOUNT){
        // ページが分割できない場合
        _add_item_to_free_node(inst,nodei,pos);
        inst->itm_p[inst->curid].done = TRUE;
    }else{
        // ページを分割する場合
        _split_node(inst,nodei,pos);
        inst->itm_p[inst->curid].done = FALSE;
    }
    return(RET_OK);
}
/** *************************************************************
 * 空き領域にデータセット\n
 * 挿入すべきアイテムは\n
 * inst->itm_p[inst->curid].current_item に配置されている\n
 *
 * @param[in]     inst      ツリーインスタンス
 * @param[in]     nodei     初回はrootノード、後は再帰
 * @param[in]     pos       挿入ノードIndex
 * @result  成功:RET_OK/エラー：!=RET_OK
 ************************************************************* */
int _add_item_to_free_node(tree_instance_ptr inst,node_instance_offset_t nodei,uint32_t pos){
    uint32_t			counter = 0;
    node_instance_ptr	node = NULL;

    // 引数チェック
    if (!inst || ISEMPTY_NODE(nodei)){
        return(RET_NG);
    }
    // ノードオフセットからデータ参照
    node = REFNODE(inst->itm_p[inst->curid].category_p,nodei);

    // アイテムを右にずらして
    for(counter = node->count;counter > pos;counter--){
        node->keys[counter]			= node->keys[counter - 1];
        node->childsi[counter + 1]	= node->childsi[counter];
    }
    // 今回のアイテムを追加
    node->keys[pos]			= inst->itm_p[inst->curid].current_item;
    node->childsi[pos + 1]	= inst->itm[inst->curid]->newnodei;
    node->count ++;
    //
    return(RET_OK);
}
/** *************************************************************
 * ノードを再帰分割\n
 *
 * @param[in]     inst      ツリーインスタンス
 * @param[in]     nodei     初回はrootノード、後は再帰
 * @param[in]     pos       挿入ノードIndex
 * @result  成功:RET_OK/エラー：!=RET_OK
 ************************************************************* */
int _split_node(tree_instance_ptr inst,node_instance_offset_t nodei,uint32_t pos){
    int						newpos,n;
    node_instance_offset_t	newnodei;
    node_instance_ptr		nodetmp = NULL;
    node_instance_ptr		node = NULL;

    // 引数チェック
    if (!inst || ISEMPTY_NODE(nodei)){
        return(RET_NG);
    }
    // ノードオフセットからデータ参照
    node = REFNODE(inst->itm_p[inst->curid].category_p,nodei);

    // 位置調整
    if (pos <= TREE_NODEMIN){
        newpos = TREE_NODEMIN;
    }else{
        newpos = (TREE_NODEMIN + 1);
    }
    // 新しいノード
    SHMALLOC_NML(node_instance_t,inst->itm_p[inst->curid],newnodei, nodetmp);
    // 半分以降のノードをLoopして分割
    for(n = (newpos + 1);n <= TREE_NODECOUNT;n++){
        nodetmp->keys[n - newpos - 1]	= node->keys[n - 1];
        nodetmp->childsi[n - newpos]	= node->childsi[n];
    }
    nodetmp->count	= (TREE_NODECOUNT - newpos);
    node->count		= newpos;

    // 今回追加するアイテムは
    if (pos <= TREE_NODEMIN){
        _add_item_to_free_node(inst,nodei,pos);
    }else{
        _add_item_to_free_node(inst,newnodei,(pos - newpos));
    }
    // キー値を変更
    inst->itm_p[inst->curid].current_item = node->keys[node->count - 1];
    // 最後のリンクを新ノードの先頭へ
    nodetmp->childsi[0] = node->childsi[node->count];
    node->count--;
    // 最後に新規ノードを保存
    inst->itm[inst->curid]->newnodei = newnodei;
    //
    return(RET_OK);
}
/** *************************************************************
 * ノード新規生成\n
 * 新規ノード用にメモリアタッチする\n
 *
 * @param[in]     inst      ツリーインスタンス
 * @result  node_instance_offset_t  成功時：有効なオフセット値/エラー時：EMPTYオフセット
 ************************************************************* */
node_instance_offset_t _calloc_new_node(tree_instance_ptr inst){
    node_instance_offset_t	newnodei;
    node_instance_offset_t	emptynode;
    node_instance_ptr		newnode = NULL;

    memset(&emptynode,0,sizeof(emptynode));

    // 引数チェック
    if (!inst){ return(emptynode); }
    if (!inst->categories){ return(emptynode); }
    //
    SHMALLOC_NML(node_instance_t,inst->itm_p[inst->curid],newnodei,newnode);
    if (newnode != NULL){
        newnode->count		= 1;
        newnode->keys[0]	= inst->itm_p[inst->curid].current_item;
        newnode->childsi[0]	= inst->itm[inst->curid]->rooti;
        newnode->childsi[1]	= inst->itm[inst->curid]->newnodei;
    }
    //
    return(newnodei);
}
/** *************************************************************
 * アイテム検索\n
 * ハッシュ値、オフセット、カテゴリの全一致で検索\n
 *
 * @param[in]     inst      ツリーインスタンス
 * @param[in]     itm       検索アイテム
 * @param[in]     func      検索結果コールバック
 * @param[in]     usrval    ↑ユーザデータ
 * @result  成功:RET_OK/エラー：!=RET_OK
 ************************************************************* */
int find_tree_safe(tree_instance_ptr inst,node_key_ptr itm,on_finded_callback func,void* usrval){
    int					finded = RET_NG,category_id;
    int					ret = RET_OK;

    // 引数チェック
    if (!inst || !itm || !func){
        return(RET_NG);
    }
    if (!inst->categories){
        return(RET_NG);
    }
    // カテゴリをkeyhashから求める
    category_id = get_category_id_by_hashkey_unsafe(inst->categories,inst->categories_on_process,itm->keyhash);
    if (category_id < 0){
        return(RET_NG);
    }
    inst->curid = category_id;

    // カテゴリ別ロック
    {	mutex_lock_t lock = {&(inst->itm_p[inst->curid].category->category_mutex), RET_NG};
        start_lock(&lock);
        if (isvalid_lock(&lock) == RET_OK){
            // ルートから検索開始
            ret |= (_search_item_recurcive(inst,inst->itm[inst->curid]->rooti,itm,func,usrval,&finded));
        }
        end_lock(&lock);
    }
    return(ret);
}
/** *************************************************************
 * 再帰検索\n
 * ※find_tree_safe から利用される\n
 *
 * @param[in]     inst      ツリーインスタンス
 * @param[in]     nodei     ノードオフセット
 * @param[in]     itm       検索アイテム
 * @param[in]     func      検索結果コールバック
 * @param[in]     usrval    ↑ユーザデータ
 * @param[in/out] finded    検索結果保持バッファ
 * @result  成功（見つかった）:RET_OK/エラー：!=RET_OK
 ************************************************************* */
int _search_item_recurcive(tree_instance_ptr inst,node_instance_offset_t nodei,node_key_ptr itm,on_finded_callback func,void* usrval,int* finded){
    uint32_t				pos = 0;
    node_instance_offset_t	findnodei;
    node_instance_ptr		findnode;
    node_instance_ptr		node = NULL;

    // 引数チェック
    if (ISEMPTY_NODE(nodei) || !itm || !finded || !func){
        return(RET_NG);
    }

    // ノードオフセットからデータ参照
    node = REFNODE(inst->itm_p[inst->curid].category_p,nodei);
    //
    findnode = node;
    // 検索
    while(findnode != NULL){
        pos = 0;
        while(pos < findnode->count && _compare_node_key_unsafe(&(findnode->keys[pos]),itm) < 0){
            pos++;
        }
        //
        if (pos < findnode->count && (_compare_node_key_unsafe(itm,&(findnode->keys[pos])) == 0)){
            // 対象になりうるハッシュ値は全てコールバック対象
            // 呼び出し側がコールバックにRET_OK 以外を返却で
            // 検索ループ終了
            return(func(&(findnode->keys[pos]),usrval));
        }
        // 子アイテムのオフセットからデータ参照
        findnodei = findnode->childsi[pos];
        if (ISEMPTY_NODE(findnodei)){
            findnode = NULL;
        }else{
            findnode = REFNODE(inst->itm_p[inst->curid].category_p,findnodei);
        }
    }
    return(RET_NG);
}
/** *************************************************************
 * アイテム検索（重複有り：キーハッシュのみ判定）\n
 * ハッシュ値のみである意味での前方一致的な検索\n
 *
 * @param[in]     inst      ツリーインスタンス
 * @param[in]     keyhash   検索ハッシュ値
 * @param[in]     func      検索結果コールバック
 * @param[in]     usrval    ↑ユーザデータ
 * @result  成功:RET_OK/エラー：!=RET_OK
 ************************************************************* */
int find_tree_only_keyhash_safe(tree_instance_ptr inst,uint32_t keyhash,on_finded_callback func,void* usrval){
    int			finded = RET_NG,category_id;
    int			ret = RET_OK;
    // 引数チェック
    if (!inst || !func){
        return(RET_NG);
    }
    if (!inst->categories){
        return(RET_NG);
    }
    // カテゴリをkeyhashから求める
    category_id = get_category_id_by_hashkey_unsafe(inst->categories,inst->categories_on_process,keyhash);
    if (category_id < 0){
        return(RET_NG);
    }
    inst->curid = category_id;

    // カテゴリ別ロック
    {	mutex_lock_t lock={&(inst->itm_p[category_id].category->category_mutex),RET_NG};
        start_lock(&lock);
        if (isvalid_lock(&lock) == RET_OK){
            // ルートから検索開始
            ret |= (_search_item_only_keyhash_recurcive(inst,inst->itm[category_id]->rooti,keyhash,func,usrval,&finded));
        }
        end_lock(&lock);
    }
    return(ret);
}

/** *************************************************************
 * 再帰検索（重複あり：キーハッシュのみ判定）\n
 * ※find_tree_only_keyhash_safe から利用される\n
 *
 * @param[in]     inst      ツリーインスタンス
 * @param[in]     nodei     ノードオフセット
 * @param[in]     keyhash   検索ハッシュ値
 * @param[in]     func      検索結果コールバック
 * @param[in]     usrval    ↑ユーザデータ
 * @param[in/out] finded    検索結果保持バッファ
 * @result  成功（見つかった）:RET_OK/エラー：!=RET_OK
 ************************************************************* */
int _search_item_only_keyhash_recurcive(tree_instance_ptr inst,node_instance_offset_t nodei,uint32_t keyhash,on_finded_callback func,void* usrval,int* finded){
    uint32_t	pos,prev_pos;
    int			result;
    //bool		between_key = FALSE;
    node_instance_offset_t	findnodei;
    node_instance_ptr		findnode;
    node_instance_ptr		node = NULL;

    // 引数チェック
    if (ISEMPTY_NODE(nodei) || !finded || !func){
        return(RET_NG);
    }
    // ノードオフセットからデータ参照
    node		= REFNODE(inst->itm_p[inst->curid].category_p,nodei);
    findnode	= node;
    // 検索
    while(findnode != NULL){
        //between_key = TRUE;
        pos = prev_pos = 0;
        while(pos < findnode->count && (findnode->keys[pos].keyhash < keyhash)){
            pos++;
        }
        // 走査対象アイテムは存在するが、最初のアイテムが
        // 検索対象キーより小さい場合、同一レベルを検索する必要がない
        if (pos == 0 && findnode->count > 0 && findnode->keys[0].keyhash > keyhash){
            // 将来的な処理追加を考慮し、一応初期化
            pos = prev_pos = 0;
        }else{
            // 同一レベルに複数個キー一致が有るかも
            for(prev_pos = pos;pos < findnode->count;pos++){
                if (keyhash == findnode->keys[pos].keyhash){
                    // hit 設定
                    (*finded) = RET_OK;

                    // 対象になりうるハッシュ値は全てコールバック対象
                    // 呼び出し側がコールバックにRET_WARN返却でindex解放予約、RET_NGで
                    // 返却で検索ループ終了
                    if ((result = func(&(findnode->keys[pos]),usrval)) == RET_NG){
                        return(RET_OK);
                    }
                }
                if (prev_pos != pos){
                    // 「より大きい」右子ノードにも存在するかもしれない
                    _search_item_only_keyhash_recurcive(inst,findnode->childsi[pos],keyhash,func,usrval,finded);
                }
            }
            // 最後の子ノード「より大きい」右子ノードにも存在するかもしれない
            if (findnode->count != prev_pos){
                _search_item_only_keyhash_recurcive(inst,findnode->childsi[findnode->count],keyhash,func,usrval,finded);
            }
        }
        // 左子ノードへ
        findnodei = findnode->childsi[prev_pos];
        // 子アイテムのオフセットからデータ参照
        if (ISEMPTY_NODE(findnodei)){
            findnode = NULL;
        }else{
            findnode = REFNODE(inst->itm_p[inst->curid].category_p,findnodei);
        }
    }
    return(RET_NG);
}

/** *************************************************************
 * アイテム削除\n
 *
 * @param[in]     inst      ツリーインスタンス
 * @param[in]     itm       削除対象アイテム
 * @result  成功:RET_OK/エラー：!=RET_OK
 ************************************************************* */
int  remove_tree_safe(tree_instance_ptr inst,node_key_ptr itm){
    int						ret = RET_NG,category_id;
    node_instance_ptr		node = NULL,rootnode = NULL;
    node_instance_offset_t	nodei;

    // 引数チェック
    if (!inst || !itm){
        return(RET_NG);
    }
    if (!inst->categories){
        return(RET_NG);
    }
    // カテゴリをkeyhashから求める
    category_id = get_category_id_by_hashkey_unsafe(inst->categories,inst->categories_on_process,itm->keyhash);
    if (category_id < 0){
        return(RET_NG);
    }
    inst->curid = category_id;

    // カテゴリ別ロック
    {	mutex_lock_t lock = {&(inst->itm_p[inst->curid].category->category_mutex),RET_NG};
        start_lock(&lock);
        if (isvalid_lock(&lock) == RET_OK){
            //
            inst->itm_p[inst->curid].deleted = inst->itm_p[inst->curid].undersize = FALSE;
            inst->itm_p[inst->curid].current_item = (*itm);

            // 削除実施
            _remove_item_recursive(inst,inst->itm[inst->curid]->rooti);
            if (inst->itm_p[inst->curid].deleted){
                rootnode = REFNODE(inst->itm_p[inst->curid].category_p,inst->itm[inst->curid]->rooti);

                // 削除完了できていれば、解放できるかをチェック
                if (rootnode->count == 0){
                    nodei	= inst->itm[inst->curid]->rooti;
                    node	= REFNODE(inst->itm_p[inst->curid].category_p,nodei);
                    inst->itm[inst->curid]->rooti = rootnode->childsi[0];
                    //
                    SHMFREE(inst->itm_p[inst->curid].category_p,node);
                }
            }
            ret = RET_OK;
        }
        end_lock(&lock);
    }
    return(ret);
}

/** *************************************************************
 * アイテム削除（ユニーク：コールバック内から利用する用）\n
 * ※ 対象カテゴリでロックせずに削除処理が実装されている※
 *
 * @param[in]     inst      ツリーインスタンス
 * @param[in]     itm       削除対象アイテム
 * @result  成功:RET_OK/エラー：!=RET_OK
 ************************************************************* */
int  remove_tree_unsafe(tree_instance_ptr inst,node_key_ptr itm){
    int						category_id;
    node_instance_ptr		node = NULL,rootnode = NULL;
    node_instance_offset_t	nodei;

    // 引数チェック
    if (!inst || !itm){
        return(RET_NG);
    }
    if (!inst->categories){
        return(RET_NG);
    }
    // カテゴリをkeyhashから求める
    category_id = get_category_id_by_hashkey_unsafe(inst->categories,inst->categories_on_process,itm->keyhash);
    if (category_id < 0){
        return(RET_NG);
    }
    inst->curid = category_id;
    //
    inst->itm_p[inst->curid].deleted = inst->itm_p[inst->curid].undersize = FALSE;
    inst->itm_p[inst->curid].current_item = (*itm);
    // 削除実施
    _remove_item_recursive(inst,inst->itm[inst->curid]->rooti);
    if (inst->itm_p[inst->curid].deleted){
        rootnode = REFNODE(inst->itm_p[inst->curid].category_p,inst->itm[inst->curid]->rooti);
        // 削除完了できていれば、解放できるかをチェック
        if (rootnode->count == 0){
            nodei	= inst->itm[inst->curid]->rooti;
            node	= REFNODE(inst->itm_p[inst->curid].category_p,nodei);
            //
            inst->itm[inst->curid]->rooti = node->childsi[0];
            //
            SHMFREE(inst->itm_p[inst->curid].category_p,node);
        }
    }
    return(RET_OK);
}

/** *************************************************************
 * 再帰削除\n
 * ※remove_tree_unsafe/remove_tree_safe から利用される\n
 *
 * @param[in]     inst      ツリーインスタンス
 * @param[in]     nodei     ノードオフセット
 * @result  成功（見つかった）:RET_OK/エラー：!=RET_OK
 ************************************************************* */
int _remove_item_recursive(tree_instance_ptr inst,node_instance_offset_t nodei){
    uint32_t	pos;
    node_instance_offset_t	tmpnodei;
    node_instance_ptr	tmpnode = NULL;
    node_instance_ptr	node = NULL;
    //
    if (ISEMPTY_NODE(nodei)){
        return(RET_NG);
    }
    // ノードオフセットからデータ参照
    node = REFNODE(inst->itm_p[inst->curid].category_p,nodei);
    // 対象アイテムまで進めて
    pos = 0;
    while(pos < node->count && _compare_node_key_unsafe(&(node->keys[pos]),
                                                        &(inst->itm_p[inst->curid].current_item)) < 0){
        pos++;
    }
    if (pos < node->count && _compare_node_key_unsafe(&(inst->itm_p[inst->curid].current_item),
                                                      &(node->keys[pos])) == 0){
        inst->itm_p[inst->curid].deleted = TRUE;
        // 一致しているのを削除
        tmpnodei = node->childsi[pos + 1];
        if (!ISEMPTY_NODE(tmpnodei)){
            tmpnode = REFNODE(inst->itm_p[inst->curid].category_p,tmpnodei);
            while(!ISEMPTY_NODE(tmpnode->childsi[0])){
                tmpnodei	= tmpnode->childsi[0];
                tmpnode		= REFNODE(inst->itm_p[inst->curid].category_p,tmpnodei);
            }
            node->keys[pos] = inst->itm_p[inst->curid].current_item = tmpnode->keys[0];
            _remove_item_recursive(inst,node->childsi[pos + 1]);
            if (inst->itm_p[inst->curid].undersize){
                _remove_item_restore(inst,nodei,(pos + 1));
            }
        }else{
            return(_remove_item_trgt(inst,nodei,pos));
        }
    }else{
        _remove_item_recursive(inst,node->childsi[pos]);
        if (inst->itm_p[inst->curid].undersize){
            return(_remove_item_restore(inst,nodei,pos));
        }
    }
    return(RET_OK);
}
/** *************************************************************
 * 削除時の右シフト\n
 *
 * @param[in]     inst      ツリーインスタンス
 * @param[in]     nodei     ノードオフセット
 * @param[in]     pos       ノードIndex
 * @result  int 固定でRET_NG を返却（呼び出し元判定の為）
 ************************************************************* */
int _remove_item_moveright(tree_instance_ptr inst,node_instance_offset_t nodei,uint32_t pos){
    node_instance_ptr		left,right;
    node_instance_offset_t	lefti,righti;
    node_instance_ptr		node = NULL;
    uint32_t				n;
    //
    node	= REFNODE(inst->itm_p[inst->curid].category_p,nodei);
    //
    righti	= node->childsi[pos];
    lefti	= node->childsi[pos - 1];
    //
    left	= REFNODE(inst->itm_p[inst->curid].category_p,lefti);
    right	= REFNODE(inst->itm_p[inst->curid].category_p,righti);
    //
    for(n = right->count;n > 0;n--){
        right->keys[n]			= right->keys[n - 1];
        right->childsi[n + 1]	= right->childsi[n];
    }
    right->childsi[1]	= right->childsi[0];
    right->count ++;
    right->keys[0]		= node->keys[pos -1];
    node->keys[pos -1]	= left->keys[left->count -1];
    right->childsi[0]	= left->childsi[left->count];
    left->count--;
    //
    return(RET_NG);
}
/** *************************************************************
 * 削除時の左シフト\n
 *
 * @param[in]     inst      ツリーインスタンス
 * @param[in]     nodei     ノードオフセット
 * @param[in]     pos       ノードIndex
 * @result  int 固定でRET_NG を返却（呼び出し元判定の為）
 ************************************************************* */
int _remove_item_moveleft(tree_instance_ptr inst,node_instance_offset_t nodei,uint32_t pos){
    node_instance_ptr		left,right;
    node_instance_offset_t	lefti,righti;
    node_instance_ptr		node = NULL;
    uint32_t				n;
    //
    node	= REFNODE(inst->itm_p[inst->curid].category_p,nodei);
    //
    righti	= node->childsi[pos];
    lefti	= node->childsi[pos - 1];
    //
    left	= REFNODE(inst->itm_p[inst->curid].category_p,lefti);
    right	= REFNODE(inst->itm_p[inst->curid].category_p,righti);
    //
    left->count++;
    left->keys[left->count -1]	= node->keys[pos - 1];
    left->childsi[left->count]	= right->childsi[0];
    node->keys[pos -1]			= right->keys[0];
    right->childsi[0]			= right->childsi[1];
    right->count--;
    //
    for(n = 1;n <= right->count;n++){
        right->keys[n - 1]	= right->keys[n];
        right->childsi[n]	= right->childsi[n + 1];
    }
    //
    return(RET_NG);
}

/** *************************************************************
 * 削除時のノードのマージ\n
 *
 * @param[in]     inst      ツリーインスタンス
 * @param[in]     nodei     ノードオフセット
 * @param[in]     pos       ノードIndex
 * @result  int   成功：RET_OK/エラー:!=RET_OK
 ************************************************************* */
int _remove_item_combine(tree_instance_ptr inst,node_instance_offset_t nodei,uint32_t pos){
    uint32_t				n;
    int						ret = RET_NG;
    node_instance_ptr		left,right;
    node_instance_offset_t	lefti,righti;
    node_instance_ptr		node = NULL;
    //
    node	= REFNODE(inst->itm_p[inst->curid].category_p,nodei);
    //
    righti	= node->childsi[pos];
    lefti	= node->childsi[pos - 1];
    //
    left	= REFNODE(inst->itm_p[inst->curid].category_p,lefti);
    right	= REFNODE(inst->itm_p[inst->curid].category_p,righti);
    //
    left->count++;
    left->keys[left->count - 1]	= node->keys[pos - 1];
    left->childsi[left->count]	= right->childsi[0];
    for(n = 1;n <= right->count;n++){
        left->count++;
        left->keys[left->count - 1]	= right->keys[n - 1];
        left->childsi[left->count]	= right->childsi[n];
    }
    ret = _remove_item_trgt(inst,nodei,(pos -1));
    SHMFREE(inst->itm_p[inst->curid].category_p,right);
    //
    return(ret);
}

/** *************************************************************
 * 削除個別\n
 *
 * @param[in]     inst      ツリーインスタンス
 * @param[in]     nodei     ノードオフセット
 * @param[in]     pos       ノードIndex
 * @result  int   成功：RET_OK/エラー:!=RET_OK
 ************************************************************* */
int _remove_item_trgt(tree_instance_ptr inst,node_instance_offset_t nodei,uint32_t pos){
    node_instance_ptr	node = NULL;
    //
    node = REFNODE(inst->itm_p[inst->curid].category_p,nodei);
    //
    while((++pos) < node->count){
        node->keys[pos - 1]	= node->keys[pos];
        node->childsi[pos]	= node->childsi[pos + 1];
    }
    inst->itm_p[inst->curid].undersize = (--(node->count) < TREE_NODEMIN);
    return(RET_OK);
}

/** *************************************************************
 * 削除時のリストア\n
 *
 * @param[in]     inst      ツリーインスタンス
 * @param[in]     nodei     ノードオフセット
 * @param[in]     pos       ノードIndex
 * @result  int   処理対象がある：RET_OK/処理対象がない!=RET_OK
 ************************************************************* */
int _remove_item_restore(tree_instance_ptr inst,node_instance_offset_t nodei,uint32_t pos){
    node_instance_ptr	node = NULL,nodec = NULL;
    if (ISEMPTY_NODE(nodei)){
        return(RET_NG);
    }
    //
    inst->itm_p[inst->curid].undersize = FALSE;
    node = REFNODE(inst->itm_p[inst->curid].category_p,nodei);
    //
    if (pos > 0){
        nodec = REFNODE(inst->itm_p[inst->curid].category_p,node->childsi[pos -1]);
        if (nodec->count > TREE_NODEMIN){
            return(_remove_item_moveright(inst,nodei,pos));
        }else{
            return(_remove_item_combine(inst,nodei,pos));
        }
    }else{
        nodec = REFNODE(inst->itm_p[inst->curid].category_p,node->childsi[1]);
        if (nodec->count > TREE_NODEMIN){
            return(_remove_item_moveleft(inst,nodei,1));
        }else{
            return(_remove_item_combine(inst,nodei,1));
        }
    }
    return(RET_NG);
}

/** *************************************************************
 * ツリープリント\n
 * ※safe - lock内で、ツリーをプリント\n
 *
 * @param[in]     inst      ツリーインスタンス
 * @result  int   成功：RET_OK/エラー:!=RET_OK
 ************************************************************* */
int print_tree_safe(tree_instance_ptr inst){
    uint32_t n;
    // 引数チェック
    if (!inst){
        return(RET_NG);
    }
    if (!inst->categories){
        return(RET_NG);
    }
    // 全カテゴリ をプリントする
    for(n = 0;n < CATEGORY_CNT;n++){
        inst->curid = n;
        // カテゴリ別ロック
        {	mutex_lock_t lock={&(inst->itm_p[inst->curid].category->category_mutex),RET_NG};
            start_lock(&lock);
            if (isvalid_lock(&lock) == RET_OK){
                printf("\n--------\n");
                _print_item_recurcive(inst,inst->itm[inst->curid]->rooti,0);
                printf("\n--------\n");
            }
            end_lock(&lock);
        }
    }
    return(RET_OK);
}
/** *************************************************************
 * 再帰プリント\n
 *
 * @param[in]     inst      ツリーインスタンス
 * @param[in]     nodei     ノードオフセット
 * @param[in]     nest      ネストレベル
 * @result  int 固定でRET_NG を返却
 ************************************************************* */
int _print_item_recurcive(tree_instance_ptr inst,node_instance_offset_t nodei,uint32_t nest){
    uint32_t			n;
    node_instance_ptr	node = NULL;
    //
    if (!ISEMPTY_NODE(nodei)){
        node = REFNODE(inst->itm_p[inst->curid].category_p,nodei);
        // 全子ノードをLoop
        printf("(");
        for(n = 0;n < node->count;n++){
            _print_item_recurcive(inst,node->childsi[n],(nest + 1));
            printf("[%u/%u/%u]",node->keys[n].keyhash,node->keys[n].data_field,node->keys[n].data_offset);
        }
        // 最後のリンク
        _print_item_recurcive(inst,node->childsi[node->count],(nest + 1));
        printf(")");
    }else{
        printf(".");
    }
    return(RET_NG);
}
/** *************************************************************
 * ノード比較function\n
 * ※ノード全一致判定に利用※
 *
 * @param[in]     a      比較元
 * @param[in]     b      比較先
 * @result  like a strcmp
 ************************************************************* */
int _compare_node_key_unsafe(node_key_ptr a,node_key_ptr b){
    // 完全一致
    if ((a->keyhash == b->keyhash) && (a->data_field == b->data_field) && (a->data_offset == b->data_offset)){
        return(0);
    }
    // ハッシュ値判定
    if (a->keyhash < b->keyhash){ return(-1); }
    else if(a->keyhash > b->keyhash){ return(1); }
    // データフィールド判定
    if (a->data_field < b->data_field){ return(-1); }
    else if(a->data_field > b->data_field){ return(1); }
    // データフィールド判定
    if (a->data_offset < b->data_offset){ return(-1); }
    else if(a->data_offset > b->data_offset){ return(1); }
    // ??
    return(0);
}
