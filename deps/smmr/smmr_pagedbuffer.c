/******************************************************************************/
/*! @addtogroup in process file mmap kvs db(shared memory map - reallocation)
    @file       smmr_pagedbuffer.c
    @brief      ページアラインドバッファ
******************************************************************************/
#include "smmr_pagedbuffer.h"
#include <assert.h>


/** *************************************************************
 * ページバッファ アロケート
 *
 * @param[in,out] pphdl    ページバッファ ハンドル
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int pagedbuffer_create(paged_buffer_ptr* pphdl){
    (*pphdl) = (paged_buffer_ptr)malloc(sizeof(paged_buffer_t));
    assert(*pphdl);

    (*pphdl)->data = (char*)malloc(4096);
    assert((*pphdl)->data);
    memset((*pphdl)->data,0,4096);
    (*pphdl)->allocated_size = 4096;
    (*pphdl)->end_offset	 = 0;

    return(RET_OK);
}
/** *************************************************************
 * ページバッファ 解放処理
 *
 * @param[in,out] pphdl    ページバッファ ハンドル
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int pagedbuffer_remove(paged_buffer_ptr* pphdl){
    if (*pphdl != NULL){
        if ((*pphdl)->data){ free((*pphdl)->data); }
        free((*pphdl));
    }
    (*pphdl) = NULL;
    return(RET_OK);
}
/** *************************************************************
 * ページバッファ ：カレントサイズ取得
 *
 * @param[in]     phdl    ページバッファ ハンドル
 * @result  size_t カレントサイズ
 ************************************************************* */
size_t pagedbuffer_current_size(paged_buffer_ptr phdl){
    assert(phdl);
    return(phdl->end_offset);
}
/** *************************************************************
 * ページバッファデータ領域を返却\n
 * \n
 *
 * @param[in]     phdl    ページバッファ ハンドル
 * @result      char*  ページバッファ、データ領域アドレス
 ************************************************************* */
char*  pagedbuffer_ptr(paged_buffer_ptr phdl){
    assert(phdl);
    return(phdl->data);
}
/** *************************************************************
 * ページバッファデータ領域のコピーを返却\n
 * \n
 *
 * @param[in]     phdl    ページバッファ ハンドル
 * @result      char*  ページバッファ、コピーしたデータ領域アドレス
 ************************************************************* */
char*  pagedbuffer_dup(paged_buffer_ptr phdl){
    char* dup = NULL;
    assert(phdl);
    dup = (char*)malloc(phdl->end_offset);
    if (!dup){
        return(NULL);
    }
    memcpy(dup,phdl->data,phdl->end_offset);
    return(dup);
}
/** *************************************************************
 * ページバッファに整形した文字列データを追加\n
 * <br>タグを追加挿入する\n
 *
 * @param[in]     phdl    ページバッファ ハンドル
 * @param[in]     fmt, ...  フォーマット可変引数
 ************************************************************* */
void pagedbuffer_append_textn(paged_buffer_ptr phdl, const char* fmt,...){
    char	bf[1024] = {0x00};
    va_list  args;
    va_start(args, fmt);
    vsnprintf(bf,sizeof(bf) -1,fmt,args);
    va_end(args);
    assert(phdl);
    //
    pagedbuffer_append(phdl,bf,strlen(bf));
    pagedbuffer_append(phdl,"<br>",strlen("<br>"));
}
/** *************************************************************
 * ページバッファに整形した文字列データを追加\n
 * \n
 *
 * @param[in]     phdl    ページバッファ ハンドル
 * @param[in]     fmt, ...  フォーマット可変引数
 ************************************************************* */
void   pagedbuffer_append_text(paged_buffer_ptr phdl, const char* fmt,...){
    char	bf[1024] = {0x00};
    va_list  args;
    va_start(args, fmt);
    vsnprintf(bf,sizeof(bf) -1,fmt,args);
    va_end(args);
    assert(phdl);
    pagedbuffer_append(phdl,bf,strlen(bf));
}
/** *************************************************************
 * ページバッファ データ追加
 *
 * @param[in,out] pphdl    ページバッファ ハンドル
 * @param[in]     data     追加データ先頭アドレス
 * @param[in]     len      追加データバッファサイズ
 * @result  int   RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int pagedbuffer_append(paged_buffer_ptr phdl,const char* data,size_t len){
    size_t	need_size	= 0;
    size_t	tmp_size	= 0;
    size_t	chk_size	= 0;
    void*	p			= NULL;

    assert(phdl);
    need_size = phdl->end_offset + len;
    tmp_size  = phdl->allocated_size;

    // 規定バッファ長を超える場合、エラーとする
    if (need_size > phdl->allocated_size){
        while(tmp_size < need_size){
            if (tmp_size == 0){ tmp_size = 4096; }
            chk_size = (tmp_size << 1);
            if (chk_size < tmp_size){
                return(RET_NG);
            }
            tmp_size = chk_size;
        }
        // 必要領域が計算OK -> 再確保
        if ((p = realloc(phdl->data, tmp_size)) == NULL){
            return(RET_NG);
        }
        phdl->data			 = (char*)p;
        phdl->allocated_size = tmp_size;
    }
    // 今回データを前回データの後ろにつなげる
    memcpy((phdl->data + phdl->end_offset),data,len);
    phdl->end_offset += len;
    return(RET_OK);
}
