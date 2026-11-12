#include "settings_window.hpp"

#include <filesystem>

#include "claudebox/identity.hpp"
#include "claudebox/mount.hpp"
#include "claudebox/gateway.hpp"
#include "claudebox/gateway_ip.hpp"
#include "claudebox/spoof.hpp"
#include "claudebox/companion.hpp"

#include <algorithm>
#include <vector>

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

using claudebox::config::VisibleDir;
using claudebox::identity::FieldKind;
using claudebox::identity::FieldMode;
using claudebox::identity::canonical_field;
using claudebox::identity::canonical_field_ids;
using claudebox::identity::classify_key;
using claudebox::identity::field_mode_from_name;
using claudebox::identity::field_mode_name;
using claudebox::identity::inventory_value;
using claudebox::identity::kind_of_canonical;
using claudebox::identity::normalize_key;
using claudebox::settings::field_kind_label_zh;
using claudebox::settings::field_label_zh;
using claudebox::settings::field_mode_label_zh;

namespace {

QString u8(const char* s) { return QString::fromUtf8(s); }

QWidget* wrapScroll(QWidget* inner) {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(inner);
    return scroll;
}

QLabel* noteLabel(const QString& text) {
    auto* lab = new QLabel(text);
    lab->setWordWrap(true);
    lab->setStyleSheet(QStringLiteral("color: #444;"));
    return lab;
}

QLineEdit* namedEdit(const QString& object_name, const QString& placeholder = {}) {
    auto* edit = new QLineEdit;
    edit->setObjectName(object_name);
    if (!placeholder.isEmpty()) {
        edit->setPlaceholderText(placeholder);
    }
    return edit;
}

void addListItem(QListWidget* list, const QString& text) {
    const auto trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->text() == trimmed) {
            list->setCurrentRow(i);
            return;
        }
    }
    list->addItem(trimmed);
}

int modeToIndex(FieldMode mode) {
    switch (mode) {
        case FieldMode::Expose:
            return 0;
        case FieldMode::Hide:
            return 1;
        case FieldMode::Override:
            return 2;
    }
    return 1;
}

FieldMode indexToMode(int index) {
    if (index == 0) return FieldMode::Expose;
    if (index == 2) return FieldMode::Override;
    return FieldMode::Hide;
}

QComboBox* makeModeCombo(FieldMode mode) {
    auto* combo = new QComboBox;
    combo->setObjectName(QStringLiteral("modeCombo"));
    combo->addItem(u8("原样展示"), QStringLiteral("expose"));
    combo->addItem(u8("隐藏"), QStringLiteral("hide"));
    combo->addItem(u8("修改后展示"), QStringLiteral("override"));
    combo->setCurrentIndex(modeToIndex(mode));
    return combo;
}

}  // namespace

SettingsWindow::SettingsWindow(claudebox::settings::Options options, QWidget* parent)
    : QMainWindow(parent), options_(std::move(options)), session_(claudebox::settings::Session::load(options_)) {
    setWindowTitle(u8("Claude Box 设置"));
    setObjectName(QStringLiteral("settingsWindow"));
    resize(1080, 740);
    setMinimumSize(880, 600);
    buildUi();
    syncFromSession();
}

void SettingsWindow::buildUi() {
    auto* central = new QWidget;
    setCentralWidget(central);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(8, 8, 8, 8);

    auto* splitter = new QSplitter(Qt::Horizontal);
    auto* nav = new QListWidget;
    nav->setObjectName(QStringLiteral("settingsNav"));
    nav->setFixedWidth(168);
    const QStringList pages = {
        u8("基座"),
        u8("网关"),
        u8("可见目录"),
        u8("挂载"),
        u8("身份伪装"),
        u8("宿主身份"),
        u8("桌面"),
        u8("功能"),
        u8("伴生浏览器"),
        u8("运行时预览"),
    };
    for (const auto& name : pages) {
        nav->addItem(name);
    }

    auto* stack = new QStackedWidget;
    stack->setObjectName(QStringLiteral("settingsStack"));
    stack->addWidget(buildBasePage());
    stack->addWidget(buildGatewayPage());
    stack->addWidget(buildVisibleDirsPage());
    stack->addWidget(buildMountPage());
    stack->addWidget(buildSpoofPage());
    stack->addWidget(buildIdentityPage());
    stack->addWidget(buildDesktopPage());
    stack->addWidget(buildFeaturesPage());
    stack->addWidget(buildCompanionPage());
    stack->addWidget(buildRuntimePage());

    connect(nav, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
    nav->setCurrentRow(0);

    splitter->addWidget(nav);
    splitter->addWidget(stack);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto* reload = new QPushButton(u8("重新加载"));
    reload->setObjectName(QStringLiteral("reloadButton"));
    auto* save = new QPushButton(u8("保存"));
    save->setObjectName(QStringLiteral("saveButton"));
    save->setDefault(true);
    buttons->addWidget(reload);
    buttons->addWidget(save);
    root->addLayout(buttons);

    connect(save, &QPushButton::clicked, this, &SettingsWindow::onSave);
    connect(reload, &QPushButton::clicked, this, &SettingsWindow::onReload);

    statusBar()->showMessage(u8("配置只写入盒子前缀，不会改宿主时区、语言或默认浏览器。"));
}

QWidget* SettingsWindow::buildBasePage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(noteLabel(
        u8("发行版基座配置。云电脑上只保存这些字段，不会真正调用 wsl.exe 或启用 WSL。")));
    auto* form = new QFormLayout;
    form->addRow(u8("配置名"), namedEdit(QStringLiteral("nameEdit")));
    form->addRow(u8("发行版名称"), namedEdit(QStringLiteral("distroNameEdit")));
    form->addRow(u8("首选来源发行版"), namedEdit(QStringLiteral("preferredSourceEdit")));
    form->addRow(u8("安装目录"), namedEdit(QStringLiteral("installDirEdit"),
                                       u8("空 = 宿主默认目录")));
    layout->addLayout(form);
    layout->addStretch(1);
    return wrapScroll(page);
}

