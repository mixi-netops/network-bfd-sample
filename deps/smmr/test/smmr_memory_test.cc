#include "gtest/gtest.h"
#include "smmr_categories.h"
#include "smmr_database.h"
#include "smmr_memory.h"
#include "smmr_pagedbuffer.h"
#include "smmr_tree.h"
// shared ..
static ipcshm_categories_ptr    CATEGORIES = (ipcshm_categories_ptr)INVALID_HANDLE;
static ipcshm_datafiles_ptr		DATAFILES = (ipcshm_datafiles_ptr)INVALID_HANDLE;

// main process.
static datafiles_on_process_t	DATAFILES_INDEX;
static categories_on_process_t	CATEGORIES_INDEX;

static tree_instance_ptr ROOT;


static uint64_t STORE_AND_SEARCH_STIME = 0;
static uint64_t STORE_AND_SEARCH_ETIME = 0;
static uint64_t STORE_STIME = 0;
static uint64_t STORE_ETIME = 0;
static uint64_t SEARCH_STIME = 0;
static uint64_t SEARCH_ETIME = 0;


// memory
TEST(SmmrMemoryTest, Memory){

    char bf[128] = {0};
    snprintf(bf,sizeof(bf)-1,"/tmp/%u-2/", (unsigned)getpid()); mkdir(bf,0755);
    set_prefix("01");

    if (create_categories_area(bf,&CATEGORIES,&CATEGORIES_INDEX) != RET_OK){ FAIL() << "failed.create_categories_area"; }
    if (init_categories_from_file(CATEGORIES,&CATEGORIES_INDEX,0) != RET_OK){ FAIL() << "failed.init_categories_from_file"; }
    if (rebuild_pages_area(&CATEGORIES_INDEX) != RET_OK){ FAIL() << "failed.rebuild_pages_area"; }
    snprintf(bf,sizeof(bf)-1,"/tmp/%u-2/db/", (unsigned)getpid()); mkdir(bf,0755);
    if (create_db_area_parent(bf,&DATAFILES) != RET_OK){ FAIL() << "failed.create_db_area_parent"; }
    ROOT = create_tree_unsafe(CATEGORIES,&CATEGORIES_INDEX);
    if (ROOT == NULL) { FAIL() << "failed.create_tree_unsafe"; }
    if (init_datafiles_from_file(DATAFILES,&DATAFILES_INDEX) != RET_OK){ FAIL() << "init_datafiles_from_file"; }

    int n;
    for(n = 0;n < CATEGORY_CNT; n++){
        rebuild_page_statics(&(CATEGORIES_INDEX.category[n]));
    }
}
TEST(SmmrMemoryTest, UsedCount_00){
    int n;
    for(n = 0;n < CATEGORY_CNT; n++){
        mempage_summary_ptr summary = (mempage_summary_ptr)malloc(sizeof(mempage_summary_t));
        assert(summary);

        // カテゴリ別ページ状況をフラッシュ
        rebuild_page_statics(&(CATEGORIES_INDEX.category[n]));
        //
        EXPECT_EQ(memory_status_unsafe(&CATEGORIES_INDEX.category[n], summary), RET_OK);

        EXPECT_EQ(summary->sys.idx, n);
        EXPECT_EQ(summary->sys.page_size, 131072);
        EXPECT_EQ(summary->sys.free_size, 0);
        EXPECT_EQ(summary->sys.free_cnt, 0);
        EXPECT_EQ(summary->sys.use_size, sizeof(ipcshm_tree_instance_itm_t) + sizeof(memitem_head_t) + sizeof(memitem_foot_t));
        EXPECT_EQ(summary->sys.use_cnt, 1);
        //
        free(summary);
    }
}
#define ROOTITEMSIZE    sizeof(ipcshm_tree_instance_itm_t) + sizeof(memitem_head_t) + sizeof(memitem_foot_t)
#define NODEITEMSIZE    sizeof(node_instance_t) + sizeof(memitem_head_t) + sizeof(memitem_foot_t)


