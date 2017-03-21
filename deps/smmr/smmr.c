/******************************************************************************/
/*! @addtogroup in process file mmap kvs db(shared memory map - reallocation)
 @file       smmr.c
 @brief      インスタンス/マルチプロセス：インメモリデータベース
 ******************************************************************************/
#include "smmr.h"


pthread_mutex_t	global_str_safe_muex;

static char global_prefix[128] = {0};

void set_prefix(const char* prefix){
    if (prefix){
        memcpy(global_prefix, prefix, MIN(strlen(prefix), sizeof(global_prefix)-1));
    }else{
        memset(global_prefix, 0, sizeof(global_prefix));
    }
}
const char* get_prefix(void){
    return(global_prefix);
}

const char* get_category_shmnm(void){
    static char shm[128] = {0};
    if (global_prefix[0]){
        snprintf(shm, sizeof(shm)-1,"%s-%s", IPC_CATEGORY_SHMNM, get_prefix());
    }else{
        snprintf(shm, sizeof(shm)-1,"%s", IPC_CATEGORY_SHMNM);
    }
    return(shm);
}
const char* get_datafile_shmnm(void){
    static char shm[128] = {0};
    if (global_prefix[0]){
        snprintf(shm, sizeof(shm)-1,"%s-%s", IPC_DATAFILE_SHMNM, get_prefix());
    }else{
        snprintf(shm, sizeof(shm)-1,"%s", IPC_DATAFILE_SHMNM);
    }
    return(shm);
}

