/******************************************************************************/
/*! @addtogroup in process file mmap kvs db(shared memory map - reallocation)
 @file       smmr_pagedbuffer.h
 @brief      バッファ実装
 ******************************************************************************/

#ifndef PROJECT_SMMR_PAGEDBUFFER_H
#define PROJECT_SMMR_PAGEDBUFFER_H



#include "smmr.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

//////////////////////
// ページアライメントバッファ

int    pagedbuffer_create(paged_buffer_ptr*);
int    pagedbuffer_remove(paged_buffer_ptr*);
size_t pagedbuffer_current_size(paged_buffer_ptr);
char*  pagedbuffer_ptr(paged_buffer_ptr);
char*  pagedbuffer_dup(paged_buffer_ptr);
void   pagedbuffer_append_text(paged_buffer_ptr, const char* fmt,...);
void   pagedbuffer_append_textn(paged_buffer_ptr, const char* fmt,...);
int    pagedbuffer_append(paged_buffer_ptr,const char*,size_t);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif //PROJECT_SMMR_PAGEDBUFFER_H
