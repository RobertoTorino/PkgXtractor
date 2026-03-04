// SPDX-FileCopyrightText: Copyright 2025 PkgXtractor Qt GUI
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QApplication>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QProgressBar>
#include <QTextEdit>
#include <QFileDialog>
#include <QThread>
#include <QMessageBox>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <filesystem>
#include <iostream>

#include "../../src/core/file_format/pkg.h"
#include "../../src/core/file_format/psf.h"
#include "../../src/common/path_util.h"
#include "../../src/common/string_util.h"
#include "../../src/core/loader.h"

std::vector<std::string> SplitString(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

class ExtractionWorker : public QObject {
    Q_OBJECT

public:
    ExtractionWorker(const std::string& pkgPath, const std::string& outputPath)
        : m_pkgPath(pkgPath), m_outputPath(outputPath) {}

public slots:
    void extract() {
        std::filesystem::path file(m_pkgPath);
        std::filesystem::path output_folder_path(m_outputPath);

        if (Loader::DetectFileType(file) != Loader::FileTypes::Pkg) {
            emit logMessage(QString::fromStdString("Error: " + m_pkgPath + " is not a valid PKG file"));
            emit finished();
            return;
        }

        emit logMessage(QString::fromStdString(m_pkgPath + " is a valid PKG"));

        PKG pkg{};
        std::string failreason;

        if (!pkg.Extract(file, output_folder_path.empty() ? file.parent_path() : output_folder_path, failreason)) {
            emit logMessage(QString::fromStdString("Cannot open PKG file: " + failreason));
            emit finished();
            return;
        }

        emit logMessage("PKG file opened successfully");

        PSF psf{};
        if (!psf.Open(pkg.sfo)) {
            emit logMessage("Could not read SFO.");
            emit finished();
            return;
        }

        emit logMessage("SFO read successfully");

        auto dlc_flag_data = psf.GetInteger("CONTENT_TYPE");

        if (output_folder_path.empty()) {
            output_folder_path = file.parent_path();
        }

        std::string folder_name = "";

        if (dlc_flag_data && dlc_flag_data.value() != 0) {
            emit logMessage("DLC detected");
            auto title_id_data = psf.GetString("TITLE_ID");
            if (title_id_data) {
                folder_name = std::string(title_id_data.value());
                folder_name.erase(std::remove(folder_name.begin(), folder_name.end(), '\0'), folder_name.end());
                folder_name = "[DLC] " + folder_name;
            }
        } else {
            emit logMessage("Game or Patch detected");
            auto title_data = psf.GetString("TITLE");
            if (title_data) {
                folder_name = std::string(title_data.value());
                folder_name.erase(std::remove(folder_name.begin(), folder_name.end(), '\0'), folder_name.end());
            }
        }

        if (!folder_name.empty()) {
            output_folder_path = output_folder_path / folder_name;
        }

        emit logMessage(QString::fromStdString("Extraction target: " + output_folder_path.string()));

        const u32 files_count = pkg.GetNumberOfFiles();
        emit setMaxProgress(files_count);

        for (u32 index = 0; index < files_count; ++index) {
            pkg.ExtractFiles(static_cast<int>(index));
            emit updateProgress(index + 1);
            QThread::msleep(1);  // Allow UI to update
        }

        emit logMessage("Extraction complete!");
        emit finished();
    }

signals:
    void logMessage(const QString& msg);
    void updateProgress(int value);
    void setMaxProgress(int max);
    void finished();

private:
    std::string m_pkgPath;
    std::string m_outputPath;
};

class PkgXtractorWindow : public QMainWindow {
    Q_OBJECT

public:
    PkgXtractorWindow() : QMainWindow() {
        setWindowTitle("PkgXtractor Qt");
        setGeometry(100, 100, 700, 600);
        setAcceptDrops(true);

        QWidget* centralWidget = new QWidget();
        setCentralWidget(centralWidget);

        QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);

        // PKG File Selection
        QHBoxLayout* pkgLayout = new QHBoxLayout();
        mainLayout->addLayout(pkgLayout);

        pkgLayout->addWidget(new QLabel("PKG File:"));
        m_pkgPathInput = new QLineEdit();
        m_pkgPathInput->setPlaceholderText("Select or drag & drop a PKG file...");
        pkgLayout->addWidget(m_pkgPathInput);

        QPushButton* browseBtn = new QPushButton("Browse");
        connect(browseBtn, &QPushButton::clicked, this, &PkgXtractorWindow::onBrowsePkg);
        pkgLayout->addWidget(browseBtn);

        // Output Folder Selection
        QHBoxLayout* outputLayout = new QHBoxLayout();
        mainLayout->addLayout(outputLayout);

        outputLayout->addWidget(new QLabel("Output:"));
        m_outputPathInput = new QLineEdit();
        m_outputPathInput->setPlaceholderText("Leave empty for auto (same as PKG location)");
        outputLayout->addWidget(m_outputPathInput);

        QPushButton* browseFolderBtn = new QPushButton("Browse");
        connect(browseFolderBtn, &QPushButton::clicked, this, &PkgXtractorWindow::onBrowseOutput);
        outputLayout->addWidget(browseFolderBtn);

        // Progress Bar
        m_progressBar = new QProgressBar();
        m_progressBar->setEnabled(false);
        mainLayout->addWidget(m_progressBar);

        // Extract Button
        m_extractBtn = new QPushButton("Extract PKG");
        connect(m_extractBtn, &QPushButton::clicked, this, &PkgXtractorWindow::onExtract);
        mainLayout->addWidget(m_extractBtn);

        // Log Area
        mainLayout->addWidget(new QLabel("Log:"));
        m_logOutput = new QTextEdit();
        m_logOutput->setReadOnly(true);
        mainLayout->addWidget(m_logOutput);

        centralWidget->setLayout(mainLayout);
    }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasUrls()) {
            event->acceptProposedAction();
        }
    }

    void dropEvent(QDropEvent* event) override {
        const QMimeData* mimeData = event->mimeData();
        if (mimeData->hasUrls()) {
            QString filePath = mimeData->urls().at(0).toLocalFile();
            if (filePath.endsWith(".pkg", Qt::CaseInsensitive)) {
                m_pkgPathInput->setText(filePath);
                event->acceptProposedAction();
            }
        }
    }

