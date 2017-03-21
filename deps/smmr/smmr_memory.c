/******************************************************************************/
/*! @addtogroup in process file mmap kvs db(shared memory map - reallocation)
    @file       smmr_memory.c
    @brief      キャッシュページ
*******************************************************************************
    ページとは、各カテゴリ配下に配置されるメモリページ\n
*******************************************************************************
******************************************************************************/
#include "smmr_memory.h"
#include "smmr_pagedbuffer.h"
#include <sys/mman.h>
#include <assert.h>

// local function's

// FREEルートアイテムから、指定サイズを割り当て可能なオフセットを返却
static uint32_t _find_freeitem_over_size(char*,uint32_t);
// リサイクルリスト －＞ 利用LISTへ、リサイクルする
static uint32_t _operate_recycle(uint32_t,char*,uint32_t);
// 空き領域に新規割り当てをセット
static uint32_t _operate_insert(uint32_t,char*,uint32_t);
// ビットマップ：サマリ値の再構築
static int _rebuild_bitmap_and_pagesummary(uint32_t,char*,char*,int);
// バケットの再構築とリサイクル領域の初期化
static int _shrink_backet_recycle(uint32_t,char*,char*);

/** *************************************************************
 * ページ管理 -> カテゴリ管理 staticsマージ\n
 * ページ毎サマリを、カテゴリ毎サマリへ反映\n
 *
 * @param[in]     categories      カテゴリアクセサ
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int merge_page_to_category(categories_on_process_ptr categories){
    int					n,m;
    char*				pctgy = NULL;
    char*				errpage_bmp = NULL;
    char*				mempage_root = NULL;
    mempage_system_ptr	mmgr = NULL;
    //memitem_head_ptr	mhpt = NULL;
    memitem_root_detail_ptr	mrpti = NULL;

    // ステータスチェック
    if (!categories){
        return(RET_NG);
    }
    // 個別カテゴリチェック
    for(n = 0;n < CATEGORY_CNT;n++){
        if (!categories->category[n].data_pointer || (categories->category[n].data_pointer == (char*)INVALID_SOCKET)){
            return(RET_NG);
        }
    }
    // 全カテゴリLoop
    for(n = 0;n < CATEGORY_CNT;n++){
        pctgy		= categories->category[n].data_pointer;
        // 管理領域アドレス参照
        mmgr		= (mempage_system_ptr)(pctgy + MMPOS_SYSPAGE_SYSTEM);	// システム管理領域
        errpage_bmp	= (pctgy + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_ERR));		// エラーページ：ビットマップ
        // システム管理領域 マージ部初期化
        mmgr->free_size	= 0;
        mmgr->free_cnt	= 0;
        mmgr->use_size	= 0;
        mmgr->use_cnt	= 0;

        // カテゴリ内：全ページを走査し再計算
        for(m = IPCSHM_PAGE_USRAREA;m < IPCSHM_PAGE_USRAREA_MAX;m++){
            // ページのroot オブジェクト
            mempage_root	= (pctgy + MMPOS_USRPAGE(m));
            //mhpt			= (memitem_head_ptr)(mempage_root + MMPOS_USED_ROOT);
            // ビットマップ、サマリの 再構築
            if (_rebuild_bitmap_and_pagesummary(m,mempage_root,(char*)pctgy,0) != RET_OK){
                PAGEBIT_ON(errpage_bmp,m);
                continue;
            }
            PAGEBIT_OFF(errpage_bmp,m);

            // ページ管理 利用領域
            mrpti = (memitem_root_detail_ptr)((mempage_root + MMPOS_USED_ROOT) + MMPOS_ROOT_DTL);
            mmgr->use_size	+= (mrpti->use_size);
            mmgr->use_cnt	+= (mrpti->use_cnt);

            // ページ管理 空き領域
            mrpti = (memitem_root_detail_ptr)((mempage_root + MMPOS_FREE_ROOT) + MMPOS_ROOT_DTL);
            mmgr->free_size	+= (mrpti->free_size);
            mmgr->free_cnt	+= (mrpti->free_cnt);
        }
        // カテゴリ毎にfileに同期
        msync(categories->category[n].data_pointer,categories->category[n].filesize,0);
    }
    return(RET_OK);
}

/** *************************************************************
 * メモリ状況の出力\n
 * printf で標準出力へ\n
 *
 * @param[in]     categories      カテゴリアクセサ
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int show_memory_status(categories_on_process_ptr categories){
    int					n,m,l,page_type;
    char*				pctgy = NULL;
    char*				page_bmp = NULL;
    char*				mempage_root = NULL;
    mempage_system_ptr	mmgr = NULL;
    //memitem_head_ptr	mhpt = NULL;
    memitem_root_detail_ptr	mrpt = NULL;

    // ステータスチェック
    if (!categories){
        return(RET_NG);
    }
    // 全カテゴリLoop
    for(n = 0;n < CATEGORY_CNT;n++){
        pctgy		= categories->category[n].data_pointer;
        // システム管理領域
        mmgr		= (mempage_system_ptr)(pctgy + MMPOS_SYSPAGE_SYSTEM);

        // システム管理領域初期値
        printf("\n-----------------\n");
        printf("system - summaries(%p).\n",pctgy);
        printf("    idx       [%d]\n",mmgr->idx);
        printf("    page_size [%d]\n",mmgr->page_size);
        printf("    free_size [%d]\n",mmgr->free_size);
        printf("    free_cnt  [%d]\n",mmgr->free_cnt);
        printf("    use_size  [%d]\n",mmgr->use_size);
        printf("    use_cnt   [%d]\n",mmgr->use_cnt);
        printf("\n-----------------\n");
        // 各種ページbitmap
        for(page_type = IPCSHM_SYSPAGE_DC_032;page_type < IPCSHM_SYSPAGE_MAX;page_type++){
            page_bmp	= (pctgy + MMPOS_SYSPAGE_BMP(page_type));
            printf("\n-----------------\n");
            printf("page bitmaps[%d](%p).\n",page_type,page_bmp);
            for(l = 0;l < PAGE_CNT;l++){
                if (ISPAGEBIT(page_bmp,l)){
                    printf("o");
                }else{
                    printf("-");
                }
                if (((l + 1) %   8) == 0){ printf(" "); }
                if (((l + 1) % 128) == 0){ printf("\n"); }
            }
            printf("\n-----------------\n");
        }
        // エラーページ判定用
        page_bmp	= (pctgy + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_ERR));

        // カテゴリ内：ユーザページを走査
        printf("\n-----------------\n");
        printf("category[%02d]used/freed  - summaries.(%p)\n",n,pctgy);
        for(l = 0,m = IPCSHM_PAGE_USRAREA;m < IPCSHM_PAGE_USRAREA_MAX;m++,l++){
            // ページのroot オブジェクト
            mempage_root	= (pctgy + MMPOS_USRPAGE(m));
            //mhpt			= (memitem_head_ptr)(mempage_root + MMPOS_USED_ROOT);
            // エラーページで無ければ
            if (!ISPAGEBIT(page_bmp,m)){
                // ---- 利用領域  ----
                mrpt = (memitem_root_detail_ptr)((mempage_root + MMPOS_USED_ROOT) + MMPOS_ROOT_DTL);
                printf(" %4d/%3d",mrpt->use_size,mrpt->use_cnt);
                // ---- 空き領域 ----
                mrpt = (memitem_root_detail_ptr)((mempage_root + MMPOS_FREE_ROOT) + MMPOS_ROOT_DTL);
                printf("(%4d/%3d)",mrpt->free_size,mrpt->free_cnt);
            }else{
                printf(" ----/---(----/---)");
            }
            if (((l + 1) % 8) == 0){ printf("\n"); }
        }
        printf("\n-----------------\n");
    }
    return(RET_OK);
}

/** *************************************************************
 * メモリ状況のバッファ文字列出力\n
 *
 * @param[in]     categories      カテゴリアクセサ
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int makebuffer_memory_status_unsafe(category_on_process_ptr category,char** ret,uint32_t* retlen){
    int					m,l,page_type;
    char*				pctgy = NULL;
    char*				page_bmp = NULL;
    char*				mempage_root = NULL;
    mempage_system_ptr	mmgr = NULL;
    memitem_root_detail_ptr	mrpt = NULL;
    paged_buffer_ptr	result_buffer;

    // ステータスチェック
    if (!category){ return(RET_NG); }
    if (pagedbuffer_create(&result_buffer)!=RET_OK){ return(RET_NG); }
    // 返却値初期値
    (*ret)		= NULL;
    (*retlen)	= 0;

    pctgy		= category->data_pointer;
    // システム管理領域
    mmgr		= (mempage_system_ptr)(pctgy + MMPOS_SYSPAGE_SYSTEM);

    // システム管理領域初期値
    pagedbuffer_append_textn(result_buffer,"-----------------");
    pagedbuffer_append_textn(result_buffer,"system - summaries(%p).",pctgy);
    pagedbuffer_append_textn(result_buffer,"    idx       [%d]",mmgr->idx);
    pagedbuffer_append_textn(result_buffer,"    page_size [%d]",mmgr->page_size);
    pagedbuffer_append_textn(result_buffer,"    free_size [%d]",mmgr->free_size);
    pagedbuffer_append_textn(result_buffer,"    free_cnt  [%d]",mmgr->free_cnt);
    pagedbuffer_append_textn(result_buffer,"    free_cnt  [%d]",mmgr->free_cnt);
    pagedbuffer_append_textn(result_buffer,"    use_size  [%d]",mmgr->use_size);
    pagedbuffer_append_textn(result_buffer,"    use_cnt   [%d]",mmgr->use_cnt);
    pagedbuffer_append_textn(result_buffer,"-----------------");

    // 各種ページbitmap
    for(page_type = IPCSHM_SYSPAGE_DC_032;page_type < IPCSHM_SYSPAGE_MAX;page_type++){
        page_bmp	= (pctgy + MMPOS_SYSPAGE_BMP(page_type));
        pagedbuffer_append_textn(result_buffer,"-----------------");
        pagedbuffer_append_textn(result_buffer,"page bitmaps[%d](%p).",page_type,page_bmp);
        //
        for(l = 0;l < PAGE_CNT;l++){
            if (ISPAGEBIT(page_bmp,l)){
                pagedbuffer_append_text(result_buffer,"o");
            }else{
                pagedbuffer_append_text(result_buffer,"-");
            }
            if (((l + 1) %   8) == 0){ pagedbuffer_append_text(result_buffer," "); }
            if (((l + 1) % 128) == 0){ pagedbuffer_append_textn(result_buffer,""); }
        }
        pagedbuffer_append_textn(result_buffer,"-----------------");
    }
    // エラーページ判定用
    page_bmp	= (pctgy + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_ERR));

    // カテゴリ内：ユーザページを走査
    pagedbuffer_append_textn(result_buffer,"-----------------");
    pagedbuffer_append_textn(result_buffer,"category[%2d]used/freed  - summaries.(%p)",category->id,pctgy);
    for(l = 0,m = IPCSHM_PAGE_USRAREA;m < IPCSHM_PAGE_USRAREA_MAX;m++,l++){
        // ページのroot オブジェクト
        mempage_root	= (pctgy + MMPOS_USRPAGE(m));
        //mhpt			= (memitem_head_ptr)(mempage_root + MMPOS_USED_ROOT);
        // エラーページで無ければ
        if (!ISPAGEBIT(page_bmp,m)){
            // ---- 利用領域  ----
            mrpt = (memitem_root_detail_ptr)((mempage_root + MMPOS_USED_ROOT) + MMPOS_ROOT_DTL);
            pagedbuffer_append_text(result_buffer," %4d/%3d",mrpt->use_size,mrpt->use_cnt);
            // ---- 空き領域 ----
            mrpt = (memitem_root_detail_ptr)((mempage_root + MMPOS_FREE_ROOT) + MMPOS_ROOT_DTL);
            pagedbuffer_append_text(result_buffer,"(%4d/%3d)",mrpt->free_size,mrpt->free_cnt);
        }else{
            pagedbuffer_append_text(result_buffer," ----/---(----/---)");
        }
        if (((l + 1) % 8) == 0){ pagedbuffer_append_textn(result_buffer,""); }
    }
    pagedbuffer_append_textn(result_buffer,"");
    pagedbuffer_append_textn(result_buffer,"-----------------");
    //
    (*ret)		= pagedbuffer_dup(result_buffer);
    (*retlen)	= pagedbuffer_current_size(result_buffer);
    assert(pagedbuffer_remove(&result_buffer)==RET_OK);
    //
    return(RET_OK);
}
/** *************************************************************
 * メモリ状況の取得\n
 *
 * @param[in]     categories      カテゴリアクセサ
 * @param[in/out] summary         サマリ
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int memory_status_unsafe(category_on_process_ptr category,mempage_summary_ptr summary){
    int					m,l,page_type;
    char*				pctgy = NULL;
    char*				page_bmp = NULL;
    char*				mempage_root = NULL;
    mempage_system_ptr	mmgr = NULL;
    memitem_root_detail_ptr	mrpt = NULL;
    paged_buffer_ptr	result_buffer;

    // ステータスチェック
    if (!category || !summary){ return(RET_NG); }
    if (pagedbuffer_create(&result_buffer)!=RET_OK){ return(RET_NG); }
    memset(summary, 0,sizeof(*summary));

    pctgy		= category->data_pointer;
    // システム管理領域
    mmgr		= (mempage_system_ptr)(pctgy + MMPOS_SYSPAGE_SYSTEM);
    // システム管理領域
    memcpy(&summary->sys, mmgr, sizeof(*mmgr));

    // 各種ページbitmap
    for(page_type = IPCSHM_SYSPAGE_DC_032;page_type < IPCSHM_SYSPAGE_MAX;page_type++){
        page_bmp	= (pctgy + MMPOS_SYSPAGE_BMP(page_type));
        memcpy(&summary->bitmap[page_type], page_bmp, PAGE_CNT);
    }
    // カテゴリ内：ユーザページを走査
    for(l = 0,m = IPCSHM_PAGE_USRAREA;m < IPCSHM_PAGE_USRAREA_MAX;m++,l++){
        // ページのroot オブジェクト
        mempage_root	= (pctgy + MMPOS_USRPAGE(m));

        mrpt = (memitem_root_detail_ptr)((mempage_root + MMPOS_USED_ROOT) + MMPOS_ROOT_DTL);
        memcpy(&summary->used[m], mrpt, sizeof(*mrpt));
        mrpt = (memitem_root_detail_ptr)((mempage_root + MMPOS_FREE_ROOT) + MMPOS_ROOT_DTL);
        memcpy(&summary->free[m], mrpt, sizeof(*mrpt));
    }
    return(RET_OK);
}

/** *************************************************************
 * ページ領域のリビルド\n
 * ※メインプロセスで一度のみ実行されること\n
 * ※新規カテゴリファイルの場合は、そのヘッダ情報の書き込みが\n
 * ※実行されるケースもある
 *
 * @param[in]     categories      カテゴリアクセサ
 * @result  RET_OK=成功、RET_OK!=エラー
 ************************************************************* */
