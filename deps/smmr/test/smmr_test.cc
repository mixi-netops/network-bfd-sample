#include "gtest/gtest.h"
#include "smmr_pagedbuffer.h"
#include "smmr_categories.h"
#include "smmr_memory.h"
#include "smmr_database.h"
#include "smmr_tree.h"
#include <libgen.h>
#include <signal.h>

#define UNUSED(x) (void)(x)


class TestEnv : public ::testing::Environment {
    protected:
    virtual void SetUp() {
        printf("tear up.(%d)\n", getpid());
    };
    virtual void TearDown() {
        printf("tear down.(%d)\n", getpid());
    };
};

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::Environment* test_env = ::testing::AddGlobalTestEnvironment(new TestEnv);
    UNUSED(test_env);
    return RUN_ALL_TESTS();
}
// test of paged buffer.
TEST(SmmrTest, PagedBuffer){
    paged_buffer_ptr    handle = NULL;
    EXPECT_EQ(pagedbuffer_create(&handle), RET_OK);
    pagedbuffer_append_text(handle, "%08d",1);
    pagedbuffer_append_text(handle, "%08d",2);
    pagedbuffer_append_text(handle, "%08d",3);
    pagedbuffer_append_text(handle, "%08d",4);
    EXPECT_EQ(pagedbuffer_current_size(handle), 32);
    EXPECT_NE(pagedbuffer_ptr(handle),(char*)NULL);
    printf("%s\n",pagedbuffer_ptr(handle));
    EXPECT_EQ(pagedbuffer_remove(&handle), RET_OK);
}

// +++++++++++++++++++++++++++++++++++++++++++++++++
static char*                    DATABASE_DIR = NULL;
// shared ..
static ipcshm_categories_ptr    CATEGORIES = (ipcshm_categories_ptr)INVALID_HANDLE;
static ipcshm_datafiles_ptr		DATAFILES = (ipcshm_datafiles_ptr)INVALID_HANDLE;

// main process.
static datafiles_on_process_t	MAINPROCESS_DATAFILES_INDEX;
static categories_on_process_t	MAINPROCESS_CATEGORIES_INDEX;


TEST(SmmrTest, Startup){
    char bf[128] = {0};
    snprintf(bf,sizeof(bf)-1,"/tmp/%u/", (unsigned)getpid()); mkdir(bf,0755);
    set_prefix("00");

    DATABASE_DIR = strdup(bf);
    // メインプロセス開始時に、カテゴリ領域の初期化
    if (create_categories_area(bf,&CATEGORIES,&MAINPROCESS_CATEGORIES_INDEX) != RET_OK){ FAIL() << "failed.create_categories_area"; }
    // メインプロセスで、カテゴリアクセスの初期値を準備
    if (init_categories_from_file(CATEGORIES,&MAINPROCESS_CATEGORIES_INDEX,0) != RET_OK){ FAIL() << "failed.init_categories_from_file"; }
    // メインプロセスで、全カテゴリのメモリページのrebuild + validation
    if (rebuild_pages_area(&MAINPROCESS_CATEGORIES_INDEX) != RET_OK){ FAIL() << "failed.rebuild_pages_area"; }
    //メインプロセスでデータファイル初期化
    snprintf(bf,sizeof(bf)-1,"/tmp/%u/db/", (unsigned)getpid()); mkdir(bf,0755);
    if (create_db_area_parent(bf,&DATAFILES) != RET_OK){ FAIL() << "failed.create_db_area_parent"; }
    if (init_datafiles_from_file(DATAFILES,&MAINPROCESS_DATAFILES_INDEX) != RET_OK){ FAIL() << "failed.init_datafiles_from_file"; }
}

static void ___child_init(tree_instance_ptr* pproot, datafiles_on_process_ptr pdf, categories_on_process_ptr pcg){
    // 子プロセスで、カテゴリアクセスを初期化（管理ページのみ全体shmとしている）
    if (init_categories_from_file(CATEGORIES,pcg,1) != RET_OK){
        fprintf(stderr, "___child_init::init_categories_from_file.");
        exit(1);
    }

    // カテゴリ全体Mutexで同期
    {	mutex_lock_t lock = {&(CATEGORIES->category_all_mutex),RET_NG};
        start_lock(&lock);
        if (isvalid_lock(&lock) != RET_OK){
            end_lock(&lock);
            fprintf(stderr, "___child_init::isvalid_lock.");
            exit(1);
        }
        // 全カテゴリのメモリページのrebuild + validation
        if (rebuild_pages_area(pcg) != RET_OK){
            end_lock(&lock);
            fprintf(stderr, "___child_init::rebuild_pages_area.");
            exit(1);
        }
        end_lock(&lock);
    }
    // インデックス ルートオブジェクト構築
    (*pproot) = create_tree_unsafe(CATEGORIES,pcg);
    if (!(*pproot)){
        fprintf(stderr, "___child_init::create_tree_unsafe.");
        exit(1);
    }
    // 全データファイルを準備する
    if (init_datafiles_from_file(DATAFILES,pdf) != RET_OK){
        fprintf(stderr, "___child_init::init_datafiles_from_file.");
        exit(1);
    }
}
static void ___child_uninit(tree_instance_ptr* pproot, datafiles_on_process_ptr pdf, categories_on_process_ptr pcg){
    fprintf(stdout, "___child_uninit\n.");

    // 子プロセスでリソース解放
    if (childuninit_db_area(&DATAFILES,pdf) != RET_OK){
        fprintf(stderr, "___child_uninit::init_datafchilduninit_db_areailes_from_file.");
        exit(1);
    }
    if ((*pproot) != NULL){
        free((*pproot));
    }
    if (childuninit_categories_area(&CATEGORIES,pcg) != RET_OK){
        fprintf(stderr, "___child_uninit::childuninit_categories_area.");
        exit(1);
    }
}