private slots:
    void onBrowsePkg() {
        QString fileFilter = "PKG Files (*.pkg);;All Files (*)";
        QString fileName = QFileDialog::getOpenFileName(this, "Select PKG File", "", fileFilter);
        if (!fileName.isEmpty()) {
            m_pkgPathInput->setText(fileName);
        }
    }

    void onBrowseOutput() {
        QString folderPath = QFileDialog::getExistingDirectory(this, "Select Output Folder");
        if (!folderPath.isEmpty()) {
            m_outputPathInput->setText(folderPath);
        }
    }

    void onExtract() {
        if (m_pkgPathInput->text().isEmpty()) {
            QMessageBox::warning(this, "Error", "Please select a PKG file first.");
            return;
        }

        m_extractBtn->setEnabled(false);
        m_progressBar->setEnabled(true);
        m_progressBar->setValue(0);
        m_logOutput->clear();

        QThread* thread = new QThread();
        ExtractionWorker* worker = new ExtractionWorker(
            m_pkgPathInput->text().toStdString(),
            m_outputPathInput->text().toStdString());

        worker->moveToThread(thread);

        connect(thread, &QThread::started, worker, &ExtractionWorker::extract);
        connect(worker, &ExtractionWorker::logMessage, this, &PkgXtractorWindow::onLog);
        connect(worker, &ExtractionWorker::updateProgress, m_progressBar, &QProgressBar::setValue);
        connect(worker, &ExtractionWorker::setMaxProgress, m_progressBar, &QProgressBar::setMaximum);
        connect(worker, &ExtractionWorker::finished, thread, &QThread::quit);
        connect(worker, &ExtractionWorker::finished, worker, &QObject::deleteLater);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        connect(thread, &QThread::finished, this, [this]() {
            m_extractBtn->setEnabled(true);
            m_progressBar->setEnabled(false);
        });

        thread->start();
    }

    void onLog(const QString& msg) {
        m_logOutput->append(msg);
    }

private:
    QLineEdit* m_pkgPathInput;
    QLineEdit* m_outputPathInput;
    QPushButton* m_extractBtn;
    QProgressBar* m_progressBar;
    QTextEdit* m_logOutput;
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    PkgXtractorWindow window;
    window.show();

    return app.exec();
}

#include "gui.moc"