int rebuild_pages_area(categories_on_process_ptr categories){
    int					n,m,isvalid;
    char*				pctgy = NULL;
    char*				errpage_bmp = NULL;
    char*				dc032_bmp = NULL;
    char*				dc128_bmp = NULL;
    char*				dc512_bmp = NULL;
    char*				dc02k_bmp = NULL;
    char*				mempage_root = NULL;
    mempage_system_ptr	mmgr = NULL;
    memitem_head_ptr	mhpt = NULL;
    memitem_foot_ptr	mfpt = NULL;
    memitem_root_detail_ptr	mrpt = NULL,mrpti = NULL;

    // ステータスチェック
    if (!categories){
        return(RET_NG);
    }
    // 個別カテゴリチェック
    for(n = 0;n < CATEGORY_CNT;n++){
        if (!categories->category[n].data_pointer || (categories->category[n].data_pointer == (char*)INVALID_SOCKET)){
            return(RET_NG);
        }
    }
    // カレント永続ファイルデータのユーザエリアより、初期ページ状況をリビルド
    // 全カテゴリLoop
    for(n = 0;n < CATEGORY_CNT;n++){
        pctgy		= categories->category[n].data_pointer;
        // 管理領域アドレス参照
        mmgr		= (mempage_system_ptr)(pctgy + MMPOS_SYSPAGE_SYSTEM);	// システム管理領域
        errpage_bmp	= (pctgy + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_ERR));		// エラーページ：ビットマップ
        dc032_bmp	= (pctgy + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_DC_032));	// 純粋な未使用領域 各サイズ毎 ：〃
        dc128_bmp	= (pctgy + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_DC_128));
        dc512_bmp	= (pctgy + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_DC_512));
        dc02k_bmp	= (pctgy + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_DC_2K));

        // システム管理領域初期値
        mmgr->idx		= n;
        mmgr->page_size	= PAGE_SIZE;
        mmgr->file_size	= categories->category[n].filesize;
        mmgr->free_size	= 0;
        mmgr->free_cnt	= 0;
        mmgr->use_size	= 0;
        mmgr->use_cnt	= 0;

        // カテゴリ内：全ページを走査しbitmapを準備
        // ユーザページは、先頭から「struct memitem_head」 のリンクで
        // 詰められている
        for(m = IPCSHM_PAGE_USRAREA;m < IPCSHM_PAGE_USRAREA_MAX;m++){
            // ページのroot オブジェクト
            mempage_root	= (pctgy + MMPOS_USRPAGE(m));
            mhpt			= (memitem_head_ptr)(mempage_root + MMPOS_USED_ROOT);
            isvalid			= RET_NG;
            // 先頭が利用領域オブジェクト以外の場合、初期データファイルである
            if (mhpt->signature != MMSIG_USED){
                // ----------------------------------------------------
                // ヘッダ（利用領域root）を書きこんで終了
                mhpt->signature		= MMSIG_USED;
                mhpt->prev_offset	= 0;
                mhpt->next_offset	= MMFLG_CLOSE;
                mhpt->len			= MMPOS_ROOT_SIZE;

                // 詳細（利用領域root）
                mrpt = (memitem_root_detail_ptr)((char*)mhpt + MMPOS_ROOT_DTL);
                mrpt->first_offset	= MMPOS_USED_ROOT;	// 初期ファイルは、、自分自身
                mrpt->last_offset	= MMPOS_USED_ROOT;
                mrpt->use_size		= 0;
                mrpt->use_cnt		= 0;
                mrpt->free_size		= 0;
                mrpt->free_cnt		= 0;

                // フッタ設定（利用領域root）
                mfpt = (memitem_foot_ptr)((char*)mhpt + MMPOS_ROOT_FOOT);
                mfpt->created		= time(0);
                mfpt->suffix		= MMSIG_SUFX;

                // バケット領域、リサイクル領域の初期値
                _shrink_backet_recycle(m,mempage_root,pctgy);

                // 初期ファイル（empty）からのページ設定
                isvalid				= RET_OK;
            }else{
                // [利用領域:root:ヘッダ]
                while(1){
                    // [利用領域:ヘッダ]
                    if ((mhpt->prev_offset != 0) || (mhpt->len != MMPOS_ROOT_SIZE)){ break; }
                    mfpt = (memitem_foot_ptr)((char*)mhpt + MMPOS_ROOT_FOOT);
                    // [利用領域:フッタ]
                    if (mfpt->suffix != MMSIG_SUFX){ break; }
                    // [空き領域:ヘッダ]
                    mhpt = (memitem_head_ptr)(mempage_root + MMPOS_FREE_ROOT);
                    if ((mhpt->prev_offset != 0) || (mhpt->len != MMPOS_ROOT_SIZE) || (mhpt->signature != MMSIG_FREE)){ break; }
                    // [空き領域:フッタ]
                    mfpt = (memitem_foot_ptr)((char*)mhpt + MMPOS_ROOT_FOOT);
                    if (mfpt->suffix != MMSIG_SUFX){ break; }
                    // [バケット領域:ヘッダ]
                    mhpt = (memitem_head_ptr)(mempage_root + MMPOS_BCKT_ROOT);
                    if ((mhpt->prev_offset != 0) || (mhpt->len != MMPOS_ROOT_SIZE) || (mhpt->signature != MMSIG_BCKT)){ break; }
                    // [バケット領域:フッタ]
                    mfpt = (memitem_foot_ptr)((char*)mhpt + MMPOS_ROOT_FOOT);
                    if (mfpt->suffix != MMSIG_SUFX){ break; }

                    // 既存ファイルからのメモリコンフィグ：有効ステータス
                    isvalid = RET_MOREDATA;
                    break;
                }
            }
            mhpt = (memitem_head_ptr)(mempage_root + MMPOS_USED_ROOT);

            // 既存ファイルから生成した以外はエラー=無し
            if (isvalid != RET_MOREDATA){
                PAGEBIT_OFF(errpage_bmp,m);
            }
            // 初期ファイルから生成されている
            if (isvalid == RET_OK){
                // 純粋未使用領域は全て空きONである
                PAGEBIT_ON(dc032_bmp,m); PAGEBIT_ON(dc128_bmp,m); PAGEBIT_ON(dc512_bmp,m); PAGEBIT_ON(dc02k_bmp,m);
            }
            // エラー状態である－＞このページにエラーをマークして次のページへ
            // ※ rootヘッダーエラー
            if (isvalid == RET_NG){
                PAGEBIT_ON(errpage_bmp,m);
                continue;
            }
            // 有効なページである －＞メモリlinkをLoopし管理情報を生成
            // ビットマップ、サマリの 再構築
            if (_rebuild_bitmap_and_pagesummary(m,mempage_root,(char*)pctgy,0) != RET_OK){
                PAGEBIT_ON(errpage_bmp,m);
                continue;
            }
            PAGEBIT_OFF(errpage_bmp,m);
            // ページ管理 利用領域
            mrpti = (memitem_root_detail_ptr)((mempage_root + MMPOS_USED_ROOT) + MMPOS_ROOT_DTL);

            if (mrpti->use_size){
                mmgr->use_size	+= (mrpti->use_size);
                mmgr->use_cnt	+= (mrpti->use_cnt);
            }

            // ページ管理 空き領域
            mrpti = (memitem_root_detail_ptr)((mempage_root + MMPOS_FREE_ROOT) + MMPOS_ROOT_DTL);
            if (mrpti->free_size){
                mmgr->free_size	+= (mrpti->free_size);
                mmgr->free_cnt	+= (mrpti->free_cnt);
            }
        }
        // カテゴリ毎にfileに同期
        msync(categories->category[n].data_pointer,categories->category[n].filesize,0);
    }
    return(RET_OK);
}
/** *************************************************************
 *  割当済アイテムから、指定サイズ + idデータを検索しアタッチ\n
 *
 * @param[in]     category      カテゴリアクセサ
 * @param[in]     size          割当サイズ（ユーザ領域）
 * @param[in]     itemid        シグネチャ：このシグネチャと一致する\n
 *                              アイテムが検索される
 * @result  成功時:not null：アタッチできたアドレス/エラー時：NULL
 ************************************************************* */