static void STORE_AND_SEARCH(const char* key,const char* val){
    char*       resp  = NULL;
    char*       valdup = strdup(val);
    uint32_t    resplen = 0;

    // 一つアイテムを追加する
    EXPECT_EQ(store(DATAFILES,&DATAFILES_INDEX, ROOT, key,strlen(key),valdup,strlen(valdup),0),RET_OK);
    // 見つかること
    EXPECT_EQ(search(DATAFILES,&DATAFILES_INDEX,ROOT,key,strlen(key),&resp,&resplen),RET_OK);
    if (resp != NULL){
        std::string res(resp, resplen);
        EXPECT_EQ(memcmp(val, resp, resplen),0);

        free(resp);
    }
    free(valdup);
}

TEST(SmmrMemoryTest, UsedCount_01){
    STORE_AND_SEARCH("hogege01", "so--noisy....please quit.");
    //
    int n,l,m;
    for(n = 0;n < CATEGORY_CNT; n++){
        // カテゴリ別ページ状況をフラッシュ
        rebuild_page_statics(&(CATEGORIES_INDEX.category[n]));
        //
        mempage_summary_ptr summary = (mempage_summary_ptr)malloc(sizeof(mempage_summary_t));
        assert(summary);
        EXPECT_EQ(memory_status_unsafe(&CATEGORIES_INDEX.category[n], summary), RET_OK);
        printf("------\n");
        printf("idx       [%d]\n", summary->sys.idx);
        printf("page_size [%d]\n", summary->sys.page_size);
        printf("free_size [%d]\n", summary->sys.free_size);
        printf("free_cnt  [%d]\n", summary->sys.free_cnt);
        printf("use_size  [%d]\n", summary->sys.use_size);
        printf("use_cnt   [%d]\n", summary->sys.use_cnt);

        for(l = 0,m = IPCSHM_PAGE_USRAREA;m < IPCSHM_PAGE_USRAREA_MAX;m++,l++){
            if (summary->used[m].use_size || summary->used[m].use_cnt || summary->used[m].free_size || summary->used[m].free_cnt){
                printf("\t---used[%d]---\n", m);
                printf("\tuse_size  [%d]\n", summary->used[m].use_size);
                printf("\tuse_cnt   [%d]\n", summary->used[m].use_cnt);
                printf("\tfree_size [%d]\n", summary->used[m].free_size);
                printf("\tfree_cnt  [%d]\n", summary->used[m].free_cnt);
            }
        }
        // カテゴリ3に格納される
        if (n==3){
            EXPECT_EQ(summary->sys.idx, n);
            EXPECT_EQ(summary->sys.page_size, 131072);
            EXPECT_EQ(summary->sys.free_size, 0);
            EXPECT_EQ(summary->sys.free_cnt, 0);
            EXPECT_EQ(summary->sys.use_size, NODEITEMSIZE + ROOTITEMSIZE);
            EXPECT_EQ(summary->sys.use_cnt, 2);
        }else{
            EXPECT_EQ(summary->sys.idx, n);
            EXPECT_EQ(summary->sys.page_size, 131072);
            EXPECT_EQ(summary->sys.free_size, 0);
            EXPECT_EQ(summary->sys.free_cnt, 0);
            EXPECT_EQ(summary->sys.use_size, ROOTITEMSIZE);
            EXPECT_EQ(summary->sys.use_cnt, 1);
        }
        //
        free(summary);
    }
}

