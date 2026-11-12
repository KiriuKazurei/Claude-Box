#pragma once

#include "claudebox/settings.hpp"

#include <QMainWindow>
#include <QString>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QTableWidget;
class QPlainTextEdit;
class QLabel;

class SettingsWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit SettingsWindow(claudebox::settings::Options options,
                            QWidget* parent = nullptr);

    claudebox::settings::Session& session() { return session_; }
    const claudebox::settings::Session& session() const { return session_; }

    void syncFromSession();
    bool harvestToSession(QString* error);
    bool saveToDisk(QString* error);
    void reloadFromDisk();

    // Test / programmatic helpers for the identity table.
    void setIdentityMode(const QString& canonical,
                         claudebox::identity::FieldMode mode,
                         const QString& override_value = {});
    QString identityModeName(const QString& canonical) const;
    QString identityOverride(const QString& canonical) const;

    // Visible-dir editor helpers (P4 edit/save; P5 adds rejection).
    void setVisibleDirs(const QVector<claudebox::config::VisibleDir>& dirs);
    QVector<claudebox::config::VisibleDir> visibleDirsFromTable() const;
    QString runtimePreview() const;
    void refreshRuntimePreview();

    QString gatewayStatusText() const;
    void refreshGatewayStatus();

    void setLineValue(const QString& object_name, const QString& value);
    QString lineValue(const QString& object_name) const;
    void setChecked(const QString& object_name, bool checked);
    bool isChecked(const QString& object_name) const;
    void setSpinValue(const QString& object_name, int value);
    int spinValue(const QString& object_name) const;
    void setListItems(const QString& object_name, const QStringList& items);
    QStringList listItems(const QString& object_name) const;

private slots:
    void onSave();
    void onReload();
    void onAddVisibleDir();
    void onRemoveVisibleDir();
    void onAddBlocklistItem();
    void onRemoveBlocklistItem();
    void onAddExtraArg();
    void onRemoveExtraArg();
    void onRefreshGatewayStatus();

private:
    void buildUi();
    QWidget* buildBasePage();
    QWidget* buildGatewayPage();
    QWidget* buildVisibleDirsPage();
    QWidget* buildMountPage();
    QWidget* buildSpoofPage();
    QWidget* buildIdentityPage();
    QWidget* buildDesktopPage();
    QWidget* buildFeaturesPage();
    QWidget* buildRuntimePage();
    void fillIdentityTable();
    int identityRow(const QString& canonical) const;
    void appendVisibleDirRow(const claudebox::config::VisibleDir& dir);

    claudebox::settings::Options options_;
    claudebox::settings::Session session_;

    QTableWidget* identity_table_ = nullptr;
    QTableWidget* dirs_table_ = nullptr;
    QListWidget* blocklist_ = nullptr;
    QListWidget* extra_args_ = nullptr;
    QLineEdit* blocklist_edit_ = nullptr;
    QLineEdit* extra_args_edit_ = nullptr;
    QLabel* status_label_ = nullptr;
    QPlainTextEdit* runtime_preview_ = nullptr;
    QPlainTextEdit* gateway_status_ = nullptr;
};
