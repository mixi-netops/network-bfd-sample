/******************************************************************************/
/*! @addtogroup in process file mmap kvs db(shared memory map - reallocation)
 @file       smmr_categories.h
 @brief      カテゴリー実装
 ******************************************************************************/
#ifndef PROJECT_SMMR_CATEGORIES_H
#define PROJECT_SMMR_CATEGORIES_H

#include "smmr.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


//////////////////////
// カテゴリ  オペレーション

// カテゴリ 管理領域 ：親プロセス用
int create_categories_area(const char*,ipcshm_categories_ptr*,categories_on_process_ptr);
int init_categories_from_file(ipcshm_categories_ptr,categories_on_process_ptr,int);
int remove_categories_area(ipcshm_categories_ptr*,categories_on_process_ptr);
// 〃                ：子プロセス用
int childinit_categories_area(ipcshm_categories_ptr*,categories_on_process_ptr);
int childuninit_categories_area(ipcshm_categories_ptr*,categories_on_process_ptr);
// カテゴリオペレーション用ハンドル取得
// unsafe（ロック外）での参照であり、カテゴリへの参照時に利用する
category_on_process_ptr get_category_unsafe(ipcshm_categories_ptr,categories_on_process_ptr,const char*,uint32_t);
// カテゴリオペレーション用ハンドル取得（キー指定）
category_on_process_ptr get_category_by_hashkey_unsafe(ipcshm_categories_ptr,categories_on_process_ptr,uint32_t);
// カテゴリindex取得（キー指定）
int get_category_id_by_hashkey_unsafe(ipcshm_categories_ptr,categories_on_process_ptr,uint32_t);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif //PROJECT_SMMR_CATEGORIES_H
