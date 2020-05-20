// Copyright (c) 2020 Zano Project
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chaingen.h"
#include "hard_fork_2.h"
#include "../../src/wallet/wallet_rpc_server.h"

using namespace currency;

//------------------------------------------------------------------------------

hard_fork_2_base_test::hard_fork_2_base_test(size_t hardfork_02_height)
  : hard_fork_2_base_test(1, hardfork_02_height)
{
}

hard_fork_2_base_test::hard_fork_2_base_test(size_t hardfork_01_height, size_t hardfork_02_height)
  : m_hardfork_01_height(hardfork_01_height)
  , m_hardfork_02_height(hardfork_02_height)
{
  REGISTER_CALLBACK_METHOD(hard_fork_2_base_test, configure_core);
}

bool hard_fork_2_base_test::configure_core(currency::core& c, size_t ev_index, const std::vector<test_event_entry>& events)
{
  currency::core_runtime_config pc = c.get_blockchain_storage().get_core_runtime_config();
  pc.min_coinstake_age = TESTS_POS_CONFIG_MIN_COINSTAKE_AGE;
  pc.pos_minimum_heigh = TESTS_POS_CONFIG_POS_MINIMUM_HEIGH;
  pc.hard_fork_01_starts_after_height = m_hardfork_01_height;
  pc.hard_fork_02_starts_after_height = m_hardfork_02_height;
  c.get_blockchain_storage().set_core_runtime_config(pc);
  return true;
}

void hard_fork_2_base_test::set_hard_fork_heights_to_generator(test_generator& generator) const
{
  generator.set_hardfork_height(1, m_hardfork_01_height);
  generator.set_hardfork_height(2, m_hardfork_02_height);
}

//------------------------------------------------------------------------------

hard_fork_2_tx_payer_in_wallet::hard_fork_2_tx_payer_in_wallet()
  : hard_fork_2_base_test(24)
{
  REGISTER_CALLBACK_METHOD(hard_fork_2_tx_payer_in_wallet, c1);
}

bool hard_fork_2_tx_payer_in_wallet::generate(std::vector<test_event_entry>& events) const
{
  // Test idea: make sure that wallet uses tx_payer_old only before HF2 and tx_payer after

  m_accounts.resize(TOTAL_ACCS_COUNT);
  account_base& miner_acc = m_accounts[MINER_ACC_IDX]; miner_acc.generate();
  account_base& alice_acc = m_accounts[ALICE_ACC_IDX]; alice_acc.generate();
  account_base& bob_acc   = m_accounts[BOB_ACC_IDX];   bob_acc.generate(true); // Bob has auditable address

  MAKE_GENESIS_BLOCK(events, blk_0, miner_acc, test_core_time::get_time());
  set_hard_fork_heights_to_generator(generator);
  DO_CALLBACK(events, "configure_core");
  REWIND_BLOCKS_N(events, blk_0r, blk_0, miner_acc, CURRENCY_MINED_MONEY_UNLOCK_WINDOW);

  MAKE_TX(events, tx_0, miner_acc, bob_acc, MK_TEST_COINS(12), blk_0r);
  MAKE_NEXT_BLOCK_TX1(events, blk_1, blk_0r, miner_acc, tx_0);

  REWIND_BLOCKS_N(events, blk_1r, blk_1, miner_acc, CURRENCY_MINED_MONEY_UNLOCK_WINDOW);

  DO_CALLBACK(events, "c1");

  return true;
}

