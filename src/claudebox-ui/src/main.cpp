#include "settings_window.hpp"

#include <QApplication>
#include <QTimer>

#include <iostream>
#include <string>

namespace {

struct Cli {
    claudebox::settings::Options options;
    bool smoke = false;
    bool help = false;
    bool ok = true;
    std::string error;
};

void usage(std::ostream& out) {
    out << "Usage: claudebox-settings --prefix DIR [--config FILE] [--inventory FILE] [--smoke]\n"
        << "\n"
        << "  --prefix DIR       Box prefix that receives policy, presentation, and saved config\n"
        << "  --config FILE      wsl-box.yaml to load (saved back here; never the in-repo example)\n"
        << "  --inventory FILE   Raw inventory YAML (settings can read it; never written back)\n"
        << "  --smoke            Load the page against the given fixtures and exit (for offscreen tests)\n";
}

Cli parse_args(int argc, char** argv) {
    Cli cli;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                cli.ok = false;
                cli.error = std::string("missing value for ") + name;
                return {};
            }
            return argv[++i];
        };
        if (arg == "--prefix") {
            cli.options.prefix = need("--prefix");
        } else if (arg == "--config") {
            cli.options.config = need("--config");
        } else if (arg == "--inventory") {
            cli.options.inventory = need("--inventory");
        } else if (arg == "--smoke") {
            cli.smoke = true;
        } else if (arg == "--help" || arg == "-h") {
            cli.help = true;
        } else {
            cli.ok = false;
            cli.error = "unknown argument: " + arg;
        }
    }
    if (!cli.help && cli.ok && cli.options.prefix.empty()) {
        cli.ok = false;
        cli.error = " --prefix is required";
    }
    return cli;
}

}  // namespace

int main(int argc, char** argv) {
    const Cli cli = parse_args(argc, argv);
    if (cli.help) {
        usage(std::cout);
        return 0;
    }
    if (!cli.ok) {
        std::cerr << "error:" << (cli.error.empty() || cli.error[0] == ' ' ? "" : " ")
                  << cli.error << "\n";
        usage(std::cerr);
        return 2;
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QString::fromUtf8("Claude Box 设置"));
    SettingsWindow window(cli.options);
    window.show();
    if (cli.smoke) {
        QTimer::singleShot(50, &app, &QApplication::quit);
        const int rc = QApplication::exec();
        if (rc != 0) {
            return rc;
        }
        std::cout << "claudebox-settings smoke ok\n";
        return 0;
    }
    return QApplication::exec();
}