void* attach(category_on_process_ptr category,uint32_t size,uint32_t itemid){
    uint32_t				offset = 0,n;
    uint32_t				aligned_size;
    uint32_t*				signature_id;
    ULONGLONG				check_counter = 0;
    memitem_head_ptr		mhpt = NULL;
    memitem_foot_ptr		mfpt = NULL;
    char*					root_pointer = NULL;

    // 引数チェック
    if (!category || !size){
        return(NULL);
    }
    // ステータスチェック
    if (!category->data_pointer || (category->data_pointer == (char*)INVALID_SOCKET)){
        return(NULL);
    }
    // 検索するサイズは、4byteにアライメントしておく
    aligned_size= (size % 4)==0?(size):(((size / 4) + 1) * 4);

    // ユーザページloop
    for(n = IPCSHM_PAGE_USRAREA;n < IPCSHM_PAGE_USRAREA_MAX;n++){
        root_pointer = (category->data_pointer + MMPOS_USRPAGE(n));
        // ルートオブジェクト
        mhpt = (memitem_head_ptr)(root_pointer + MMPOS_USED_ROOT);
        mfpt = (memitem_foot_ptr)((root_pointer + MMPOS_USED_ROOT) + MMPOS_ROOT_FOOT);
        //
        for(check_counter = 0,offset = MMPOS_USED_ROOT;offset < PAGE_SIZE;check_counter++){
            // validate
            if (mhpt->signature != MMSIG_USED){ break; }					// シグネチャ
            if (mhpt->len < MMPOS_MIN_OFST){ break; }						// 割当サイズ
            if (mfpt->suffix != MMSIG_SUFX){ break; }						// シグネチャ（フッタ）
            // ルートオブジェクト以外＋指定サイズに足りる
            if (offset != MMPOS_USED_ROOT && mhpt->len == (MMSIZ_HF_SIZE + aligned_size)){
                // 自ページに収まらないのはバグ
                if (offset > PAGE_SIZE){
                    printf("invalid double link list(attach.offset).\n");
                    return(NULL);
                }
                // そして先頭4byteが指定signatureである場合にhit
                signature_id =  (uint32_t*)(root_pointer + offset + sizeof(memitem_head_t));
                if ((*signature_id) == itemid){
                    return(signature_id);
                }
            }
            // link終了
            if (mhpt->next_offset == MMFLG_CLOSE){ break; }
            // check hung loop.
            if (check_counter > UINT_MAX){
                printf("invalid double link list(attach).\n");
                break;
            }
            // 次のlinkへ
            offset = mhpt->next_offset;
            mhpt = (memitem_head_ptr)(root_pointer + offset);
            mfpt = (memitem_foot_ptr)((root_pointer + offset) + (mhpt->len - sizeof(memitem_foot_t)));
        }
    }
    return(NULL);
}