TEST(SmmrMemoryTest, UsedCount_02){
    STORE_AND_SEARCH("fugafuga00", "today is cloudy.march.6.2017(monday)");
    //
    int n,l,m;
    for(n = 0;n < CATEGORY_CNT; n++){
        // カテゴリ別ページ状況をフラッシュ
        rebuild_page_statics(&(CATEGORIES_INDEX.category[n]));
        //
        mempage_summary_ptr summary = (mempage_summary_ptr)malloc(sizeof(mempage_summary_t));
        assert(summary);
        EXPECT_EQ(memory_status_unsafe(&CATEGORIES_INDEX.category[n], summary), RET_OK);
        printf("------\n");
        printf("idx       [%d]\n", summary->sys.idx);
        printf("page_size [%d]\n", summary->sys.page_size);
        printf("free_size [%d]\n", summary->sys.free_size);
        printf("free_cnt  [%d]\n", summary->sys.free_cnt);
        printf("use_size  [%d]\n", summary->sys.use_size);
        printf("use_cnt   [%d]\n", summary->sys.use_cnt);

        for(l = 0,m = IPCSHM_PAGE_USRAREA;m < IPCSHM_PAGE_USRAREA_MAX;m++,l++){
            if (summary->used[m].use_size || summary->used[m].use_cnt || summary->used[m].free_size || summary->used[m].free_cnt){
                printf("\t---used[%d]---\n", m);
                printf("\tuse_size  [%d]\n", summary->used[m].use_size);
                printf("\tuse_cnt   [%d]\n", summary->used[m].use_cnt);
                printf("\tfree_size [%d]\n", summary->used[m].free_size);
                printf("\tfree_cnt  [%d]\n", summary->used[m].free_cnt);
            }
        }

        // カテゴリ3,1に格納される
        if (n==3 || n ==1){
            EXPECT_EQ(summary->sys.idx, n);
            EXPECT_EQ(summary->sys.page_size, 131072);
            EXPECT_EQ(summary->sys.free_size, 0);
            EXPECT_EQ(summary->sys.free_cnt, 0);
            EXPECT_EQ(summary->sys.use_size, NODEITEMSIZE + ROOTITEMSIZE);
            EXPECT_EQ(summary->sys.use_cnt, 2);
        }else{
            EXPECT_EQ(summary->sys.idx, n);
            EXPECT_EQ(summary->sys.page_size, 131072);
            EXPECT_EQ(summary->sys.free_size, 0);
            EXPECT_EQ(summary->sys.free_cnt, 0);
            EXPECT_EQ(summary->sys.use_size, ROOTITEMSIZE);
            EXPECT_EQ(summary->sys.use_cnt, 1);
        }
        //
        free(summary);
    }
}

#define STORESS_COUNT   (32768)
TEST(SmmrMemoryTest, UsedCount_03){
    int n,l,m;

    STORE_STIME = get_microsecond();
    for(n = 0;n < STORESS_COUNT;n++){
        char kbf[32] = {0};
        char vbf[256] = {0};
        snprintf(kbf, sizeof(kbf)-1,"other_test-%08d", n);
        snprintf(vbf, sizeof(vbf)-1,"other_test-value-%08d", n);
        //
        EXPECT_EQ(store(DATAFILES,&DATAFILES_INDEX, ROOT, kbf,strlen(kbf),vbf,strlen(vbf),0),RET_OK);
    }
    STORE_ETIME = get_microsecond();

    STORE_AND_SEARCH_STIME = get_microsecond();
    for(n = 0;n < STORESS_COUNT;n++){
        char kbf[32] = {0};
        char vbf[256] = {0};
        snprintf(kbf, sizeof(kbf)-1,"smmr_memory_test-%08d", n);
        snprintf(vbf, sizeof(vbf)-1,"smmr_memory_test-value-%08d", n);
        //
        STORE_AND_SEARCH(kbf, vbf);
    }
    STORE_AND_SEARCH_ETIME = get_microsecond();

    //
    for(n = 0;n < CATEGORY_CNT; n++){
        // カテゴリ別ページ状況をフラッシュ
        rebuild_page_statics(&(CATEGORIES_INDEX.category[n]));
        //
        mempage_summary_ptr summary = (mempage_summary_ptr)malloc(sizeof(mempage_summary_t));
        assert(summary);
        EXPECT_EQ(memory_status_unsafe(&CATEGORIES_INDEX.category[n], summary), RET_OK);
        printf("------\n");
        printf("idx       [%d]\n", summary->sys.idx);
        printf("page_size [%d]\n", summary->sys.page_size);
        printf("free_size [%d]\n", summary->sys.free_size);
        printf("free_cnt  [%d]\n", summary->sys.free_cnt);
        printf("use_size  [%d]\n", summary->sys.use_size);
        printf("use_cnt   [%d]\n", summary->sys.use_cnt);

        //
        free(summary);
    }

    SEARCH_STIME = get_microsecond();

    for(n = 0;n < STORESS_COUNT;n++){
        char*       resp  = NULL;
        uint32_t    resplen = 0;
        char kbf[32] = {0};
        char vbf[256] = {0};
        snprintf(kbf, sizeof(kbf)-1,"smmr_memory_test-%08d", n);
        snprintf(vbf, sizeof(vbf)-1,"smmr_memory_test-value-%08d", n);

        // 見つかること
        EXPECT_EQ(search(DATAFILES,&DATAFILES_INDEX,ROOT,kbf,strlen(kbf),&resp,&resplen),RET_OK);
        if (resp != NULL){
            EXPECT_EQ(memcmp(vbf, resp, resplen),0);
            free(resp);
        }else{
            FAIL() << "not found " << kbf;
        }
    }
    SEARCH_ETIME = get_microsecond();

    printf("\n++++results++++------+------------------+\n");
    printf(  "|     operation      |   TPS            |\n");
    printf(  "+--------------------+------------------+\n");
    printf("|  STORE_AND_SEARCH  | %16.4lf |\n",
           1000000.0e0 * (double)STORESS_COUNT/((double)(STORE_AND_SEARCH_ETIME - STORE_AND_SEARCH_STIME)));
    printf("|  STORE             | %16.4lf |\n",
           1000000.0e0 * (double)STORESS_COUNT/((double)(STORE_ETIME - STORE_STIME)));
    printf("|  SEARCH            | %16.4lf |\n",
           1000000.0e0*(double)STORESS_COUNT/((double)(SEARCH_ETIME - SEARCH_STIME)));
    printf(  "<<<< 1 process  +----+------------------+\n");

}

