// SPDX-FileCopyrightText: Copyright 2025 PkgXtractor Qt GUI
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QMainWindow>
#include <QFile>
#include <QTextStream>

namespace Ui {
class PkgXtractorWindow;
}

class PkgXtractorWindow : public QMainWindow {
    Q_OBJECT

public:
    PkgXtractorWindow(QWidget* parent = nullptr);
    ~PkgXtractorWindow();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onBrowsePkg();
    void onBrowseOutput();
    void onExtract();
    void onPreviewOnly();
    void updateSfoPreview(QString pkgPath);
    void onLogMessage(const QString& msg);
    void onUpdateProgress(int value);
    void onSetMaxProgress(int max);
    void onFinished();
    void startWorker(bool previewOnly);

private:
    Ui::PkgXtractorWindow* ui;
    QFile m_logFile;
    QTextStream m_logStream;
    QString m_crashMarkerPath;
    QString m_progressPath;
};