/** *************************************************************
 *  カテゴリ：メモリ割当\n
 *  （カテゴリから指定サイズの領域を割り当てる）
 *
 * @param[in]     category      カテゴリアクセサ
 * @param[in]     size          割当サイズ（ユーザ領域）
 * @param[in/out] cateid        割り当てたカテゴリindex
 * @param[in/out] pageid        〃 ページindex
 * @param[in/out] offset        〃 オフセット値
 * @result  成功時:not null：割り当てたユーザアドレス/エラー時：NULL
 ************************************************************* */
void* shmalloc(category_on_process_ptr category,uint32_t size,uint16_t* cateid,uint16_t* pageid,uint32_t* offset){
    uint32_t			aligned_size;
    uint32_t			finded_offset;
    uint32_t			category_id;
    bitmap_itm_t		bitmapids[IPCSHM_BMPID_MAX] =
            {
                    {IPCSHM_SYSPAGE_DC_032,	IPCSHM_SYSPAGE_FREE_000},
                    {IPCSHM_SYSPAGE_DC_032,	IPCSHM_SYSPAGE_FREE_032},
                    {IPCSHM_SYSPAGE_DC_128,	IPCSHM_SYSPAGE_FREE_128},
                    {IPCSHM_SYSPAGE_DC_512,	IPCSHM_SYSPAGE_FREE_512},
                    {IPCSHM_SYSPAGE_DC_2K,	IPCSHM_SYSPAGE_FREE_2K},
            };
    int					start_bmp_id = IPCSHM_BMPID_MIN;
    int					n,m;
    char*				pctgy = NULL;
    char*				mempage_root = NULL;
    char*				errpage_bmp = NULL;
    char*				dc_bmp = NULL;
    char*				free_bmp = NULL;

    // 引数チェック
    if (!category || !size){
        return(NULL);
    }
    // 割当サイズ範囲[1 - 2048] bytes
    if (size == 0 || size > 2048){
        return(NULL);
    }
    // ステータスチェック
    if (!category){
        return(NULL);
    }
    if (!category->data_pointer || (category->data_pointer == (char*)INVALID_SOCKET)){
        return(NULL);
    }
    // 確保するサイズは、4byteにアライメントしておく
    aligned_size= (size % 4)==0?(size):(((size / 4) + 1) * 4);

    // 管理領域アドレス参照
    pctgy		= category->data_pointer;
    category_id	= category->id;
    // エラーページ：ビットマップ
    errpage_bmp	= (pctgy + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_ERR));

    // 指定サイズ 空きサイズ 小さい順で処理
    start_bmp_id = IPCSHM_BMPID_MIN;
    if (aligned_size > 0x0200){			start_bmp_id = IPCSHM_BMPID_512;
    }else if (aligned_size > 0x0080){	start_bmp_id = IPCSHM_BMPID_128;
    }else if (aligned_size > 0x0020){	start_bmp_id = IPCSHM_BMPID_032;
    }
    // 対象bitmap Loop
    for(m = start_bmp_id;m < IPCSHM_BMPID_MAX;m++){
        // 管理ビットマップにアタッチしておく
        free_bmp	= (pctgy + MMPOS_SYSPAGE_BMP(bitmapids[m].free_bmp_id));
        dc_bmp		= (pctgy + MMPOS_SYSPAGE_BMP(bitmapids[m].dc_bmp_id));
        //
        for(n = IPCSHM_PAGE_USRAREA;n < IPCSHM_PAGE_USRAREA_MAX;n++){
            mempage_root = (pctgy + MMPOS_USRPAGE(n));
            // エラーページは使わない
            if (ISPAGEBIT(errpage_bmp,n)){ continue; }
            // リサイクル領域を検索
            if (ISPAGEBIT(free_bmp,n)){
                // 対象ページでリサイクル可能 －＞ページ内詳細を検索
                if ((finded_offset = _operate_recycle(category_id,mempage_root,aligned_size)) != 0){
                    // 自ページに収まらないのはバグ
                    if (finded_offset > PAGE_SIZE){
                        fprintf(stderr, "invalid _operate_recycle(shmalloc).\n");
                        return(NULL);
                    }
                    // オフセット値保持の為
                    (*cateid)		= category_id;
                    (*pageid)		= n;
                    (*offset)		= finded_offset;
                    // 割り当て済み領域のユーザアドレス部を返却
                    return((mempage_root + finded_offset + sizeof(memitem_head_t)));
                }
            }
            // 普通の空き領域を検索
            if (ISPAGEBIT(dc_bmp,n)){
                // 対象ページに普通の空きがありそう －＞ページ内詳細を検索
                if ((finded_offset = _operate_insert(category_id,mempage_root,aligned_size)) != 0){
                    // 自ページに収まらないのはバグ
                    if (finded_offset > PAGE_SIZE){
                        fprintf(stderr, "invalid _operate_insert(shmalloc).\n");
                        return(NULL);
                    }
                    // オフセット値保持の為
                    (*cateid)		= category_id;
                    (*pageid)		= n;
                    (*offset)		= finded_offset;
                    // 割り当て済み領域のユーザアドレス部を返却
                    return((mempage_root + finded_offset + sizeof(memitem_head_t)));
                }
            }
        }
    }
    return(NULL);
}

/** *************************************************************
 * 空き領域に新規割り当てをセット\n
 *
 * @param[in]     category_id   カテゴリindex
 * @param[in]     mempage_root  メモリページの先頭
 * @param[in]     aligned_size  アライメント済み割り当てたいサイズ
 * @result  成功時:not zero：割り当てたユーザアドレス/エラー時：0
 ************************************************************* */
uint32_t _operate_insert(uint32_t category_id,char* mempage_root,uint32_t aligned_size){
    uint32_t				inserted_offset = 0;
    uint32_t				backet_len_bk	= 0;
    memitem_root_detail_ptr	mrpt_used = NULL;
    memitem_head_ptr		mhpt_used_last = NULL,mhpt_bckt = NULL,mhpt_bckt_root = NULL;
    memitem_head_ptr		mhpt_new = NULL;
    memitem_foot_ptr		mfpt_new = NULL;

    // 引数チェック
    if (!mempage_root || !aligned_size){
        return(0);
    }
    // --------------------------------------
    // rootアイテム から「利用中」の最後のリンクを参照しておく
    // ↑の後ろにつなげる
    mrpt_used		= (memitem_root_detail_ptr)((mempage_root + MMPOS_USED_ROOT) + MMPOS_ROOT_DTL);
    mhpt_used_last	= (memitem_head_ptr)(mempage_root + mrpt_used->last_offset);
    mhpt_bckt_root	= (memitem_head_ptr)(mempage_root + MMPOS_BCKT_ROOT);
    mhpt_bckt		= (memitem_head_ptr)(mempage_root + mhpt_bckt_root->next_offset);

    // バケット から今回分：割当サイズを アドレスの先頭から使っていく
    // つまりバケットは初期起動から、減り続けて行き、デフラグのタイミングで
    // 回収されるイメージ
    /*
     * バケット
     * -----------------------------
     * |x|           | |        |y|                              | |
     * | |backet root| |  <---> | |           backet             | |
     * |w|           |y|        |x|                              |-|
     *
     * -----------------------------
     * |x|           | |                 |z|                     | |
     * | |backet root| |  <----------->  | |   backet            | |
     * |w|           |z|                 |x|                     |-|
     *
     * 利用中リスト
     * -----------------------------
     * |a|          | |         |b|   | |
     * | |use last  | | <---->  | |new| |
     * |g|          |b|         |a|   |-|
     *
     */
    // バケットに空きがない？？
    if (mhpt_bckt->len < (aligned_size + MMSIZ_HF_SIZE)){
        return(0);
    }
    // 元バケットのバックアップ
    backet_len_bk		= mhpt_bckt->len;
    inserted_offset		= mhpt_bckt_root->next_offset;

    // バケット先頭を移動
    mhpt_bckt_root->next_offset = (mhpt_bckt_root->next_offset + (aligned_size + MMSIZ_HF_SIZE));
    // 新しい位置にバケット（ヘッダーだけ、フッターは変わらない）を配置
    mhpt_bckt = (memitem_head_ptr)(mempage_root + mhpt_bckt_root->next_offset);
    mhpt_bckt->signature	= MMSIG_BCKT;
    mhpt_bckt->prev_offset	= MMPOS_BCKT_ROOT;
    mhpt_bckt->next_offset	= MMFLG_CLOSE;
    mhpt_bckt->len			= (backet_len_bk - (aligned_size + MMSIZ_HF_SIZE));

    // 新規アイテムをリンクリストに追加
    mhpt_new				= (memitem_head_ptr)(mempage_root + inserted_offset);
    mhpt_new->signature		= MMSIG_USED;
    mhpt_new->prev_offset	= mrpt_used->last_offset;
    mhpt_new->next_offset	= MMFLG_CLOSE;
    mhpt_new->len			= (aligned_size + MMSIZ_HF_SIZE);

    // 新規アイテムデータ部を、0初期化：calloc みたいに
    memset(((char*)mhpt_new + sizeof(memitem_head_t)),0, aligned_size);

    // 新規アイテムフッター
    mfpt_new				= (memitem_foot_ptr)((char*)mhpt_new + (mhpt_new->len - sizeof(memitem_foot_t)));
    mfpt_new->created		= time(0);
    mfpt_new->suffix		= MMSIG_SUFX;
    mfpt_new->category_id	= category_id;

    // 利用領域 詳細管理領域の最終リンク先を新規アイテムに変更
    mrpt_used->last_offset		= inserted_offset;
    // 利用領域の最終アイテムのnextリンク先を新規アイテムに
    mhpt_used_last->next_offset	= inserted_offset;
    //
    return(inserted_offset);
}
/** *************************************************************
 * リサイクルリスト －＞ 利用LISTへ、リサイクルする\n
 * FREEリスト/USEDリスト 等 、双方向リストをいろいろつなげたり\n
 * 切り離したり\n
 *
 * @param[in]     category_id   カテゴリindex
 * @param[in]     mempage_root  メモリページの先頭
 * @param[in]     aligned_size  アライメント済み割り当てたいサイズ
 * @result  成功時:not zero：割り当てたユーザアドレス/エラー時：0
 ************************************************************* */
