cmake_minimum_required(VERSION 3.21)
project(lobx_exchange LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LOB_REPO "${CMAKE_CURRENT_SOURCE_DIR}/third_party/limit-order-book"
    CACHE PATH "Path to the limit-order-book source tree")

if(NOT EXISTS "${LOB_REPO}/cpp/include/lob/book_core.hpp" OR
   NOT EXISTS "${LOB_REPO}/cpp/src/book_core.cpp" OR
   NOT EXISTS "${LOB_REPO}/cpp/src/price_levels.cpp")
  message(FATAL_ERROR
    "LOB_REPO does not contain the required limit-order-book sources: ${LOB_REPO}. "
    "Use the vendored third_party copy or configure -DLOB_REPO=/path/to/limit-order-book.")
endif()

add_library(lob_core_embedded STATIC
  ${LOB_REPO}/cpp/src/book_core.cpp
  ${LOB_REPO}/cpp/src/price_levels.cpp
)
target_include_directories(lob_core_embedded PUBLIC ${LOB_REPO}/cpp/include)

set(LOBX_CORE_SOURCES
  cpp/src/asset_registry.cpp
  cpp/src/market_registry.cpp
  cpp/src/account_ledger.cpp
  cpp/src/risk_engine.cpp
  cpp/src/event_store.cpp
  cpp/src/market_engine.cpp
  cpp/src/kline_aggregator.cpp
  cpp/src/position_engine.cpp
  cpp/src/exchange.cpp
  cpp/src/agents/agent_factory.cpp
  cpp/src/agents/builtins/static_market_maker_agent.cpp
  cpp/src/agents/builtins/noise_trader_agent.cpp
  cpp/src/agents/builtins/momentum_follower_agent.cpp
  cpp/src/agents/builtins/mean_reverter_agent.cpp
  cpp/src/agents/builtins/whale_sweeper_agent.cpp
  cpp/src/simulation/scenario_config.cpp
  cpp/src/simulation/strategy_metrics.cpp
  cpp/src/simulation/parameter_sweep.cpp
  cpp/src/simulation/strategy_ranking.cpp
  cpp/src/simulation/multi_seed_evaluation.cpp
  cpp/src/simulation/research_runner.cpp
  cpp/src/simulation/config_loader.cpp
  cpp/src/simulation/file_export.cpp
  cpp/src/simulation/research_cli.cpp
  cpp/src/simulation/market_environment.cpp
  cpp/src/simulation/agent_population.cpp
  cpp/src/simulation/market_phenomenon.cpp
  cpp/src/simulation/emergence_metrics.cpp
  cpp/src/simulation/emergence_runner.cpp
  cpp/src/simulation/emergence_export.cpp
  cpp/src/simulation/action_queue.cpp
  cpp/src/simulation/agent_state_store.cpp
  cpp/src/simulation/simulation_event.cpp
  cpp/src/simulation/agent_runtime.cpp
  cpp/src/simulation/price_series_recorder.cpp
  cpp/src/simulation/diagnostic_recorder.cpp
  cpp/src/simulation/diagnostic_experiment.cpp
  cpp/src/simulation/mesa_agent_sim.cpp
)

add_library(lobx_core STATIC ${LOBX_CORE_SOURCES})
target_include_directories(lobx_core PUBLIC cpp/include ${LOB_REPO}/cpp/include)
target_link_libraries(lobx_core PUBLIC lob_core_embedded)