bool hard_fork_2_tx_payer_in_wallet::c1(currency::core& c, size_t ev_index, const std::vector<test_event_entry>& events)
{
  bool r = false, stub_bool = false;
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 0, false, "Incorrect txs count in the pool: " << c.get_pool_transactions_count());
  std::shared_ptr<tools::wallet2> miner_wlt = init_playtime_test_wallet(events, c, m_accounts[MINER_ACC_IDX]);
  std::shared_ptr<tools::wallet2> alice_wlt = init_playtime_test_wallet(events, c, m_accounts[ALICE_ACC_IDX]);
  std::shared_ptr<tools::wallet2> bob_wlt   = init_playtime_test_wallet(events, c, m_accounts[BOB_ACC_IDX]);

  miner_wlt->refresh();
  CHECK_AND_ASSERT_MES(refresh_wallet_and_check_balance("", "Alice", alice_wlt, 0), false, "");
  CHECK_AND_ASSERT_MES(refresh_wallet_and_check_balance("", "Bob", bob_wlt, MK_TEST_COINS(12)), false, "");

  // wallet RPC server
  tools::wallet_rpc_server miner_wlt_rpc(*miner_wlt);
  epee::json_rpc::error je;
  tools::wallet_rpc_server::connection_context ctx;

  // Before HF2: Miner -> Alice (normal address) with payer info
  tools::wallet_public::COMMAND_RPC_TRANSFER::request req_a = AUTO_VAL_INIT(req_a);
  req_a.destinations.push_back(tools::wallet_public::transfer_destination{ MK_TEST_COINS(1), m_accounts[ALICE_ACC_IDX].get_public_address_str() });
  req_a.fee = TESTS_DEFAULT_FEE;
  req_a.push_payer = true;

  tools::wallet_public::COMMAND_RPC_TRANSFER::response res = AUTO_VAL_INIT(res);
  
  r = miner_wlt_rpc.on_transfer(req_a, res, je, ctx);
  CHECK_AND_ASSERT_MES(r, false, "on_transfer failed");
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 1, false, "Incorrect txs count in the pool: " << c.get_pool_transactions_count());

  crypto::hash tx_hash = null_hash;
  CHECK_AND_ASSERT_MES(epee::string_tools::hex_to_pod(res.tx_hash, tx_hash), false, "");
  
  transaction tx = AUTO_VAL_INIT(tx);
  CHECK_AND_ASSERT_MES(c.get_transaction(tx_hash, tx), false, "");

  r = have_type_in_variant_container<tx_payer_old>(tx.extra);
  CHECK_AND_ASSERT_MES(r, false, "tx_payer_old is not found in extra");
  r = !have_type_in_variant_container<tx_payer>(tx.extra);
  CHECK_AND_ASSERT_MES(r, false, "tx_payer is found in extra");


  // Before HF2: Miner -> Bob (auditable address) with payer info
  tools::wallet_public::COMMAND_RPC_TRANSFER::request req_b = AUTO_VAL_INIT(req_b);
  req_b.destinations.push_back(tools::wallet_public::transfer_destination{ MK_TEST_COINS(1), m_accounts[BOB_ACC_IDX].get_public_address_str() });
  req_b.fee = TESTS_DEFAULT_FEE;
  req_b.push_payer = true;

  res = AUTO_VAL_INIT(res);
  
  r = miner_wlt_rpc.on_transfer(req_b, res, je, ctx);
  CHECK_AND_ASSERT_MES(r, false, "on_transfer failed");
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 2, false, "Incorrect txs count in the pool: " << c.get_pool_transactions_count());

  tx_hash = null_hash;
  CHECK_AND_ASSERT_MES(epee::string_tools::hex_to_pod(res.tx_hash, tx_hash), false, "");
  
  tx = AUTO_VAL_INIT(tx);
  CHECK_AND_ASSERT_MES(c.get_transaction(tx_hash, tx), false, "");

  r = have_type_in_variant_container<tx_payer_old>(tx.extra);
  CHECK_AND_ASSERT_MES(r, false, "tx_payer_old is not found in extra");
  r = !have_type_in_variant_container<tx_payer>(tx.extra);
  CHECK_AND_ASSERT_MES(r, false, "tx_payer is found in extra");


  // mine a block and confirm both transactions
  r = mine_next_pow_block_in_playtime(m_accounts[MINER_ACC_IDX].get_public_address(), c);
  CHECK_AND_ASSERT_MES(r, false, "mine_next_pow_block_in_playtime failed");
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 0, false, "Incorrect txs count in the pool: " << c.get_pool_transactions_count());

  size_t callback_counter = 0;
  std::shared_ptr<wlt_lambda_on_transfer2_wrapper> l(new wlt_lambda_on_transfer2_wrapper(
    [&](const tools::wallet_public::wallet_transfer_info& wti, uint64_t balance, uint64_t unlocked_balance, uint64_t total_mined) -> bool {
      CHECK_AND_ASSERT_THROW_MES(wti.show_sender, "show_sender is false");
      CHECK_AND_ASSERT_THROW_MES(wti.remote_addresses.size() == 1, "incorrect wti.remote_addresses.size() = " << wti.remote_addresses.size());
      CHECK_AND_ASSERT_THROW_MES(wti.remote_addresses.front() == m_accounts[MINER_ACC_IDX].get_public_address_str(), "wti.remote_addresses.front is incorrect");
      ++callback_counter;
      return true;
    }
  ));
  alice_wlt->callback(l);
  CHECK_AND_ASSERT_MES(refresh_wallet_and_check_balance("", "Alice", alice_wlt, MK_TEST_COINS(1)), false, "");
  CHECK_AND_ASSERT_MES(callback_counter == 1, false, "callback_counter = " << callback_counter);

  bob_wlt->callback(l); // same callback -- same changes
  callback_counter = 0;
  CHECK_AND_ASSERT_MES(refresh_wallet_and_check_balance("", "Bob", bob_wlt, MK_TEST_COINS(13)), false, "");
  CHECK_AND_ASSERT_MES(callback_counter == 1, false, "callback_counter = " << callback_counter);

  alice_wlt->callback(std::make_shared<tools::i_wallet2_callback>()); // clear callback
  bob_wlt->callback(std::make_shared<tools::i_wallet2_callback>()); // clear callback

  // Before HF2: Bob (auditable address) -> Alice with payer info requested (should NOT put tx_payer or tx_payer_old)
  tools::wallet_rpc_server bob_wlt_rpc(*bob_wlt);
  tools::wallet_public::COMMAND_RPC_TRANSFER::request req_c = AUTO_VAL_INIT(req_c);
  req_c.destinations.push_back(tools::wallet_public::transfer_destination{ MK_TEST_COINS(1), m_accounts[ALICE_ACC_IDX].get_public_address_str() });
  req_c.fee = TESTS_DEFAULT_FEE;
  req_c.push_payer = true;
  res = AUTO_VAL_INIT(res);
  r = bob_wlt_rpc.on_transfer(req_c, res, je, ctx);
  CHECK_AND_ASSERT_MES(r, false, "on_transfer failed");
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 1, false, "Incorrect txs count in the pool: " << c.get_pool_transactions_count());

  tx_hash = null_hash;
  CHECK_AND_ASSERT_MES(epee::string_tools::hex_to_pod(res.tx_hash, tx_hash), false, "");
  tx = AUTO_VAL_INIT(tx);
  CHECK_AND_ASSERT_MES(c.get_transaction(tx_hash, tx), false, "");
  r = !have_type_in_variant_container<tx_payer_old>(tx.extra);
  CHECK_AND_ASSERT_MES(r, false, "tx_payer_old is found in extra");
  r = !have_type_in_variant_container<tx_payer>(tx.extra);
  CHECK_AND_ASSERT_MES(r, false, "tx_payer is found in extra");

  r = mine_next_pow_block_in_playtime(m_accounts[MINER_ACC_IDX].get_public_address(), c);
  CHECK_AND_ASSERT_MES(r, false, "mine_next_pow_block_in_playtime failed");
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 0, false, "Incorrect txs count in the pool: " << c.get_pool_transactions_count());

  CHECK_AND_ASSERT_MES(refresh_wallet_and_check_balance("", "Bob", bob_wlt, MK_TEST_COINS(11)), false, "");


  //
  // mine blocks 24, 25, 26 to activate HF2
  //
  r = mine_next_pow_blocks_in_playtime(m_accounts[MINER_ACC_IDX].get_public_address(), c, 3);
  CHECK_AND_ASSERT_MES(r, false, "mine_next_pow_blocks_in_playtime failed");
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 0, false, "Incorrect txs count in the pool: " << c.get_pool_transactions_count());

  miner_wlt->refresh();
  alice_wlt->refresh();

  // check again (Miner -> Alice), with different amount
  req_a.destinations.front().amount = MK_TEST_COINS(2);
  r = miner_wlt_rpc.on_transfer(req_a, res, je, ctx);
  CHECK_AND_ASSERT_MES(r, false, "on_transfer failed");
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 1, false, "Incorrect txs count in the pool: " << c.get_pool_transactions_count());

  tx_hash = null_hash;
  CHECK_AND_ASSERT_MES(epee::string_tools::hex_to_pod(res.tx_hash, tx_hash), false, "");
  CHECK_AND_ASSERT_MES(c.get_transaction(tx_hash, tx), false, "");

  r = have_type_in_variant_container<tx_payer>(tx.extra);
  CHECK_AND_ASSERT_MES(r, false, "tx_payer is not found in extra");
  r = !have_type_in_variant_container<tx_payer_old>(tx.extra);
  CHECK_AND_ASSERT_MES(r, false, "tx_payer_old is found in extra");


  // check again (Miner -> Bob), with different amount
  req_b.destinations.front().amount = MK_TEST_COINS(2);
  r = miner_wlt_rpc.on_transfer(req_b, res, je, ctx);
  CHECK_AND_ASSERT_MES(r, false, "on_transfer failed");
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 2, false, "Incorrect txs count in the pool: " << c.get_pool_transactions_count());

  tx_hash = null_hash;
  CHECK_AND_ASSERT_MES(epee::string_tools::hex_to_pod(res.tx_hash, tx_hash), false, "");
  CHECK_AND_ASSERT_MES(c.get_transaction(tx_hash, tx), false, "");

  r = have_type_in_variant_container<tx_payer>(tx.extra);
  CHECK_AND_ASSERT_MES(r, false, "tx_payer is not found in extra");
  r = !have_type_in_variant_container<tx_payer_old>(tx.extra);
  CHECK_AND_ASSERT_MES(r, false, "tx_payer_old is found in extra");


  r = mine_next_pow_block_in_playtime(m_accounts[MINER_ACC_IDX].get_public_address(), c);
  CHECK_AND_ASSERT_MES(r, false, "mine_next_pow_block_in_playtime failed");
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 0, false, "Incorrect txs count in the pool: " << c.get_pool_transactions_count());

  std::shared_ptr<wlt_lambda_on_transfer2_wrapper> l2(new wlt_lambda_on_transfer2_wrapper(
    [&](const tools::wallet_public::wallet_transfer_info& wti, uint64_t balance, uint64_t unlocked_balance, uint64_t total_mined) -> bool {
      CHECK_AND_ASSERT_THROW_MES(wti.amount == MK_TEST_COINS(2), "incorrect wti.amount = " << print_money_brief(wti.amount));
      CHECK_AND_ASSERT_THROW_MES(wti.show_sender, "show_sender is false");
      CHECK_AND_ASSERT_THROW_MES(wti.remote_addresses.size() == 1, "incorrect wti.remote_addresses.size() = " << wti.remote_addresses.size());
      CHECK_AND_ASSERT_THROW_MES(wti.remote_addresses.front() == m_accounts[MINER_ACC_IDX].get_public_address_str(), "wti.remote_addresses.front is incorrect");
      ++callback_counter;
      return true;
    }
  ));

  alice_wlt->callback(l2);
  CHECK_AND_ASSERT_MES(refresh_wallet_and_check_balance("", "Alice", alice_wlt, MK_TEST_COINS(4)), false, "");

  bob_wlt->callback(l2);
  CHECK_AND_ASSERT_MES(refresh_wallet_and_check_balance("", "Bob", bob_wlt, MK_TEST_COINS(13)), false, "");

  alice_wlt->callback(std::make_shared<tools::i_wallet2_callback>()); // clear callback
  bob_wlt->callback(std::make_shared<tools::i_wallet2_callback>()); // clear callback

  // After HF2: Bob (auditable address) -> Alice with payer info requested (should put tx_payer)
  req_c = AUTO_VAL_INIT(req_c);
  req_c.destinations.push_back(tools::wallet_public::transfer_destination{ MK_TEST_COINS(1), m_accounts[ALICE_ACC_IDX].get_public_address_str() });
  req_c.fee = TESTS_DEFAULT_FEE;
  req_c.push_payer = true;
  res = AUTO_VAL_INIT(res);
  r = bob_wlt_rpc.on_transfer(req_c, res, je, ctx);
  CHECK_AND_ASSERT_MES(r, false, "on_transfer failed");
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 1, false, "Incorrect txs count in the pool: " << c.get_pool_transactions_count());

  tx_hash = null_hash;
  CHECK_AND_ASSERT_MES(epee::string_tools::hex_to_pod(res.tx_hash, tx_hash), false, "");
  tx = AUTO_VAL_INIT(tx);
  CHECK_AND_ASSERT_MES(c.get_transaction(tx_hash, tx), false, "");
  r = !have_type_in_variant_container<tx_payer_old>(tx.extra);
  CHECK_AND_ASSERT_MES(r, false, "tx_payer_old is found in extra");
  r = have_type_in_variant_container<tx_payer>(tx.extra);
  CHECK_AND_ASSERT_MES(r, false, "tx_payer is NOT found in extra");

  r = mine_next_pow_block_in_playtime(m_accounts[MINER_ACC_IDX].get_public_address(), c);
  CHECK_AND_ASSERT_MES(r, false, "mine_next_pow_block_in_playtime failed");
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 0, false, "Incorrect txs count in the pool: " << c.get_pool_transactions_count());

  CHECK_AND_ASSERT_MES(refresh_wallet_and_check_balance("", "Alice", alice_wlt, MK_TEST_COINS(5)), false, "");
  CHECK_AND_ASSERT_MES(refresh_wallet_and_check_balance("", "Bob", bob_wlt, MK_TEST_COINS(11)), false, "");

  return true;
}