uint32_t _operate_recycle(uint32_t category_id,char* mempage_root,uint32_t aligned_size){
    uint32_t				finded_offset;
    memitem_head_ptr		mhpt = NULL,mhpt_prev = NULL,mhpt_next = NULL;
    memitem_head_ptr		mhpt_used_last = NULL;
    memitem_root_detail_ptr	mrpt = NULL;
    memitem_root_detail_ptr	mrpt_used = NULL;

    // 引数チェック
    if (!mempage_root || !aligned_size){
        return(0);
    }
    // --------------------------------------
    // 割り当て可能なリサイクルアイテムを指定ページから検索
    if ((finded_offset = _find_freeitem_over_size(mempage_root,aligned_size)) == 0){
        // リサイクルリストからは見つからなかった
        return(0);
    }
    // --------------------------------------
    // リサイクルアイテムをチェーンから切り離し
    // メモリアイテム参照準備
    mhpt		= (memitem_head_ptr)(mempage_root + finded_offset);
    mhpt_prev	= (memitem_head_ptr)(mempage_root + mhpt->prev_offset);
    mrpt		= (memitem_root_detail_ptr)((mempage_root + MMPOS_FREE_ROOT) + MMPOS_ROOT_DTL);
    mrpt_used	= (memitem_root_detail_ptr)((mempage_root + MMPOS_USED_ROOT) + MMPOS_ROOT_DTL);

    // ひとつ前のアイテムのネクストを修正
    mhpt_prev->next_offset	= mhpt->next_offset;
    //
    if (mhpt->next_offset == MMFLG_CLOSE){
        // 対象領域が最後のリンクだった場合
        /*
         * root.last_offset = y;
         * -----------------------------
         * |x|    | |       |y|      | |
         * | |prev| |  <->  | |finded| |
         * |w|    |y|       |x|      |-|
         *
         * root.last_offset = x;
         * -----------------------------
         * |x|    | |
         * | |prev| |
         * |w|    |-|
         */
        // ルートアイテム：最終リンク更新
        mrpt->last_offset	= mhpt->prev_offset;
    }else{
        // 対象領域が、途中リンクだった場合
        /*
         * リンクをつなげなおす
         * -----------------------------
         * |x|    | |       |y|      | |       |z|      | |
         * | |prev| |  <->  | |finded| |  <->  | | next | |
         * |w|    |y|       |x|      |z|       |y|      |-|
         *
         * -----------------------------
         * |x|    | |                          |z|      | |
         * | |prev| |  <-------------------->  | | next | |
         * |w|    |z|                          |x|      |-|
         *
         */
        // ネクストアイテム の前のリンクを付け替える
        mhpt_next	= (memitem_head_ptr)(mempage_root + mhpt->next_offset);
        mhpt_next->prev_offset	= mhpt->prev_offset;
    }

    // --------------------------------------
    // 切り離した リサイクルアイテムを「利用」チェーンの最後に接続

    // シグネチャを「利用」に書き換えて
    mhpt->signature	= MMSIG_USED;
    // 更新前の最終アイテム
    mhpt_used_last	= (memitem_head_ptr)(mempage_root + mrpt_used->last_offset);
    /*
     * root.last_offset = x;
     * -----------------------------
     * |x|    | |
     * | |last| |
     * |w|    |-|
     *
     * root.last_offset = y;
     * -----------------------------
     * |x|    | |       |y|      | |
     * | |last| |  <->  | |change| |
     * |w|    |y|       |x|      |-|
     *
     */
    // リサイクル -> 利用 アイテムへ最終アイテム設定
    mhpt->next_offset			= MMFLG_CLOSE;
    mhpt->prev_offset			= mrpt_used->last_offset;

    mhpt_used_last->next_offset	= finded_offset;
    mrpt_used->last_offset		= finded_offset;

    // ここまでで、「FREEリスト」 －＞ 「利用リスト」 チェーンに
    // アイテムが移動しており、このアドレスが「割り当て済み」
    // として返却できる
    return(finded_offset);
}
/** *************************************************************
 * FREEルートアイテムから、指定サイズを割り当て可能なオフセットを返却\n
 *
 * @param[in]     root_pointer  メモリページの先頭
 * @param[in]     size          アライメント済み割り当てたいサイズ
 * @result  成功時:not zero：割り当てたユーザアドレス/エラー時：0
 * @result  成功時:not zero：割り当て可能なシステムオフセット\n
 *          0：割り当て可能なFREEアイテムがない
 ************************************************************* */
uint32_t _find_freeitem_over_size(char* root_pointer,uint32_t size){
    uint32_t				offset = 0;
    ULONGLONG				check_counter = 0;
    memitem_head_ptr		mhpt = NULL;
    memitem_foot_ptr		mfpt = NULL;
    //
    if (!root_pointer || !size){
        return(0);
    }
    mhpt = (memitem_head_ptr)(root_pointer + MMPOS_FREE_ROOT);
    mfpt = (memitem_foot_ptr)((root_pointer + MMPOS_FREE_ROOT) + MMPOS_ROOT_FOOT);
    //
    for(check_counter = 0,offset = MMPOS_FREE_ROOT;offset < PAGE_SIZE;check_counter++){
        // validate
        if (mhpt->signature != MMSIG_FREE){ break; }					// シグネチャ
        if (mhpt->len < MMPOS_MIN_OFST){ break; }						// 割当サイズ
        if (mfpt->suffix != MMSIG_SUFX){ break; }						// シグネチャ（フッタ）
        // ルートオブジェクト以外＋指定サイズに足りる
        if (offset != MMPOS_FREE_ROOT && mhpt->len >= (MMSIZ_HF_SIZE + size)){
            return(offset);
        }
        // link終了
        if (mhpt->next_offset == MMFLG_CLOSE){ break; }
        // check hung loop.
        if (check_counter > UINT_MAX){
            printf("invalid double link list(_find_freeitem_over_size).\n");
            break;
        }
        // 次のlinkへ
        offset = mhpt->next_offset;
        mhpt = (memitem_head_ptr)(root_pointer + offset);
        mfpt = (memitem_foot_ptr)((root_pointer + offset) + (mhpt->len - sizeof(memitem_foot_t)));
    }
    return(0);
}
/** *************************************************************
 * 空き状況、FREE状況BITMAP、サマリを再構築\n
 *
 * @param[in]     pageid                 ページindex
 * @param[in]     root_page_pointer      ↑ページの先頭アドレス
 * @param[in]     root_category_pointer  当該ページが含まれるカテゴリの先頭アドレス
 * @param[in]     only_dc                空き領域のみ再構築フラグ
 * @result  成功時:RET_OK/エラー時：!=RET_OK
 ************************************************************* */
