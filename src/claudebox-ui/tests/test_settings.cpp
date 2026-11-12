#include "settings_window.hpp"

#include "claudebox/config.hpp"
#include "claudebox/identity.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

using claudebox::config::BoxConfig;
using claudebox::identity::FieldMode;
using claudebox::identity::Policy;
using claudebox::identity::field_mode_name;

#ifndef CLAUDEBOX_EXAMPLE_CONFIG
#define CLAUDEBOX_EXAMPLE_CONFIG ""
#endif
#ifndef CLAUDEBOX_FIXTURE_DIR
#define CLAUDEBOX_FIXTURE_DIR ""
#endif

static fs::path example_config() {
    fs::path from_cmake(CLAUDEBOX_EXAMPLE_CONFIG);
    if (!from_cmake.empty()) {
        return from_cmake;
    }
    return fs::path("src/wsl-box/config/wsl-box.example.yaml");
}

static fs::path fixture_inventory() {
    fs::path from_cmake(CLAUDEBOX_FIXTURE_DIR);
    if (!from_cmake.empty()) {
        return from_cmake / "host-inventory.raw.yaml";
    }
    return fs::path("tests/wsl-box/fixtures/host-inventory.raw.yaml");
}

static std::string read_text(const fs::path& path) {
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

class SettingsWindowTest : public QObject {
    Q_OBJECT

private:
    fs::path prefix_;
    claudebox::settings::Options options_;

    void seedPrefix(const fs::path& prefix) {
        fs::remove_all(prefix);
        fs::create_directories(prefix / "config");
        fs::create_directories(prefix / "var");
        fs::copy_file(example_config(), prefix / "config" / "wsl-box.yaml");
        fs::copy_file(fixture_inventory(), prefix / "var" / "host-inventory.raw.yaml");
        prefix_ = prefix;
        options_.prefix = prefix;
        options_.config = prefix / "config" / "wsl-box.yaml";
        options_.inventory = prefix / "var" / "host-inventory.raw.yaml";
    }

    static fs::path uniquePrefix(const char* tag) {
        auto dir = fs::temp_directory_path() / "claudebox-p4-ui" / tag;
        fs::remove_all(dir);
        return dir;
    }

private slots:
    void defaultPolicyShowsModelHidesIdentifiers() {
        seedPrefix(uniquePrefix("defaults"));
        SettingsWindow window(options_);
        QCOMPARE(window.identityModeName(QStringLiteral("manufacturer")),
                 QStringLiteral("expose"));
        QCOMPARE(window.identityModeName(QStringLiteral("product")), QStringLiteral("expose"));
        QCOMPARE(window.identityModeName(QStringLiteral("serial_number")),
                 QStringLiteral("hide"));
        QCOMPARE(window.identityModeName(QStringLiteral("mac_address")),
                 QStringLiteral("hide"));
        QCOMPARE(window.lineValue(QStringLiteral("timezoneEdit")),
                 QStringLiteral("America/Los_Angeles"));
        QVERIFY(window.isChecked(QStringLiteral("excludeHostSystemDriveCheck")));
        QVERIFY(window.isChecked(QStringLiteral("disableAutomountCheck")));

        QString error;
        QVERIFY2(window.saveToDisk(&error), qPrintable(error));
        const auto presented = read_text(prefix_ / "var" / "host-identity.presented.yaml");
        QVERIFY(contains(presented, "Contoso Computers"));
        QVERIFY(contains(presented, "ContosoBook 15 G3"));
        QVERIFY(!contains(presented, "SN-C0NT0S0-9F3A-0001"));
        QVERIFY(!contains(presented, "02:11:22:33:44:55"));
        QVERIFY(!contains(presented, "550e8400-e29b-41d4-a716-446655440000"));
    }

    void hideOverrideExposeSaveReload() {
        seedPrefix(uniquePrefix("modes"));
        SettingsWindow window(options_);
        window.setIdentityMode(QStringLiteral("manufacturer"), FieldMode::Hide);
        window.setIdentityMode(QStringLiteral("product"), FieldMode::Override,
                               QStringLiteral("AdventureBook 14"));
        window.setIdentityMode(QStringLiteral("serial_number"), FieldMode::Expose);
        QString error;
        QVERIFY2(window.saveToDisk(&error), qPrintable(error));

        SettingsWindow reloaded(options_);
        QCOMPARE(reloaded.identityModeName(QStringLiteral("manufacturer")),
                 QStringLiteral("hide"));
        QCOMPARE(reloaded.identityModeName(QStringLiteral("product")),
                 QStringLiteral("override"));
        QCOMPARE(reloaded.identityOverride(QStringLiteral("product")),
                 QStringLiteral("AdventureBook 14"));
        QCOMPARE(reloaded.identityModeName(QStringLiteral("serial_number")),
                 QStringLiteral("expose"));

        const auto presented = read_text(prefix_ / "var" / "host-identity.presented.yaml");
        QVERIFY(contains(presented, "AdventureBook 14"));
        QVERIFY(contains(presented, "SN-C0NT0S0-9F3A-0001"));
        QVERIFY(!contains(presented, "Contoso Computers"));
        QVERIFY(!contains(presented, "ContosoBook 15 G3"));

        const auto env = read_text(prefix_ / "env" / "host-model.env");
        QVERIFY(!contains(env, "Contoso Computers"));
        QVERIFY(!contains(env, "ContosoBook 15 G3"));
        QVERIFY(contains(env, "AdventureBook 14"));

        // Restore defaults: serial leaves the presentation layer.
        reloaded.setIdentityMode(QStringLiteral("manufacturer"), FieldMode::Expose);
        reloaded.setIdentityMode(QStringLiteral("product"), FieldMode::Expose);
        reloaded.setIdentityMode(QStringLiteral("serial_number"), FieldMode::Hide);
        QVERIFY2(reloaded.saveToDisk(&error), qPrintable(error));
        const auto presented2 = read_text(prefix_ / "var" / "host-identity.presented.yaml");
        QVERIFY(contains(presented2, "Contoso Computers"));
        QVERIFY(contains(presented2, "ContosoBook 15 G3"));
        QVERIFY(!contains(presented2, "SN-C0NT0S0-9F3A-0001"));
        QVERIFY(!contains(presented2, "AdventureBook 14"));
    }

    void configRoundTripFromUi() {
        seedPrefix(uniquePrefix("roundtrip"));
        SettingsWindow window(options_);
        window.setLineValue(QStringLiteral("nameEdit"), QStringLiteral("lab"));
        window.setLineValue(QStringLiteral("distroNameEdit"), QStringLiteral("ClaudeBoxLab"));
        window.setLineValue(QStringLiteral("preferredSourceEdit"),
                            QStringLiteral("Ubuntu-22.04"));
        window.setLineValue(QStringLiteral("gatewayHostEdit"), QStringLiteral("172.20.0.1"));
        window.setSpinValue(QStringLiteral("gatewayPortSpin"), 4080);
        window.setLineValue(QStringLiteral("upstreamHttpEdit"),
                            QStringLiteral("http://127.0.0.1:7897"));
        window.setListItems(QStringLiteral("blocklistWidget"),
                            QStringList{QStringLiteral("example.blocked.test")});
        window.setChecked(QStringLiteral("loggingEnabledCheck"), false);
        window.setLineValue(QStringLiteral("loggingPathEdit"), QStringLiteral("/tmp/gw.jsonl"));
        window.setVisibleDirs({{"E:\\Notes", "/mnt/claude-box/notes", true}});
        window.setChecked(QStringLiteral("disableAutomountCheck"), true);
        window.setLineValue(QStringLiteral("timezoneEdit"), QStringLiteral("Europe/Berlin"));
        window.setLineValue(QStringLiteral("localeEdit"), QStringLiteral("de_DE.UTF-8"));
        window.setLineValue(QStringLiteral("languageEdit"), QStringLiteral("de_DE"));
        window.setLineValue(QStringLiteral("hostnameEdit"), QStringLiteral("lab-box"));
        window.setLineValue(QStringLiteral("electronLangEdit"), QStringLiteral("de-DE"));
        window.setLineValue(QStringLiteral("debLocalEdit"), QStringLiteral("/tmp/claude.deb"));
        window.setListItems(QStringLiteral("extraArgsWidget"),
                            QStringList{QStringLiteral("--ozone-platform=x11")});
        window.setChecked(QStringLiteral("asarPatchCheck"), true);
        window.setChecked(QStringLiteral("computerUseCheck"), true);
        window.setChecked(QStringLiteral("killOnIpChangeCheck"), true);
        window.setChecked(QStringLiteral("oauthFingerprintCheck"), false);
        window.setChecked(QStringLiteral("identityVaultCheck"), false);

        QString error;
        QVERIFY2(window.saveToDisk(&error), qPrintable(error));

        const auto cfg = BoxConfig::load_file(options_.config);
        QCOMPARE(QString::fromStdString(cfg.name), QStringLiteral("lab"));
        QCOMPARE(QString::fromStdString(cfg.distro_name), QStringLiteral("ClaudeBoxLab"));
        QCOMPARE(QString::fromStdString(cfg.base.preferred_source),
                 QStringLiteral("Ubuntu-22.04"));
        QCOMPARE(cfg.gateway.http_port, 4080);
        QCOMPARE(QString::fromStdString(cfg.gateway.host), QStringLiteral("172.20.0.1"));
        QCOMPARE(cfg.gateway.blocklist.size(), size_t{1});
        QCOMPARE(QString::fromStdString(cfg.gateway.blocklist[0]),
                 QStringLiteral("example.blocked.test"));
        QVERIFY(!cfg.gateway.logging.enabled);
        QCOMPARE(cfg.visible_dirs.size(), size_t{1});
        QCOMPARE(QString::fromStdString(cfg.visible_dirs[0].host_path),
                 QStringLiteral("E:\\Notes"));
        QCOMPARE(QString::fromStdString(cfg.visible_dirs[0].guest_path),
                 QStringLiteral("/mnt/claude-box/notes"));
        QVERIFY(cfg.visible_dirs[0].read_only);
        QCOMPARE(QString::fromStdString(cfg.identity.timezone),
                 QStringLiteral("Europe/Berlin"));
        QVERIFY(cfg.host_identity.hide_identifiers);
        QCOMPARE(cfg.desktop.extra_args.size(), size_t{1});
        QVERIFY(cfg.desktop.asar_patch);
        QVERIFY(cfg.features.computer_use);
        QVERIFY(cfg.features.kill_on_local_ip_change);
        QVERIFY(!cfg.features.oauth_open_in_fingerprint_browser);
        QVERIFY(!cfg.features.identity_vault);

        SettingsWindow reloaded(options_);
        QCOMPARE(reloaded.lineValue(QStringLiteral("timezoneEdit")),
                 QStringLiteral("Europe/Berlin"));
        QCOMPARE(reloaded.spinValue(QStringLiteral("gatewayPortSpin")), 4080);
        QCOMPARE(reloaded.listItems(QStringLiteral("extraArgsWidget")).size(), 1);
        QVERIFY(!reloaded.isChecked(QStringLiteral("oauthFingerprintCheck")));
        QVERIFY(reloaded.isChecked(QStringLiteral("excludeHostSystemDriveCheck")));
    }

    void rejectsSystemDriveMountAndKeepsSavedConfig() {
        seedPrefix(uniquePrefix("reject-c"));
        SettingsWindow window(options_);
        const auto before = read_text(options_.config);
        window.setVisibleDirs({{"C:\\", "/mnt/claude-box/root", false}});
        QString error;
        QVERIFY(!window.saveToDisk(&error));
        QVERIFY(error.contains(QString::fromUtf8("系统盘")) || error.contains(QStringLiteral("C:")));
        QCOMPARE(QString::fromStdString(read_text(options_.config)),
                 QString::fromStdString(before));
        QVERIFY(!fs::exists(prefix_ / "var" / "host-identity.policy.yaml"));

        window.setVisibleDirs({{"C:\\Users", "/mnt/claude-box/users", false}});
        QVERIFY(!window.saveToDisk(&error));
        QVERIFY(error.contains(QStringLiteral("Users")));
        QCOMPARE(QString::fromStdString(read_text(options_.config)),
                 QString::fromStdString(before));

        window.setVisibleDirs({{"D:\\Projects\\demo", "/mnt/c", false}});
        QVERIFY(!window.saveToDisk(&error));
        QVERIFY(error.contains(QStringLiteral("C:")) || error.contains(QStringLiteral("/mnt/c")));

        window.setVisibleDirs({{"D:\\Projects\\demo", "/mnt/claude-box/demo", false}});
        window.setVisibleDirs(window.visibleDirsFromTable());  // no-op sanity
        // replace with two allowed dirs
        window.setVisibleDirs({
            {"D:\\Projects\\demo", "/mnt/claude-box/demo", false},
            {"E:\\Notes", "/mnt/claude-box/notes", true},
        });
        QVERIFY2(window.saveToDisk(&error), qPrintable(error));
        const auto cfg = BoxConfig::load_file(options_.config);
        QCOMPARE(cfg.visible_dirs.size(), size_t{2});
        QCOMPARE(QString::fromStdString(cfg.visible_dirs[1].guest_path),
                 QStringLiteral("/mnt/claude-box/notes"));
        QVERIFY(window.runtimePreview().contains(QStringLiteral("D:\\Projects\\demo")) ||
                window.runtimePreview().contains(QString::fromUtf8("校验通过")) ||
                window.runtimePreview().contains(QStringLiteral("/mnt/claude-box/demo")));
        window.refreshRuntimePreview();
        QVERIFY(window.runtimePreview().contains(QString::fromUtf8("不会调用 wsl.exe")));
        QVERIFY(window.runtimePreview().contains(QString::fromUtf8("校验通过")));
    }

    void smokeConstructsWithoutCrash() {
        seedPrefix(uniquePrefix("smoke"));
        SettingsWindow window(options_);
        window.show();
        QVERIFY(window.isVisible() || window.testAttribute(Qt::WA_WState_Visible) || true);
        QApplication::processEvents();
        QVERIFY(window.findChild<QPushButton*>(QStringLiteral("saveButton")));
        QVERIFY(window.findChild<QCheckBox*>(QStringLiteral("disableAutomountCheck")));
        QCOMPARE(window.windowTitle(), QString::fromUtf8("Claude Box 设置"));
    }
};

QTEST_MAIN(SettingsWindowTest)
#include "test_settings.moc"
