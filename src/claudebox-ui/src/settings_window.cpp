#include "settings_window.hpp"

#include "claudebox/identity.hpp"
#include "claudebox/mount.hpp"

#include <algorithm>

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
        u8("网关配置。本页只负责读写这些字段；唯一网关运行时接线属于后续模块。")));
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
                                         u8("空 = 宿主默认日志路径")));
    layout->addWidget(logBox);
    layout->addStretch(1);

    connect(addBl, &QPushButton::clicked, this, &SettingsWindow::onAddBlocklistItem);
    connect(delBl, &QPushButton::clicked, this, &SettingsWindow::onRemoveBlocklistItem);
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
        u8("以下是盒子内的伪装值，只写入盒子配置。不会改宿主时区、语言或默认浏览器，"
           "也不把「暴露宿主真实时区 / 语言」当作可展示的清单字段。")));
    auto* form = new QFormLayout;
    form->addRow(u8("时区"), namedEdit(QStringLiteral("timezoneEdit")));
    form->addRow(u8("区域设置（locale）"), namedEdit(QStringLiteral("localeEdit")));
    form->addRow(u8("语言"), namedEdit(QStringLiteral("languageEdit")));
    form->addRow(u8("主机名"), namedEdit(QStringLiteral("hostnameEdit")));
    form->addRow(u8("Electron 语言"), namedEdit(QStringLiteral("electronLangEdit")));
    layout->addLayout(form);
    layout->addStretch(1);
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
        u8("功能开关同样是配置项。伴生指纹浏览器、身份保险库、本机 IP 变化杀进程等运行时接线属于后续模块。")));
    auto add = [&](const QString& name, const QString& label) {
        auto* check = new QCheckBox(label);
        check->setObjectName(name);
        layout->addWidget(check);
    };
    add(QStringLiteral("excludeHostSystemDriveCheck"),
        u8("排除宿主系统盘（拒绝把整个 C: 挂进盒子）"));
    add(QStringLiteral("computerUseCheck"), u8("允许 computer use"));
    add(QStringLiteral("killOnIpChangeCheck"),
        u8("本机 IP 变化时结束盒子内 Claude（默认关）"));
    add(QStringLiteral("oauthFingerprintCheck"),
        u8("OAuth / 授权页用伴生指纹浏览器打开（不走宿主默认浏览器）"));
    add(QStringLiteral("identityVaultCheck"), u8("启用盒子内身份保险库"));
    layout->addStretch(1);
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
    setChecked(QStringLiteral("oauthFingerprintCheck"),
               c.features.oauth_open_in_fingerprint_browser);
    setChecked(QStringLiteral("identityVaultCheck"), c.features.identity_vault);
    refreshRuntimePreview();
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

void SettingsWindow::onSave() {
    QString error;
    if (!saveToDisk(&error)) {
        statusBar()->showMessage(u8("保存失败：") + error, 8000);
        if (!QApplication::arguments().contains(QStringLiteral("--smoke"))) {
            QMessageBox::warning(this, u8("保存失败"), error);
        }
        return;
    }
    statusBar()->showMessage(u8("已保存盒子配置、身份策略，并更新了展示层。"), 8000);
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