QWidget* SettingsWindow::buildGatewayPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(noteLabel(
        u8("配置唯一网关的上游、拦截名单、JSONL 日志，以及本机 IP 波动时的响应。"
           "保存只更新盒子配置。本页可以预览中继计划、最近日志和 IP 波动的并行动作，"
           "但不会在这台云电脑上启动或停止 gost，也不会绑定 Win11 网卡或结束真实进程。"
           "gost 仍由现有 ClaudeBox.Gateway 模块在 Win11 上启动。")));
    auto* form = new QFormLayout;
    form->addRow(u8("宿主地址"), namedEdit(QStringLiteral("gatewayHostEdit")));
    auto* port = new QSpinBox;
    port->setObjectName(QStringLiteral("gatewayPortSpin"));
    port->setRange(1, 65535);
    form->addRow(u8("HTTP 端口"), port);
    form->addRow(u8("上游 HTTP"), namedEdit(QStringLiteral("upstreamHttpEdit")));
    form->addRow(u8("上游 SOCKS5"), namedEdit(QStringLiteral("upstreamSocksEdit")));
    layout->addLayout(form);

    auto* blBox = new QGroupBox(u8("拦截名单"));
    auto* blLayout = new QVBoxLayout(blBox);
    blocklist_ = new QListWidget;
    blocklist_->setObjectName(QStringLiteral("blocklistWidget"));
    blLayout->addWidget(blocklist_);
    auto* blRow = new QHBoxLayout;
    blocklist_edit_ = namedEdit(QStringLiteral("blocklistEdit"), u8("例如 ingest.example.test"));
    auto* addBl = new QPushButton(u8("添加"));
    addBl->setObjectName(QStringLiteral("addBlocklistButton"));
    auto* delBl = new QPushButton(u8("删除所选"));
    delBl->setObjectName(QStringLiteral("removeBlocklistButton"));
    blRow->addWidget(blocklist_edit_, 1);
    blRow->addWidget(addBl);
    blRow->addWidget(delBl);
    blLayout->addLayout(blRow);
    layout->addWidget(blBox);

    auto* logBox = new QGroupBox(u8("日志"));
    auto* logForm = new QFormLayout(logBox);
    auto* enabled = new QCheckBox(u8("启用 JSONL 日志"));
    enabled->setObjectName(QStringLiteral("loggingEnabledCheck"));
    logForm->addRow(enabled);
    logForm->addRow(u8("日志路径"), namedEdit(QStringLiteral("loggingPathEdit"),
                                         u8("空 = 盒子前缀下 var/logs/gateway.jsonl")));
    layout->addWidget(logBox);

    auto* ipBox = new QGroupBox(u8("本机 IP 波动响应"));
    auto* ipLayout = new QVBoxLayout(ipBox);
    ipLayout->addWidget(noteLabel(
        u8("这是唯一网关模块的策略，默认关闭。打开后，一旦宿主本机 IP 集合变化，"
           "断开网关和礼貌关闭盒子内 Claude 会作为两条并行线程同时启动，互不等待。"
           "进程在下方 grace 秒后若仍存活再强杀；网关断开不会等到这段 grace。"
           "只作用于盒子内的 claude-desktop / claude / Claude，不会结束机器上的其他进程。"
           "本页预览是 dry-run，不会调用 wsl.exe，也不会真的停止 gost。")));
    auto* kill = new QCheckBox(u8("本机 IP 变化时断开网关并结束盒子内 Claude（默认关）"));
    kill->setObjectName(QStringLiteral("killOnIpChangeCheck"));
    ipLayout->addWidget(kill);
    auto* ipForm = new QFormLayout;
    auto* grace = new QSpinBox;
    grace->setObjectName(QStringLiteral("killGraceSpin"));
    grace->setRange(0, 600);
    grace->setSuffix(u8(" 秒"));
    ipForm->addRow(u8("进程礼貌关后的等待（grace）"), grace);
    auto* interval = new QSpinBox;
    interval->setObjectName(QStringLiteral("watchIntervalSpin"));
    interval->setRange(1, 600);
    interval->setSuffix(u8(" 秒"));
    ipForm->addRow(u8("监视间隔"), interval);
    ipLayout->addLayout(ipForm);
    ip_change_status_ = new QPlainTextEdit;
    ip_change_status_->setObjectName(QStringLiteral("ipChangeStatusPreview"));
    ip_change_status_->setReadOnly(true);
    ip_change_status_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    ip_change_status_->setMinimumHeight(180);
    auto ipFont = ip_change_status_->font();
    ipFont.setStyleHint(QFont::Monospace);
    ipFont.setFamily(QStringLiteral("monospace"));
    ip_change_status_->setFont(ipFont);
    ipLayout->addWidget(ip_change_status_, 1);
    auto* refreshIp = new QPushButton(u8("刷新 IP 波动预览"));
    refreshIp->setObjectName(QStringLiteral("refreshIpChangeStatusButton"));
    ipLayout->addWidget(refreshIp, 0, Qt::AlignLeft);
    layout->addWidget(ipBox);
    connect(refreshIp, &QPushButton::clicked, this, &SettingsWindow::onRefreshIpChangeStatus);

    auto* statusBox = new QGroupBox(u8("状态与预览"));
    auto* statusLayout = new QVBoxLayout(statusBox);
    statusLayout->addWidget(noteLabel(
        u8("根据当前表单生成 dry-run 中继计划，并读取本地 JSONL 的最近活动。"
           "刷新可对着夹具或临时前缀工作，不会探测真实上游，也不会启动进程。")));
    gateway_status_ = new QPlainTextEdit;
    gateway_status_->setObjectName(QStringLiteral("gatewayStatusPreview"));
    gateway_status_->setReadOnly(true);
    gateway_status_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    gateway_status_->setMinimumHeight(220);
    auto gwFont = gateway_status_->font();
    gwFont.setStyleHint(QFont::Monospace);
    gwFont.setFamily(QStringLiteral("monospace"));
    gateway_status_->setFont(gwFont);
    statusLayout->addWidget(gateway_status_, 1);
    auto* refreshGw = new QPushButton(u8("刷新状态"));
    refreshGw->setObjectName(QStringLiteral("refreshGatewayStatusButton"));
    statusLayout->addWidget(refreshGw, 0, Qt::AlignLeft);
    layout->addWidget(statusBox, 1);

    connect(addBl, &QPushButton::clicked, this, &SettingsWindow::onAddBlocklistItem);
    connect(delBl, &QPushButton::clicked, this, &SettingsWindow::onRemoveBlocklistItem);
    connect(refreshGw, &QPushButton::clicked, this, &SettingsWindow::onRefreshGatewayStatus);
    return wrapScroll(page);
}