//------------------------------------------------------------------------------

hard_fork_2_tx_receiver_in_wallet::hard_fork_2_tx_receiver_in_wallet()
  : hard_fork_2_base_test(23)
  , m_alice_start_balance(0)
{
  REGISTER_CALLBACK_METHOD(hard_fork_2_tx_receiver_in_wallet, c1);
}

bool hard_fork_2_tx_receiver_in_wallet::generate(std::vector<test_event_entry>& events) const
{
  // Test idea: make sure that wallet uses tx_receiver_old only before HF2 and tx_receiver after

  m_accounts.resize(TOTAL_ACCS_COUNT);
  account_base& miner_acc = m_accounts[MINER_ACC_IDX]; miner_acc.generate();
  account_base& alice_acc = m_accounts[ALICE_ACC_IDX]; alice_acc.generate();
  account_base& bob_acc   = m_accounts[BOB_ACC_IDX];   bob_acc.generate(true); // Bob has auditable address

  MAKE_GENESIS_BLOCK(events, blk_0, miner_acc, test_core_time::get_time());
  set_hard_fork_heights_to_generator(generator);
  DO_CALLBACK(events, "configure_core");
  REWIND_BLOCKS_N(events, blk_0r, blk_0, miner_acc, CURRENCY_MINED_MONEY_UNLOCK_WINDOW + 1);

  m_alice_start_balance = MK_TEST_COINS(111);
  MAKE_TX(events, tx_0, miner_acc, alice_acc, m_alice_start_balance, blk_0r);
  MAKE_NEXT_BLOCK_TX1(events, blk_1, blk_0r, miner_acc, tx_0);

  REWIND_BLOCKS_N(events, blk_1r, blk_1, miner_acc, WALLET_DEFAULT_TX_SPENDABLE_AGE);

  DO_CALLBACK(events, "c1");

  return true;
}