target_compile_options(lobx_core PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(lob_core_embedded PRIVATE -Wall -Wextra -Wpedantic)

add_library(lobx_core_testing STATIC ${LOBX_CORE_SOURCES})
target_include_directories(lobx_core_testing PUBLIC cpp/include ${LOB_REPO}/cpp/include)
target_link_libraries(lobx_core_testing PUBLIC lob_core_embedded)
target_compile_options(lobx_core_testing PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(lobx_core_testing PUBLIC LOBX_TESTING=1)

add_executable(lobx_smoke cpp/tests/smoke.cpp)
target_link_libraries(lobx_smoke PRIVATE lobx_core)

add_executable(lobx_simulation_kline cpp/tests/simulation_kline.cpp)
target_link_libraries(lobx_simulation_kline PRIVATE lobx_core)

add_executable(lobx_perpetual_position cpp/tests/perpetual_position.cpp)
target_link_libraries(lobx_perpetual_position PRIVATE lobx_core)

function(add_lobx_test_target target_name)
  add_executable(${target_name}
    cpp/tests/test_helpers/test_main.cpp
    ${ARGN}
  )
  if("${target_name}" STREQUAL "lobx_regression_tests")
    target_link_libraries(${target_name} PRIVATE lobx_core_testing)
  else()
    target_link_libraries(${target_name} PRIVATE lobx_core)
  endif()
  target_include_directories(${target_name} PRIVATE cpp/tests)
endfunction()

add_lobx_test_target(lobx_unit_tests
  cpp/tests/unit/order_flags_test.cpp
  cpp/tests/unit/price_qty_validation_test.cpp
  cpp/tests/unit/book_core_test.cpp
  cpp/tests/unit/risk_engine_test.cpp
  cpp/tests/unit/risk_engine_regression.cpp
  cpp/tests/unit/risk_engine_boundary_regression.cpp
  cpp/tests/unit/position_engine_test.cpp
  cpp/tests/unit/event_store_regression.cpp
  cpp/tests/unit/account_ledger_adversarial_regression.cpp
)

add_lobx_test_target(lobx_regression_tests
  cpp/tests/regression/foundation_regression.cpp
  cpp/tests/regression/fok_stp_regression.cpp
  cpp/tests/regression/halt_regression.cpp
  cpp/tests/regression/perp_margin_regression.cpp
  cpp/tests/regression/pnl_settlement_regression.cpp
  cpp/tests/regression/order_lifecycle_regression.cpp
  cpp/tests/regression/market_engine_settlement_regression.cpp
  cpp/tests/regression/market_engine_spot_fee_regression.cpp
  cpp/tests/regression/market_engine_perp_margin_regression.cpp
  cpp/tests/regression/market_engine_reduce_only_regression.cpp
  cpp/tests/regression/market_engine_order_flags_regression.cpp
  cpp/tests/regression/market_engine_reduce_only_purge_regression.cpp
  cpp/tests/regression/market_engine_retry_idempotency_regression.cpp
  cpp/tests/regression/market_engine_seen_order_id_rollback.cpp
  cpp/tests/regression/market_engine_scalar_map_rollback.cpp
  cpp/tests/regression/market_engine_transaction_fault_injection.cpp
  cpp/tests/regression/liquidation_realism.cpp
  cpp/tests/regression/market_phenomena_detection.cpp
  cpp/tests/regression/market_phenomena_batch2.cpp
  cpp/tests/regression/robot_market_scenarios.cpp
  cpp/tests/regression/exchange_retention_options.cpp
  cpp/tests/regression/market_engine_fee_account_regression.cpp
  cpp/tests/regression/market_engine_cancel_consistency_regression.cpp
  cpp/tests/regression/market_engine_perp_atomicity_edges.cpp
  cpp/tests/regression/market_microstructure_regression.cpp
  cpp/tests/test_exchange_matching.cpp
  cpp/tests/test_exchange_settlement.cpp
  cpp/tests/test_order_book.cpp
  cpp/tests/regression/risk_engine_fee_regression.cpp
  cpp/tests/regression/event_store_commit_regression.cpp
)

add_lobx_test_target(lobx_integration_tests
  cpp/tests/integration/spot_matching_integration.cpp
  cpp/tests/integration/spot_settlement_integration.cpp
  cpp/tests/integration/perp_trading_integration.cpp
  cpp/tests/integration/cancel_lifecycle_integration.cpp
  cpp/tests/integration/market_state_integration.cpp
  cpp/tests/integration/exchange_spot_consistency.cpp
  cpp/tests/integration/exchange_perp_consistency.cpp
  cpp/tests/integration/exchange_event_history_consistency.cpp
  cpp/tests/integration/exchange_kline_trade_commit_consistency.cpp
  cpp/tests/integration/exchange_replay_consistency.cpp
  cpp/tests/integration/market_data_consistency.cpp
  cpp/tests/integration/kline_commit_consistency.cpp
)

add_lobx_test_target(lobx_property_tests
  cpp/tests/property/spot_book_property_test.cpp
  cpp/tests/property/order_flags_property_test.cpp
  cpp/tests/property/cancel_sequence_property_test.cpp
  cpp/tests/property/fok_ioc_stp_property_test.cpp
  cpp/tests/property/perp_position_property_test.cpp
  cpp/tests/property/market_engine_invariants.cpp
  cpp/tests/property/system_conservation_invariants.cpp
  cpp/tests/property/order_book_priority_invariants.cpp
  cpp/tests/property/fee_pnl_conservation_invariants.cpp
  cpp/tests/property/random_market_sequence_invariants.cpp
)

add_lobx_test_target(lobx_random_bot_invariants_tests
  cpp/tests/property/random_bot_simulation_invariants.cpp
)

add_lobx_test_target(lobx_simulation_tests
  cpp/tests/simulation/simulation_clock_regression.cpp
  cpp/tests/simulation/bot_scenario_smoke.cpp
)

add_lobx_test_target(lobx_mesa_agent_sim_tests
  cpp/tests/simulation/mesa_agent_sim_tests.cpp
  cpp/tests/test_mesa_agent_sim.cpp
  cpp/tests/test_mesa_agents.cpp
  cpp/tests/test_price_dynamics.cpp
  cpp/tests/test_realtime_events.cpp
)

add_lobx_test_target(lobx_bot_harness_tests
  cpp/tests/simulation/bot_strategy_harness.cpp
)

add_lobx_test_target(lobx_bot_metrics_tests
  cpp/tests/simulation/bot_strategy_metrics.cpp
)

add_lobx_test_target(lobx_multi_bot_tests
  cpp/tests/simulation/multi_bot_scenario.cpp
)

add_lobx_test_target(lobx_bot_boundary_tests
  cpp/tests/simulation/bot_behavior_boundaries.cpp
)

add_lobx_test_target(lobx_scenario_config_tests
  cpp/tests/simulation/scenario_config_tests.cpp
)

add_lobx_test_target(lobx_parameter_sweep_tests
  cpp/tests/simulation/parameter_sweep_tests.cpp
)

add_lobx_test_target(lobx_strategy_ranking_tests
  cpp/tests/simulation/strategy_ranking_tests.cpp
)

add_lobx_test_target(lobx_multi_seed_tests
  cpp/tests/simulation/multi_seed_strategy_evaluation.cpp
)

add_lobx_test_target(lobx_research_runner_tests
  cpp/tests/simulation/research_runner_tests.cpp
)

add_lobx_test_target(lobx_research_export_tests
  cpp/tests/simulation/research_export_tests.cpp
)

add_lobx_test_target(lobx_research_workflow_tests
  cpp/tests/simulation/research_workflow_integration.cpp
)

add_lobx_test_target(lobx_config_loader_tests
  cpp/tests/simulation/config_loader_tests.cpp
)

add_lobx_test_target(lobx_file_export_tests
  cpp/tests/simulation/file_export_tests.cpp
)

add_lobx_test_target(lobx_research_config_workflow_tests
  cpp/tests/simulation/research_config_workflow_tests.cpp
)

add_lobx_test_target(lobx_research_runner_cli_tests
  cpp/tests/simulation/research_runner_cli_tests.cpp
)

add_lobx_test_target(lobx_market_environment_tests
  cpp/tests/simulation/market_environment_tests.cpp
)

add_lobx_test_target(lobx_agent_population_tests
  cpp/tests/simulation/agent_population_tests.cpp
)

add_lobx_test_target(lobx_agent_runtime_tests
  cpp/tests/agents/agent_runtime_tests.cpp
)

add_lobx_test_target(lobx_agent_price_impact_tests
  cpp/tests/agents/agent_price_impact_tests.cpp
)

add_lobx_test_target(lobx_exchange_accounting_tests
  cpp/tests/exchange/exchange_accounting_invariants_tests.cpp
)

add_lobx_test_target(lobx_agent_pnl_invariants_tests
  cpp/tests/agents/agent_pnl_invariants_tests.cpp
)

add_lobx_test_target(lobx_agent_long_diagnostic_tests
  cpp/tests/agents/agent_long_diagnostic_tests.cpp
)

add_lobx_test_target(lobx_emergence_metrics_tests
  cpp/tests/simulation/emergence_metrics_tests.cpp
)

add_lobx_test_target(lobx_emergence_runner_tests
  cpp/tests/simulation/emergence_runner_tests.cpp
)

add_lobx_test_target(lobx_market_emergence_scenarios
  cpp/tests/simulation/market_emergence_scenarios.cpp
)

add_lobx_test_target(lobx_simulate_fill_tests
  cpp/tests/integration/simulate_fill_execution_consistency.cpp
)

add_lobx_test_target(lobx_perp_risk_tests
  cpp/tests/perp_mark_price_unrealized_pnl.cpp
  cpp/tests/perp_risk_tiers_maintenance_margin.cpp
  cpp/tests/perp_liquidation.cpp
)

add_lobx_test_target(lobx_perp_fee_tests
  cpp/tests/perp_fees.cpp
)

add_lobx_test_target(lobx_perp_funding_tests
  cpp/tests/perp_funding.cpp
)

add_lobx_test_target(lobx_perp_simulate_fill_tests
  cpp/tests/perp_simulate_fill.cpp
)

add_lobx_test_target(lobx_perp_insurance_adl_tests
  cpp/tests/perp_insurance_adl.cpp
)

add_lobx_test_target(lobx_perp_order_types_tests
  cpp/tests/perp_order_types.cpp
)

add_lobx_test_target(lobx_perp_trigger_orders_tests
  cpp/tests/perp_trigger_orders.cpp
)

add_executable(lobx_simulator cpp/apps/lobx_simulator.cpp)
target_link_libraries(lobx_simulator PRIVATE lobx_core)

add_executable(lobx_realtime_simulator cpp/apps/lobx_realtime_simulator.cpp)
target_link_libraries(lobx_realtime_simulator PRIVATE lobx_core)

add_executable(lobx_step_exchange cpp/apps/lobx_step_exchange.cpp)
target_link_libraries(lobx_step_exchange PRIVATE lobx_core)

add_executable(lobx_mesa_agent_simulator cpp/apps/lobx_mesa_agent_simulator.cpp)
target_link_libraries(lobx_mesa_agent_simulator PRIVATE lobx_core)

add_executable(lobx_bench_perp cpp/bench/bench_perp.cpp)
target_link_libraries(lobx_bench_perp PRIVATE lobx_core)

add_executable(lobx_bench_exchange cpp/bench/bench_exchange.cpp)
target_link_libraries(lobx_bench_exchange PRIVATE lobx_core)

add_executable(lobx_bench_agents cpp/bench/bench_agents.cpp)
target_link_libraries(lobx_bench_agents PRIVATE lobx_core)

add_executable(lobx_bench_events cpp/bench/bench_events.cpp)
target_link_libraries(lobx_bench_events PRIVATE lobx_core)

add_executable(lobx_bench_mesa_agent_sim cpp/bench/bench_mesa_agent_sim.cpp)
target_link_libraries(lobx_bench_mesa_agent_sim PRIVATE lobx_core)

add_executable(lobx_research_runner cpp/tools/lobx_research_runner.cpp)
target_link_libraries(lobx_research_runner PRIVATE lobx_core)

enable_testing()
add_test(NAME lobx_smoke COMMAND lobx_smoke)
add_test(NAME lobx_simulation_kline COMMAND lobx_simulation_kline)
add_test(NAME lobx_perpetual_position COMMAND lobx_perpetual_position)
add_test(NAME lobx_unit_tests COMMAND lobx_unit_tests)
add_test(NAME lobx_regression_tests COMMAND lobx_regression_tests)
add_test(NAME lobx_integration_tests COMMAND lobx_integration_tests)
add_test(NAME lobx_property_tests COMMAND lobx_property_tests)
add_test(NAME lobx_random_bot_invariants_tests COMMAND lobx_random_bot_invariants_tests)
add_test(NAME lobx_simulation_tests COMMAND lobx_simulation_tests)
add_test(NAME lobx_mesa_agent_sim_tests COMMAND lobx_mesa_agent_sim_tests)
add_test(NAME lobx_bot_harness_tests COMMAND lobx_bot_harness_tests)
add_test(NAME lobx_bot_metrics_tests COMMAND lobx_bot_metrics_tests)
add_test(NAME lobx_multi_bot_tests COMMAND lobx_multi_bot_tests)
add_test(NAME lobx_bot_boundary_tests COMMAND lobx_bot_boundary_tests)
add_test(NAME lobx_scenario_config_tests COMMAND lobx_scenario_config_tests)
add_test(NAME lobx_parameter_sweep_tests COMMAND lobx_parameter_sweep_tests)
add_test(NAME lobx_strategy_ranking_tests COMMAND lobx_strategy_ranking_tests)
add_test(NAME lobx_multi_seed_tests COMMAND lobx_multi_seed_tests)
add_test(NAME lobx_research_runner_tests COMMAND lobx_research_runner_tests)
add_test(NAME lobx_research_export_tests COMMAND lobx_research_export_tests)
add_test(NAME lobx_research_workflow_tests COMMAND lobx_research_workflow_tests)
add_test(NAME lobx_config_loader_tests COMMAND lobx_config_loader_tests)
add_test(NAME lobx_file_export_tests COMMAND lobx_file_export_tests)
add_test(NAME lobx_research_config_workflow_tests COMMAND lobx_research_config_workflow_tests)
add_test(NAME lobx_research_runner_cli_tests COMMAND lobx_research_runner_cli_tests)
add_test(NAME lobx_market_environment_tests COMMAND lobx_market_environment_tests)
add_test(NAME lobx_agent_population_tests COMMAND lobx_agent_population_tests)
add_test(NAME lobx_agent_runtime_tests COMMAND lobx_agent_runtime_tests)
add_test(NAME lobx_agent_price_impact_tests COMMAND lobx_agent_price_impact_tests)
add_test(NAME lobx_exchange_accounting_tests COMMAND lobx_exchange_accounting_tests)
add_test(NAME lobx_agent_pnl_invariants_tests COMMAND lobx_agent_pnl_invariants_tests)
add_test(NAME lobx_agent_long_diagnostic_tests COMMAND lobx_agent_long_diagnostic_tests)
add_test(NAME lobx_emergence_metrics_tests COMMAND lobx_emergence_metrics_tests)
add_test(NAME lobx_emergence_runner_tests COMMAND lobx_emergence_runner_tests)
add_test(NAME lobx_market_emergence_scenarios COMMAND lobx_market_emergence_scenarios)
add_test(NAME lobx_simulate_fill_tests COMMAND lobx_simulate_fill_tests)
add_test(NAME lobx_perp_risk_tests COMMAND lobx_perp_risk_tests)
add_test(NAME lobx_perp_fee_tests COMMAND lobx_perp_fee_tests)
add_test(NAME lobx_perp_funding_tests COMMAND lobx_perp_funding_tests)
add_test(NAME lobx_perp_simulate_fill_tests COMMAND lobx_perp_simulate_fill_tests)
add_test(NAME lobx_perp_insurance_adl_tests COMMAND lobx_perp_insurance_adl_tests)
add_test(NAME lobx_perp_order_types_tests COMMAND lobx_perp_order_types_tests)
add_test(NAME lobx_perp_trigger_orders_tests COMMAND lobx_perp_trigger_orders_tests)

set_tests_properties(lobx_unit_tests PROPERTIES LABELS "unit")
set_tests_properties(lobx_regression_tests PROPERTIES LABELS "regression;foundation;atomicity;market_boundary")
set_tests_properties(lobx_integration_tests PROPERTIES LABELS "integration")
set_tests_properties(lobx_property_tests PROPERTIES LABELS "property;slow;randomized")
set_tests_properties(lobx_random_bot_invariants_tests PROPERTIES LABELS "property;simulation;bot_harness;random_invariants")
set_tests_properties(lobx_simulation_tests PROPERTIES LABELS "simulation")
set_tests_properties(lobx_mesa_agent_sim_tests PROPERTIES LABELS "simulation;mesa;agent_sim;realtime")
set_tests_properties(lobx_bot_harness_tests PROPERTIES LABELS "simulation;bot_harness")
set_tests_properties(lobx_bot_metrics_tests PROPERTIES LABELS "simulation;metrics")
set_tests_properties(lobx_multi_bot_tests PROPERTIES LABELS "simulation;multi_bot")
set_tests_properties(lobx_bot_boundary_tests PROPERTIES LABELS "simulation;bot_harness;security_boundary")
set_tests_properties(lobx_scenario_config_tests PROPERTIES LABELS "simulation;scenario")
set_tests_properties(lobx_parameter_sweep_tests PROPERTIES LABELS "simulation;parameter_sweep")
set_tests_properties(lobx_strategy_ranking_tests PROPERTIES LABELS "simulation;strategy_ranking")
set_tests_properties(lobx_multi_seed_tests PROPERTIES LABELS "simulation;multi_seed")
set_tests_properties(lobx_research_runner_tests PROPERTIES LABELS "simulation;research_runner;scenario;parameter_sweep;strategy_ranking;multi_seed")
set_tests_properties(lobx_research_export_tests PROPERTIES LABELS "simulation;research_runner;export")
set_tests_properties(lobx_research_workflow_tests PROPERTIES LABELS "simulation;research_runner;parameter_sweep;strategy_ranking;multi_seed;export")
set_tests_properties(lobx_config_loader_tests PROPERTIES LABELS "simulation;research_runner;config_loader")
set_tests_properties(lobx_file_export_tests PROPERTIES LABELS "simulation;research_runner;file_export;export")
set_tests_properties(lobx_research_config_workflow_tests PROPERTIES LABELS "simulation;research_runner;research_workflow;config_loader;file_export")
set_tests_properties(lobx_research_runner_cli_tests PROPERTIES LABELS "simulation;research_runner;research_cli")
set_tests_properties(lobx_market_environment_tests PROPERTIES LABELS "simulation;emergence;market_environment")
set_tests_properties(lobx_agent_population_tests PROPERTIES LABELS "simulation;emergence;agent_population")
set_tests_properties(lobx_agent_runtime_tests PROPERTIES LABELS "simulation;agents;agent_runtime")
set_tests_properties(lobx_agent_price_impact_tests PROPERTIES LABELS "simulation;agents;price_impact;integration")
set_tests_properties(lobx_exchange_accounting_tests PROPERTIES LABELS "exchange;accounting;invariants")
set_tests_properties(lobx_agent_pnl_invariants_tests PROPERTIES LABELS "simulation;agents;pnl;accounting")
set_tests_properties(lobx_agent_long_diagnostic_tests PROPERTIES LABELS "simulation;agents;diagnostic;long")
set_tests_properties(lobx_emergence_metrics_tests PROPERTIES LABELS "simulation;emergence;emergence_metrics")
set_tests_properties(lobx_emergence_runner_tests PROPERTIES LABELS "simulation;emergence;emergence_runner")
set_tests_properties(lobx_market_emergence_scenarios PROPERTIES LABELS "simulation;emergence;market_emergence")
set_tests_properties(lobx_simulate_fill_tests PROPERTIES LABELS "integration;simulate_fill;microstructure")
set_tests_properties(lobx_perp_risk_tests PROPERTIES LABELS "correctness;perp;mark_price;risk;liquidation")
set_tests_properties(lobx_perp_fee_tests PROPERTIES LABELS "correctness;perp;fees")
set_tests_properties(lobx_perp_funding_tests PROPERTIES LABELS "correctness;perp;funding")
set_tests_properties(lobx_perp_simulate_fill_tests PROPERTIES LABELS "correctness;perp;simulate_fill;dry_run")
set_tests_properties(lobx_perp_insurance_adl_tests PROPERTIES LABELS "correctness;perp;insurance;bad_debt;adl;liquidation")
set_tests_properties(lobx_perp_order_types_tests PROPERTIES LABELS "correctness;perp;market_order;order_types")
set_tests_properties(lobx_perp_trigger_orders_tests PROPERTIES LABELS "correctness;perp;trigger;tp_sl")
