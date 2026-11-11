#include "claudebox/identity.hpp"

#include <iostream>
#include <string>
#include <vector>

using claudebox::identity::ApplyResult;
using claudebox::identity::Inventory;
using claudebox::identity::Policy;
using claudebox::identity::apply_policy;
using claudebox::identity::load_inventory_file;
using claudebox::identity::verify;
using claudebox::identity::verify_prefix;
using claudebox::identity::write_presentation;

namespace {

struct Options {
    std::string command;
    std::filesystem::path inventory;
    std::filesystem::path policy;
    std::filesystem::path prefix;
};

void usage(std::ostream& out) {
    out << "usage:\n"
        << "  claudebox-identity apply --inventory FILE [--policy FILE] --prefix DIR\n"
        << "  claudebox-identity verify --inventory FILE [--policy FILE] --prefix DIR\n"
        << "  claudebox-identity print-defaults\n";
}

Options parse_args(int argc, char** argv) {
    Options opt;
    if (argc < 2) {
        throw std::runtime_error("missing command");
    }
    opt.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error(std::string("missing value for ") + name);
            }
            return argv[++i];
        };
        if (arg == "--inventory") {
            opt.inventory = need("--inventory");
        } else if (arg == "--policy") {
            opt.policy = need("--policy");
        } else if (arg == "--prefix") {
            opt.prefix = need("--prefix");
        } else if (arg == "-h" || arg == "--help") {
            opt.command = "help";
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }
    return opt;
}

Policy load_policy(const Options& opt) {
    if (opt.policy.empty()) {
        return Policy::defaults();
    }
    return Policy::load_file(opt.policy);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto opt = parse_args(argc, argv);
        if (opt.command == "help") {
            usage(std::cout);
            return 0;
        }
        if (opt.command == "print-defaults") {
            std::cout << "# Default identity policy\nfields:\n";
            const auto p = Policy::defaults();
            for (const auto& [id, fp] : p.fields) {
                std::cout << "  " << id << ":\n    mode: "
                          << claudebox::identity::field_mode_name(fp.mode) << "\n";
            }
            return 0;
        }
        if (opt.command != "apply" && opt.command != "verify") {
            usage(std::cerr);
            return 2;
        }
        if (opt.inventory.empty() || opt.prefix.empty()) {
            std::cerr << "error: --inventory and --prefix are required\n";
            usage(std::cerr);
            return 2;
        }
        const Inventory inv = load_inventory_file(opt.inventory);
        const Policy policy = load_policy(opt);
        if (opt.command == "apply") {
            const ApplyResult result = apply_policy(inv, policy);
            if (!result.ok) {
                std::cerr << "error: " << result.error << "\n";
                return 1;
            }
            write_presentation(opt.prefix, result.presentation);
            std::cout << "ok apply (" << result.presentation.fields.size() << " field(s))\n";
            return 0;
        }
        auto issues = verify_prefix(inv, policy, opt.prefix);
        if (!issues.empty()) {
            for (const auto& issue : issues) {
                std::cerr << "verify: " << issue.message << "\n";
            }
            std::cerr << "FAILED verify (" << issues.size() << " issue(s))\n";
            return 1;
        }
        std::cout << "ok verify\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
