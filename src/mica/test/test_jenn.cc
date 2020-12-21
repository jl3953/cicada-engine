#include <cstdio>
#include <thread>
#include <random>
#include "mica/transaction/db.h"
#include "mica/util/lcore.h"
#include "mica/util/zipf.h"
#include "mica/util/rand.h"
#include "mica/test/test_tx_conf.h"

typedef DBConfig::Alloc Alloc;
typedef DBConfig::Logger Logger;
typedef DBConfig::Timestamp Timestamp;
typedef DBConfig::ConcurrentTimestamp ConcurrentTimestamp;
typedef DBConfig::Timing Timing;
typedef ::mica::transaction::PagePool<DBConfig> PagePool;
typedef ::mica::transaction::DB<DBConfig> DB;
typedef ::mica::transaction::Table<DBConfig> Table;
typedef DB::HashIndexUniqueU64 HashIndex;
typedef DB::BTreeIndexUniqueU64 BTreeIndex;
typedef ::mica::transaction::RowVersion<DBConfig> RowVersion;
typedef ::mica::transaction::RowAccessHandle<DBConfig> RowAccessHandle;
typedef ::mica::transaction::RowAccessHandlePeekOnly<DBConfig>
        RowAccessHandlePeekOnly;
typedef ::mica::transaction::Transaction<DBConfig> Transaction;
typedef ::mica::transaction::Result Result;

static ::mica::util::Stopwatch sw;

int main(int argc, const char* argv[]) {

    // what used to be argc
    uint64_t num_rows = 100;
    uint64_t num_threads = 1;

    auto config = ::mica::util::Config::load_file("test_tx.json");

    Alloc alloc(config.get("alloc"));

    auto page_pool_size = 24 * uint64_t(1073741824);

    PagePool *page_pools[2];
    page_pools[0] = new PagePool(&alloc, page_pool_size / 2, 0);
    page_pools[1] = new PagePool(&alloc, page_pool_size / 2, 1);

    Logger logger;
    DB db(page_pools, &logger, &sw, static_cast<uint16_t>(num_threads));

    const uint64_t data_sizes[] = {kDataSize};
    bool ret = db.create_table("main", 1, data_sizes);
    assert(ret);
    (void) ret;

    auto tbl = db.get_table("main");

    HashIndex *hash_idx = nullptr;
    {
        bool ret = db.create_hash_index_unique_u64("main_idx", tbl, num_rows);
        assert(ret);
        (void) ret;

        hash_idx = db.get_hash_index_unique_u64("main_idx");
        Transaction tx(db.context(0));
        hash_idx->init(&tx);
    }

    uint64_t init_num_threads = std::min(uint64_t(2), num_threads);
    for (uint64_t thread_id = 0; thread_id < init_num_threads; thread_id++) {

        db.activate(static_cast<uint16_t>(thread_id));

        // Randomize the data layout by shuffling row insert order.
        std::mt19937 g(thread_id);
        std::vector<uint64_t> row_ids;
        row_ids.reserve((num_rows + init_num_threads - 1) / init_num_threads);
        for (uint64_t i = thread_id; i < num_rows; i += init_num_threads)
            row_ids.push_back(i);
        std::shuffle(row_ids.begin(), row_ids.end(), g);

        Transaction tx(db.context(static_cast<uint16_t>(thread_id)));
        const uint64_t kBatchSize = 16;
        for (uint64_t i = 0; i < row_ids.size(); i += kBatchSize) {

            while (true) {
                bool ret = tx.begin();
                if (!ret) {
                    printf("failed to start a transaction\n");
                    continue;
                }

                bool aborted = false;
                auto i_end = std::min(i + kBatchSize, row_ids.size());
                for (uint64_t j = i; j < i_end; j++) {
                    RowAccessHandle rah(&tx);
                    if (!rah.new_row(tbl, 0, Transaction::kNewRowID, true,
                                     kDataSize)) {
                        printf("failed to insert rows at new_row(), row = %" PRIu64 "\n", j);
                        aborted = true;
                        tx.abort();
                        break;
                    }

                    // if (kUseHashIndex)
                    {
                        auto ret = hash_idx->insert(&tx, row_ids[j], rah.row_id());
                        if (ret != 1 || ret == HashIndex::kHaveToAbort) {
                            printf("failed to update index row = %" PRIu64 "\n", j);
                            aborted = true;
                            tx.abort();
                            break;
                        }
                    }
                }

                if (aborted)
                    continue;

                Result result;
                if (!tx.commit(&result)) {
                    printf("failed to insert rows at commit(), row = %" PRIu64 "; result=%d\n",
                           i_end - 1, static_cast<int>(result));
                    continue;
                }
                break;
            }
        }
        db.deactivate(static_cast<uint16_t>(thread_id));
        return 0;
    }
}
