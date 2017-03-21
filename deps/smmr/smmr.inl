/******************************************************************************/
/*! @addtogroup in process file mmap kvs db(shared memory map - reallocation)
 @file       smmr.inl
 @brief      マルチプロセス：インメモリデータベース
 ******************************************************************************/
#ifndef PROJECT_SMMR_INL
#define PROJECT_SMMR_INL


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
// inline function's
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//


/**
 msecond \n
 *******************************************************************************
 *******************************************************************************
 @return        micro 秒を取得
 */
static inline uint64_t get_microsecond(void){
    struct timeval	tv;
    gettimeofday(&tv,NULL);
    return((((uint64_t)tv.tv_sec*1000000) + ((uint64_t)tv.tv_usec)));
}

/**
 sscanf：スレッドセーフ：インライン\n
 *******************************************************************************
 *******************************************************************************
 @param[in]     str      sscanfと同一
 @param[in]     fmt      sscanfと同一
 @return        sscanfと同一
 */
static inline int safe_sscanf(const char* str,const char* fmt,...){
    int ret = -1;
    pthread_mutex_lock(&global_str_safe_muex);
    {
        va_list		args;
        va_start(args,fmt);
        ret = vsscanf(str,fmt,args);
        va_end(args);
    }
    pthread_mutex_unlock(&global_str_safe_muex);
    return(ret);
}
/**
 snprintf：スレッドセーフ：インライン\n
 *******************************************************************************

 *******************************************************************************
 @param[in]     str      snprintfと同一
 @param[in]     size     snprintfと同一
 @param[in]     fmt      snprintfと同一
 @return        snprintfと同一
 */
static inline int safe_snprintf(char* str,size_t size,const char* fmt,...){
    int ret = -1;
    pthread_mutex_lock(&global_str_safe_muex);
    {
        va_list		args;
        va_start(args,fmt);
        ret = vsnprintf(str,size,fmt,args);
        va_end(args);
    }
    pthread_mutex_unlock(&global_str_safe_muex);
    return(ret);
}

/**
 簡易ハッシュ\n
 *******************************************************************************
 *******************************************************************************
 @param[in]     key      ハッシュソース
 @param[in]     len      ↑データ長
 @return        ハッシュ値
 */
static inline uint32_t safe_hash(const char* key,uint32_t len){
    uint32_t hash = 5381;
    const char* rp = (char*)(key + len);
    while(len--){
        hash = ((hash << 5) + hash) ^ *(uint8_t*)--rp;
    }
    return(hash);
}
/** *************************************************************
 * mutex  lock
 * @param[in]    lockobj  ロックオブジェクト
 ************************************************************* */
static inline void start_lock(mutex_lock_ptr lockobj){
    if (lockobj != NULL){
        lockobj->result = pthread_mutex_lock(lockobj->obj);
#ifdef EOWNERDEAD
        if (lockobj->result == EOWNERDEAD){
#ifdef pthread_mutex_consistent_np
			pthread_mutex_consistent_np(lockobj->obj);
			fprintf(stderr, ">>invalid lock state(%p:%d)\n",lockobj->obj,lockobj->result);
#endif
		}
#endif
    }
}
/** *************************************************************
 * mutex ロックステータス
 * @param[in]    lockobj  ロックオブジェクト
 * @result  RET_OK=ロックできている、RET_OK!=ロックできていない
 ************************************************************* */

static inline int isvalid_lock(mutex_lock_ptr lockobj){
    if (lockobj != NULL){
        return(lockobj->result==0?RET_OK:RET_NG);
    }
    return(RET_NG);
}
/** *************************************************************
 * mutex  unlock
 * @param[in]    lockobj  ロックオブジェクト
 ************************************************************* */
static inline void end_lock(mutex_lock_ptr lockobj){
    if (lockobj != NULL){
        pthread_mutex_unlock(lockobj->obj);
        if (lockobj->result != 0){
            fprintf(stderr, ">>invalid unlock state(%p:%d)\n",lockobj->obj,lockobj->result);
        }
    }
}
/** *************************************************************
 * ファイル存在チェック＋サイズ取得
 * @param[in]     path  パス
 * @param[in,out] size  ファイルのサイズ
 * @result  RET_OK=存在、RET_OK!=見つからない
 ************************************************************* */
static inline int is_exists(const char* path,ULONGLONG* size){
    struct stat		st;
    if (!path)	return(RET_NG);
    if (stat(path,&st) == INVALID_FILE){
        return(RET_NG);
    }
    if (size){
        (*size) = (ULONGLONG)st.st_size;
    }
    return(RET_OK);
}

/** *************************************************************
 * カテゴリ領域ファイル生成\n
 * 指定ファイルが存在しない場合に、ファイルを生成する\n
 * 指定ファイルが存在する場合、RET_OK が返却される\n
 *
 * @param[in]     path        ファイルパス
 * @param[in]     init_size   初期サイズ
 * @result  int 成功時：RET_OK/エラー時:!=RET_OK
 ************************************************************* */
static inline int _create_file(const char* path,uint64_t init_size){
    int			newfile;
    ULONGLONG	newfilesize;
    char	zero	= 0x00;

    // ファイルが存在する場合処理無
    if (is_exists(path,&newfilesize) == RET_OK){
        return(RET_OK);
    }
    //ファイルCreate-Open
#ifdef __APPLE__
    if ((newfile = open(path,O_CREAT|O_RDWR,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)) == INVALID_SOCKET){
#else
        if ((newfile = open(path,O_CREAT|O_RDWR|O_TRUNC,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)) == INVALID_SOCKET){
#endif
        fprintf(stderr, "failed.open(%d:%s:%s)\n", errno, strerror(errno),path);
        return(RET_NG);
    }
    //まずファイルをページサイズに拡張
    if (lseek(newfile,(init_size - 1),SEEK_SET) == INVALID_SOCKET){
        close(newfile);
        unlink(path);
        return(RET_NG);
    }
    if (write(newfile,&zero,sizeof(zero)) == INVALID_SOCKET){
        close(newfile);
        unlink(path);
        return(RET_NG);
    }
    close(newfile);
    //
    return(RET_OK);
}
/** *************************************************************
 * 指定ディレクトリ配下のファイルを削除\n
 *
 * @param[in]     dir         ディレクトリパス
 ************************************************************* */
static inline void remove_safe(const char* dir){
    DIR *d = opendir(dir);
    size_t path_len = strlen(dir);
    //
    if (d) {
        struct dirent *p;
        while ((p=readdir(d))) {
            size_t len;
            struct stat statbuf;
            char buf[128] = {0};
            if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) {
                continue;
            }
            len = path_len + strlen(p->d_name) + 2;
            snprintf(buf, len, "%s/%s", dir, p->d_name);
            if (!stat(buf, &statbuf)) {
                if (!S_ISDIR(statbuf.st_mode)) {
                    unlink(buf);
                }
            }
        }
        closedir(d);
    }
    rmdir(dir);
}

#endif //PROJECT_SMMR_INL
