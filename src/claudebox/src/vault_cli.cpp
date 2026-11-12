#include "claudebox/config.hpp"
#include "claudebox/vault.hpp"

#include <iostream>
#include <string>

using claudebox::config::BoxConfig;
using claudebox::config::default_config_path;
using claudebox::vault::ActionOptions;
using claudebox::vault::archive;
using claudebox::vault::format_status;
using claudebox::vault::load_passphrase;
using claudebox::vault::plan;
using claudebox::vault::rollback;
using claudebox::vault::switch_to;

namespace {

struct Options {
    std::string command;
    std::filesystem::path prefix;
    std::filesystem::path config;
    std::string target;
    std::string label;
};

void usage(std::ostream& out) {
    out << "usage:\n"
        << "  claudebox-vault status --prefix DIR [--config FILE]\n"
        << "  claudebox-vault archive --prefix DIR [--config FILE] [--label NAME]\n"
        << "  claudebox-vault switch --prefix DIR [--config FILE] --target ID|empty\n"
        << "  claudebox-vault rollback --prefix DIR [--config FILE]\n"
        << "Passphrase: CLAUDE_BOX_VAULT_PASSPHRASE_FILE or CLAUDE_BOX_VAULT_PASSPHRASE.\n"
        << "The passphrase is never printed.\n";
}

Options parse_args(int argc, char** argv) {
    Options opt;
    if (argc < 2) throw std::runtime_error("missing command");
    opt.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need = [&](const char* name) -> std::string {
            if (i + 1 >= argc) throw std::runtime_error(std::string("missing value for ") + name);
            return argv[++i];
        };
        if (arg == "--prefix") opt.prefix = need("--prefix");
        else if (arg == "--config") opt.config = need("--config");
        else if (arg == "--target") opt.target = need("--target");
        else if (arg == "--label") opt.label = need("--label");
        else if (arg == "-h" || arg == "--help") opt.command = "help";
        else throw std::runtime_error("unknown argument: " + arg);
    }
    return opt;
}

BoxConfig load_config(const Options& opt) {
    const auto path = opt.config.empty() ? default_config_path(opt.prefix) : opt.config;
    if (std::filesystem::exists(path)) return BoxConfig::load_file(path);
    return BoxConfig::defaults();
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto opt = parse_args(argc, argv);
        if (opt.command == "help") {
            usage(std::cout);
            return 0;
        }
        if (opt.prefix.empty()) {
            std::cerr << "error: --prefix is required\n";
            usage(std::cerr);
            return 2;
        }
        const auto config = load_config(opt);
        if (opt.command == "status") {
            std::cout << format_status(plan(config, opt.prefix));
            return 0;
        }
        std::string pass;
        std::string err;
        if (!load_passphrase(&pass, &err)) {
            std::cerr << "error: " << err << "\n";
            return 2;
        }
        ActionOptions action;
        action.label = opt.label;
        if (opt.command == "archive") {
            const auto result = archive(config, opt.prefix, pass, action);
            if (!result.ok) {
                std::cerr << "error: " << result.error << "\n";
                return 1;
            }
            std::cout << "ok archive " << result.current_id << "\n";
            return 0;
        }
        if (opt.command == "switch") {
            if (opt.target.empty()) {
                std::cerr << "error: --target is required\n";
                return 2;
            }
            const auto result = switch_to(config, opt.prefix, opt.target, pass, action);
            if (!result.ok) {
                std::cerr << "error: " << result.error << "\n";
                return 1;
            }
            std::cout << "ok switch " << result.current_id << "\n";
            return 0;
        }
        if (opt.command == "rollback") {
            const auto result = rollback(config, opt.prefix, pass, action);
            if (!result.ok) {
                std::cerr << "error: " << result.error << "\n";
                return 1;
            }
            std::cout << "ok rollback " << result.current_id << "\n";
            return 0;
        }
        usage(std::cerr);
        return 2;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
