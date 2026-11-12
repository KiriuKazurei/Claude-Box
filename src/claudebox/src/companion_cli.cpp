#include "claudebox/companion.hpp"
#include "claudebox/config.hpp"

#include <iostream>
#include <string>
#include <vector>

using claudebox::companion::format_status;
using claudebox::companion::install_intercept;
using claudebox::companion::open_url;
using claudebox::companion::plan;
using claudebox::config::BoxConfig;
using claudebox::config::default_config_path;

namespace {

struct Options {
    std::string command;
    std::filesystem::path prefix;
    std::filesystem::path config;
    std::string url;
    bool dry_run = false;
};

void usage(std::ostream& out) {
    out << "usage:\n"
        << "  claudebox-companion status --prefix DIR [--config FILE]\n"
        << "  claudebox-companion plan --prefix DIR [--config FILE] [--url URL]\n"
        << "  claudebox-companion install-intercept --prefix DIR [--config FILE]\n"
        << "  claudebox-companion open --prefix DIR [--config FILE] --url URL [--dry-run]\n";
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
        if (arg == "--prefix") {
            opt.prefix = need("--prefix");
        } else if (arg == "--config") {
            opt.config = need("--config");
        } else if (arg == "--url") {
            opt.url = need("--url");
        } else if (arg == "--dry-run") {
            opt.dry_run = true;
        } else if (arg == "-h" || arg == "--help") {
            opt.command = "help";
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }
    return opt;
}

BoxConfig load_config(const Options& opt) {
    const auto path = opt.config.empty() ? default_config_path(opt.prefix) : opt.config;
    if (std::filesystem::exists(path)) {
        return BoxConfig::load_file(path);
    }
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
        if (opt.command == "status" || opt.command == "plan") {
            const auto planned = plan(config, opt.prefix, opt.url);
            std::cout << format_status(planned);
            if (!opt.url.empty()) {
                std::cout << "URL: " << opt.url << "\n";
                if (!planned.command_line.empty()) {
                    std::cout << "command: " << planned.command_line << "\n";
                }
            }
            return planned.ok || opt.command == "status" ? 0 : 1;
        }
        if (opt.command == "install-intercept") {
            const auto installed = install_intercept(config, opt.prefix);
            if (!installed.ok) {
                std::cerr << "error: " << installed.error << "\n";
                return 1;
            }
            std::cout << "ok install-intercept -> " << installed.plan.xdg_open << "\n";
            return 0;
        }
        if (opt.command == "open") {
            if (opt.url.empty()) {
                std::cerr << "error: --url is required\n";
                return 2;
            }
            const auto opened = open_url(config, opt.prefix, opt.url, opt.dry_run);
            if (!opened.ok) {
                std::cerr << opened.error << "\n";
                return opened.exit_code != 0 ? opened.exit_code : 1;
            }
            if (opt.dry_run) {
                std::cout << "dry-run: " << opened.plan.command_line << "\n";
            } else if (!opened.output.empty()) {
                std::cout << opened.output;
                if (opened.output.back() != '\n') {
                    std::cout << "\n";
                }
            }
            std::cout << "ok open\n";
            return 0;
        }
        usage(std::cerr);
        return 2;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
