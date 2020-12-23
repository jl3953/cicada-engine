/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include <cstdio>
#include <thread>
#include <random>
#include "mica/transaction/db.h"
#include "mica/util/lcore.h"
//#include "mica/util/zipf.h"
#include "mica/util/rand.h"
#include "mica/test/test_tx_conf.h"

#include <iostream>
#include <memory>
#include <string>
#include <stdlib.h>

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

#include "../build/smdbrpc.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using smdbrpc::HotshardRequest;
using smdbrpc::HotshardReply;
using smdbrpc::HotshardGateway;
using smdbrpc::HLCTimestamp;

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

HashIndex* hash_idx = nullptr;
DB* db_ptr = nullptr;


// Logic and data behind the server's behavior.
class HotshardGatewayServiceImpl final : public HotshardGateway::Service {
  Status ContactHotshard(ServerContext* context, const HotshardRequest* request,
                  HotshardReply* reply) override {

      auto tbl = db_ptr->get_table("main");

      ::mica::util::lcore.pin_thread(0);

      db_ptr->activate(static_cast<uint16_t>(0));
      Transaction tx(db_ptr->context(0));

      RowAccessHandle rah(&tx);
      bool ret_jenn = tx.begin();
      if (!ret_jenn)
          printf("jenndebug tx.begin() screwed up\n");

      if (!rah.new_row(tbl, 0, Transaction::kNewRowID, true,
                       kDataSize)) {
          printf("jenndebug rah screwed up\n");
      }

      uint64_t key = uint64_t(rand());
      uint64_t value = 214;

      auto ret = hash_idx->insert(&tx, key, value);
      if (ret != 1) {
          printf("jenndebug uh oh insert %lu failed\n", key);
      }

      Result result;
      tx.commit(&result);

      // check
      tx.begin();
      uint64_t looked_value = 0;
      auto lookup_result =
              hash_idx->lookup(&tx, key, kSkipValidationForIndexAccess,
                               [&looked_value](auto& k, auto& v) {
                                   (void)k;
                                   looked_value = v;
                                   return false;
                               });
      tx.commit();
      if (value == looked_value) {
          printf("jenndebug ok we did it, value %lu, looked_value %lu\n", value, looked_value);
      } else {
          printf("jenndebug oh we screwed up, value %lu, looked_value %lu\n", value, looked_value);
      }

    reply->set_is_committed(Result::kCommitted == result);
      reply->set_is_committed(true);
    HLCTimestamp *hlcTimestamp = new HLCTimestamp(request->hlctimestamp());
    reply->set_allocated_hlctimestamp(hlcTimestamp);
    return Status::OK;
  }
};

