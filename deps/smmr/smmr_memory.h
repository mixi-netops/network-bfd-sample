/******************************************************************************/
/*! @addtogroup in process file mmap kvs db(shared memory map - reallocation)
 @file       smmr_categories.h
 @brief      メモリ実装
 ******************************************************************************/

#ifndef PROJECT_SMMR_MEMORY_H
#define PROJECT_SMMR_MEMORY_H


#include "smmr.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

//////////////////////
// メモリページ オペレーション

// メモリ状況の出力
int show_memory_status(categories_on_process_ptr);
// ページ領域のリビルド
int rebuild_pages_area(categories_on_process_ptr);
// ページ管理 -> カテゴリ管理 staticsマージ
int merge_page_to_category(categories_on_process_ptr);
// 割当済アイテムから、指定idデータをアタッチ
void* attach(category_on_process_ptr,uint32_t,uint32_t);
// カテゴリ：メモリ割当
void* shmalloc(category_on_process_ptr,uint32_t,uint16_t*,uint16_t*,uint32_t*);
// カテゴリ：メモリ解放
void shmfree(category_on_process_ptr,void*);
// カテゴリ：サマリ情報の再生成
void rebuild_page_statics(category_on_process_ptr);
// メモリ状況のカテゴリ毎文字列出力
int makebuffer_memory_status_unsafe(category_on_process_ptr,char**,uint32_t*);
// メモリ状況の取得
int memory_status_unsafe(category_on_process_ptr,mempage_summary_ptr);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //PROJECT_SMMR_MEMORY_H
