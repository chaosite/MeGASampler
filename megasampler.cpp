#include "megasampler.h"

#include <capnp/serialize.h>

#include <cinttypes>
#include <iostream>
#include <random>

#include "pythonfuncs.h"
#include "strengthen.capnp.h"

MEGASampler::MEGASampler(std::string _input, std::string _output_dir,
                         int _max_samples, double _max_time,
                         int _max_epoch_samples, double _max_epoch_time,
                         int _strategy, bool _json, bool _blocking)
    : Sampler(_input, _output_dir, _max_samples, _max_time, _max_epoch_samples,
              _max_epoch_time, _strategy, _json, _blocking),
      simpl_formula(c) {
  initialize_solvers();
  std::cout << "starting MEGA" << std::endl;
}

void MEGASampler::do_epoch(const z3::model& m) {
  is_time_limit_reached();
  struct buflen ret = call_strengthen(original_formula, m);
  const auto view = kj::arrayPtr(reinterpret_cast<const capnp::word*>(ret.buf),
                                 ret.len / sizeof(capnp::word));
  capnp::FlatArrayMessageReader message(view);
  auto container = message.getRoot<StrengthenResult>();

  auto res = container.getRes();
  auto failureDescription = container.getFailuredecription();
  if (!res) {
    std::cout << "An error has occurred during epoch: "
              << failureDescription.cStr() << "\n";
    failure_cause = failureDescription.cStr();
    safe_exit(1);
  }
  for (auto varinterval : container.getIntervalmap()) {
    std::string varname = varinterval.getVariable().cStr();
    auto interval = varinterval.getInterval();
    bool isLowMinf = interval.getIslowminf();
    bool isHighInf = interval.getIshighinf();
    auto low = isLowMinf ? "MINF" : std::to_string(interval.getLow());
    auto high = isHighInf ? "INF" : std::to_string(interval.getHigh());
    std::cout << varname << ": "
              << "[" << low << "," << high << "] ";
  }
  std::cout << "\n";

  if (is_time_limit_reached("epoch")) return;

  sample_intervals_in_rounds(container.getIntervalmap());
}

void MEGASampler::finish() {
  json_output["method name"] = "megasampler";
  Sampler::finish();
}

void MEGASampler::sample_intervals_in_rounds(
    const capnp::List<StrengthenResult::VarInterval>::Reader& intervalmap) {
  int MAX_ROUNDS = 6;
  float rate = 1.0;
  for (int round = 0; round < MAX_ROUNDS && rate > 0.2; round++) {
    is_time_limit_reached();
    std::cout << "Starting round: " << round << "\n";
    int new_samples = 0;
    int MAX_SAMPLES = 100;
    int round_samples = 0;
    while (round_samples < MAX_SAMPLES) {
      std::string sample = get_random_sample_from_intervals(intervalmap);
      total_samples++;
      bool is_unique = save_and_output_sample_if_unique(sample);
      if (is_unique) {
        new_samples++;
      }
      round_samples++;
    }
    rate = new_samples / round_samples;
  }
}

std::string MEGASampler::get_random_sample_from_intervals(
    const capnp::List<StrengthenResult::VarInterval>::Reader& intervalmap) {
  std::string sample_string;
  for (auto varinterval : intervalmap) {
    std::string varname = varinterval.getVariable().cStr();
    auto interval = varinterval.getInterval();
    sample_string += varname;
    sample_string += ":";
    int64_t low = interval.getLow();
    int64_t high = interval.getHigh();
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int64_t> gen(low, high);  // uniform, unbiased
    int64_t randNum = gen(rng);
    sample_string += std::to_string(randNum);
    sample_string += ";";
  }
  return sample_string;
}