bool hard_fork_2_tx_receiver_in_wallet::c1(currency::core& c, size_t ev_index, const std::vector<test_event_entry>& events)
{
  bool r = false, stub_bool = false;
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 0, false, "Incorrect txs count in the pool: " << c.get_pool_transactions_count());
  std::shared_ptr<tools::wallet2> miner_wlt = init_playtime_test_wallet(events, c, m_accounts[MINER_ACC_IDX]);
  std::shared_ptr<tools::wallet2> alice_wlt = init_playtime_test_wallet(events, c, m_accounts[ALICE_ACC_IDX]);
  std::shared_ptr<tools::wallet2> bob_wlt   = init_playtime_test_wallet(events, c, m_accounts[BOB_ACC_IDX]);

  miner_wlt->refresh();
  CHECK_AND_ASSERT_MES(refresh_wallet_and_check_balance("", "Alice", alice_wlt, m_alice_start_balance), false, "");
  CHECK_AND_ASSERT_MES(refresh_wallet_and_check_balance("", "Bob", bob_wlt, 0), false, "");

  // wallet RPC server
  tools::wallet_rpc_server alice_wlt_rpc(*alice_wlt);
  epee::json_rpc::error je;
  tools::wallet_rpc_server::connection_context ctx;

  tools::wallet_public::COMMAND_RPC_TRANSFER::request req = AUTO_VAL_INIT(req);
  req.destinations.push_back(tools::wallet_public::transfer_destination { MK_TEST_COINS(1), m_accounts[MINER_ACC_IDX].get_public_address_str() }); 
  req.destinations.push_back(tools::wallet_public::transfer_destination { MK_TEST_COINS(1), m_accounts[BOB_ACC_IDX].get_public_address_str() });  // auditable address
  req.fee = TESTS_DEFAULT_FEE;
  req.hide_receiver = false; // just to emphasize, this is false by default

  LOG_PRINT_L0("Miner's address: " << m_accounts[MINER_ACC_IDX].get_public_address_str());
  LOG_PRINT_L0("Alice's address: " << m_accounts[ALICE_ACC_IDX].get_public_address_str());
  LOG_PRINT_L0("Bob's address: "   << m_accounts[BOB_ACC_IDX].get_public_address_str());

  tools::wallet_public::COMMAND_RPC_TRANSFER::response res = AUTO_VAL_INIT(res);
  
  r = alice_wlt_rpc.on_transfer(req, res, je, ctx);
  CHECK_AND_ASSERT_MES(r, false, "on_transfer failed");

  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 1, false, "Incorrect txs count in the pool: " << c.get_pool_transactions_count());

  crypto::hash tx_hash = null_hash;
  CHECK_AND_ASSERT_MES(epee::string_tools::hex_to_pod(res.tx_hash, tx_hash), false, "");
  
  transaction tx = AUTO_VAL_INIT(tx);
  CHECK_AND_ASSERT_MES(c.get_transaction(tx_hash, tx), false, "");

  // there should be one tx_receiver_old, as Bob's auditable address should not be supported for tx_receiver
  size_t count = count_type_in_variant_container<tx_receiver_old>(tx.extra);
  CHECK_AND_ASSERT_MES(count == 1, false, "tx_receiver_old count: " << count);

  count = count_type_in_variant_container<tx_receiver>(tx.extra);
  CHECK_AND_ASSERT_MES(count == 0, false, "tx_receiver count: " << count);

  r = mine_next_pow_block_in_playtime(m_accounts[MINER_ACC_IDX].get_public_address(), c);
  CHECK_AND_ASSERT_MES(r, false, "mine_next_pow_block_in_playtime failed");
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 0, false, "Incorrect txs count in the pool: " << c.get_pool_transactions_count());

  std::shared_ptr<wlt_lambda_on_transfer2_wrapper> l(new wlt_lambda_on_transfer2_wrapper(
    [&](const tools::wallet_public::wallet_transfer_info& wti, uint64_t balance, uint64_t unlocked_balance, uint64_t total_mined) -> bool {
      CHECK_AND_ASSERT_THROW_MES(!wti.is_income, "wti.is_income is " << wti.is_income);
      CHECK_AND_ASSERT_THROW_MES(wti.remote_addresses.size() == 2, "incorrect wti.remote_addresses.size() = " << wti.remote_addresses.size());
      CHECK_AND_ASSERT_THROW_MES(wti.remote_addresses.front() == m_accounts[MINER_ACC_IDX].get_public_address_str(), "wti.remote_addresses.front is incorrect");
      CHECK_AND_ASSERT_THROW_MES(wti.remote_addresses.back() == m_accounts[BOB_ACC_IDX].get_public_address_str(), "wti.remote_addresses.back is incorrect");
      return true;
    }
  ));
  alice_wlt->callback(l);

  CHECK_AND_ASSERT_MES(refresh_wallet_and_check_balance("", "Alice", alice_wlt, m_alice_start_balance - MK_TEST_COINS(2) - TESTS_DEFAULT_FEE), false, "");

  // mine blocks 23, 24, 25 to activate HF2
  r = mine_next_pow_blocks_in_playtime(m_accounts[MINER_ACC_IDX].get_public_address(), c, 3);
  CHECK_AND_ASSERT_MES(r, false, "mine_next_pow_blocks_in_playtime failed");
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 0, false, "Incorrect txs count in the pool: " << c.get_pool_transactions_count());

  miner_wlt->refresh();
  alice_wlt->refresh();

  // check again
  req.destinations.front().amount = MK_TEST_COINS(2);
  req.destinations.back().amount = MK_TEST_COINS(2);
  r = alice_wlt_rpc.on_transfer(req, res, je, ctx);
  CHECK_AND_ASSERT_MES(r, false, "on_transfer failed");
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 1, false, "Incorrect txs count in the pool: " << c.get_pool_transactions_count());

  tx_hash = null_hash;
  CHECK_AND_ASSERT_MES(epee::string_tools::hex_to_pod(res.tx_hash, tx_hash), false, "");
  CHECK_AND_ASSERT_MES(c.get_transaction(tx_hash, tx), false, "");

  // shouold be 2 tx_receiver as we passed HF2 and auditable addresses CAN be used with tx_receiver
  count = count_type_in_variant_container<tx_receiver>(tx.extra);
  CHECK_AND_ASSERT_MES(count == 2, false, "tx_receiver count = " << count);
  count = count_type_in_variant_container<tx_receiver_old>(tx.extra);
  CHECK_AND_ASSERT_MES(count == 0, false, "tx_receiver_old count = " << count);

  r = mine_next_pow_block_in_playtime(m_accounts[MINER_ACC_IDX].get_public_address(), c);
  CHECK_AND_ASSERT_MES(r, false, "mine_next_pow_block_in_playtime failed");
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 0, false, "Incorrect txs count in the pool: " << c.get_pool_transactions_count());

  std::shared_ptr<wlt_lambda_on_transfer2_wrapper> l2(new wlt_lambda_on_transfer2_wrapper(
    [&](const tools::wallet_public::wallet_transfer_info& wti, uint64_t balance, uint64_t unlocked_balance, uint64_t total_mined) -> bool {
      CHECK_AND_ASSERT_THROW_MES(!wti.is_income, "wti.is_income is " << wti.is_income);
      CHECK_AND_ASSERT_THROW_MES(wti.amount == MK_TEST_COINS(4), "incorrect wti.amount = " << print_money_brief(wti.amount));
      CHECK_AND_ASSERT_THROW_MES(wti.remote_addresses.size() == 2, "incorrect wti.remote_addresses.size() = " << wti.remote_addresses.size());
      CHECK_AND_ASSERT_THROW_MES(wti.remote_addresses.front() == m_accounts[MINER_ACC_IDX].get_public_address_str(), "wti.remote_addresses.front is incorrect");
      CHECK_AND_ASSERT_THROW_MES(wti.remote_addresses.back() == m_accounts[BOB_ACC_IDX].get_public_address_str(), "wti.remote_addresses.back is incorrect");
      return true;
    }
  ));
  alice_wlt->callback(l2);

  CHECK_AND_ASSERT_MES(refresh_wallet_and_check_balance("", "Alice", alice_wlt, m_alice_start_balance - MK_TEST_COINS(6) - TESTS_DEFAULT_FEE * 2), false, "");

  return true;
}

