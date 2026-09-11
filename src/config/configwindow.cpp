// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "configwindow.h"
#include "config/configresolver.h"
#include "config/filenameeditor.h"
#include "config/generalconf.h"
#include "config/shortcutswidget.h"
#include "config/visualseditor.h"
#include "utils/colorutils.h"
#include "utils/confighandler.h"
#include "utils/globalvalues.h"
#include "utils/pathinfo.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFileSystemWatcher>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QSizePolicy>
#include <QTabBar>
#include <QTextStream>
#include <QVBoxLayout>

// ConfigWindow contains the menus where you can configure the application

ConfigWindow::ConfigWindow(QWidget* parent)
  : QWidget(parent)
{
    setObjectName(QStringLiteral("mobshotSettings"));
    setStyleSheet(QStringLiteral(
      "#mobshotSettings { background: #f7f5ff; color: #24143d; }"
      "QTabWidget::pane { border: 1px solid #ddd6fe; border-radius: 14px; background: white; top: -1px; }"
      "QTabBar::tab { padding: 10px 18px; margin: 3px; border-radius: 10px; color: #5b4b78; }"
      "QTabBar::tab:selected { background: #7c3aed; color: white; font-weight: 600; }"
      "QPushButton { min-height: 28px; padding: 3px 12px; border: 1px solid #c4b5fd; border-radius: 9px; background: #faf8ff; }"
      "QPushButton:hover { background: #ede9fe; border-color: #8b5cf6; }"
      "QLineEdit, QComboBox, QSpinBox { min-height: 28px; border: 1px solid #d8d1ea; border-radius: 8px; padding: 2px 8px; background: white; }"
      "QGroupBox { margin-top: 14px; padding-top: 12px; border: 1px solid #e9e5f5; border-radius: 12px; font-weight: 600; }"
      "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }"));
    // We wrap QTabWidget in a QWidget because of a Qt bug
    auto* layout = new QVBoxLayout(this);
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->tabBar()->setUsesScrollButtons(false);
#if defined(Q_OS_MACOS)
    // Fix Qt6 macOS bug where tab pane content renders behind the tab bar
    m_tabWidget->setStyleSheet(
      "QTabWidget::pane { border-top: 2px solid palette(mid); }");
#endif
    layout->addWidget(m_tabWidget);

    setAttribute(Qt::WA_DeleteOnClose);
    setWindowIcon(QIcon(GlobalValues::iconPath()));
    setWindowTitle(tr("Configuration"));

    connect(ConfigHandler::getInstance(),
            &ConfigHandler::fileChanged,
            this,
            &ConfigWindow::updateChildren);

    QColor background = this->palette().window().color();
    bool isDark = ColorUtils::colorIsDark(background);
    QString modifier =
      isDark ? PathInfo::whiteIconPath() : PathInfo::blackIconPath();

    // general
    m_generalConfig = new GeneralConf();
    m_generalConfigTab = new QWidget();
    auto* generalConfigLayout = new QVBoxLayout(m_generalConfigTab);
    m_generalConfigTab->setLayout(generalConfigLayout);
    generalConfigLayout->addWidget(m_generalConfig);
    m_tabWidget->addTab(
      m_generalConfigTab, QIcon(modifier + "config.svg"), tr("General"));

    // visuals
    m_visuals = new VisualsEditor();
    m_visualsTab = new QWidget();
    auto* visualsLayout = new QVBoxLayout(m_visualsTab);
    m_visualsTab->setLayout(visualsLayout);
    visualsLayout->addWidget(m_visuals);
    m_tabWidget->addTab(
      m_visualsTab, QIcon(modifier + "graphics.svg"), tr("Interface"));

    // filename
    m_filenameEditor = new FileNameEditor();
    m_filenameEditorTab = new QWidget();
    auto* filenameEditorLayout = new QVBoxLayout(m_filenameEditorTab);
    m_filenameEditorTab->setLayout(filenameEditorLayout);
    filenameEditorLayout->addWidget(m_filenameEditor);
    m_tabWidget->addTab(m_filenameEditorTab,
                        QIcon(modifier + "name_edition.svg"),
                        tr("Filename Editor"));

    // shortcuts
    m_shortcuts = new ShortcutsWidget();
    m_shortcutsTab = new QWidget();
    auto* shortcutsLayout = new QVBoxLayout(m_shortcutsTab);
    m_shortcutsTab->setLayout(shortcutsLayout);
    shortcutsLayout->addWidget(m_shortcuts);
    m_tabWidget->addTab(
      m_shortcutsTab, QIcon(modifier + "shortcut.svg"), tr("Shortcuts"));

    // connect update sigslots
    connect(this,
            &ConfigWindow::updateChildren,
            m_filenameEditor,
            &FileNameEditor::updateComponents);
    connect(this,
            &ConfigWindow::updateChildren,
            m_visuals,
            &VisualsEditor::updateComponents);
    connect(this,
            &ConfigWindow::updateChildren,
            m_generalConfig,
            &GeneralConf::updateComponents);

    // Error indicator (this must come last)
    initErrorIndicator(m_visualsTab, m_visuals);
    initErrorIndicator(m_filenameEditorTab, m_filenameEditor);
    initErrorIndicator(m_generalConfigTab, m_generalConfig);
    initErrorIndicator(m_shortcutsTab, m_shortcuts);
}

void ConfigWindow::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        close();
    }
}

void ConfigWindow::initErrorIndicator(QWidget* tab, QWidget* widget)
{
    auto* label = new QLabel(tab);
    auto* btnResolve = new QPushButton(tr("Resolve"), tab);
    auto* btnLayout = new QHBoxLayout();

    // Set up label
    label->setText(tr(
      "<b>Configuration file has errors. Resolve them before continuing.</b>"));
    label->setStyleSheet(QStringLiteral(":disabled { color: %1; }")
                           .arg(qApp->palette().color(QPalette::Text).name()));
    label->setVisible(ConfigHandler().hasError());

    // Set up "Show errors" button
    btnResolve->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    btnLayout->addWidget(btnResolve);
    btnResolve->setVisible(ConfigHandler().hasError());

    widget->setEnabled(!ConfigHandler().hasError());

    // Add label and button to the parent widget's layout
    auto* layout = static_cast<QBoxLayout*>(tab->layout());
    if (layout != nullptr) {
        layout->insertWidget(0, label);
        layout->insertLayout(1, btnLayout);
    } else {
        widget->layout()->addWidget(label);
        widget->layout()->addWidget(btnResolve);
    }

    // Sigslots
    connect(
      ConfigHandler::getInstance(), &ConfigHandler::error, widget, [=, this]() {
          widget->setEnabled(false);
          label->show();
          btnResolve->show();
      });
    connect(ConfigHandler::getInstance(),
            &ConfigHandler::errorResolved,
            widget,
            [=]() {
                widget->setEnabled(true);
                label->hide();
                btnResolve->hide();
            });
    connect(btnResolve, &QPushButton::clicked, this, [this]() {
        ConfigResolver().exec();
    });
}