int _rebuild_bitmap_and_pagesummary(uint32_t pageid,char* root_page_pointer,char* root_category_pointer,int only_dc){
    uint32_t				offset = 0,used_size_summary = 0;
    uint32_t				remain_size = 0;
    uint32_t				used_count_summary = 0,recycle_size_summary = 0;
    uint32_t				recycle_count_summary = 0;
    int 					is_valid = 0;
    ULONGLONG				check_counter = 0;
    memitem_head_ptr		mhpt = NULL,mhpt_free = NULL;
    memitem_foot_ptr		mfpt = NULL,mfpt_free = NULL;
    memitem_root_detail_ptr	mrdt = NULL;
    char*					dc032_bmp = NULL;
    char*					dc128_bmp = NULL;
    char*					dc512_bmp = NULL;
    char*					dc02k_bmp = NULL;
    char*					free000_bmp = NULL;
    char*					free032_bmp = NULL;
    char*					free128_bmp = NULL;
    char*					free512_bmp = NULL;
    char*					free02k_bmp = NULL;
    //
    if (!root_page_pointer || !root_category_pointer){
        return(RET_NG);
    }
    // ページチェック
    if (pageid < 1 || pageid >= PAGE_CNT){
        printf("invalid page.(%d)\n",pageid);
        return(RET_NG);
    }

    // ビットマップ参照
    dc032_bmp	= (root_category_pointer + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_DC_032));	// 完全な未使用領域 各サイズ毎 ：〃
    dc128_bmp	= (root_category_pointer + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_DC_128));
    dc512_bmp	= (root_category_pointer + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_DC_512));
    dc02k_bmp	= (root_category_pointer + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_DC_2K));
    // 当該ページのBITだけをすべて落とす
    PAGEBIT_OFF(dc032_bmp,pageid);   PAGEBIT_OFF(dc128_bmp,pageid);   PAGEBIT_OFF(dc512_bmp,pageid);
    PAGEBIT_OFF(dc02k_bmp,pageid);

    // 空き領域のみ再構築の場合
    // リサイクル用ビットマップは処理しない
    if (!only_dc){
        free000_bmp	= (root_category_pointer + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_FREE_000));	// FREE領域 各サイズ毎 ：〃
        free032_bmp	= (root_category_pointer + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_FREE_032));
        free128_bmp	= (root_category_pointer + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_FREE_128));
        free512_bmp	= (root_category_pointer + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_FREE_512));
        free02k_bmp	= (root_category_pointer + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_FREE_2K));
        //
        PAGEBIT_OFF(free000_bmp,pageid); PAGEBIT_OFF(free032_bmp,pageid); PAGEBIT_OFF(free128_bmp,pageid);
        PAGEBIT_OFF(free512_bmp,pageid); PAGEBIT_OFF(free02k_bmp,pageid);
    }

    // 空き領域のみ再構築の場合
    // リサイクル用ビットマップは処理しない
    if (!only_dc){
        // -------------------------------
        // FREEルートアイテム
        mhpt_free	= (memitem_head_ptr)(root_page_pointer + MMPOS_FREE_ROOT);
        mfpt_free	= (memitem_foot_ptr)((root_page_pointer + MMPOS_FREE_ROOT) + MMPOS_ROOT_FOOT);
        //
        recycle_count_summary = 0;
        recycle_size_summary = 0;
        is_valid = 0;
        for(check_counter = 0,offset = MMPOS_FREE_ROOT;offset < PAGE_SIZE;check_counter++){
            // validate
            if (mhpt_free->signature != MMSIG_FREE){ break; }					// シグネチャ
            if (mhpt_free->len < MMPOS_MIN_OFST){ break; }						// 割当サイズ
            if (mfpt_free->suffix != MMSIG_SUFX){ break; }						// シグネチャ（フッタ）

            // リサイクルサマリを管理領域に更新用に計算しておく
            if ((mhpt_free->len - MMPOS_MIN_OFST) > 0 && (offset != MMPOS_FREE_ROOT)){
                recycle_count_summary ++;
                recycle_size_summary += mhpt_free->len;

                // フリーサイズを各ビットマップに更新
                remain_size = (mhpt_free->len - MMPOS_MIN_OFST);
                if (remain_size > 0x0800){
                    PAGEBIT_ON(free000_bmp,pageid); PAGEBIT_ON(free032_bmp,pageid); PAGEBIT_ON(free128_bmp,pageid);
                    PAGEBIT_ON(free512_bmp,pageid); PAGEBIT_ON(free02k_bmp,pageid);
                }else if (remain_size > 0x0200){
                    PAGEBIT_ON(free000_bmp,pageid); PAGEBIT_ON(free032_bmp,pageid); PAGEBIT_ON(free128_bmp,pageid);
                    PAGEBIT_ON(free512_bmp,pageid);
                }else if (remain_size > 0x0080){
                    PAGEBIT_ON(free000_bmp,pageid); PAGEBIT_ON(free032_bmp,pageid); PAGEBIT_ON(free128_bmp,pageid);
                }else if (remain_size > 0x0020){
                    PAGEBIT_ON(free000_bmp,pageid); PAGEBIT_ON(free032_bmp,pageid);
                }else{
                    PAGEBIT_ON(free000_bmp,pageid);
                }
            }
            // link終了
            if (mhpt_free->next_offset == MMFLG_CLOSE){
                is_valid = 1;
                break;
            }
            // hung loop check.
            if (check_counter > UINT_MAX){
                printf("invalid double link list(_rebuild_bitmap_and_pagesummary.2).\n");
                break;
            }
            // 次のlinkへ
            offset = mhpt_free->next_offset;
            mhpt_free = (memitem_head_ptr)(root_page_pointer + offset);
            mfpt_free = (memitem_foot_ptr)((root_page_pointer + offset) + (mhpt_free->len - sizeof(memitem_foot_t)));
        }
        // 不正なデータ状態
        if (!is_valid){
            return(RET_NG);
        }
    }
    // -------------------------------
    // USEDルートアイテム
    mhpt		= (memitem_head_ptr)(root_page_pointer + MMPOS_USED_ROOT);
    mfpt		= (memitem_foot_ptr)((root_page_pointer + MMPOS_USED_ROOT) + MMPOS_ROOT_FOOT);
    used_size_summary = 0;
    used_count_summary = 0;
    is_valid	= 0;
    //
    for(check_counter = 0,offset = MMPOS_USED_ROOT;offset < PAGE_SIZE;check_counter++){
        // validate
        if (mhpt->signature != MMSIG_USED){ break; }					// シグネチャ
        if (mhpt->len < MMPOS_MIN_OFST){ break; }						// 割当サイズ
        if (mfpt->suffix != MMSIG_SUFX){ break; }						// シグネチャ（フッタ）

        // 利用状況サマリを管理領域に更新(rootアイテム除く)
        if ((mhpt->len - MMPOS_MIN_OFST) > 0 && (offset != MMPOS_USED_ROOT)){
            used_size_summary += mhpt->len;
            used_count_summary ++;
        }
        // link終了
        if (mhpt->next_offset == MMFLG_CLOSE){
            is_valid = 1;
            break;
        }
        // hung loop check.
        if (check_counter > UINT_MAX){
            printf("invalid double link list(_rebuild_bitmap_and_pagesummary.2).\n");
            break;
        }
        // 次のlinkへ
        offset = mhpt->next_offset;
        mhpt = (memitem_head_ptr)(root_page_pointer + offset);
        mfpt = (memitem_foot_ptr)((root_page_pointer + offset) + (mhpt->len - sizeof(memitem_foot_t)));
    }
    // 不正なデータ状態
    if (!is_valid){
        return(RET_NG);
    }
    // 空きサイズbitmap
    remain_size = (PAGE_SIZE - MMSIZ_ALL_ROOT - used_size_summary) - (used_count_summary * MMSIZ_HF_SIZE);
    if (remain_size > 0x0800){
        PAGEBIT_ON(dc032_bmp,pageid); PAGEBIT_ON(dc128_bmp,pageid); PAGEBIT_ON(dc512_bmp,pageid); PAGEBIT_ON(dc02k_bmp,pageid);
    }else if (remain_size > 0x0200){
        PAGEBIT_ON(dc032_bmp,pageid); PAGEBIT_ON(dc128_bmp,pageid); PAGEBIT_ON(dc512_bmp,pageid);
    }else if (remain_size > 0x0080){
        PAGEBIT_ON(dc032_bmp,pageid); PAGEBIT_ON(dc128_bmp,pageid);
    }else if (remain_size > 0x0020){
        PAGEBIT_ON(dc032_bmp,pageid);
    }
    // ---- 利用summary  ----
    mrdt = (memitem_root_detail_ptr)((root_page_pointer + MMPOS_USED_ROOT) + MMPOS_ROOT_DTL);
    // ルートオブジェクト分を含む
    mrdt->use_size	= used_size_summary;
    mrdt->use_cnt	= used_count_summary;
    // ---- リサイクルsummary ----
    if (!only_dc){
        mrdt = (memitem_root_detail_ptr)((root_page_pointer + MMPOS_FREE_ROOT) + MMPOS_ROOT_DTL);
        mrdt->free_size	= recycle_size_summary;
        mrdt->free_cnt	= recycle_count_summary;
    }
    // リサイクルも処理する場合、初期化時 or mearge時で
    // 利用領域が[0] の場合にshrinkして、free-backetを全体にセット
    if (!only_dc && used_size_summary == 0){
        _shrink_backet_recycle(pageid,root_page_pointer,root_category_pointer);
    }
    return(RET_OK);
}
/** *************************************************************
 * バケットの再構築とリサイクル領域の初期化\n
 *
 * @param[in]     pageid                 ページindex
 * @param[in]     mempage_root          ↑ページの先頭アドレス
 * @param[in]     pctgy                  当該ページが含まれるカテゴリの先頭アドレス
 * @result  成功時:RET_OK/エラー時：!=RET_OK
 ************************************************************* */