//------------------------------------------------------------------------------

hard_fork_2_tx_extra_alias_entry_in_wallet::hard_fork_2_tx_extra_alias_entry_in_wallet()
  : hard_fork_2_base_test(22)
{
  REGISTER_CALLBACK_METHOD(hard_fork_2_tx_extra_alias_entry_in_wallet, c1);
}

bool hard_fork_2_tx_extra_alias_entry_in_wallet::generate(std::vector<test_event_entry>& events) const
{
  m_accounts.resize(TOTAL_ACCS_COUNT);
  account_base& miner_acc = m_accounts[MINER_ACC_IDX]; miner_acc.generate();
  account_base& alice_acc = m_accounts[ALICE_ACC_IDX]; alice_acc.generate();
  account_base& bob_acc   = m_accounts[BOB_ACC_IDX];   bob_acc.generate();

  MAKE_GENESIS_BLOCK(events, blk_0, miner_acc, test_core_time::get_time());
  set_hard_fork_heights_to_generator(generator);
  DO_CALLBACK(events, "configure_core");
  REWIND_BLOCKS_N(events, blk_0r, blk_0, miner_acc, CURRENCY_MINED_MONEY_UNLOCK_WINDOW);

  uint64_t biggest_alias_reward = get_alias_coast_from_fee("a", TESTS_DEFAULT_FEE);
  MAKE_TX(events, tx_0, miner_acc, alice_acc, biggest_alias_reward + TESTS_DEFAULT_FEE, blk_0r);
  MAKE_TX(events, tx_1, miner_acc, alice_acc, biggest_alias_reward + TESTS_DEFAULT_FEE, blk_0r);
  MAKE_TX(events, tx_2, miner_acc, alice_acc, biggest_alias_reward + TESTS_DEFAULT_FEE, blk_0r);

  MAKE_NEXT_BLOCK_TX_LIST(events, blk_1, blk_0r, miner_acc, std::list<transaction>({ tx_0, tx_1, tx_2 }));

  REWIND_BLOCKS_N(events, blk_1r, blk_1, miner_acc, WALLET_DEFAULT_TX_SPENDABLE_AGE);

  DO_CALLBACK(events, "c1");

  return true;
}

