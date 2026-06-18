#include "canary/gate.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "canaryledger POLICY.conf WINDOWS.csv\n"
                     "Exit: 0 PROMOTE, 10 HOLD, 20 ROLLBACK, 2 invalid input.\n";
        return 0;
    }
    if (argc != 3) {
        std::cerr << "Usage: canaryledger POLICY.conf WINDOWS.csv\n";
        return 2;
    }
    try {
        std::ifstream policy_file(argv[1]), telemetry(argv[2]);
        if (!policy_file) throw std::runtime_error("cannot open policy file");
        if (!telemetry) throw std::runtime_error("cannot open telemetry file");
        const auto policy = canary::read_policy(policy_file);
        const auto report = canary::evaluate(telemetry, policy);
        std::cout << canary::to_json(report) << std::flush;
        if (!std::cout) throw std::runtime_error("cannot write report");
        return canary::exit_code(report.decision);
    } catch (const std::exception& e) {
        std::cerr << "canaryledger: " << e.what() << '\n';
        return 2;
    }
}
