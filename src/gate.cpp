#include "canary/gate.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <istream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace canary {
namespace {
constexpr Count max_requests = 1'000'000'000;
constexpr std::size_t max_rows = 100'000;

void strip_cr(std::string& value) {
    if (!value.empty() && value.back() == '\r') value.pop_back();
}

Count number(std::string_view text, Count limit) {
    Count n{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), n);
    if (text.empty() || text.front() == '-' || error != std::errc{} ||
        end != text.data() + text.size() || n > limit) {
        throw std::runtime_error("invalid nonnegative integer: " + std::string(text));
    }
    return n;
}

bool identifier(std::string_view text) {
    return !text.empty() && text.size() <= 64 &&
        text.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")
            == std::string_view::npos;
}

std::array<std::string_view, 6> fields(std::string_view row) {
    std::array<std::string_view, 6> out;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto comma = row.find(',');
        if ((i == out.size() - 1) != (comma == row.npos)) {
            throw std::runtime_error("expected six comma-separated fields");
        }
        out[i] = row.substr(0, comma);
        if (comma != row.npos) row.remove_prefix(comma + 1);
    }
    return out;
}

struct Cohort {
    std::set<std::string> windows;
    Metrics metrics;
};

void ingest(std::string_view row, std::map<std::string, std::array<Cohort, 2>>& routes) {
    const auto [window, route, cohort, volume, errors, slow] = fields(row);
    if (!identifier(window) || !identifier(route)) throw std::runtime_error("invalid window or route ID");
    if (cohort != "baseline" && cohort != "candidate") throw std::runtime_error("unknown cohort");
    auto& group = routes[std::string(route)][cohort == "candidate" ? 1 : 0];
    if (!group.windows.emplace(window).second) throw std::runtime_error("duplicate window for route and cohort");
    const auto requests = number(volume, max_requests);
    auto& m = group.metrics;
    if (requests > max_requests - m.requests) throw std::runtime_error("cohort request cap exceeded");
    m.requests += requests;
    m.errors += number(errors, requests);
    m.slow += number(slow, requests);
}

std::optional<Count> rate(Count count, Count requests) {
    if (requests == 0) return std::nullopt;
    return (count * 10'000 + requests - 1) / requests;
}

Route judge(const std::string& name, const std::array<Cohort, 2>& groups, const Policy& p) {
    const auto& [baseline, candidate] = groups;
    Route r{name, baseline.metrics, candidate.metrics, Decision::hold, {}};
    if (baseline.windows != candidate.windows) r.reasons.emplace_back("unpaired_windows");
    if (r.baseline.requests < p.minimum_requests) r.reasons.emplace_back("low_baseline_volume");
    if (r.candidate.requests < p.minimum_requests) r.reasons.emplace_back("low_candidate_volume");
    if (!r.reasons.empty()) return r;

    const auto err = *r.candidate.error_bps();
    const auto slow = *r.candidate.slow_bps();
    if (err > p.error_limit_bps) r.reasons.emplace_back("candidate_error_limit");
    if (slow > p.slow_limit_bps) r.reasons.emplace_back("candidate_slow_limit");
    if (err - *r.baseline.error_bps() > p.error_increase_bps) r.reasons.emplace_back("error_regression");
    if (slow - *r.baseline.slow_bps() > p.slow_increase_bps) r.reasons.emplace_back("latency_regression");
    r.decision = r.reasons.empty() ? Decision::promote : Decision::rollback;
    return r;
}

std::string metrics_json(const Metrics& m) {
    const auto value = [](std::optional<Count> n) { return n ? std::to_string(*n) : "null"; };
    std::ostringstream out;
    out << "{\"requests\":" << m.requests << ",\"errors\":" << m.errors
        << ",\"slow\":" << m.slow << ",\"error_bps\":" << value(m.error_bps())
        << ",\"slow_bps\":" << value(m.slow_bps()) << "}";
    return out.str();
}
}  // namespace

