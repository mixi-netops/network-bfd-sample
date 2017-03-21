/******************************************************************************/
/*! @addtogroup in process file mmap kvs db(shared memory map - reallocation)
 @file       smmr_database.h
 @brief      database実装
 ******************************************************************************/

#ifndef PROJECT_SMMR_DATABASE_H
#define PROJECT_SMMR_DATABASE_H


#include "smmr.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


//////////////////////
// データファイル オペレーション

// データファイル   ：親プロセス用
int init_datafiles_from_file(ipcshm_datafiles_ptr,datafiles_on_process_ptr);
int create_db_area_parent(const char*,ipcshm_datafiles_ptr*);
int remove_db_area_parent(ipcshm_datafiles_ptr*,datafiles_on_process_ptr);
// 〃                ：子プロセス用
int childinit_db_area(ipcshm_datafiles_ptr*,datafiles_on_process_ptr);
int childuninit_db_area(ipcshm_datafiles_ptr*,datafiles_on_process_ptr);
// データファイル領域の最後に、データを追加
int insert_to_datafile(find_container_ptr);
// データファイル領域の空き領域に、データを追加
int insert_to_datafile_at_freepage(find_container_ptr);
// データファイル指定データアイテムを空き領域としてマーク
int change_free_to_datafile(ipcshm_datafiles_ptr,datafiles_on_process_ptr,node_key_ptr,int);
// 検索
int search(ipcshm_datafiles_ptr,datafiles_on_process_ptr,tree_instance_ptr,const char*,uint32_t,char**,uint32_t*);
//  格納（キーバリューで文字列をセット）
int store(ipcshm_datafiles_ptr,datafiles_on_process_ptr,tree_instance_ptr,const char*,uint32_t,char*,uint32_t,uint32_t);
// メモリ状況 ：文字列取得
int print(tree_instance_ptr,char**,uint32_t*);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif //PROJECT_SMMR_DATABASE_H