int _shrink_backet_recycle(uint32_t pageid,char* mempage_root,char* pctgy){
    memitem_head_ptr	mhpt = NULL;
    memitem_foot_ptr	mfpt = NULL;
    memitem_root_detail_ptr	mrpt = NULL;
    char*				page_bmp = NULL;

    // カテゴリ管理領域 を「空き状態」に
    // 純粋空き領域をALL-ON
    page_bmp = (pctgy + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_DC_032));	PAGEBIT_ON(page_bmp,pageid);
    page_bmp = (pctgy + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_DC_128));	PAGEBIT_ON(page_bmp,pageid);
    page_bmp = (pctgy + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_DC_512));	PAGEBIT_ON(page_bmp,pageid);
    page_bmp = (pctgy + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_DC_2K));	PAGEBIT_ON(page_bmp,pageid);
    // リサイクル領域をALL-OFF
    page_bmp = (pctgy + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_FREE_000));PAGEBIT_OFF(page_bmp,pageid);
    page_bmp = (pctgy + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_FREE_032));PAGEBIT_OFF(page_bmp,pageid);
    page_bmp = (pctgy + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_FREE_128));PAGEBIT_OFF(page_bmp,pageid);
    page_bmp = (pctgy + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_FREE_512));PAGEBIT_OFF(page_bmp,pageid);
    page_bmp = (pctgy + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_FREE_2K));	PAGEBIT_OFF(page_bmp,pageid);

    // ヘッダ（空き領域root）：初期値
    mhpt				= (memitem_head_ptr)(mempage_root + MMPOS_FREE_ROOT);
    mhpt->signature		= MMSIG_FREE;
    mhpt->prev_offset	= 0;
    mhpt->next_offset	= MMFLG_CLOSE;
    mhpt->len			= MMPOS_ROOT_SIZE;

    // 詳細（空き領域root）：初期値
    mrpt = (memitem_root_detail_ptr)((char*)mhpt + MMPOS_ROOT_DTL);
    mrpt->first_offset	= MMPOS_FREE_ROOT;	// 初期ファイルは、自分自身
    mrpt->last_offset	= MMPOS_FREE_ROOT;
    mrpt->use_size		= 0;
    mrpt->use_cnt		= 0;
    mrpt->free_size		= 0;
    mrpt->free_cnt		= 0;

    // フッタ設定（空き領域root）：初期値
    mfpt = (memitem_foot_ptr)((char*)mhpt + MMPOS_ROOT_FOOT);
    mfpt->created		= time(0);
    mfpt->suffix		= MMSIG_SUFX;

    // ヘッダ（バケットroot）：初期値
    mhpt				= (memitem_head_ptr)(mempage_root + MMPOS_BCKT_ROOT);
    mhpt->signature		= MMSIG_BCKT;
    mhpt->prev_offset	= 0;
    mhpt->next_offset	= MMPOS_BCKT_INIT;
    mhpt->len			= MMPOS_ROOT_SIZE;

    // 詳細（バケットroot）：初期値
    mrpt = (memitem_root_detail_ptr)((char*)mhpt + MMPOS_ROOT_DTL);
    mrpt->first_offset	= MMPOS_BCKT_ROOT;	// 初期ファイルは、自分自身
    mrpt->last_offset	= MMPOS_BCKT_INIT;	// ※バケットは初期状態で全体である
    mrpt->use_size		= 0;
    mrpt->use_cnt		= 0;
    mrpt->free_size		= 0;
    mrpt->free_cnt		= 0;

    // フッタ設定（空き領域root）：初期値
    mfpt = (memitem_foot_ptr)((char*)mhpt + MMPOS_ROOT_FOOT);
    mfpt->created		= time(0);
    mfpt->suffix		= MMSIG_SUFX;

    // 初期状態のバケット をセット：初期値
    mhpt				= (memitem_head_ptr)(mempage_root + MMPOS_BCKT_INIT);
    mhpt->signature		= MMSIG_BCKT;
    mhpt->prev_offset	= MMPOS_BCKT_ROOT;
    mhpt->next_offset	= MMFLG_CLOSE;
    mhpt->len			= MMSIZ_BCKT_INIT_SIZE;

    // フッタ設定（フリーバケット root）：初期値
    mfpt = (memitem_foot_ptr)((char*)mhpt + (MMSIZ_BCKT_INIT_SIZE - sizeof(memitem_foot_t)));
    mfpt->created		= time(0);
    mfpt->suffix		= MMSIG_SUFX;
    //
    return(RET_OK);
}

/** *************************************************************
 *  カテゴリ：メモリ解放\n
 *  （カテゴリから指定アドレスの領域を解放する）
 *
 * @param[in]     category      カテゴリアクセサ
 * @param[in]     pointer       解放対象アドレス（ユーザ領域）
 * @result  void
 ************************************************************* */
