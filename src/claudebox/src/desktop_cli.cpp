#include "claudebox/config.hpp"
#include "claudebox/desktop.hpp"

#include <iostream>
#include <string>

using claudebox::config::BoxConfig;
using claudebox::config::default_config_path;
using claudebox::desktop::format_status;
using claudebox::desktop::plan;
using claudebox::desktop::start;
using claudebox::desktop::stop;
using claudebox::gateway::BoxProcess;

namespace {

struct Options {
    std::string command;
    std::filesystem::path prefix;
    std::filesystem::path config;
};

void usage(std::ostream& out) {
    out << "usage:\n"
        << "  claudebox-desktop plan|status|start|stop --prefix DIR [--config FILE]\n"
        << "start and stop are dry-run on this computer: no window, no real signals.\n";
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
        else if (arg == "-h" || arg == "--help") opt.command = "help";
        else if (arg == "--dry-run") continue;
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
            return 2;
        }
        const auto config = load_config(opt);
        if (opt.command == "plan" || opt.command == "status") {
            const auto planned = plan(config, opt.prefix);
            std::cout << format_status(planned);
            return 0;
        }
        if (opt.command == "start") {
            const auto result = start(config, opt.prefix, true);
            std::cout << format_status(result.plan);
            if (!result.started) {
                std::cout << "not started (dry-run";
                if (!result.plan.binary_found) std::cout << ", binary missing";
                std::cout << ")\n";
            }
            return result.ok ? 0 : 1;
        }
        if (opt.command == "stop") {
            const std::vector<BoxProcess> none;
            const auto result = stop(config, none, true);
            if (!result.ok) {
                std::cerr << "error: " << result.error << "\n";
                return 1;
            }
            std::cout << format_status(plan(config, opt.prefix), &result);
            std::cout << "ok dry-run stop\n";
            return 0;
        }
        usage(std::cerr);
        return 2;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
