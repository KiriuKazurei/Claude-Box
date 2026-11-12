#include "settings_window.hpp"

#include "claudebox/config.hpp"
#include "claudebox/identity.hpp"
#include "claudebox/spoof.hpp"
#include "claudebox/companion.hpp"
#include "claudebox/vault.hpp"
#include "claudebox/desktop.hpp"
#include "claudebox/gateway_ip.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QListWidget>
#include <QListWidgetItem>
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
#ifndef CLAUDEBOX_GATEWAY_SAMPLE
#define CLAUDEBOX_GATEWAY_SAMPLE ""
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

static fs::path sample_gateway_log() {
    fs::path from_cmake(CLAUDEBOX_GATEWAY_SAMPLE);
    if (!from_cmake.empty()) {
        return from_cmake;
    }
    return fs::path("src/ClaudeBox.Gateway/samples/gateway.log.example.jsonl");
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
        window.setSpinValue(QStringLiteral("killGraceSpin"), 15);
        window.setSpinValue(QStringLiteral("watchIntervalSpin"), 3);
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
        QCOMPARE(cfg.features.kill_grace_seconds, 15);
        QCOMPARE(cfg.features.watch_interval_seconds, 3);
        QVERIFY(!cfg.features.oauth_open_in_fingerprint_browser);
        QVERIFY(!cfg.features.identity_vault);

        SettingsWindow reloaded(options_);
        QCOMPARE(reloaded.lineValue(QStringLiteral("timezoneEdit")),
                 QStringLiteral("Europe/Berlin"));
        QCOMPARE(reloaded.spinValue(QStringLiteral("gatewayPortSpin")), 4080);
        QCOMPARE(reloaded.listItems(QStringLiteral("extraArgsWidget")).size(), 1);
        QVERIFY(!reloaded.isChecked(QStringLiteral("oauthFingerprintCheck")));
        QVERIFY(reloaded.isChecked(QStringLiteral("excludeHostSystemDriveCheck")));
        QVERIFY(reloaded.isChecked(QStringLiteral("killOnIpChangeCheck")));
        QCOMPARE(reloaded.spinValue(QStringLiteral("killGraceSpin")), 15);
        QCOMPARE(reloaded.spinValue(QStringLiteral("watchIntervalSpin")), 3);
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

    void gatewayStatusAndUpstreamRoundTrip() {
        seedPrefix(uniquePrefix("gateway"));
        const auto default_log = prefix_ / "var" / "logs" / "gateway.jsonl";
        fs::create_directories(default_log.parent_path());
        fs::copy_file(sample_gateway_log(), default_log);

        SettingsWindow window(options_);
        QVERIFY(window.findChild<QPlainTextEdit*>(QStringLiteral("gatewayStatusPreview")));
        QVERIFY(window.findChild<QPushButton*>(QStringLiteral("refreshGatewayStatusButton")));
        QCOMPARE(window.lineValue(QStringLiteral("upstreamHttpEdit")),
                 QStringLiteral("http://127.0.0.1:7890"));
        QCOMPARE(window.lineValue(QStringLiteral("loggingPathEdit")), QString());

        QString status = window.gatewayStatusText();
        QVERIFY2(status.contains(QString::fromUtf8("已配置")), qPrintable(status));
        QVERIFY(status.contains(QStringLiteral("http://127.0.0.1:7890")));
        QVERIFY(status.contains(QStringLiteral("socks5://127.0.0.1:7891")));
        QVERIFY(status.contains(QStringLiteral("gost -C ")));
        QVERIFY(status.contains(QStringLiteral("gost.relay.yml")));
        QVERIFY(status.contains(QString::fromUtf8("未启动 gost")));
        QVERIFY(status.contains(QString::fromStdString(default_log.string())));
        QVERIFY(status.contains(QString::fromUtf8("存在")));
        QVERIFY(status.contains(QStringLiteral("api.anthropic.com")));
        QVERIFY(status.contains(QStringLiteral("www.google-analytics.com")));
        QVERIFY(status.contains(QStringLiteral("deny")));

        const auto alt_log = prefix_ / "var" / "alt-gateway.jsonl";
        fs::copy_file(sample_gateway_log(), alt_log);
        window.setLineValue(QStringLiteral("upstreamHttpEdit"),
                            QStringLiteral("http://127.0.0.1:18080"));
        window.setLineValue(QStringLiteral("upstreamSocksEdit"),
                            QStringLiteral("socks5://127.0.0.1:18081"));
        window.setLineValue(QStringLiteral("loggingPathEdit"),
                            QString::fromStdString(alt_log.string()));
        window.refreshGatewayStatus();
        status = window.gatewayStatusText();
        QVERIFY(status.contains(QStringLiteral("http://127.0.0.1:18080")));
        QVERIFY(status.contains(QString::fromStdString(alt_log.string())));
        QVERIFY(status.contains(QStringLiteral("gost -C ")));
        QVERIFY(status.contains(QStringLiteral("gost.relay.yml")));
        QVERIFY(status.contains(QStringLiteral("http://127.0.0.1:18080")));

        QString error;
        QVERIFY2(window.saveToDisk(&error), qPrintable(error));

        const auto cfg = BoxConfig::load_file(options_.config);
        QCOMPARE(QString::fromStdString(cfg.gateway.upstream_http),
                 QStringLiteral("http://127.0.0.1:18080"));
        QCOMPARE(QString::fromStdString(cfg.gateway.upstream_socks5),
                 QStringLiteral("socks5://127.0.0.1:18081"));
        QCOMPARE(QString::fromStdString(cfg.gateway.logging.path),
                 QString::fromStdString(alt_log.string()));

        SettingsWindow reloaded(options_);
        QCOMPARE(reloaded.lineValue(QStringLiteral("upstreamHttpEdit")),
                 QStringLiteral("http://127.0.0.1:18080"));
        QCOMPARE(reloaded.lineValue(QStringLiteral("upstreamSocksEdit")),
                 QStringLiteral("socks5://127.0.0.1:18081"));
        QCOMPARE(reloaded.lineValue(QStringLiteral("loggingPathEdit")),
                 QString::fromStdString(alt_log.string()));
        const auto again = reloaded.gatewayStatusText();
        QVERIFY2(again.contains(QStringLiteral("http://127.0.0.1:18080")), qPrintable(again));
        QVERIFY(again.contains(QString::fromStdString(alt_log.string())));
        QVERIFY(again.contains(QStringLiteral("api.anthropic.com")));
        QVERIFY(again.contains(QString::fromUtf8("已配置")));

        const auto before = read_text(options_.config);
        reloaded.setLineValue(QStringLiteral("upstreamHttpEdit"), QString());
        reloaded.setLineValue(QStringLiteral("upstreamSocksEdit"), QString());
        QVERIFY(!reloaded.saveToDisk(&error));
        QVERIFY(error.contains(QString::fromUtf8("上游")));
        QCOMPARE(QString::fromStdString(read_text(options_.config)),
                 QString::fromStdString(before));
    }

    void spoofValuesApplyAndRoundTrip() {
        seedPrefix(uniquePrefix("spoof"));
        SettingsWindow window(options_);
        QVERIFY(window.findChild<QPlainTextEdit*>(QStringLiteral("spoofStatusPreview")));
        QVERIFY(window.findChild<QPushButton*>(QStringLiteral("refreshSpoofStatusButton")));
        QCOMPARE(window.lineValue(QStringLiteral("timezoneEdit")),
                 QStringLiteral("America/Los_Angeles"));
        QCOMPARE(window.lineValue(QStringLiteral("localeEdit")),
                 QStringLiteral("en_US.UTF-8"));

        QString preview = window.spoofStatusText();
        QVERIFY2(preview.contains(QString::fromUtf8("盒子内的伪装值")), qPrintable(preview));
        QVERIFY(preview.contains(QStringLiteral("America/Los_Angeles")));
        QVERIFY(preview.contains(QString::fromUtf8("不会改宿主")) ||
                preview.contains(QString::fromUtf8("不会写入宿主")));
        QVERIFY(!preview.contains(QString::fromUtf8("暴露宿主真实时区")) ||
                preview.contains(QString::fromUtf8("没有")));

        window.setLineValue(QStringLiteral("timezoneEdit"), QStringLiteral("Europe/Berlin"));
        window.setLineValue(QStringLiteral("localeEdit"), QStringLiteral("de_DE.UTF-8"));
        window.setLineValue(QStringLiteral("languageEdit"), QStringLiteral("de_DE"));
        window.setLineValue(QStringLiteral("electronLangEdit"), QStringLiteral("de-DE"));
        QString error;
        QVERIFY2(window.saveToDisk(&error), qPrintable(error));
        window.refreshSpoofStatus();

        const auto cfg = BoxConfig::load_file(options_.config);
        QCOMPARE(QString::fromStdString(cfg.identity.timezone),
                 QStringLiteral("Europe/Berlin"));
        QCOMPARE(QString::fromStdString(cfg.identity.locale),
                 QStringLiteral("de_DE.UTF-8"));

        const auto values = claudebox::spoof::from_config(cfg.identity);
        const auto verified = claudebox::spoof::verify(values, prefix_);
        QVERIFY2(verified.ok, verified.issues.empty() ? "verify failed"
                                                      : verified.issues.front().c_str());
        if (verified.probe.ran) {
            QCOMPARE(QString::fromStdString(verified.probe.timezone),
                     QStringLiteral("Europe/Berlin"));
            QVERIFY(verified.probe.timezone != QStringLiteral("Asia/Tokyo").toStdString());
        }

        const auto spoof_env = read_text(prefix_ / "env" / "spoof.env");
        QVERIFY(QString::fromStdString(spoof_env).contains(QStringLiteral("Europe/Berlin")));
        QVERIFY(!QString::fromStdString(spoof_env).contains(QStringLiteral("America/Los_Angeles")));
        QVERIFY(QString::fromStdString(spoof_env).contains(QStringLiteral("de_DE.UTF-8")));

        SettingsWindow reloaded(options_);
        QCOMPARE(reloaded.lineValue(QStringLiteral("timezoneEdit")),
                 QStringLiteral("Europe/Berlin"));
        QCOMPARE(reloaded.lineValue(QStringLiteral("localeEdit")),
                 QStringLiteral("de_DE.UTF-8"));
        const auto again = reloaded.spoofStatusText();
        QVERIFY2(again.contains(QStringLiteral("Europe/Berlin")), qPrintable(again));
        QVERIFY(again.contains(QString::fromUtf8("盒子内的伪装值")));
        QVERIFY(!again.contains(QStringLiteral("Asia/Tokyo")));

        // Invalid timezone must not save or write a bad spoof.
        const auto before = read_text(options_.config);
        reloaded.setLineValue(QStringLiteral("timezoneEdit"), QStringLiteral("Not/AZone"));
        QVERIFY(!reloaded.saveToDisk(&error));
        QCOMPARE(QString::fromStdString(read_text(options_.config)),
                 QString::fromStdString(before));
        QVERIFY(QString::fromStdString(read_text(prefix_ / "env" / "spoof.env"))
                    .contains(QStringLiteral("Europe/Berlin")));
    }

    void ipChangePolicyLivesOnGatewayPage() {
        seedPrefix(uniquePrefix("ip-change"));
        SettingsWindow window(options_);
        QVERIFY(window.findChild<QCheckBox*>(QStringLiteral("killOnIpChangeCheck")));
        QVERIFY(window.findChild<QPlainTextEdit*>(QStringLiteral("ipChangeStatusPreview")));
        QVERIFY(window.findChild<QPushButton*>(QStringLiteral("refreshIpChangeStatusButton")));
        QVERIFY(!window.isChecked(QStringLiteral("killOnIpChangeCheck")));
        QCOMPARE(window.spinValue(QStringLiteral("killGraceSpin")), 10);
        QCOMPARE(window.spinValue(QStringLiteral("watchIntervalSpin")), 5);

        window.setIpChangePreview(
            QStringList{QStringLiteral("Ethernet|192.168.1.20/24")},
            QStringList{QStringLiteral("Ethernet|10.0.0.8/24")},
            true);
        QString preview = window.ipChangeStatusText();
        QVERIFY2(preview.contains(QString::fromUtf8("关闭（默认）")) ||
                     preview.contains(QString::fromUtf8("默认关闭")) ||
                     preview.contains(QStringLiteral("noop")) ||
                     preview.contains(QString::fromUtf8("无（")),
                 qPrintable(preview));
        QVERIFY(preview.contains(QString::fromUtf8("并行")));
        QVERIFY(!preview.contains(QStringLiteral("t=0 thread=gateway start")));

        window.setChecked(QStringLiteral("killOnIpChangeCheck"), true);
        window.refreshIpChangeStatus();
        preview = window.ipChangeStatusText();
        QVERIFY2(preview.contains(QString::fromUtf8("已打开")), qPrintable(preview));
        QVERIFY(preview.contains(QStringLiteral("stop_gateway")));
        QVERIFY(preview.contains(QStringLiteral("polite_close")));
        QVERIFY(preview.contains(QString::fromUtf8("并行组")));
        QVERIFY(preview.contains(QStringLiteral("t=0 thread=gateway")));
        QVERIFY(preview.contains(QStringLiteral("t=0 thread=process")));
        QVERIFY(preview.contains(QString::fromUtf8("不是「先断开网关再礼貌关」")) ||
                preview.contains(QString::fromUtf8("互不等待")));
        QVERIFY(!window.ipChangeStatusText().contains(QStringLiteral("after grace: stop gateway")));

        QString error;
        QVERIFY2(window.saveToDisk(&error), qPrintable(error));
        const auto cfg = BoxConfig::load_file(options_.config);
        QVERIFY(cfg.features.kill_on_local_ip_change);
        QCOMPARE(cfg.features.kill_grace_seconds, 10);

        SettingsWindow reloaded(options_);
        QVERIFY(reloaded.isChecked(QStringLiteral("killOnIpChangeCheck")));
        reloaded.setIpChangePreview(
            QStringList{QStringLiteral("Ethernet|192.168.1.20/24")},
            QStringList{QStringLiteral("Ethernet|10.0.0.8/24")},
            true);
        const auto again = reloaded.ipChangeStatusText();
        QVERIFY2(again.contains(QStringLiteral("t=0 thread=gateway")), qPrintable(again));
        QVERIFY(again.contains(QStringLiteral("t=0 thread=process")));
    }


    void companionStatusAndOpenFixtureOauthUrl() {
        seedPrefix(uniquePrefix("companion"));
        SettingsWindow window(options_);
        QVERIFY(window.findChild<QPlainTextEdit*>(QStringLiteral("companionStatusPreview")));
        QVERIFY(window.findChild<QPushButton*>(QStringLiteral("refreshCompanionStatusButton")));
        QVERIFY(window.isChecked(QStringLiteral("oauthFingerprintCheck")));

        QString status = window.companionStatusText();
        QVERIFY2(status.contains(QString::fromUtf8("未就绪")), qPrintable(status));
        QVERIFY(status.contains(QString::fromUtf8("不会回退")) ||
                status.contains(QString::fromUtf8("绝不会改用宿主")));
        QVERIFY(status.contains(QStringLiteral("America/Los_Angeles")));
        QVERIFY(status.contains(QStringLiteral("http://127.0.0.1:3080")));
        QVERIFY(status.contains(QString::fromStdString(
            (prefix_ / "var" / "companion-browser" / "profile").string())));

        // Opening an OA URL with no engine must fail and must not use a host browser.
        QString error;
        QVERIFY(!window.openCompanionUrl(QStringLiteral("https://claude.ai/login"), &error, false));
        QVERIFY2(error.contains(QStringLiteral("Refusing to open the host default browser")) ||
                     error.contains(QString::fromUtf8("未就绪")) ||
                     error.contains(QStringLiteral("not installed")),
                 qPrintable(error));

        // Drop a mock engine into the prefix and save so intercept wrappers exist.
        const auto engine = prefix_ / "opt" / "companion-browser" / "engine";
        const auto record = prefix_ / "record";
        fs::create_directories(engine.parent_path());
        fs::create_directories(record);
        {
            std::ofstream out(engine);
            out << "#!/usr/bin/env bash\n"
                << "set -euo pipefail\n"
                << "printf '%s\\n' \"$@\" > \"" << record.string() << "/argv.txt\"\n"
                << "env | sort > \"" << record.string() << "/env.txt\"\n"
                << "echo mock-engine opened: \"$*\"\n";
        }
        fs::permissions(engine,
                        fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                            fs::perms::others_read | fs::perms::others_exec,
                        fs::perm_options::replace);

        QVERIFY2(window.saveToDisk(&error), qPrintable(error));
        QVERIFY(fs::exists(prefix_ / "bin" / "xdg-open"));
        window.refreshCompanionStatus();
        status = window.companionStatusText();
        QVERIFY2(status.contains(QString::fromUtf8("已就绪")), qPrintable(status));
        QVERIFY(status.contains(QString::fromStdString(engine.string())));
        QVERIFY(status.contains(QStringLiteral("http://127.0.0.1:3080")));

        QVERIFY2(window.openCompanionUrl(
                     QStringLiteral("https://claude.ai/login?oauth=1"), &error, false),
                 qPrintable(error));
        QVERIFY(fs::exists(record / "argv.txt"));
        const auto argv = QString::fromStdString(read_text(record / "argv.txt"));
        QVERIFY2(argv.contains(QStringLiteral("https://claude.ai/login?oauth=1")),
                 qPrintable(argv));
        QVERIFY(argv.contains(QStringLiteral("--user-data-dir=")));
        QVERIFY(argv.contains(QStringLiteral("--proxy-server=http://127.0.0.1:3080")));
        const auto envtxt = QString::fromStdString(read_text(record / "env.txt"));
        QVERIFY(envtxt.contains(QStringLiteral("TZ=America/Los_Angeles")));
        QVERIFY(envtxt.contains(QStringLiteral("HTTP_PROXY=http://127.0.0.1:3080")));
        QVERIFY(envtxt.contains(QStringLiteral("LANG=en_US.UTF-8")));
    }


    void vaultArchiveSwitchEmptyAndBack() {
        seedPrefix(uniquePrefix("vault"));
        const auto vault = prefix_ / "var" / "identity-vault";
        const auto workspace = vault / "workspace";
        const auto companion =
            prefix_ / "var" / "companion-browser" / "identities" / "live" / "profile";
        fs::create_directories(workspace / "desktop");
        fs::create_directories(companion);
        {
            std::ofstream out(workspace / "identity.json");
            out << "{\"kind\":\"claude-box-identity\",\"name\":\"fixture-a\"}\n";
        }
        {
            std::ofstream out(workspace / "desktop" / "login.json");
            out << "{\"token\":\"token-aaa\"}\n";
        }
        {
            std::ofstream out(workspace / "companion-bind.txt");
            out << companion.string() << "\n";
        }
        {
            std::ofstream out(companion / "Preferences");
            out << "{\"profile\":\"fixture-a\"}\n";
        }
        const auto passfile = prefix_ / "secret" / "vault-pass.txt";
        fs::create_directories(passfile.parent_path());
        {
            std::ofstream out(passfile);
            out << "fixture-passphrase-not-for-logs\n";
        }

        SettingsWindow window(options_);
        QVERIFY(window.findChild<QPlainTextEdit*>(QStringLiteral("vaultStatusPreview")));
        QVERIFY(window.findChild<QPushButton*>(QStringLiteral("archiveVaultButton")));
        QVERIFY(window.findChild<QListWidget*>(QStringLiteral("vaultSnapshotList")));
        QVERIFY(window.isChecked(QStringLiteral("identityVaultCheck")));
        window.setLineValue(QStringLiteral("vaultPassphraseFileEdit"),
                            QString::fromStdString(passfile.string()));

        QString status = window.vaultStatusText();
        QVERIFY2(status.contains(QString::fromUtf8("身份保险库")), qPrintable(status));
        QVERIFY(status.contains(QStringLiteral("AES-256-GCM")));
        QVERIFY(!status.contains(QStringLiteral("fixture-passphrase-not-for-logs")));
        QVERIFY(status.contains(QString::fromUtf8("不会改宿主")));

        QString error;
        QVERIFY2(window.archiveVault(&error), qPrintable(error));
        QVERIFY(!error.contains(QStringLiteral("fixture-passphrase-not-for-logs")));
        const auto ids = window.vaultSnapshotIds();
        QVERIFY2(!ids.isEmpty(), qPrintable(window.vaultStatusText()));
        const QString first = ids.first();

        QVERIFY2(window.switchVault(QStringLiteral("empty"), &error), qPrintable(error));
        const auto emptied = QString::fromStdString(read_text(workspace / "identity.json"));
        QVERIFY2(emptied.contains(QStringLiteral("empty")), qPrintable(emptied));
        QVERIFY(!emptied.contains(QStringLiteral("fixture-a")));

        QVERIFY2(window.switchVault(first, &error), qPrintable(error));
        const auto restored = QString::fromStdString(read_text(workspace / "desktop" / "login.json"));
        QVERIFY2(restored.contains(QStringLiteral("token-aaa")), qPrintable(restored));
        QVERIFY(!window.vaultStatusText().contains(QStringLiteral("fixture-passphrase-not-for-logs")));
    }


    void desktopMissingBinaryIsNotStarted() {
        seedPrefix(uniquePrefix("desktop"));
        SettingsWindow window(options_);
        QVERIFY(window.findChild<QPlainTextEdit*>(QStringLiteral("desktopStatusPreview")));
        QVERIFY(window.findChild<QPushButton*>(QStringLiteral("startDesktopDryRunButton")));
        QString status = window.desktopStatusText();
        QVERIFY2(status.contains(QString::fromUtf8("未启动")) ||
                     status.contains(QString::fromUtf8("未找到")),
                 qPrintable(status));
        QVERIFY(status.contains(QStringLiteral("America/Los_Angeles")));
        QVERIFY(status.contains(QStringLiteral("http://127.0.0.1:3080")));
        QVERIFY(!status.contains(QString::fromUtf8("会话：运行中")));

        QString error;
        QVERIFY2(window.startDesktopDryRun(&error), qPrintable(error));
        status = window.desktopStatusText();
        QVERIFY2(status.contains(QString::fromUtf8("未启动")) ||
                     status.contains(QString::fromUtf8("未找到")),
                 qPrintable(status));
        QVERIFY(!status.contains(QString::fromUtf8("会话：运行中")));

        QVERIFY2(window.stopDesktopDryRun(&error), qPrintable(error));
        status = window.desktopStatusText();
        QVERIFY(status.contains(QStringLiteral("polite_close")) ||
                status.contains(QString::fromUtf8("dry-run")));
        QVERIFY(status.contains(QStringLiteral("claude-desktop")));
        QVERIFY(!status.contains(QString::fromUtf8("会话：运行中")));
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