QWidget* SettingsWindow::buildVisibleDirsPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(noteLabel(
        u8("只挂这些宿主目录到盒子里。客机路径建议落在 /mnt/claude-box/ 下。"
           "不要把整个宿主系统盘或整棵 Users 挂进去。")));
    dirs_table_ = new QTableWidget(0, 3);
    dirs_table_->setObjectName(QStringLiteral("visibleDirsTable"));
    dirs_table_->setHorizontalHeaderLabels({u8("宿主路径"), u8("客机路径"), u8("只读")});
    dirs_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    dirs_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    dirs_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    dirs_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    dirs_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(dirs_table_, 1);
    auto* row = new QHBoxLayout;
    auto* add = new QPushButton(u8("添加目录"));
    add->setObjectName(QStringLiteral("addVisibleDirButton"));
    auto* del = new QPushButton(u8("删除所选"));
    del->setObjectName(QStringLiteral("removeVisibleDirButton"));
    row->addWidget(add);
    row->addWidget(del);
    row->addStretch(1);
    layout->addLayout(row);
    connect(add, &QPushButton::clicked, this, &SettingsWindow::onAddVisibleDir);
    connect(del, &QPushButton::clicked, this, &SettingsWindow::onRemoveVisibleDir);
    return wrapScroll(page);
}

QWidget* SettingsWindow::buildMountPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(noteLabel(
        u8("关闭默认 automount 后，发行版不应再自动出现 /mnt/c。"
           "本页只保存该配置；真正写 /etc/wsl.conf 要在真机由现有脚本执行。")));
    auto* check = new QCheckBox(u8("禁用 WSL 默认 automount（/etc/wsl.conf automount.enabled=false）"));
    check->setObjectName(QStringLiteral("disableAutomountCheck"));
    layout->addWidget(check);
    layout->addStretch(1);
    return wrapScroll(page);
}

QWidget* SettingsWindow::buildSpoofPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(noteLabel(
        u8("以下全部是盒子内的伪装值，不是宿主真实时区或语言。"
           "保存时会写入盒子配置，并把伪装应用到盒子前缀"
           "（env/spoof.env、env/session.env、etc/localtime）。"
           "不会改宿主 /etc/localtime、系统时区、显示语言或默认浏览器，"
           "也没有「把宿主真实时区 / 语言交给 Claude」的选项或清单字段。")));
    auto* form = new QFormLayout;
    form->addRow(u8("盒子时区（IANA）"), namedEdit(QStringLiteral("timezoneEdit")));
    form->addRow(u8("盒子区域设置（locale）"), namedEdit(QStringLiteral("localeEdit")));
    form->addRow(u8("盒子语言"), namedEdit(QStringLiteral("languageEdit")));
    form->addRow(u8("盒子主机名"), namedEdit(QStringLiteral("hostnameEdit")));
    form->addRow(u8("盒子 Electron 语言"), namedEdit(QStringLiteral("electronLangEdit")));
    layout->addLayout(form);

    auto* statusBox = new QGroupBox(u8("伪装状态与预览"));
    auto* statusLayout = new QVBoxLayout(statusBox);
    statusLayout->addWidget(noteLabel(
        u8("根据当前表单生成盒子伪装计划，并在已保存时校验前缀里的伪装文件与 Intl 探针。"
           "这里看到的永远是配置中的假时区 / 语言，不会读取或展示宿主真实时区。")));
    spoof_status_ = new QPlainTextEdit;
    spoof_status_->setObjectName(QStringLiteral("spoofStatusPreview"));
    spoof_status_->setReadOnly(true);
    spoof_status_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    spoof_status_->setMinimumHeight(180);
    auto font = spoof_status_->font();
    font.setStyleHint(QFont::Monospace);
    font.setFamily(QStringLiteral("monospace"));
    spoof_status_->setFont(font);
    statusLayout->addWidget(spoof_status_, 1);
    auto* refresh = new QPushButton(u8("刷新伪装预览"));
    refresh->setObjectName(QStringLiteral("refreshSpoofStatusButton"));
    statusLayout->addWidget(refresh, 0, Qt::AlignLeft);
    layout->addWidget(statusBox, 1);
    connect(refresh, &QPushButton::clicked, this, &SettingsWindow::onRefreshSpoofStatus);
    return wrapScroll(page);
}

QWidget* SettingsWindow::buildIdentityPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(noteLabel(
        u8("原始清单只在设置页可见。展示层只包含选择「原样展示」或「修改后展示」的字段。"
           "默认厂商和型号可原样展示，硬件编码默认隐藏。"
           "过渡约束 hide_identifiers 保持开启，关闭它不会一键公开整份清单；"
           "要展示或修改某个字段，请用下表的每字段策略。")));
    identity_table_ = new QTableWidget(0, 5);
    identity_table_->setObjectName(QStringLiteral("identityTable"));
    identity_table_->setHorizontalHeaderLabels(
        {u8("字段"), u8("类别"), u8("原始值"), u8("处理方式"), u8("修改后的值")});
    identity_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    identity_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    identity_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    identity_table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    identity_table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    identity_table_->verticalHeader()->setVisible(false);
    identity_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(identity_table_, 1);
    return wrapScroll(page);
}