static void ___child_main(void){
    tree_instance_ptr		tree_root = NULL;
    datafiles_on_process_t	datafiles_index;
    categories_on_process_t	categories_index;
    //
    ___child_init(&tree_root, &datafiles_index, &categories_index);

    int n;
    for(n = 0;n < CATEGORY_CNT; n++){
        rebuild_page_statics(&(categories_index.category[n]));
    }
    //
    ___child_uninit(&tree_root, &datafiles_index, &categories_index);

    exit(0);
}
TEST(SmmrTest, ChildProcessStartStop){
    {   int cstat = 0;
        pid_t pid = fork();
        //
        if (pid < 0){ ASSERT_FALSE(1);
        }else if (pid == 0){
            ___child_main();

            exit(0);
        }else{
            // switch context.
            usleep(1000);
            // parent
            pid_t pidr = waitpid(pid, &cstat, 0);
            EXPECT_EQ(pidr, pid);
            EXPECT_EQ(cstat, 0);
        }
    }
}

static void ___child_main_insert_find(void){
    tree_instance_ptr		tree_root = NULL;
    datafiles_on_process_t	datafiles_index;
    categories_on_process_t	categories_index;
    //
    ___child_init(&tree_root, &datafiles_index, &categories_index);

    const char  *key00 = "notfound";
    const char  *key01 = "isexistskey";
    char        *key01val = strdup("so--noisy....please quit.");
    uint32_t    key01vallen = strlen(key01val);
    char*       resp  = NULL;
    uint32_t    resplen = 0;
    // 見つからないこと
    EXPECT_NE(search(DATAFILES,&datafiles_index,tree_root,key00,strlen(key00),&resp,&resplen),RET_OK);
    if (resp!=NULL) { free(resp); }
    resp = NULL;
    // 一つアイテムを追加する
    EXPECT_EQ(store(DATAFILES,&datafiles_index, tree_root, key01,strlen(key01),key01val,key01vallen,0),RET_OK);
    // 見つかること
    EXPECT_EQ(search(DATAFILES,&datafiles_index,tree_root,key01,strlen(key01),&resp,&resplen),RET_OK);
    EXPECT_NE((void*)resp, (void*)NULL);
    if (resp!=NULL) {
        EXPECT_EQ(memcmp(resp,key01val,MIN(resplen, strlen(key01val))),0);
        free(resp);
        resp=NULL;
    }else{
        fprintf(stderr, "search failed\n");
        exit(1);
    }
    if (key01val != NULL){
        free(key01val);
    }
    key01val = NULL;

    print(tree_root,&resp,&resplen);
    //
    FILE *fp = fopen("/tmp/hoge.html","wb");
    fwrite(resp, resplen,1,fp);
    fclose(fp);

    // system("open /tmp/hoge.html");
    //
    ___child_uninit(&tree_root, &datafiles_index, &categories_index);
    //
    exit(0);
}

TEST(SmmrTest, ChildProcessInsertFind){
    int cstat = 0;
    pid_t pid = fork();
    //
    if (pid < 0){ ASSERT_FALSE(1);
    }else if (pid == 0){ ___child_main_insert_find(); exit(0);
    }else{
        // switch context.
        usleep(10);
        // parent
        pid_t pidr = waitpid(pid, &cstat, 0);
        EXPECT_EQ(pidr, pid);
        EXPECT_EQ(cstat, 0);
    }
}


TEST(SmmrTest, Cleanup){
    // メインプロセスは、各ファイルの初期処理の実行後、全ハンドルをCLOSE
    if (remove_db_area_parent(&DATAFILES,&MAINPROCESS_DATAFILES_INDEX) != RET_OK){ FAIL() << "failed.remove_db_area_parent"; }
    if (remove_categories_area(&CATEGORIES,&MAINPROCESS_CATEGORIES_INDEX) != RET_OK){ FAIL() << "failed.remove_categories_area"; }
    if (DATABASE_DIR != NULL){ free(DATABASE_DIR); }
    DATABASE_DIR = NULL;

    char bf[128] = {0};
    snprintf(bf,sizeof(bf)-1,"/tmp/%u/db", (unsigned)getpid());
    remove_safe(bf);
    snprintf(bf,sizeof(bf)-1,"/tmp/%u", (unsigned)getpid());
    remove_safe(bf);
}

