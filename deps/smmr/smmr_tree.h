/******************************************************************************/
/*! @addtogroup in process file mmap kvs db(shared memory map - reallocation)
 @file       smmr_tree.h
 @brief      btree実装
 ******************************************************************************/
#ifndef PROJECT_SMMR_TREE_H
#define PROJECT_SMMR_TREE_H

#include "smmr.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

//////////////////////
// btree オペレーション

// 検索結果コールバック
typedef int (*on_finded_callback)(node_key_ptr,void*);
// ツリー生成・解放
tree_instance_ptr create_tree_unsafe(ipcshm_categories_ptr,categories_on_process_ptr);
int free_tree_unsafe(tree_instance_ptr);
// アイテム追加
int add_tree_safe(tree_instance_ptr,node_key_ptr);
// アイテム追加（ロック無し：コールバック内から利用する用）
int add_tree_unsafe(tree_instance_ptr,node_key_ptr);
// アイテム検索（ユニーク）
int find_tree_safe(tree_instance_ptr,node_key_ptr,on_finded_callback,void*);
// アイテム検索（重複有り：キーハッシュのみ判定）
int find_tree_only_keyhash_safe(tree_instance_ptr,uint32_t,on_finded_callback,void*);
// アイテム削除（ユニーク）
int   remove_tree_safe(tree_instance_ptr,node_key_ptr);
// アイテム削除（ユニーク：コールバック内から利用する用）
int   remove_tree_ussafe(tree_instance_ptr,node_key_ptr);
// ツリープリント
int   print_tree_safe(tree_instance_ptr);
// ツリー count
uint32_t	count_tree_safe(tree_instance_ptr);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif //PROJECT_SMMR_TREE_H