#define MODCNT  (4)
static int CHILD_PID[MODCNT];

typedef struct _MP_CALC{
    uint64_t STOREM_COUNTER[MODCNT];
    uint64_t STOREM_STIME[MODCNT];
    uint64_t STOREM_ETIME[MODCNT];
    uint64_t SEARCHM_COUNTER[MODCNT];
    uint64_t SEARCHM_STIME[MODCNT];
    uint64_t SEARCHM_ETIME[MODCNT];
}_MP_CALC_T,*_MP_CALC_PTR;

static char TEST_SHMNM[128] = {0};
static int  TEST_SHMFD = -1;
static _MP_CALC_PTR TEST_MMAPED = NULL;


static void ___child_main_00(int id){
    fprintf(stderr, "___child_main_00(%d,%d).\n", getpid(), id);

    // 子プロセスで、カテゴリアクセスを初期化（管理ページのみ全体shmとしている）
    if (init_categories_from_file(CATEGORIES,&CATEGORIES_INDEX,1) != RET_OK){
        fprintf(stderr, "___child_main_00::init_categories_from_file.");
        exit(1);
    }

    // カテゴリ全体Mutexで同期
    {	mutex_lock_t lock = {&(CATEGORIES->category_all_mutex),RET_NG};
        start_lock(&lock);
        if (isvalid_lock(&lock) != RET_OK){
            end_lock(&lock);
            fprintf(stderr, "___child_main_00::isvalid_lock.");
            exit(2);
        }
        // 全カテゴリのメモリページのrebuild + validation
        if (rebuild_pages_area(&CATEGORIES_INDEX) != RET_OK){
            end_lock(&lock);
            fprintf(stderr, "___child_main_00::rebuild_pages_area.");
            exit(3);
        }
        end_lock(&lock);
    }
    // インデックス ルートオブジェクト構築
    ROOT = create_tree_unsafe(CATEGORIES,&CATEGORIES_INDEX);
    if (!ROOT){
        fprintf(stderr, "___child_main_00::create_tree_unsafe.");
        exit(4);
    }
    // 全データファイルを準備する
    if (init_datafiles_from_file(DATAFILES,&DATAFILES_INDEX) != RET_OK){
        fprintf(stderr, "___child_main_00::init_datafiles_from_file.");
        exit(5);
    }

    uint64_t counter = 0;

    int childshmfd = shm_open(TEST_SHMNM, O_RDWR, 0);
    _MP_CALC_PTR CHILD = (_MP_CALC_PTR)mmap(0, sizeof(_MP_CALC_T), PROT_READ | PROT_WRITE, MAP_SHARED, childshmfd, 0);
    close(childshmfd);

    CHILD->STOREM_STIME[id] = get_microsecond();
    for(int n = 0;n < STORESS_COUNT;n++){
        char kbf[32] = {0};
        char vbf[256] = {0};
        snprintf(kbf, sizeof(kbf)-1,"st-%08d", n);
        snprintf(vbf, sizeof(vbf)-1,"stval-%08d", n);
        int mod_hashed = (safe_hash(kbf,strlen(kbf)) % CATEGORY_CNT);

        if (mod_hashed==id){
            counter ++;
            if (counter%32==0){
//              printf("store . .%d..%llu\n",id,counter);
                usleep(10);
            }
            //
            EXPECT_EQ(store(DATAFILES,&DATAFILES_INDEX, ROOT, kbf,strlen(kbf),vbf,strlen(vbf),0),RET_OK);
        }
    }
    CHILD->STOREM_ETIME[id] = get_microsecond();
    CHILD->STOREM_COUNTER[id] = counter;

    printf("\n++++++++\n[ipc_store(%llu-%d) : %0.4lf TPS]\n\n++++++++\n",
           CHILD->STOREM_COUNTER[id],
           id,
           1000000.0e0 * (double)CHILD->STOREM_COUNTER[id]/((double)(CHILD->STOREM_ETIME[id] - CHILD->STOREM_STIME[id])));


    CHILD->SEARCHM_STIME[id] = get_microsecond();
    counter =0;
    for(int n = 0;n < STORESS_COUNT;n++){
        char*       resp  = NULL;
        uint32_t    resplen = 0;
        char kbf[32] = {0};
        char vbf[256] = {0};
        snprintf(kbf, sizeof(kbf)-1,"st-%08d", n);
        snprintf(vbf, sizeof(vbf)-1,"stval-%08d", n);

        int mod_hashed = (safe_hash(kbf,strlen(kbf)) % CATEGORY_CNT);

        if (mod_hashed==id){
            if (counter%32==0){
//              printf("search . .%d..%llu\n",id,counter);
                usleep(10);
            }
            counter ++;
            // 見つかること
            EXPECT_EQ(search(DATAFILES,&DATAFILES_INDEX,ROOT,kbf,strlen(kbf),&resp,&resplen),RET_OK);
            if (resp != NULL){
                EXPECT_EQ(memcmp(vbf, resp, resplen),0);
                free(resp);
            }else{
                FAIL() << "not found " << kbf;
            }
        }
    }
    CHILD->SEARCHM_ETIME[id] = get_microsecond();
    CHILD->SEARCHM_COUNTER[id] = counter;
    printf("\n++++++++\n[ipc_search(%llu-%d) : %0.4lf TPS]\n\n++++++++\n",
           CHILD->SEARCHM_COUNTER[id],
           id,
           (1000000.0e0*(double)CHILD->SEARCHM_COUNTER[id]/((double)(CHILD->SEARCHM_ETIME[id] - CHILD->SEARCHM_STIME[id]))));

    munmap(CHILD,sizeof(_MP_CALC_T));
    exit(0);
}
TEST(SmmrMemoryTest, UsedCount_04){
    int n;
    int v;
    snprintf(TEST_SHMNM,sizeof(TEST_SHMNM)-1,"/smmr_test_%d", getpid());
    shm_unlink(TEST_SHMNM);
    TEST_SHMFD = shm_open(TEST_SHMNM, (O_CREAT|O_RDWR), (S_IRUSR | S_IWUSR));
    if (TEST_SHMFD < 0){
        FAIL() << "shmopen.." << strerror(errno);
        ASSERT_FALSE(1);
    }
    if (ftruncate(TEST_SHMFD, sizeof(_MP_CALC_T)) < 0) {
        FAIL() << "ftruncate.." << strerror(errno);
        ASSERT_FALSE(1);
    }

    TEST_MMAPED= (_MP_CALC_PTR)mmap(0, sizeof(_MP_CALC_T), PROT_READ | PROT_WRITE, MAP_SHARED, TEST_SHMFD, 0);
    if (TEST_MMAPED ==(_MP_CALC_PTR)-1){
        FAIL() << "mmap.." << strerror(errno);
        ASSERT_FALSE(1);
    }

    // fork..
    for( n=0 ; n < MODCNT && (CHILD_PID[n] = fork()) > 0 ; n++ );
    //
    if( n == MODCNT ){
        // 親プロセス
        for(  n = 0 ; n < MODCNT; n++ ){
            wait(&v);
        }

        printf("\n++++results++++-------+--------------------+------------------+\n");
        printf(  "| process idx | count |     operation      |   TPS            |\n");
        printf(  "+-------------+-------+--------------------+------------------+\n");
        double  store_tps = 0.0e0,search_tps = 0.0e0;
        for(n = 0;n < MODCNT;n++){
            printf("|   %8d  |%6llu |  STORE             | %16.4lf |\n",
                    n,
                   TEST_MMAPED->STOREM_COUNTER[n],
                   1000000.0e0 * (double)TEST_MMAPED->STOREM_COUNTER[n]/((double)(TEST_MMAPED->STOREM_ETIME[n] - TEST_MMAPED->STOREM_STIME[n])));
            printf("|   %8d  |%6llu |  SEARCH            | %16.4lf |\n",
                   n,
                   TEST_MMAPED->SEARCHM_COUNTER[n],
                   1000000.0e0 * (double)TEST_MMAPED->SEARCHM_COUNTER[n]/((double)(TEST_MMAPED->SEARCHM_ETIME[n] - TEST_MMAPED->SEARCHM_STIME[n])));
            store_tps += 1000000.0e0 * (double)TEST_MMAPED->STOREM_COUNTER[n]/((double)(TEST_MMAPED->STOREM_ETIME[n] - TEST_MMAPED->STOREM_STIME[n]));
            search_tps += 1000000.0e0 * (double)TEST_MMAPED->SEARCHM_COUNTER[n]/((double)(TEST_MMAPED->SEARCHM_ETIME[n] - TEST_MMAPED->SEARCHM_STIME[n]));
        }
        printf(  "+<< %d process +-------+--------------------+------------------+\n",MODCNT);
        printf(  "+------------------------------------------+------------------+\n");
        printf(  "| STORE                                    | %16.4lf |\n",store_tps);
        printf(  "+------------------------------------------+------------------+\n");
        printf(  "| SEARCH                                   | %16.4lf |\n",search_tps);
        printf(  "+------------------------------------------+------------------+\n");

        munmap(TEST_MMAPED,sizeof(_MP_CALC_T));
        close(TEST_SHMFD);
        shm_unlink(TEST_SHMNM);

    }else if( CHILD_PID[n] == 0){
        // こプロ
        printf("child:%d\n",n);
        ___child_main_00(n);
        exit(0);
    }else{
        ASSERT_FALSE(1);
    }
}

TEST(SmmrMemoryTest, CleanupMemory){
    if (remove_db_area_parent(&DATAFILES,&DATAFILES_INDEX) != RET_OK){ FAIL() << "failed.remove_db_area_parent"; }
    if (remove_categories_area(&CATEGORIES,&CATEGORIES_INDEX) != RET_OK){ FAIL() << "failed.remove_categories_area"; }

    char bf[128] = {0};

    snprintf(bf,sizeof(bf)-1,"/tmp/%u-2/db/", (unsigned)getpid());
    remove_safe(bf);
    snprintf(bf,sizeof(bf)-1,"/tmp/%u-2/", (unsigned)getpid());
    remove_safe(bf);
}
