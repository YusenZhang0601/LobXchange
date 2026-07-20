#include "lobx/simulation/mesa_agent_sim.hpp"

#include "lobx/agents/agent_factory.hpp"
#include "lobx/simulation/agent_runtime.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

struct Args {
  lobx::sim::MesaAgentSimConfig config;
  bool jsonl{false};
  bool use_agent_runtime{false};
  int sleep_ms{0};
  std::string actions_out;
  std::string events_out;
};

void usage() {
  std::cout << "Usage: lobx_mesa_agent_simulator [options]\n"
            << "  --steps N              simulation steps, default 80\n"
            << "  --seed N               random seed, default 42\n"
            << "  --reference-price N    reference price, default 100\n"
            << "  --makers N             market maker count, default 4\n"
            << "  --noise N              noise trader count, default 6\n"
            << "  --momentum N           momentum trader count, default 2\n"
            << "  --mean-reversion N     mean reversion trader count, default 2\n"
            << "  --whales N             whale sweeper count, default 1\n"
            << "  --sleep-ms N           sleep after each JSONL step, default 0\n"
            << "  --jsonl                emit agent_mix/trade/candle/stats lines while running\n"
            << "  --use-agent-runtime    run through new decoupled AgentRuntime path\n"
            << "  --actions-out PATH     write AgentRuntime action JSONL\n"
            << "  --events-out PATH      write AgentRuntime event JSONL\n";
}

int parse_int_arg(char** argv, int& i, int argc, const std::string& name) {
  if (i + 1 >= argc) throw std::runtime_error(name + " requires a value");
  return std::stoi(argv[++i]);
}

uint64_t parse_u64_arg(char** argv, int& i, int argc, const std::string& name) {
  if (i + 1 >= argc) throw std::runtime_error(name + " requires a value");
  return std::stoull(argv[++i]);
}