QWidget* SettingsWindow::buildDesktopPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(noteLabel(
        u8("Desktop 安装与启动配置。本页只负责读写这些字段；真正启停 Desktop 属于后续模块。")));
    auto* form = new QFormLayout;
    form->addRow(u8("deb 下载 URL"), namedEdit(QStringLiteral("debUrlEdit")));
    form->addRow(u8("本地 deb 路径"), namedEdit(QStringLiteral("debLocalEdit")));
    form->addRow(u8("启动命令"), namedEdit(QStringLiteral("desktopCommandEdit")));
    layout->addLayout(form);

    auto* argBox = new QGroupBox(u8("额外参数"));
    auto* argLayout = new QVBoxLayout(argBox);
    extra_args_ = new QListWidget;
    extra_args_->setObjectName(QStringLiteral("extraArgsWidget"));
    argLayout->addWidget(extra_args_);
    auto* row = new QHBoxLayout;
    extra_args_edit_ = namedEdit(QStringLiteral("extraArgsEdit"), u8("例如 --ozone-platform=x11"));
    auto* add = new QPushButton(u8("添加"));
    add->setObjectName(QStringLiteral("addExtraArgButton"));
    auto* del = new QPushButton(u8("删除所选"));
    del->setObjectName(QStringLiteral("removeExtraArgButton"));
    row->addWidget(extra_args_edit_, 1);
    row->addWidget(add);
    row->addWidget(del);
    argLayout->addLayout(row);
    layout->addWidget(argBox);

    auto* asar = new QCheckBox(u8("启用 asar 补丁（备用，默认关）"));
    asar->setObjectName(QStringLiteral("asarPatchCheck"));
    layout->addWidget(asar);
    layout->addStretch(1);
    connect(add, &QPushButton::clicked, this, &SettingsWindow::onAddExtraArg);
    connect(del, &QPushButton::clicked, this, &SettingsWindow::onRemoveExtraArg);
    return wrapScroll(page);
}

QWidget* SettingsWindow::buildFeaturesPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(noteLabel(
        u8("功能开关同样是配置项。伴生指纹浏览器的状态与拦截预览在「伴生浏览器」页。身份保险库的运行时接线属于后续模块。"
           "本机 IP 波动时的网关断开与进程结束策略在「网关」页配置（同一模块，默认关），这里只作说明。")));
    auto add = [&](const QString& name, const QString& label) {
        auto* check = new QCheckBox(label);
        check->setObjectName(name);
        layout->addWidget(check);
    };
    add(QStringLiteral("excludeHostSystemDriveCheck"),
        u8("排除宿主系统盘（拒绝把整个 C: 挂进盒子）"));
    add(QStringLiteral("computerUseCheck"), u8("允许 computer use"));
    auto* ipNote = noteLabel(
        u8("本机 IP 变化时：在「网关」页打开开关。动作是并行启动「断开网关」和"
           "「礼貌关闭盒子内 Claude」，互不等待；进程 grace 后如仍存活再强杀。默认关闭。"));
    ipNote->setObjectName(QStringLiteral("killOnIpChangeFeatureNote"));
    layout->addWidget(ipNote);
    add(QStringLiteral("oauthFingerprintCheck"),
        u8("OAuth / 授权页用伴生指纹浏览器打开（不走宿主默认浏览器）"));
    add(QStringLiteral("identityVaultCheck"), u8("启用盒子内身份保险库"));
    layout->addStretch(1);
    return wrapScroll(page);
}

QWidget* SettingsWindow::buildCompanionPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(noteLabel(
        u8("Claude 跳转 OAuth / 授权页时，必须用盒子内的伴生指纹浏览器打开，"
           "不会回退到宿主默认浏览器（xdg-open / chrome / msedge）。"
           "资料目录是盒子前缀下的独立 user-data-dir，时区 / 语言与盒子伪装值对齐，"
           "出网仍走唯一网关。本页不会自动下载 Fingerprint Chromium，"
           "也不会在这台云电脑上改宿主默认浏览器或弹出真实窗口。"
           "开关与「功能」页是同一项 features.oauth_open_in_fingerprint_browser。")));
    auto* statusBox = new QGroupBox(u8("状态与预览"));
    auto* statusLayout = new QVBoxLayout(statusBox);
    companion_status_ = new QPlainTextEdit;
    companion_status_->setObjectName(QStringLiteral("companionStatusPreview"));
    companion_status_->setReadOnly(true);
    companion_status_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    companion_status_->setMinimumHeight(220);
    auto font = companion_status_->font();
    font.setStyleHint(QFont::Monospace);
    font.setFamily(QStringLiteral("monospace"));
    companion_status_->setFont(font);
    statusLayout->addWidget(companion_status_, 1);
    auto* refresh = new QPushButton(u8("刷新伴生浏览器状态"));
    refresh->setObjectName(QStringLiteral("refreshCompanionStatusButton"));
    statusLayout->addWidget(refresh, 0, Qt::AlignLeft);
    layout->addWidget(statusBox, 1);
    connect(refresh, &QPushButton::clicked, this, &SettingsWindow::onRefreshCompanionStatus);
    return wrapScroll(page);
}