bool hard_fork_2_tx_extra_alias_entry_in_wallet::c1(currency::core& c, size_t ev_index, const std::vector<test_event_entry>& events)
{
  bool r = false, stub_bool = false;
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 0, false, "Incorrect txs count in the pool: " << c.get_pool_transactions_count());
  std::shared_ptr<tools::wallet2> alice_wlt = init_playtime_test_wallet(events, c, m_accounts[ALICE_ACC_IDX]);
  std::shared_ptr<tools::wallet2> miner_wlt = init_playtime_test_wallet(events, c, m_accounts[MINER_ACC_IDX]);

  size_t blocks_fetched = 0;
  bool received_money;
  std::atomic<bool> atomic_false = ATOMIC_VAR_INIT(false);
  alice_wlt->refresh(blocks_fetched, received_money, atomic_false);
  CHECK_AND_ASSERT_MES(blocks_fetched == CURRENCY_MINED_MONEY_UNLOCK_WINDOW + 1 + WALLET_DEFAULT_TX_SPENDABLE_AGE, false, "Incorrect numbers of blocks fetched");

  extra_alias_entry ai = AUTO_VAL_INIT(ai);
  ai.m_alias = "alicealice";
  ai.m_address = m_accounts[ALICE_ACC_IDX].get_public_address();
  uint64_t alias_reward = get_alias_coast_from_fee(ai.m_alias, TESTS_DEFAULT_FEE);
  transaction res_tx = AUTO_VAL_INIT(res_tx);
  alice_wlt->request_alias_registration(ai, res_tx, TESTS_DEFAULT_FEE, alias_reward);

  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 1, false, "Incorrect txs count in the pool");

  // before the HF2 -- old structure should be present
  r = have_type_in_variant_container<extra_alias_entry_old>(res_tx.extra);
  CHECK_AND_ASSERT_MES(r, false, "extra_alias_entry_old is not found in extra");
  r = !have_type_in_variant_container<extra_alias_entry>(res_tx.extra);
  CHECK_AND_ASSERT_MES(r, false, "extra_alias_entry is found in extra");

  // mine few block to activate HF2
  r = mine_next_pow_blocks_in_playtime(m_accounts[MINER_ACC_IDX].get_public_address(), c, 3);
  CHECK_AND_ASSERT_MES(r, false, "mine_next_pow_block_in_playtime failed");
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 0, false, "Incorrect txs count in the pool");
  
  CHECK_AND_ASSERT_MES(c.get_current_blockchain_size() == CURRENCY_MINED_MONEY_UNLOCK_WINDOW + 1 + WALLET_DEFAULT_TX_SPENDABLE_AGE + 4, false, "Incorrect blockchain size");

  alice_wlt->refresh(blocks_fetched, received_money, atomic_false);
  CHECK_AND_ASSERT_MES(blocks_fetched == 3, false, "Incorrect numbers of blocks fetched");

  // update alias, change comment and address
  ai.m_text_comment = "Update!";
  ai.m_address = m_accounts[MINER_ACC_IDX].get_public_address();
  alice_wlt->request_alias_update(ai, res_tx, TESTS_DEFAULT_FEE, 0);

  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 1, false, "Incorrect txs count in the pool");
  r = mine_next_pow_block_in_playtime(m_accounts[MINER_ACC_IDX].get_public_address(), c);
  CHECK_AND_ASSERT_MES(r, false, "mine_next_pow_block_in_playtime failed");
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 0, false, "Incorrect txs count in the pool");

  // after HF2: extra_alias_entry should be here, not extra_alias_entry_old
  r = have_type_in_variant_container<extra_alias_entry>(res_tx.extra);
  CHECK_AND_ASSERT_MES(r, false, "extra_alias_entry is not found in extra");
  r = !have_type_in_variant_container<extra_alias_entry_old>(res_tx.extra);
  CHECK_AND_ASSERT_MES(r, false, "extra_alias_entry_old is found in extra");

  // make sure alias was updated indeed
  extra_alias_entry ai2 = AUTO_VAL_INIT(ai2);
  r = c.get_blockchain_storage().get_alias_info(ai.m_alias, ai2);
  CHECK_AND_ASSERT_MES(r, false, "get_alias_info failed");
  CHECK_AND_ASSERT_MES(ai2.m_text_comment == ai.m_text_comment && ai2.m_address == m_accounts[MINER_ACC_IDX].get_public_address(),
    false, "Incorrect alias info retunred by get_alias_info");

  return true;
}

//------------------------------------------------------------------------------

hard_fork_2_auditable_addresses_basics::hard_fork_2_auditable_addresses_basics()
  : hard_fork_2_base_test(23)
{
  REGISTER_CALLBACK_METHOD(hard_fork_2_auditable_addresses_basics, c1);
}

bool hard_fork_2_auditable_addresses_basics::generate(std::vector<test_event_entry>& events) const
{
  /*
    Test idea: make sure that:
      (1) before HF2 txs with mix_attr == 1 can be sent and then they are recognized by wallet;
      (2) before HF2 txs to an auditable address CAN be sent via wallet2::transfer()
      (3) after HF2 txs to an auditable address CAN be sent via wallet2::transfer()
  */

  m_accounts.resize(TOTAL_ACCS_COUNT);
  account_base& miner_acc = m_accounts[MINER_ACC_IDX]; miner_acc.generate();
  account_base& alice_acc = m_accounts[ALICE_ACC_IDX]; alice_acc.generate();
  account_base& bob_acc   = m_accounts[BOB_ACC_IDX];   bob_acc.generate(true); // Bob has auditable address

  MAKE_GENESIS_BLOCK(events, blk_0, miner_acc, test_core_time::get_time());
  set_hard_fork_heights_to_generator(generator);
  DO_CALLBACK(events, "configure_core");
  REWIND_BLOCKS_N(events, blk_0r, blk_0, miner_acc, CURRENCY_MINED_MONEY_UNLOCK_WINDOW);

  MAKE_TX(events, tx_0a, miner_acc, alice_acc, MK_TEST_COINS(11), blk_0r);
  MAKE_TX(events, tx_0b, miner_acc, alice_acc, MK_TEST_COINS(11), blk_0r);

  // tx_1 has outputs to an auditable address, it's allowed before HF2
  MAKE_TX(events, tx_1, miner_acc, bob_acc, MK_TEST_COINS(5), blk_0r);

  // make sure all Bob's outputs has mix_attr = 1
  for (auto& out : tx_1.vout)
  {
    if (out.amount != MK_TEST_COINS(5))
      continue; // skip change
    uint8_t mix_attr = boost::get<txout_to_key>(out.target).mix_attr;
    CHECK_AND_ASSERT_MES(mix_attr == CURRENCY_TO_KEY_OUT_FORCED_NO_MIX, false, "Incorrect mix_attr in tx_1: " << mix_attr);
  }
  
  MAKE_NEXT_BLOCK_TX_LIST(events, blk_1, blk_0r, miner_acc, std::list<transaction>({ tx_0a, tx_0b, tx_1 }));

  REWIND_BLOCKS_N(events, blk_1r, blk_1, miner_acc, WALLET_DEFAULT_TX_SPENDABLE_AGE);

  DO_CALLBACK(events, "c1");

  return true;
}