Args parse_args(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--steps") args.config.steps = parse_int_arg(argv, i, argc, arg);
    else if (arg == "--seed") args.config.seed = parse_u64_arg(argv, i, argc, arg);
    else if (arg == "--reference-price") args.config.reference_price = parse_int_arg(argv, i, argc, arg);
    else if (arg == "--makers") args.config.agents.market_makers = parse_int_arg(argv, i, argc, arg);
    else if (arg == "--noise") args.config.agents.noise_traders = parse_int_arg(argv, i, argc, arg);
    else if (arg == "--momentum") args.config.agents.momentum = parse_int_arg(argv, i, argc, arg);
    else if (arg == "--mean-reversion") args.config.agents.mean_reversion = parse_int_arg(argv, i, argc, arg);
    else if (arg == "--whales") args.config.agents.whale_sweepers = parse_int_arg(argv, i, argc, arg);
    else if (arg == "--sleep-ms") args.sleep_ms = parse_int_arg(argv, i, argc, arg);
    else if (arg == "--jsonl") args.jsonl = true;
    else if (arg == "--use-agent-runtime") args.use_agent_runtime = true;
    else if (arg == "--actions-out" && i + 1 < argc) args.actions_out = argv[++i];
    else if (arg == "--events-out" && i + 1 < argc) args.events_out = argv[++i];
    else if (arg == "--help") {
      usage();
      std::exit(0);
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  return args;
}

void add_agents(lobx::simulation::AgentRuntime& runtime,
                lobx::agents::AgentFactoryRegistry& registry,
                const std::string& type,
                int count,
                lobx::agents::AgentId& next_agent_id,
                lobx::agents::AgentGroupId group_id,
                uint64_t seed,
                lobx::agents::Price reference_price) {
  for (int i = 0; i < count; ++i) {
    lobx::agents::AgentConfig config{};
    config.type = type;
    config.group_id = group_id;
    config.numeric_params["seed"] = static_cast<double>(seed + next_agent_id * 17 + static_cast<uint64_t>(i));
    config.numeric_params["reference_price"] = static_cast<double>(reference_price);
    runtime.add_agent(registry.create(type, next_agent_id++, config));
  }
}

int run_agent_runtime(const Args& args) {
  lobx::simulation::AgentRuntimeConfig config{};
  config.steps = args.config.steps;
  config.symbol = args.config.market_symbol;
  config.reference_price = args.config.reference_price;
  config.initial_quote = args.config.initial_quote;
  config.initial_base = args.config.initial_base;
  config.book_levels = args.config.book_levels;

  lobx::simulation::AgentRuntime runtime(config);
  lobx::agents::AgentFactoryRegistry registry;
  lobx::agents::register_builtin_agents(registry);

  lobx::agents::AgentId next_agent_id = static_cast<lobx::agents::AgentId>(args.config.first_user_id);
  add_agents(runtime, registry, "static_market_maker", args.config.agents.market_makers, next_agent_id, 1,
             args.config.seed, args.config.reference_price);
  add_agents(runtime, registry, "noise_trader", args.config.agents.noise_traders, next_agent_id, 2,
             args.config.seed, args.config.reference_price);
  add_agents(runtime, registry, "momentum_follower", args.config.agents.momentum, next_agent_id, 3,
             args.config.seed, args.config.reference_price);
  add_agents(runtime, registry, "mean_reverter", args.config.agents.mean_reversion, next_agent_id, 4,
             args.config.seed, args.config.reference_price);
  add_agents(runtime, registry, "whale_sweeper", args.config.agents.whale_sweepers, next_agent_id, 5,
             args.config.seed, args.config.reference_price);

  runtime.run();
  if (!args.actions_out.empty() && !runtime.write_action_trace_jsonl(args.actions_out)) {
    throw std::runtime_error("failed to write actions trace: " + args.actions_out);
  }
  if (!args.events_out.empty() && !runtime.write_event_trace_jsonl(args.events_out)) {
    throw std::runtime_error("failed to write events trace: " + args.events_out);
  }

  const lobx::simulation::AgentRuntimeSummary& summary = runtime.summary();
  std::cout << "{"
            << "\"runtime\":\"agent\","
            << "\"steps\":" << summary.steps << ","
            << "\"agent_count\":" << summary.agent_count << ","
            << "\"actions\":" << summary.action_count << ","
            << "\"accepted_orders\":" << summary.accepted_orders << ","
            << "\"rejected_orders\":" << summary.rejected_orders << ","
            << "\"trade_count\":" << summary.trade_count << ","
            << "\"event_count\":" << summary.event_count << ","
            << "\"final_best_bid\":" << summary.final_best_bid << ","
            << "\"final_best_ask\":" << summary.final_best_ask
            << "}\n";
  return summary.action_count > 0 && summary.accepted_orders > 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
  try {
    Args args = parse_args(argc, argv);
    if (args.use_agent_runtime) return run_agent_runtime(args);

    lobx::sim::MesaAgentSimulation sim(args.config);
    if (args.jsonl) {
      std::cout << lobx::sim::mesa_agent_mix_json(sim.summary()) << "\n";
      for (int i = 0; i < args.config.steps; ++i) {
        const lobx::sim::MesaStepEvents events = sim.step();
        for (const lobx::TradeEvent& trade : events.trades) {
          std::cout << lobx::sim::mesa_trade_json(trade, events.step) << "\n";
        }
        for (const lobx::sim::MesaStepCandle& candle : events.candles) {
          std::cout << lobx::sim::mesa_step_candle_json(candle) << "\n";
        }
        std::cout << lobx::sim::mesa_step_stats_json(events.stats) << "\n";
        if (args.sleep_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(args.sleep_ms));
      }
      return 0;
    }

    const lobx::sim::MesaAgentSimSummary summary = sim.run();
    std::cout << lobx::sim::mesa_agent_summary_json(summary, true) << "\n";
    return summary.trade_count > 0 && summary.accepted_orders > 0 ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "lobx_mesa_agent_simulator error: " << e.what() << "\n";
    return 1;
  }
}
