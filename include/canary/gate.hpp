#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace canary {
using Count = std::int64_t;

struct Policy {
    Count minimum_requests;
    Count error_limit_bps;
    Count slow_limit_bps;
    Count error_increase_bps;
    Count slow_increase_bps;
};

struct Metrics {
    Count requests = 0;
    Count errors = 0;
    Count slow = 0;
    std::optional<Count> error_bps() const;
    std::optional<Count> slow_bps() const;
};

enum class Decision { promote, hold, rollback };

struct Route {
    std::string name;
    Metrics baseline, candidate;
    Decision decision = Decision::hold;
    std::vector<std::string> reasons;
};

struct Report {
    Decision decision = Decision::promote;
    Policy policy;
    std::vector<Route> routes;
};

Policy read_policy(std::istream& input);
Report evaluate(std::istream& windows, const Policy& policy);
std::string to_json(const Report& report);
const char* label(Decision decision);
int exit_code(Decision decision);
}  // namespace canary