bool hard_fork_2_auditable_addresses_basics::c1(currency::core& c, size_t ev_index, const std::vector<test_event_entry>& events)
{
  bool r = false, stub_bool = false;
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 0, false, "Incorrect txs count in the pool: " << c.get_pool_transactions_count());
  std::shared_ptr<tools::wallet2> alice_wlt = init_playtime_test_wallet(events, c, m_accounts[ALICE_ACC_IDX]);
  std::shared_ptr<tools::wallet2> bob_wlt   = init_playtime_test_wallet(events, c, m_accounts[BOB_ACC_IDX]);

  CHECK_AND_ASSERT_MES(refresh_wallet_and_check_balance("", "Alice", alice_wlt, MK_TEST_COINS(22), false, CURRENCY_MINED_MONEY_UNLOCK_WINDOW + 1 + WALLET_DEFAULT_TX_SPENDABLE_AGE), false, "");
  CHECK_AND_ASSERT_MES(refresh_wallet_and_check_balance("", "Bob", bob_wlt, MK_TEST_COINS(5), false, CURRENCY_MINED_MONEY_UNLOCK_WINDOW + 1 + WALLET_DEFAULT_TX_SPENDABLE_AGE), false, "");

  // sending coins to an auditable address should not be allowed until HF2
  std::vector<tx_destination_entry> destination{tx_destination_entry(MK_TEST_COINS(1), bob_wlt->get_account().get_public_address())};
  transaction tx = AUTO_VAL_INIT(tx);
  alice_wlt->transfer(destination, 0, 0, TESTS_DEFAULT_FEE, empty_extra, empty_attachment, tx);

  // make sure all Bob's outputs has mix_attr = 1
  for (auto& out : tx.vout)
  {
    if (out.amount != MK_TEST_COINS(1))
      continue; // skip change
    uint8_t mix_attr = boost::get<txout_to_key>(out.target).mix_attr;
    CHECK_AND_ASSERT_MES(mix_attr == CURRENCY_TO_KEY_OUT_FORCED_NO_MIX, false, "Incorrect mix_attr in tx: " << mix_attr);
  }

  // mine a block to confirm the tx
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 1, false, "Incorrect txs count in the pool");
  r = mine_next_pow_block_in_playtime(m_accounts[MINER_ACC_IDX].get_public_address(), c);
  CHECK_AND_ASSERT_MES(r, false, "mine_next_pow_block_in_playtime failed");
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 0, false, "Incorrect txs count in the pool");

  // mine few block to activate HF2
  r = mine_next_pow_blocks_in_playtime(m_accounts[MINER_ACC_IDX].get_public_address(), c, 2);
  CHECK_AND_ASSERT_MES(r, false, "mine_next_pow_block_in_playtime failed");
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 0, false, "Incorrect txs count in the pool");

  CHECK_AND_ASSERT_MES(refresh_wallet_and_check_balance("", "Alice", alice_wlt, MK_TEST_COINS(20), false, 3), false, "");

  // repeat the transfer after HF2 (using the same destinations)
  alice_wlt->transfer(destination, 0, 0, TESTS_DEFAULT_FEE, empty_extra, empty_attachment, tx);

  // make sure all Bob's outputs has mix_attr = 1
  for (auto& out : tx.vout)
  {
    if (out.amount != MK_TEST_COINS(1))
      continue; // skip change
    uint8_t mix_attr = boost::get<txout_to_key>(out.target).mix_attr;
    CHECK_AND_ASSERT_MES(mix_attr == CURRENCY_TO_KEY_OUT_FORCED_NO_MIX, false, "Incorrect mix_attr in tx: " << mix_attr);
  }

  // mine a block to confirm the tx
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 1, false, "Incorrect txs count in the pool");
  r = mine_next_pow_block_in_playtime(m_accounts[MINER_ACC_IDX].get_public_address(), c);
  CHECK_AND_ASSERT_MES(r, false, "mine_next_pow_block_in_playtime failed");
  CHECK_AND_ASSERT_MES(c.get_pool_transactions_count() == 0, false, "Incorrect txs count in the pool");

  // make sure the funds were received
  CHECK_AND_ASSERT_MES(refresh_wallet_and_check_balance("", "Bob", bob_wlt, MK_TEST_COINS(5 + 1 + 1), false, 3 + 1), false, "");

  return true;
}


//------------------------------------------------------------------------------

hard_fork_2_no_new_structures_before_hf::hard_fork_2_no_new_structures_before_hf()
  : hard_fork_2_base_test(16)
{
  REGISTER_CALLBACK_METHOD(hard_fork_2_no_new_structures_before_hf, c1);
}