std::optional<Count> Metrics::error_bps() const { return rate(errors, requests); }
std::optional<Count> Metrics::slow_bps() const { return rate(slow, requests); }

Policy read_policy(std::istream& in) {
    const std::map<std::string, Count> bounds{
        {"minimum_requests", max_requests}, {"error_limit_bps", 10000}, {"slow_limit_bps", 10000},
        {"error_increase_bps", 10000}, {"slow_increase_bps", 10000}
    };
    std::map<std::string, Count> values;
    std::string row;
    while (std::getline(in, row)) {
        strip_cr(row);
        if (row.empty() || row.front() == '#') continue;
        const auto eq = row.find('=');
        const auto key = row.substr(0, eq);
        if (eq == row.npos || !bounds.contains(key) || values.contains(key))
            throw std::runtime_error("unknown, malformed or duplicate policy field: " + key);
        values[key] = number(std::string_view(row).substr(eq + 1), bounds.at(key));
    }
    if (in.bad() || values.size() != bounds.size() || values.at("minimum_requests") == 0)
        throw std::runtime_error("incomplete policy or zero minimum_requests");
    return {values.at("minimum_requests"), values.at("error_limit_bps"), values.at("slow_limit_bps"),
            values.at("error_increase_bps"), values.at("slow_increase_bps")};
}

Report evaluate(std::istream& in, const Policy& policy) {
    std::string row;
    std::getline(in, row);
    strip_cr(row);
    if (row != "window,route,cohort,requests,errors,slow") throw std::runtime_error("invalid CSV header");
    std::map<std::string, std::array<Cohort, 2>> groups;
    std::size_t n = 1;
    while (std::getline(in, row)) {
        strip_cr(row);
        ++n;
        if (n > max_rows + 1) throw std::runtime_error("input exceeds 100000 rows");
        try {
            ingest(row, groups);
        } catch (const std::runtime_error& e) {
            throw std::runtime_error("CSV line " + std::to_string(n) + ": " + e.what());
        }
    }
    if (in.bad() || groups.empty()) throw std::runtime_error("unreadable or empty telemetry");
    Report report{Decision::promote, policy, {}};
    for (const auto& [name, cohorts] : groups) {
        auto route = judge(name, cohorts, policy);
        report.decision = std::max(report.decision, route.decision);
        report.routes.push_back(std::move(route));
    }
    return report;
}

const char* label(Decision d) {
    switch (d) {
        case Decision::promote: return "PROMOTE";
        case Decision::hold: return "HOLD";
        case Decision::rollback: return "ROLLBACK";
    }
    throw std::logic_error("unknown decision");
}

int exit_code(Decision d) {
    switch (d) {
        case Decision::promote: return 0;
        case Decision::hold: return 10;
        case Decision::rollback: return 20;
    }
    throw std::logic_error("unknown decision");
}

std::string to_json(const Report& report) {
    const auto& p = report.policy;
    std::ostringstream out;
    out << "{\"schema_version\":1,\"decision\":\"" << label(report.decision)
        << "\",\"policy\":{\"minimum_requests\":" << p.minimum_requests
        << ",\"error_limit_bps\":" << p.error_limit_bps << ",\"slow_limit_bps\":" << p.slow_limit_bps
        << ",\"error_increase_bps\":" << p.error_increase_bps
        << ",\"slow_increase_bps\":" << p.slow_increase_bps << "},\"routes\":[";
    bool first = true;
    for (const auto& r : report.routes) {
        if (!first) out << ',';
        first = false;
        out << "{\"route\":\"" << r.name << "\",\"decision\":\"" << label(r.decision)
            << "\",\"baseline\":" << metrics_json(r.baseline) << ",\"candidate\":" << metrics_json(r.candidate)
            << ",\"reasons\":[";
        for (std::size_t i = 0; i < r.reasons.size(); ++i)
            out << (i ? "," : "") << '"' << r.reasons[i] << '"';
        out << "]}";
    }
    return out.str() + "]}\n";
}
}  // namespace canary
