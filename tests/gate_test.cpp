#include "canary/gate.hpp"
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace canary;
const Policy rules{500, 100, 500, 50, 200};
const std::string header = "window,route,cohort,requests,errors,slow\n";
const std::string base = "a,pay,baseline,1000,5,30\n";
const std::string good = "a,pay,candidate,1000,10,50\n";

Report run(const std::string& rows) {
    std::istringstream input(header + rows);
    return evaluate(input, rules);
}

void check(bool ok) { if (!ok) throw std::runtime_error("expectation failed"); }

void invalid(const std::string& rows) {
    try { run(rows); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error("invalid data accepted");
}

int failures = 0;
void test(const char* name, const std::function<void()>& body) {
    try { body(); std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& e) { ++failures; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
}
}  // namespace

int main() {
    test("equality at four thresholds passes", [] {
        check(run(base + good).decision == Decision::promote);
    });
    test("independent error increase blocks", [] {
        auto r = run("a,pay,baseline,1000,0,0\na,pay,candidate,1000,6,0\n");
        check(r.routes[0].reasons == std::vector<std::string>{"error_regression"});
    });
    test("slow limit catches unhealthy baseline and candidate", [] {
        auto r = run("a,pay,baseline,1000,0,60\na,pay,candidate,1000,0,60\n");
        check(r.routes[0].reasons == std::vector<std::string>{"candidate_slow_limit"});
    });
    test("low sample holds", [] {
        check(run(base + "a,pay,candidate,499,9,60\n").decision == Decision::hold);
    });
    test("equal volume minimum qualifies", [] {
        check(run(base + "a,pay,candidate,500,0,0\n").decision == Decision::promote);
    });
    test("mismatched windows hold even at sufficient volume", [] {
        check(run(base + "b,pay,candidate,9999,0,0\n").routes[0].reasons ==
              std::vector<std::string>{"unpaired_windows"});
    });
    test("absent cohort has null rate", [] {
        check(to_json(run(base)).find("\"error_bps\":null") != std::string::npos);
    });
    test("rollback outranks hold", [] {
        check(run(base + "b,cart,baseline,2000,0,0\nb,cart,candidate,1000,25,100\n").decision ==
              Decision::rollback);
    });
    test("row order does not affect report", [] {
        check(to_json(run(base + good)) == to_json(run(good + base)));
    });
    test("pool request counts across windows", [] {
        const auto r = run(base + good + "b,pay,baseline,10,0,0\nb,pay,candidate,10,1,0\n");
        check(r.routes[0].candidate.error_bps() == 109);
    });
    test("duplicate records rejected", [] { invalid(base + base); });
    test("negative counts rejected", [] { invalid("a,pay,baseline,1000,-1,0\n"); });
    test("errors beyond volume rejected", [] { invalid("a,pay,baseline,10,11,0\n"); });
    test("unknown cohort rejected", [] { invalid("a,pay,other,0,0,0\n"); });
    test("truncated row rejected", [] { invalid("a,pay,baseline,1000,1\n"); });
    test("empty telemetry rejected", [] { invalid(""); });
    test("aggregate cap enforced", [] {
        invalid("a,pay,baseline,1000000000,0,0\nb,pay,baseline,1,0,0\n");
    });
    test("duplicate policy field rejected", [] {
        std::istringstream in("minimum_requests=10\nminimum_requests=20\n");
        try { read_policy(in); } catch (const std::runtime_error&) { return; }
        throw std::runtime_error("duplicate policy accepted");
    });
    return failures == 0 ? 0 : 1;
}