void RunServer() {
  std::string server_address("0.0.0.0:50051");
  HotshardGatewayServiceImpl service;

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

int main(int argc, char** argv) {
    auto config = ::mica::util::Config::load_file("test_tx.json");

    uint64_t num_rows = /* static_cast<uint64_t>(atol(argv[1]));*/ 100;
    uint64_t reqs_per_tx = /*static_cast<uint64_t>(atol(argv[2]));*/ 1;
    double read_ratio = /*atof(argv[3]);*/ 95;
    double zipf_theta = /*atof(argv[4]);*/ 0.5;
    uint64_t tx_count = /*static_cast<uint64_t>(atol(argv[5]));*/ 1;
    uint64_t num_threads = /*static_cast<uint64_t>(atol(argv[6]));*/ 2;

    Alloc alloc(config.get("alloc"));
    auto page_pool_size = 24 * uint64_t(1073741824);
    PagePool* page_pools[2];
     if (num_threads == 1) {
       page_pools[0] = new PagePool(&alloc, page_pool_size, 0);
       page_pools[1] = nullptr;
     } else {
        page_pools[0] = new PagePool(&alloc, page_pool_size / 2, 0);
        page_pools[1] = new PagePool(&alloc, page_pool_size / 2, 1);
     }

    ::mica::util::lcore.pin_thread(0);

    sw.init_start();
    sw.init_end();

    if (num_rows == 0) {
        num_rows = SYNTH_TABLE_SIZE;
        reqs_per_tx = REQ_PER_QUERY;
        read_ratio = READ_PERC;
        zipf_theta = ZIPF_THETA;
        tx_count = MAX_TXN_PER_PART;
        num_threads = THREAD_CNT;
#ifndef NDEBUG
        printf("!NDEBUG\n");
    return EXIT_FAILURE;
#endif
    }

    printf("num_rows = %" PRIu64 "\n", num_rows);
    printf("reqs_per_tx = %" PRIu64 "\n", reqs_per_tx);
    printf("read_ratio = %lf\n", read_ratio);
    printf("zipf_theta = %lf\n", zipf_theta);
    printf("tx_count = %" PRIu64 "\n", tx_count);
    printf("num_threads = %" PRIu64 "\n", num_threads);
#ifndef NDEBUG
    printf("!NDEBUG\n");
#endif
    printf("\n");

    Logger logger;
    db_ptr = new DB(page_pools, &logger, &sw, static_cast<uint16_t>(num_threads));
    DB &db = *db_ptr;
    // DB db(page_pools, &logger, &sw, static_cast<uint16_t>(num_threads));


    const uint64_t data_sizes[] = {kDataSize};
    bool ret = db.create_table("main", 1, data_sizes);
    assert(ret);
    (void)ret;

    auto tbl = db.get_table("main");

    db.activate(0);

    // jenncomment hash_idx is on a certain table

    if (kUseHashIndex) {
        bool ret = db.create_hash_index_unique_u64("main_idx", tbl, num_rows);
        assert(ret);
        (void)ret;

        hash_idx = db.get_hash_index_unique_u64("main_idx");
        Transaction tx(db.context(0));
        hash_idx->init(&tx);
    }

    {
        printf("initializing table\n");

        std::vector<std::thread> threads;
        uint64_t init_num_threads = std::min(uint64_t(2), num_threads);
        for (uint64_t thread_id = 0; thread_id < init_num_threads; thread_id++) {
            threads.emplace_back([&, thread_id] {
                ::mica::util::lcore.pin_thread(thread_id);

                db.activate(static_cast<uint16_t>(thread_id));
                while (db.active_thread_count() < init_num_threads) {
                    ::mica::util::pause();
                    db.idle(static_cast<uint16_t>(thread_id));
                }

                // Randomize the data layout by shuffling row insert order.
                std::mt19937 g(thread_id);
                std::vector<uint64_t> row_ids;
                row_ids.reserve((num_rows + init_num_threads - 1) / init_num_threads);
                for (uint64_t i = thread_id; i < num_rows; i += init_num_threads)
                    row_ids.push_back(i);
                std::shuffle(row_ids.begin(), row_ids.end(), g);

                /** jennsection **/
                std::vector<uint64_t> keys;
                std::vector<uint64_t> values;
                /** end jennsection **/

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
                                printf("failed to insert rows at new_row(), row = %" PRIu64
                                       "\n",
                                       j);
                                aborted = true;
                                tx.abort();
                                break;
                            }

                            if (kUseHashIndex) {
                                auto row_id_jenn = row_ids[j];
                                auto value_jenn = rah.row_id();
                                keys.push_back(row_id_jenn); // jennsection
                                values.push_back(value_jenn); // jennsection
                                auto ret = hash_idx->insert(&tx, row_id_jenn, value_jenn);
                                if (ret != 1 || ret == HashIndex::kHaveToAbort) {
                                    printf("failed to update index row = %" PRIu64 "\n", j);
                                    aborted = true;
                                    tx.abort();
                                    break;
                                } else {
                                    printf("jenndebug inserted (%lu, %lu)\n", row_id_jenn, value_jenn);
                                }
                            }

                        }

                        if (aborted) continue;

                        Result result;
                        if (!tx.commit(&result)) {
                            printf("failed to insert rows at commit(), row = %" PRIu64
                                   "; result=%d\n",
                                   i_end - 1, static_cast<int>(result));
                            continue;
                        }
                        break;
                    }

                    /** jennsection **/
                    tx.begin();

                    uint64_t value = 0;
                    for (const auto& key : keys) {
                        auto lookup_result =
                                hash_idx->lookup(&tx, key, kSkipValidationForIndexAccess,
                                                 [&value](auto& k, auto& v) {
                                                     (void)k;
                                                     value = v;
                                                     return false;
                                                 });
                        printf("jenndebug found %lu, (%lu, %lu)\n", lookup_result, key, value);
                    }
                    Result result;
                    tx.commit(&result);
                    /** end jennsection **/

                }

                db.deactivate(static_cast<uint16_t>(thread_id));
                return 0;
            });
        }
        printf("jenndebug 3\n");

        while (threads.size() > 0) {
            threads.back().join();
            threads.pop_back();
        }

        // TODO: Use multiple threads to renew rows for more balanced memory access.

        db.activate(0);
        {
            uint64_t i = 0;
            tbl->renew_rows(db.context(0), 0, i, static_cast<uint64_t>(-1), false);
        }
        if (hash_idx != nullptr) {
            uint64_t i = 0;
            hash_idx->index_table()->renew_rows(db.context(0), 0, i,
                                                static_cast<uint64_t>(-1), false);
        }

        db.deactivate(0);

        db.reset_stats();
        db.reset_backoff();

        printf("jenndebug 4\n");
    }

  RunServer();

  return 0;
}