QWidget* SettingsWindow::buildRuntimePage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(noteLabel(
        u8("根据当前设置生成发行版生命周期和可见目录挂载计划。这是 dry-run："
           "不会调用 wsl.exe，也不会在这台云电脑上启用 WSL 或改写活的 /mnt/c。")));
    runtime_preview_ = new QPlainTextEdit;
    runtime_preview_->setObjectName(QStringLiteral("runtimePreview"));
    runtime_preview_->setReadOnly(true);
    runtime_preview_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    auto font = runtime_preview_->font();
    font.setStyleHint(QFont::Monospace);
    font.setFamily(QStringLiteral("monospace"));
    runtime_preview_->setFont(font);
    layout->addWidget(runtime_preview_, 1);
    auto* refresh = new QPushButton(u8("刷新预览"));
    refresh->setObjectName(QStringLiteral("refreshRuntimePreviewButton"));
    layout->addWidget(refresh, 0, Qt::AlignLeft);
    connect(refresh, &QPushButton::clicked, this, &SettingsWindow::refreshRuntimePreview);
    return wrapScroll(page);
}

void SettingsWindow::fillIdentityTable() {
    identity_table_->setRowCount(0);
    std::vector<std::string> ids = canonical_field_ids();
    for (const auto& [key, val] : session_.inventory) {
        auto canon = canonical_field(key);
        const std::string id = canon ? *canon : normalize_key(key);
        if (id.empty()) {
            continue;
        }
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
            ids.push_back(id);
        }
    }
    for (const auto& id : ids) {
        const int row = identity_table_->rowCount();
        identity_table_->insertRow(row);
        const auto kind = kind_of_canonical(id);
        auto raw = inventory_value(session_.inventory, id);
        if (!raw) {
            const auto it = session_.inventory.find(id);
            if (it != session_.inventory.end()) {
                raw = it->second;
            }
        }
        const auto fp = session_.policy.policy_for(id);

        auto* name = new QTableWidgetItem(u8(field_label_zh(id).c_str()));
        name->setData(Qt::UserRole, QString::fromStdString(id));
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        auto* kindItem = new QTableWidgetItem(u8(field_kind_label_zh(kind).c_str()));
        kindItem->setFlags(kindItem->flags() & ~Qt::ItemIsEditable);
        auto* rawItem = new QTableWidgetItem(QString::fromStdString(raw.value_or("")));
        rawItem->setFlags(rawItem->flags() & ~Qt::ItemIsEditable);

        identity_table_->setItem(row, 0, name);
        identity_table_->setItem(row, 1, kindItem);
        identity_table_->setItem(row, 2, rawItem);
        identity_table_->setCellWidget(row, 3, makeModeCombo(fp.mode));
        auto* override_edit = new QLineEdit(QString::fromStdString(fp.override_value));
        override_edit->setObjectName(QStringLiteral("overrideEdit"));
        identity_table_->setCellWidget(row, 4, override_edit);
        (void)FieldKind{};
    }
}

int SettingsWindow::identityRow(const QString& canonical) const {
    for (int row = 0; row < identity_table_->rowCount(); ++row) {
        const auto* item = identity_table_->item(row, 0);
        if (item && item->data(Qt::UserRole).toString() == canonical) {
            return row;
        }
    }
    return -1;
}

