#include "smmr_pagedbuffer.h"
#include "smmr_categories.h"
#include "smmr_memory.h"
#include "smmr_database.h"
#include "smmr_tree.h"
#include <sys/types.h>
#include <sys/wait.h>


#define UNUSED(x) (void)(x)


const char *SERVER_MSGKEY = "from_server_to_client";
char SERVER_HELLOW[] = "hellow client.";
const char *CLIENT_MSGKEY = "from_client_to_server";
char CLIENT_HELLOW[] = "hi server.";
#define CLIENT_TO   (10)

// -----------------------
// クライアント
// -----------------------
static void do_client(void){
    //
    ipcshm_categories_ptr   CATEGORIES_C = (ipcshm_categories_ptr)-1;
    ipcshm_datafiles_ptr	DATAFILES_C = (ipcshm_datafiles_ptr)-1;
    datafiles_on_process_t	DATAFILES_INDEX_C;
    categories_on_process_t	CATEGORIES_INDEX_C;
    tree_instance_ptr       ROOT_C;

    printf("client\n");
    //
    if (childinit_categories_area(&CATEGORIES_C,&CATEGORIES_INDEX_C) != RET_OK){ exit(1); }
    if (childinit_db_area(&DATAFILES_C,&DATAFILES_INDEX_C) != RET_OK){ exit(3); }
    // インデックス ルートオブジェクト構築
    ROOT_C = create_tree_unsafe(CATEGORIES_C,&CATEGORIES_INDEX_C);
    if (!ROOT_C){ exit(2); }
    // サーバメッセージを読み取る
    char *resp = NULL;
    uint32_t resplen = 0;
    //
    if (search(DATAFILES_C,&DATAFILES_INDEX_C,ROOT_C,SERVER_MSGKEY,strlen(SERVER_MSGKEY),&resp,&resplen) == RET_OK){
        if (resp && resplen){
            printf("from server .. %s == %s\n", SERVER_HELLOW, resp);
        }else{
            fprintf(stderr, "failed.search.:resp(%s)\n", SERVER_MSGKEY);
        }
    }else{
        fprintf(stderr, "failed.search.(%s)\n", SERVER_MSGKEY);
    }
    // ack to server
    if (store(DATAFILES_C,&DATAFILES_INDEX_C,ROOT_C,CLIENT_MSGKEY,strlen(CLIENT_MSGKEY),CLIENT_HELLOW,strlen(CLIENT_HELLOW),0)!= RET_OK){
        fprintf(stderr, "failed.store.(%s)\n", CLIENT_MSGKEY);
    }
    // 子プロセスでリソース解放
    if (childuninit_db_area(&DATAFILES_C,&DATAFILES_INDEX_C) != RET_OK){ exit(4); }
    if (ROOT_C != NULL){
        free(ROOT_C);
    }
    ROOT_C = NULL;
    if (childuninit_categories_area(&CATEGORIES_C,&CATEGORIES_INDEX_C) != RET_OK){ exit(5); }
    printf("client-completed.\n");
}

// -----------------------
// サーバ
// -----------------------
static void do_server(pid_t pid){
    printf("server\n");
    //
    ipcshm_categories_ptr   CATEGORIES = (ipcshm_categories_ptr)-1;
    ipcshm_datafiles_ptr	DATAFILES = (ipcshm_datafiles_ptr)-1;
    datafiles_on_process_t	DATAFILES_INDEX;
    categories_on_process_t	CATEGORIES_INDEX;
    tree_instance_ptr       ROOT;
    int cstat = 0;

    int timeout = 0;
    char index_dir[128] = {0},data_dir[128] = {0};
    snprintf(index_dir,sizeof(index_dir)-1,"/tmp/%u/", (unsigned)getpid());
    mkdir(index_dir,0755);
    snprintf(data_dir,sizeof(data_dir)-1,"/tmp/%u/db/", (unsigned)getpid());
    mkdir(data_dir,0755);
    // システム初期化
    // サーバプロセスでインデクス初期化
    if (create_categories_area(index_dir,&CATEGORIES,&CATEGORIES_INDEX) != RET_OK){ exit(1); }
    if (init_categories_from_file(CATEGORIES,&CATEGORIES_INDEX,0) != RET_OK){ exit(1); }
    if (rebuild_pages_area(&CATEGORIES_INDEX) != RET_OK){ exit(1); }
    // サーバプロセスでデータファイル初期化
    if (create_db_area_parent(data_dir,&DATAFILES) != RET_OK){ exit(1); }
    if (init_datafiles_from_file(DATAFILES,&DATAFILES_INDEX) != RET_OK){ exit(1); }
    // ツリールート
    if ((ROOT = create_tree_unsafe(CATEGORIES,&CATEGORIES_INDEX)) ==NULL){ exit(1); }

    // サーバでデータを書き込み
    if (store(DATAFILES,&DATAFILES_INDEX,ROOT,SERVER_MSGKEY,strlen(SERVER_MSGKEY),SERVER_HELLOW,strlen(SERVER_HELLOW),0)!= RET_OK){ exit(1); }
    // クライアントのデータ書き込みを待機
    for(timeout=0;timeout < CLIENT_TO;timeout++){
        char *resp = NULL;
        uint32_t resplen = 0;
        sleep(1);
        if (search(DATAFILES,&DATAFILES_INDEX,ROOT,CLIENT_MSGKEY,strlen(CLIENT_MSGKEY),&resp,&resplen) == RET_OK){
            if (resp && resplen){
                printf("%s -> %s\n",CLIENT_MSGKEY, resp);
                free(resp);
            }
            break;
        }
        fprintf(stderr, "nothing client. responce...(%d)\n", timeout);
    }
    if (timeout >= CLIENT_TO){
        fprintf(stderr, "timeout client. responce...\n");
    }
    // parent
    pid_t pidr = waitpid(pid, &cstat, 0);
    printf("waitpid(for client complete) /%d -> %d : %d\n",pid ,pidr, cstat);

    // システム終了処理
    if (remove_db_area_parent(&DATAFILES,&DATAFILES_INDEX) != RET_OK){ exit(1); }
    if (remove_categories_area(&CATEGORIES,&CATEGORIES_INDEX) != RET_OK){ exit(1); }

    remove_safe(index_dir);
    remove_safe(data_dir);
}

int main(int argc, char** argv) {
    pid_t pid = fork();
    //
    if (pid < 0){ exit(1);
    }else if (pid == 0){
        // wait for prepared shared area from parent.
        sleep(3);
        do_client();
        exit(0);
    }else{
        do_server(pid);
    }
    return(0);
}