void shmfree(category_on_process_ptr category,void* pointer){
    int					n;
    uint32_t			category_id = 0,offset;
    char*				pctgy = NULL;
    char*				errpage_bmp = NULL;
    char*				mempage_root = NULL;
    char*				trgtaddress = (char*)pointer;
    ULONGLONG			check_counter = 0;
    memitem_head_ptr	mhpt_free = NULL,mhpt = NULL,mhpt_prev = NULL,mhpt_next = NULL;
    memitem_head_ptr	mhpt_recycle_last = NULL;
    memitem_root_detail_ptr	mrpt = NULL,mrpt_recycle = NULL;
    memitem_foot_ptr	mfpt_free = NULL,mfpt = NULL;

    // 引数チェック
    if (!category || !pointer){
        return;
    }
    // ステータスチェック
    if (!category){
        return;
    }
    if (!category->data_pointer || (category->data_pointer == (char*)INVALID_SOCKET)){
        return;
    }
    pctgy		= category->data_pointer;
    category_id	= category->id;
    // エラーページ：ビットマップ
    errpage_bmp	= (pctgy + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_ERR));

    // ページのアドレス範囲に存在すること
    if ((pctgy + MMPOS_BCKT_INIT + sizeof(memitem_head_t)) > trgtaddress){
        return;
    }
    // フリー対象アイテム
    mhpt_free	= (memitem_head_ptr)(trgtaddress - sizeof(memitem_head_t));
    mfpt_free	= (memitem_foot_ptr)(((char*)mhpt_free + mhpt_free->len) - sizeof(memitem_foot_t));
    // 基本validate
    if (mhpt_free->signature != MMSIG_USED){ return; }
    if (mhpt_free->len == 0){ return; }
    if (mhpt_free->len <= MMPOS_MIN_OFST){ return; }
    if (mfpt_free->suffix != MMSIG_SUFX){ return; }
    if (mfpt_free->category_id != category_id){ return; }

    // 基本チェックOKなので、全ページ使用中リストを検索
    for(n = IPCSHM_PAGE_USRAREA;n < IPCSHM_PAGE_USRAREA_MAX;n++){
        mempage_root = (pctgy + MMPOS_USRPAGE(n));
        // エラーページは処理しない
        if (ISPAGEBIT(errpage_bmp,n)){ continue; }

        // ルートアイテム
        mhpt		= (memitem_head_ptr)(mempage_root + MMPOS_USED_ROOT);
        mfpt		= (memitem_foot_ptr)((mempage_root + MMPOS_USED_ROOT) + MMPOS_ROOT_FOOT);
        mrpt		= (memitem_root_detail_ptr)((mempage_root + MMPOS_USED_ROOT) + MMPOS_ROOT_DTL);
        mrpt_recycle= (memitem_root_detail_ptr)((mempage_root + MMPOS_FREE_ROOT) + MMPOS_ROOT_DTL);
        //
        for(check_counter = 0,offset = MMPOS_USED_ROOT;offset < PAGE_SIZE;check_counter++){
            // validate
            if (mhpt->signature != MMSIG_USED){ break; }					// シグネチャ
            if (mhpt->len <= MMPOS_MIN_OFST){ break; }						// 割当サイズ
            if (mfpt->suffix != MMSIG_SUFX){ break; }						// シグネチャ（フッタ）

            // アイテムアドレス一致 -> 解放リストへ
            if (mhpt_free == mhpt){
                // 解放アドレスをロギング
//				printf("\tfree[%p] size[%d]\n",mhpt_free,mhpt->len);
                // 最後のリンクの場合
                if (mhpt->next_offset == MMFLG_CLOSE){
                    // PREVアイテムと、NEXTアイテムからチェーン
                    mhpt_prev	= (memitem_head_ptr)(mempage_root + mhpt->prev_offset);
                    /*
                     * USEDアイテムチェーンから切り離す
                     * -----------------------------
                     * |x|    | |       |y|      | |
                     * | |prev| |  <->  | | trgt | |
                     * |w|    |y|       |x|      |-|
                     *
                     * -----------------------------
                     * |x|    | |
                     * | |prev| |
                     * |w|    |-|
                     *
                     */
                    mhpt_prev->next_offset	= MMFLG_CLOSE;
                    mrpt->last_offset		= mhpt->prev_offset;

                    // --------------------------------------
                    // 切り離した USEDアイテムを「リサイクル」チェーンの最後に接続

                    // シグネチャを「リサイクル」に書き換えて
                    mhpt->signature	= MMSIG_FREE;
                    // 更新前の最終アイテム
                    mhpt_recycle_last	= (memitem_head_ptr)(mempage_root + mrpt_recycle->last_offset);
                    /*
                     * root.last_offset = x;
                     * -----------------------------
                     * |x|    | |
                     * | |last| |
                     * |w|    |-|
                     *
                     * root.last_offset = y;
                     * -----------------------------
                     * |x|    | |       |y|      | |
                     * | |last| |  <->  | |change| |
                     * |w|    |y|       |x|      |-|
                     *
                     */
                    // USED -> リサイクル アイテムへ最終アイテム設定
                    mhpt->next_offset				= MMFLG_CLOSE;
                    mhpt->prev_offset				= mrpt_recycle->last_offset;

                    mhpt_recycle_last->next_offset	= offset;
                    mrpt_recycle->last_offset		= offset;
                }else{
                    // PREVアイテムと、NEXTアイテムからチェーン
                    mhpt_prev	= (memitem_head_ptr)(mempage_root + mhpt->prev_offset);
                    mhpt_next	= (memitem_head_ptr)(mempage_root + mhpt->next_offset);
                    /*
                     * USEDアイテムチェーンから切り離す
                     * -----------------------------
                     * |x|    | |       |y|      | |       |z|      | |
                     * | |prev| |  <->  | | trgt | |  <->  | | next | |
                     * |w|    |y|       |x|      |z|       |y|      |-|
                     *
                     * -----------------------------
                     * |x|    | |                          |z|      | |
                     * | |prev| |  <-------------------->  | | next | |
                     * |w|    |z|                          |x|      |-|
                     *
                     */
                    mhpt_prev->next_offset	= mhpt->next_offset;
                    mhpt_next->prev_offset	= mhpt->prev_offset;
                    // --------------------------------------
                    // 切り離した USEDアイテムを「リサイクル」チェーンの最後に接続

                    // シグネチャを「リサイクル」に書き換えて
                    mhpt->signature	= MMSIG_FREE;
                    // 更新前の最終アイテム
                    mhpt_recycle_last	= (memitem_head_ptr)(mempage_root + mrpt_recycle->last_offset);
                    /*
                     * root.last_offset = x;
                     * -----------------------------
                     * |x|    | |
                     * | |last| |
                     * |w|    |-|
                     *
                     * root.last_offset = y;
                     * -----------------------------
                     * |x|    | |       |y|      | |
                     * | |last| |  <->  | |change| |
                     * |w|    |y|       |x|      |-|
                     *
                     */
                    // USED -> リサイクル アイテムへ最終アイテム設定
                    mhpt->next_offset				= MMFLG_CLOSE;
                    mhpt->prev_offset				= mrpt_recycle->last_offset;

                    mhpt_recycle_last->next_offset	= offset;
                    mrpt_recycle->last_offset		= offset;
                }
                return;
            }
            // link終了
            if (mhpt->next_offset == MMFLG_CLOSE){
                break;
            }
            // hung loop check.
            if (check_counter > UINT_MAX){
                printf("invalid double link list(shmfree).\n");
                break;
            }
            // 次のlinkへ
            offset = mhpt->next_offset;
            mhpt = (memitem_head_ptr)(mempage_root + offset);
            mfpt = (memitem_foot_ptr)((mempage_root + offset) + (mhpt->len - sizeof(memitem_foot_t)));
        }
    }
}


/** *************************************************************
 *  メモリサマリ情報再構築\n
 *
 * @param[in]     category      カテゴリアクセサ
 * @param[in]     size          割当サイズ（ユーザ領域）
 * @param[in]     itemid        シグネチャ：このシグネチャと一致する\n
 *                              アイテムが検索される
 * @result  成功時:not null：アタッチできたアドレス/エラー時：NULL
 ************************************************************* */
void rebuild_page_statics(category_on_process_ptr category){
    int					m;
    char*				pctgy = NULL;
    char*				errpage_bmp = NULL;
    char*				mempage_root = NULL;
    mempage_system_ptr	mmgr = NULL;
    //memitem_head_ptr	mhpt = NULL;
    memitem_root_detail_ptr	mrpti = NULL;

    // 引数チェック
    if (!category){
        return;
    }
    // ステータスチェック
    if (!category->data_pointer || (category->data_pointer == (char*)INVALID_SOCKET)){
        return;
    }
    pctgy		= category->data_pointer;
    // 管理領域アドレス参照
    mmgr		= (mempage_system_ptr)(pctgy + MMPOS_SYSPAGE_SYSTEM);	// システム管理領域
    errpage_bmp	= (pctgy + MMPOS_SYSPAGE_BMP(IPCSHM_SYSPAGE_ERR));		// エラーページ：ビットマップ
    // システム管理領域 マージ部初期化
    mmgr->free_size	= 0;
    mmgr->free_cnt	= 0;
    mmgr->use_size	= 0;
    mmgr->use_cnt	= 0;

    // カテゴリ内：全ページを走査し再計算
    for(m = IPCSHM_PAGE_USRAREA;m < IPCSHM_PAGE_USRAREA_MAX;m++){
        // ページのroot オブジェクト
        mempage_root	= (pctgy + MMPOS_USRPAGE(m));
        //mhpt			= (memitem_head_ptr)(mempage_root + MMPOS_USED_ROOT);
        // ビットマップ、サマリの 再構築
        if (_rebuild_bitmap_and_pagesummary(m,mempage_root,(char*)pctgy,0) != RET_OK){
            PAGEBIT_ON(errpage_bmp,m);
            continue;
        }
        PAGEBIT_OFF(errpage_bmp,m);

        // ページ管理 利用領域
        mrpti = (memitem_root_detail_ptr)((mempage_root + MMPOS_USED_ROOT) + MMPOS_ROOT_DTL);
        mmgr->use_size	+= (mrpti->use_size);
        mmgr->use_cnt	+= (mrpti->use_cnt);

        // ページ管理 空き領域
        mrpti = (memitem_root_detail_ptr)((mempage_root + MMPOS_FREE_ROOT) + MMPOS_ROOT_DTL);
        mmgr->free_size	+= (mrpti->free_size);
        mmgr->free_cnt	+= (mrpti->free_cnt);
    }
    // カテゴリ毎にfileに同期
    msync(category->data_pointer,category->filesize,0);
}