void SettingsWindow::appendVisibleDirRow(const VisibleDir& dir) {
    const int row = dirs_table_->rowCount();
    dirs_table_->insertRow(row);
    dirs_table_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(dir.host_path)));
    dirs_table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(dir.guest_path)));
    auto* ro = new QTableWidgetItem;
    ro->setFlags((ro->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
    ro->setCheckState(dir.read_only ? Qt::Checked : Qt::Unchecked);
    ro->setTextAlignment(Qt::AlignCenter);
    dirs_table_->setItem(row, 2, ro);
}

void SettingsWindow::setVisibleDirs(const QVector<VisibleDir>& dirs) {
    dirs_table_->setRowCount(0);
    for (const auto& dir : dirs) {
        appendVisibleDirRow(dir);
    }
}

QVector<VisibleDir> SettingsWindow::visibleDirsFromTable() const {
    QVector<VisibleDir> out;
    for (int row = 0; row < dirs_table_->rowCount(); ++row) {
        VisibleDir dir;
        const auto* host = dirs_table_->item(row, 0);
        const auto* guest = dirs_table_->item(row, 1);
        const auto* ro = dirs_table_->item(row, 2);
        dir.host_path = host ? host->text().trimmed().toStdString() : std::string{};
        dir.guest_path = guest ? guest->text().trimmed().toStdString() : std::string{};
        dir.read_only = ro && ro->checkState() == Qt::Checked;
        if (dir.host_path.empty() && dir.guest_path.empty()) {
            continue;
        }
        out.push_back(std::move(dir));
    }
    return out;
}

void SettingsWindow::syncFromSession() {
    const auto& c = session_.config;
    setLineValue(QStringLiteral("nameEdit"), QString::fromStdString(c.name));
    setLineValue(QStringLiteral("distroNameEdit"), QString::fromStdString(c.distro_name));
    setLineValue(QStringLiteral("preferredSourceEdit"),
                 QString::fromStdString(c.base.preferred_source));
    setLineValue(QStringLiteral("installDirEdit"), QString::fromStdString(c.base.install_dir));

    setLineValue(QStringLiteral("gatewayHostEdit"), QString::fromStdString(c.gateway.host));
    setSpinValue(QStringLiteral("gatewayPortSpin"), c.gateway.http_port);
    setLineValue(QStringLiteral("upstreamHttpEdit"),
                 QString::fromStdString(c.gateway.upstream_http));
    setLineValue(QStringLiteral("upstreamSocksEdit"),
                 QString::fromStdString(c.gateway.upstream_socks5));
    QStringList blocklist;
    for (const auto& item : c.gateway.blocklist) {
        blocklist << QString::fromStdString(item);
    }
    setListItems(QStringLiteral("blocklistWidget"), blocklist);
    setChecked(QStringLiteral("loggingEnabledCheck"), c.gateway.logging.enabled);
    setLineValue(QStringLiteral("loggingPathEdit"),
                 QString::fromStdString(c.gateway.logging.path));

    QVector<VisibleDir> dirs;
    for (const auto& d : c.visible_dirs) {
        dirs.push_back(d);
    }
    setVisibleDirs(dirs);

    setChecked(QStringLiteral("disableAutomountCheck"), c.mount.disable_automount);

    setLineValue(QStringLiteral("timezoneEdit"), QString::fromStdString(c.identity.timezone));
    setLineValue(QStringLiteral("localeEdit"), QString::fromStdString(c.identity.locale));
    setLineValue(QStringLiteral("languageEdit"), QString::fromStdString(c.identity.language));
    setLineValue(QStringLiteral("hostnameEdit"), QString::fromStdString(c.identity.hostname));
    setLineValue(QStringLiteral("electronLangEdit"),
                 QString::fromStdString(c.identity.electron_lang));

    fillIdentityTable();

    setLineValue(QStringLiteral("debUrlEdit"), QString::fromStdString(c.desktop.deb_url));
    setLineValue(QStringLiteral("debLocalEdit"), QString::fromStdString(c.desktop.deb_local));
    setLineValue(QStringLiteral("desktopCommandEdit"), QString::fromStdString(c.desktop.command));
    QStringList args;
    for (const auto& item : c.desktop.extra_args) {
        args << QString::fromStdString(item);
    }
    setListItems(QStringLiteral("extraArgsWidget"), args);
    setChecked(QStringLiteral("asarPatchCheck"), c.desktop.asar_patch);

    setChecked(QStringLiteral("excludeHostSystemDriveCheck"),
               c.features.exclude_host_system_drive);
    setChecked(QStringLiteral("computerUseCheck"), c.features.computer_use);
    setChecked(QStringLiteral("killOnIpChangeCheck"), c.features.kill_on_local_ip_change);
    setSpinValue(QStringLiteral("killGraceSpin"), c.features.kill_grace_seconds);
    setSpinValue(QStringLiteral("watchIntervalSpin"), c.features.watch_interval_seconds);
    setChecked(QStringLiteral("oauthFingerprintCheck"),
               c.features.oauth_open_in_fingerprint_browser);
    setChecked(QStringLiteral("identityVaultCheck"), c.features.identity_vault);
    refreshRuntimePreview();
    refreshGatewayStatus();
    refreshSpoofStatus();
    refreshIpChangeStatus();
    refreshCompanionStatus();
}

bool SettingsWindow::harvestToSession(QString* error) {
    auto& c = session_.config;
    c.name = lineValue(QStringLiteral("nameEdit")).toStdString();
    c.distro_name = lineValue(QStringLiteral("distroNameEdit")).toStdString();
    c.base.preferred_source = lineValue(QStringLiteral("preferredSourceEdit")).toStdString();
    c.base.install_dir = lineValue(QStringLiteral("installDirEdit")).toStdString();

    c.gateway.host = lineValue(QStringLiteral("gatewayHostEdit")).toStdString();
    c.gateway.http_port = spinValue(QStringLiteral("gatewayPortSpin"));
    c.gateway.upstream_http = lineValue(QStringLiteral("upstreamHttpEdit")).toStdString();
    c.gateway.upstream_socks5 = lineValue(QStringLiteral("upstreamSocksEdit")).toStdString();
    c.gateway.blocklist.clear();
    for (const auto& item : listItems(QStringLiteral("blocklistWidget"))) {
        c.gateway.blocklist.push_back(item.toStdString());
    }
    c.gateway.logging.enabled = isChecked(QStringLiteral("loggingEnabledCheck"));
    c.gateway.logging.path = lineValue(QStringLiteral("loggingPathEdit")).toStdString();

    c.visible_dirs.clear();
    for (const auto& dir : visibleDirsFromTable()) {
        c.visible_dirs.push_back(dir);
    }

    c.mount.disable_automount = isChecked(QStringLiteral("disableAutomountCheck"));

    c.identity.timezone = lineValue(QStringLiteral("timezoneEdit")).toStdString();
    c.identity.locale = lineValue(QStringLiteral("localeEdit")).toStdString();
    c.identity.language = lineValue(QStringLiteral("languageEdit")).toStdString();
    c.identity.hostname = lineValue(QStringLiteral("hostnameEdit")).toStdString();
    c.identity.electron_lang = lineValue(QStringLiteral("electronLangEdit")).toStdString();

    c.host_identity.hide_identifiers = true;

    c.desktop.deb_url = lineValue(QStringLiteral("debUrlEdit")).toStdString();
    c.desktop.deb_local = lineValue(QStringLiteral("debLocalEdit")).toStdString();
    c.desktop.command = lineValue(QStringLiteral("desktopCommandEdit")).toStdString();
    c.desktop.extra_args.clear();
    for (const auto& item : listItems(QStringLiteral("extraArgsWidget"))) {
        c.desktop.extra_args.push_back(item.toStdString());
    }
    c.desktop.asar_patch = isChecked(QStringLiteral("asarPatchCheck"));

    c.features.exclude_host_system_drive = isChecked(QStringLiteral("excludeHostSystemDriveCheck"));
    c.features.computer_use = isChecked(QStringLiteral("computerUseCheck"));
    c.features.kill_on_local_ip_change = isChecked(QStringLiteral("killOnIpChangeCheck"));
    c.features.kill_grace_seconds = spinValue(QStringLiteral("killGraceSpin"));
    c.features.watch_interval_seconds = spinValue(QStringLiteral("watchIntervalSpin"));
    c.features.oauth_open_in_fingerprint_browser =
        isChecked(QStringLiteral("oauthFingerprintCheck"));
    c.features.identity_vault = isChecked(QStringLiteral("identityVaultCheck"));

    for (int row = 0; row < identity_table_->rowCount(); ++row) {
        const auto* name = identity_table_->item(row, 0);
        if (!name) {
            continue;
        }
        const auto id = name->data(Qt::UserRole).toString().toStdString();
        auto* combo = qobject_cast<QComboBox*>(identity_table_->cellWidget(row, 3));
        auto* override_edit = qobject_cast<QLineEdit*>(identity_table_->cellWidget(row, 4));
        const FieldMode mode = combo ? indexToMode(combo->currentIndex()) : FieldMode::Hide;
        const QString value = override_edit ? override_edit->text() : QString{};
        if (mode == FieldMode::Override && value.trimmed().isEmpty()) {
            if (error) {
                *error = u8("「修改后展示」需要填写修改后的值：") +
                         QString::fromStdString(field_label_zh(id));
            }
            return false;
        }
        session_.set_field_mode(id, mode, value.toStdString());
    }
    return true;
}

bool SettingsWindow::saveToDisk(QString* error) {
    if (!harvestToSession(error)) {
        return false;
    }
    const auto saved = session_.save();
    if (!saved.ok) {
        if (error) {
            *error = QString::fromStdString(saved.error);
        }
        return false;
    }
    return true;
}

void SettingsWindow::reloadFromDisk() {
    session_.reload();
    syncFromSession();
}

void SettingsWindow::setIdentityMode(const QString& canonical, FieldMode mode,
                                     const QString& override_value) {
    const int row = identityRow(canonical);
    if (row < 0) {
        return;
    }
    if (auto* combo = qobject_cast<QComboBox*>(identity_table_->cellWidget(row, 3))) {
        combo->setCurrentIndex(modeToIndex(mode));
    }
    if (auto* edit = qobject_cast<QLineEdit*>(identity_table_->cellWidget(row, 4))) {
        if (!override_value.isNull()) {
            edit->setText(override_value);
        }
    }
}

QString SettingsWindow::identityModeName(const QString& canonical) const {
    const int row = identityRow(canonical);
    if (row < 0) {
        return {};
    }
    auto* combo = qobject_cast<QComboBox*>(identity_table_->cellWidget(row, 3));
    if (!combo) {
        return {};
    }
    return QString::fromUtf8(field_mode_name(indexToMode(combo->currentIndex())).data());
}

QString SettingsWindow::identityOverride(const QString& canonical) const {
    const int row = identityRow(canonical);
    if (row < 0) {
        return {};
    }
    auto* edit = qobject_cast<QLineEdit*>(identity_table_->cellWidget(row, 4));
    return edit ? edit->text() : QString{};
}

void SettingsWindow::setLineValue(const QString& object_name, const QString& value) {
    if (auto* edit = findChild<QLineEdit*>(object_name)) {
        edit->setText(value);
    }
}

QString SettingsWindow::lineValue(const QString& object_name) const {
    if (auto* edit = findChild<QLineEdit*>(object_name)) {
        return edit->text();
    }
    return {};
}

void SettingsWindow::setChecked(const QString& object_name, bool checked) {
    if (auto* box = findChild<QCheckBox*>(object_name)) {
        box->setChecked(checked);
    }
}

bool SettingsWindow::isChecked(const QString& object_name) const {
    if (auto* box = findChild<QCheckBox*>(object_name)) {
        return box->isChecked();
    }
    return false;
}

void SettingsWindow::setSpinValue(const QString& object_name, int value) {
    if (auto* spin = findChild<QSpinBox*>(object_name)) {
        spin->setValue(value);
    }
}

int SettingsWindow::spinValue(const QString& object_name) const {
    if (auto* spin = findChild<QSpinBox*>(object_name)) {
        return spin->value();
    }
    return 0;
}

void SettingsWindow::setListItems(const QString& object_name, const QStringList& items) {
    if (auto* list = findChild<QListWidget*>(object_name)) {
        list->clear();
        list->addItems(items);
    }
}

QStringList SettingsWindow::listItems(const QString& object_name) const {
    QStringList out;
    if (auto* list = findChild<QListWidget*>(object_name)) {
        for (int i = 0; i < list->count(); ++i) {
            out << list->item(i)->text();
        }
    }
    return out;
}

QString SettingsWindow::runtimePreview() const {
    return runtime_preview_ ? runtime_preview_->toPlainText() : QString{};
}

void SettingsWindow::refreshRuntimePreview() {
    if (!runtime_preview_) {
        return;
    }
    const auto previous = session_;
    QString error;
    const bool harvested = harvestToSession(&error);
    const auto plan = claudebox::mount::plan_mounts(session_.config);
    runtime_preview_->setPlainText(QString::fromStdString(claudebox::mount::format_plan(plan)));
    if (!harvested && !error.isEmpty()) {
        runtime_preview_->appendPlainText(QString::fromUtf8("\n表单还未填完：") + error);
    }
    if (!harvested) {
        session_ = previous;
    }
}

QString SettingsWindow::gatewayStatusText() const {
    return gateway_status_ ? gateway_status_->toPlainText() : QString{};
}

void SettingsWindow::refreshGatewayStatus() {
    if (!gateway_status_) {
        return;
    }
    const auto previous = session_;
    QString error;
    const bool harvested = harvestToSession(&error);
    const auto st = claudebox::gateway::status(session_.config, session_.paths.prefix);
    gateway_status_->setPlainText(QString::fromStdString(claudebox::gateway::format_status(st)));
    if (!harvested && !error.isEmpty()) {
        gateway_status_->appendPlainText(QString::fromUtf8("\n表单还未填完：") + error);
    }
    if (!harvested) {
        session_ = previous;
    }
}

void SettingsWindow::onRefreshGatewayStatus() {
    refreshGatewayStatus();
}

QString SettingsWindow::spoofStatusText() const {
    return spoof_status_ ? spoof_status_->toPlainText() : QString{};
}

void SettingsWindow::refreshSpoofStatus() {
    if (!spoof_status_) {
        return;
    }
    const auto previous = session_;
    QString error;
    const bool harvested = harvestToSession(&error);
    const auto values = claudebox::spoof::from_config(session_.config.identity);
    const auto planned = claudebox::spoof::plan(values, session_.paths.prefix);
    claudebox::spoof::VerifyResult verified;
    const bool can_verify =
        planned.ok && std::filesystem::exists(claudebox::spoof::spoof_env_path(session_.paths.prefix));
    const claudebox::spoof::VerifyResult* verified_ptr = nullptr;
    if (can_verify) {
        verified = claudebox::spoof::verify(values, session_.paths.prefix);
        verified_ptr = &verified;
    }
    spoof_status_->setPlainText(
        QString::fromStdString(claudebox::spoof::format_status(planned, verified_ptr)));
    if (!harvested && !error.isEmpty()) {
        spoof_status_->appendPlainText(QString::fromUtf8("\n表单还未填完：") + error);
    }
    if (!harvested) {
        session_ = previous;
    }
}

void SettingsWindow::onRefreshSpoofStatus() {
    refreshSpoofStatus();
}

void SettingsWindow::setIpChangePreview(const QStringList& previous, const QStringList& current,
                                        bool gateway_running) {
    ip_prev_ = previous;
    ip_curr_ = current;
    ip_gateway_running_ = gateway_running;
    refreshIpChangeStatus();
}

QString SettingsWindow::ipChangeStatusText() const {
    return ip_change_status_ ? ip_change_status_->toPlainText() : QString{};
}

void SettingsWindow::refreshIpChangeStatus() {
    if (!ip_change_status_) {
        return;
    }
    const auto previous_session = session_;
    QString error;
    const bool harvested = harvestToSession(&error);
    std::vector<std::string> prev_raw;
    std::vector<std::string> curr_raw;
    for (const auto& item : ip_prev_) {
        prev_raw.push_back(item.toStdString());
    }
    for (const auto& item : ip_curr_) {
        curr_raw.push_back(item.toStdString());
    }
    const auto previous = claudebox::gateway::make_fingerprint(prev_raw);
    const auto current = claudebox::gateway::make_fingerprint(curr_raw);
    const std::vector<claudebox::gateway::BoxProcess> fixture_procs = {
        {"claude-desktop", 101, true},
        {"claude", 102, true},
        {"firefox", 200, true},
        {"claudebox-identity", 202, true},
    };
    const auto run = claudebox::gateway::dry_run_ip_change(
        session_.config, previous, current, fixture_procs, ip_gateway_running_);
    ip_change_status_->setPlainText(
        QString::fromStdString(claudebox::gateway::format_ip_change_plan(run.plan, &run)));
    if (!harvested && !error.isEmpty()) {
        ip_change_status_->appendPlainText(QString::fromUtf8("\n表单还未填完：") + error);
    }
    if (!harvested) {
        session_ = previous_session;
    }
}

void SettingsWindow::onRefreshIpChangeStatus() {
    refreshIpChangeStatus();
}

QString SettingsWindow::companionStatusText() const {
    return companion_status_ ? companion_status_->toPlainText() : QString{};
}

void SettingsWindow::refreshCompanionStatus() {
    if (!companion_status_) {
        return;
    }
    const auto previous = session_;
    QString error;
    const bool harvested = harvestToSession(&error);
    const auto planned =
        claudebox::companion::plan(session_.config, session_.paths.prefix);
    companion_status_->setPlainText(
        QString::fromStdString(claudebox::companion::format_status(planned)));
    if (!harvested && !error.isEmpty()) {
        companion_status_->appendPlainText(QString::fromUtf8("\n表单还未填完：") + error);
    }
    if (!harvested) {
        session_ = previous;
    }
}

void SettingsWindow::onRefreshCompanionStatus() {
    refreshCompanionStatus();
}

bool SettingsWindow::openCompanionUrl(const QString& url, QString* error, bool dry_run) {
    const auto previous = session_;
    QString harvest_error;
    const bool harvested = harvestToSession(&harvest_error);
    if (!harvested) {
        if (error) {
            *error = harvest_error;
        }
        session_ = previous;
        return false;
    }
    const auto opened = claudebox::companion::open_url(
        session_.config, session_.paths.prefix, url.toStdString(), dry_run);
    if (!opened.ok) {
        if (error) {
            *error = QString::fromStdString(opened.error);
        }
        return false;
    }
    return true;
}


void SettingsWindow::onSave() {
    QString error;
    if (!saveToDisk(&error)) {
        statusBar()->showMessage(u8("保存失败：") + error, 8000);
        if (!QApplication::arguments().contains(QStringLiteral("--smoke"))) {
            QMessageBox::warning(this, u8("保存失败"), error);
        }
        return;
    }
    statusBar()->showMessage(
        u8("已保存盒子配置、身份策略，并更新了展示层、盒子伪装值和伴生浏览器拦截。"), 8000);
    refreshRuntimePreview();
    refreshGatewayStatus();
    refreshSpoofStatus();
    refreshIpChangeStatus();
    refreshCompanionStatus();
}

void SettingsWindow::onReload() {
    reloadFromDisk();
    statusBar()->showMessage(u8("已从磁盘重新加载。"), 5000);
}

void SettingsWindow::onAddVisibleDir() {
    appendVisibleDirRow(VisibleDir{"", "/mnt/claude-box/", false});
    dirs_table_->setCurrentCell(dirs_table_->rowCount() - 1, 0);
}

void SettingsWindow::onRemoveVisibleDir() {
    const int row = dirs_table_->currentRow();
    if (row >= 0) {
        dirs_table_->removeRow(row);
    }
}

void SettingsWindow::onAddBlocklistItem() {
    addListItem(blocklist_, blocklist_edit_->text());
    blocklist_edit_->clear();
}

void SettingsWindow::onRemoveBlocklistItem() {
    const int row = blocklist_->currentRow();
    if (row >= 0) {
        delete blocklist_->takeItem(row);
    }
}

void SettingsWindow::onAddExtraArg() {
    addListItem(extra_args_, extra_args_edit_->text());
    extra_args_edit_->clear();
}

void SettingsWindow::onRemoveExtraArg() {
    const int row = extra_args_->currentRow();
    if (row >= 0) {
        delete extra_args_->takeItem(row);
    }
}