bool hard_fork_2_no_new_structures_before_hf::generate(std::vector<test_event_entry>& events) const
{
  bool r = false;
  m_accounts.resize(TOTAL_ACCS_COUNT);
  account_base& miner_acc = m_accounts[MINER_ACC_IDX]; miner_acc.generate();
  account_base& alice_acc = m_accounts[ALICE_ACC_IDX]; alice_acc.generate();

  MAKE_GENESIS_BLOCK(events, blk_0, miner_acc, test_core_time::get_time());
  set_hard_fork_heights_to_generator(generator);
  DO_CALLBACK(events, "configure_core");
  REWIND_BLOCKS_N(events, blk_0r, blk_0, miner_acc, CURRENCY_MINED_MONEY_UNLOCK_WINDOW);

  std::vector<currency::extra_v> extra;


  // extra with tx_payer -- not allowed before HF2
  tx_payer payer = AUTO_VAL_INIT(payer);
  payer.acc_addr = miner_acc.get_public_address();
  extra.push_back(payer);
  MAKE_TX_MIX_ATTR_EXTRA(events, tx_0, miner_acc, alice_acc, MK_TEST_COINS(1), 0, blk_0r, 0, extra, true);

  // blk_1b_1 is invalid as containing tx_0
  DO_CALLBACK(events, "mark_invalid_block");
  MAKE_NEXT_BLOCK_TX1(events, blk_1b, blk_0r, miner_acc, tx_0);


  // extra with tx_payer_old -- okay
  extra.clear();
  tx_payer_old payer_old = AUTO_VAL_INIT(payer_old);
  payer_old.acc_addr = miner_acc.get_public_address().to_old();
  extra.push_back(payer_old);
  MAKE_TX_MIX_ATTR_EXTRA(events, tx_0_old, miner_acc, alice_acc, MK_TEST_COINS(1), 0, blk_0r, 0, extra, true);
  MAKE_NEXT_BLOCK_TX1(events, blk_1, blk_0r, miner_acc, tx_0_old);


  // extra with tx_receiver -- not allowed before HF2
  extra.clear();
  tx_receiver receiver = AUTO_VAL_INIT(receiver);
  receiver.acc_addr = miner_acc.get_public_address();
  extra.push_back(receiver);
  MAKE_TX_MIX_ATTR_EXTRA(events, tx_1, miner_acc, alice_acc, MK_TEST_COINS(1), 0, blk_1, 0, extra, true);

  // blk_1b_2 is invalid as containing tx_1
  DO_CALLBACK(events, "mark_invalid_block");
  MAKE_NEXT_BLOCK_TX1(events, blk_2b, blk_1, miner_acc, tx_1);


  // extra with tx_receiver_old -- okay
  extra.clear();
  tx_receiver_old receiver_old = AUTO_VAL_INIT(receiver_old);
  receiver_old.acc_addr = miner_acc.get_public_address().to_old();
  extra.push_back(receiver_old);
  MAKE_TX_MIX_ATTR_EXTRA(events, tx_1_old, miner_acc, alice_acc, MK_TEST_COINS(1), 0, blk_1, 0, extra, true);
  MAKE_NEXT_BLOCK_TX1(events, blk_2, blk_1, miner_acc, tx_1_old);


  // extra with extra_alias_entry -- not allowed before HF2
  extra_alias_entry alias_entry = AUTO_VAL_INIT(alias_entry);
  alias_entry.m_address = miner_acc.get_public_address();
  alias_entry.m_alias = "minerminer";

  std::list<transaction> tx_set;
  r = put_alias_via_tx_to_list(events, tx_set, blk_2, miner_acc, alias_entry, generator);
  CHECK_AND_ASSERT_MES(r, false, "put_alias_via_tx_to_list failed");
  transaction tx_2 = tx_set.front();

  // blk_1b_3 is invalid as containing tx_2
  DO_CALLBACK(events, "mark_invalid_block");
  MAKE_NEXT_BLOCK_TX1(events, blk_3b, blk_2, miner_acc, tx_2);


  // extra with extra_alias_entry_old -- okay
  extra_alias_entry_old alias_entry_old = AUTO_VAL_INIT(alias_entry_old);
  alias_entry_old.m_address = alice_acc.get_public_address().to_old();
  alias_entry_old.m_alias = "alicealice";

  tx_set.clear();
  r = put_alias_via_tx_to_list(events, tx_set, blk_2, miner_acc, alias_entry_old, generator);
  CHECK_AND_ASSERT_MES(r, false, "put_alias_via_tx_to_list failed");
  transaction tx_2_old = tx_set.front();
  MAKE_NEXT_BLOCK_TX1(events, blk_3, blk_2, miner_acc, tx_2_old);


  // activate HF2
  MAKE_NEXT_BLOCK(events, blk_4, blk_3, miner_acc);
  MAKE_NEXT_BLOCK(events, blk_5, blk_4, miner_acc);
  MAKE_NEXT_BLOCK(events, blk_6, blk_5, miner_acc);
  MAKE_NEXT_BLOCK(events, blk_7, blk_6, miner_acc);


  // tx_0 with tx_payer should be accepted after HF2
  MAKE_NEXT_BLOCK_TX1(events, blk_8, blk_7, miner_acc, tx_0);

  // tx_1 with tx_receiver should be accepted after HF2
  MAKE_NEXT_BLOCK_TX1(events, blk_9, blk_8, miner_acc, tx_1);

  // tx_2 with extra_alias_entry should be accepted after HF2
  MAKE_NEXT_BLOCK_TX1(events, blk_10, blk_9, miner_acc, tx_2);

  // check aliases
  DO_CALLBACK(events, "c1");

  return true;
}

bool hard_fork_2_no_new_structures_before_hf::c1(currency::core& c, size_t ev_index, const std::vector<test_event_entry>& events)
{
  extra_alias_entry_base ai = AUTO_VAL_INIT(ai);
  bool r = c.get_blockchain_storage().get_alias_info("alicealice", ai);
  CHECK_AND_ASSERT_MES(r, false, "failed to get alias");
  CHECK_AND_ASSERT_MES(ai.m_address == m_accounts[ALICE_ACC_IDX].get_public_address(), false, "invalid address for alicealice alias");

  ai = AUTO_VAL_INIT(ai);
  r = c.get_blockchain_storage().get_alias_info("minerminer", ai);
  CHECK_AND_ASSERT_MES(r, false, "failed to get alias minerminer");
  CHECK_AND_ASSERT_MES(ai.m_address == m_accounts[MINER_ACC_IDX].get_public_address(), false, "invalid address for minerminer alias");

  return true;
}