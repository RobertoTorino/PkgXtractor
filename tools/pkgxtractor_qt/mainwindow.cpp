// SPDX-FileCopyrightText: Copyright 2025 PkgXtractor Qt GUI
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QApplication>
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
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QDateTime>
#include <QCoreApplication>
#include <QImage>
#include <QPixmap>
#include <QDesktopServices>
#include <QUrl>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <algorithm>

#include "mainwindow.h"
#include "ui_mainwindow.h"
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

std::string SanitizeFolderName(std::string folder_name) {
    static constexpr char invalid_chars[] = {'<', '>', ':', '"', '/', '\\', '|', '?', '*'};
    for (char& c : folder_name) {
        if (static_cast<unsigned char>(c) < 0x20) {
            c = '_';
            continue;
        }
        for (char invalid : invalid_chars) {
            if (c == invalid) {
                c = '_';
                break;
            }
        }
    }

    while (!folder_name.empty() && (folder_name.back() == ' ' || folder_name.back() == '.')) {
        folder_name.pop_back();
    }

    if (folder_name.empty()) {
        return "unknown_title";
    }
    return folder_name;
}

class ExtractionWorker : public QObject {
    Q_OBJECT

public:
    ExtractionWorker(const std::string& pkgPath, const std::string& outputPath,
                     const QString& progressPath, bool previewOnly)
        : m_pkgPath(pkgPath), m_outputPath(outputPath), m_progressPath(progressPath),
          m_previewOnly(previewOnly) {}

public slots:
    void extract() {
        emit logMessage("=== Starting extraction ===");
        
        try {
            std::filesystem::path file(m_pkgPath);
            std::filesystem::path output_folder_path(m_outputPath);

            emit logMessage(QString::fromStdString("Checking file: " + m_pkgPath));
            
            if (!std::filesystem::exists(file)) {
                emit logMessage(QString::fromStdString("Error: File does not exist: " + m_pkgPath));
                emit finished();
                return;
            }

            emit logMessage("Detecting file type...");
            if (Loader::DetectFileType(file) != Loader::FileTypes::Pkg) {
                emit logMessage(QString::fromStdString("Error: " + m_pkgPath + " is not a valid PKG file"));
                emit finished();
                return;
            }

            emit logMessage(QString::fromStdString(m_pkgPath + " is a valid PKG"));

            emit logMessage("Initializing PKG extractor...");
            PKG pkg{};
            std::string failreason;

            emit logMessage("Opening PKG file for SFO metadata...");
            if (!pkg.Open(file, failreason)) {
                emit logMessage(QString::fromStdString("Cannot open PKG file: " + failreason));
                emit finished();
                return;
            }

            emit logMessage("PKG file opened successfully");

            emit logMessage("Reading SFO metadata...");
            emit logMessage(QString::fromStdString("SFO data size: " + std::to_string(pkg.sfo.size()) + " bytes"));
            
            PSF psf{};
            emit logMessage("Parsing SFO...");
            
            try {
                if (!psf.Open(pkg.sfo)) {
                    emit logMessage("Error: psf.Open() returned false - Could not read SFO metadata");
                    emit finished();
                    return;
                }
            } catch (const std::exception& e) {
                emit logMessage(QString::fromStdString("Exception in psf.Open(): " + std::string(e.what())));
                emit finished();
                return;
            } catch (...) {
                emit logMessage("Unknown exception in psf.Open()");
                emit finished();
                return;
            }

            emit logMessage("SFO read successfully");

            emit logMessage("Reading title id for extraction folder...");

            std::string folder_name;
            auto title_id_data = psf.GetString("TITLE_ID");
            if (title_id_data) {
                folder_name = std::string(title_id_data.value());
                folder_name.erase(std::remove(folder_name.begin(), folder_name.end(), '\0'),
                                  folder_name.end());
                emit logMessage(QString::fromStdString("TITLE_ID from SFO: " + folder_name));
            }

            if (folder_name.empty()) {
                folder_name = std::string(pkg.GetTitleID());
                folder_name.erase(std::remove(folder_name.begin(), folder_name.end(), '\0'),
                                  folder_name.end());
                emit logMessage(QString::fromStdString("TITLE_ID fallback from PKG header: " +
                                                       folder_name));
            }

            if (folder_name.empty()) {
                emit logMessage("Warning: TITLE_ID missing, using unknown_title");
                folder_name = "unknown_title";
            }

            const auto sanitized_folder_name = SanitizeFolderName(folder_name);
            if (sanitized_folder_name != folder_name) {
                emit logMessage(QString::fromStdString("Sanitized folder name: " +
                                                       sanitized_folder_name));
            }

            emit logMessage("Creating TITLE_ID extraction subfolder...");
            output_folder_path = output_folder_path / sanitized_folder_name;
            
            emit logMessage("Running full PKG extraction with TITLE_ID path...");
            if (!pkg.Extract(file, output_folder_path, failreason)) {
                emit logMessage(QString::fromStdString("Cannot extract PKG: " + failreason));
                emit finished();
                return;
            }

            emit logMessage(QString::fromStdString("Extraction target: " + output_folder_path.string()));

            try {
                std::filesystem::create_directories(output_folder_path);
            } catch (const std::exception& e) {
                emit logMessage(QString::fromStdString("Warning: Failed to create extraction folder before file list export: " + std::string(e.what())));
            }

            emit logMessage("Getting file count from PKG...");
            const u32 files_count = pkg.GetNumberOfFiles();
            emit logMessage(QString::fromStdString("Total files to extract: " + std::to_string(files_count)));
            
            // Export file list for debugging
            emit logMessage("Exporting file list...");
            auto filelist_path = output_folder_path / "filelist.txt";
            if (pkg.ExportFileList(filelist_path)) {
                emit logMessage(QString::fromStdString("File list exported to: " + filelist_path.string()));
            } else {
                emit logMessage("Warning: Failed to export file list");
            }
            
            if (m_previewOnly) {
                emit setMaxProgress(1);
                emit updateProgress(1);
                emit logMessage("=== Preview complete (no files extracted) ===");
                emit logMessage("Opening destination folder...");
                try {
                    std::filesystem::path folder_path(m_outputPath);
                    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(folder_path.string())));
                } catch (const std::exception& e) {
                    emit logMessage(QString::fromStdString("Warning: Could not open folder: " + std::string(e.what())));
                }
                clearProgressCheckpoint();
                emit finished();
                return;
            }

            emit logMessage("Setting up progress bar...");
            emit setMaxProgress(files_count);

            emit logMessage("Starting file extraction loop...");
            u32 failed_files = 0;
            for (u32 index = 0; index < files_count; ++index) {
                writeProgressCheckpoint(index + 1, files_count);
                emit logMessage(QString::fromStdString("Extracting file " + std::to_string(index + 1) + "/" + std::to_string(files_count)));
                try {
                    pkg.ExtractFiles(static_cast<int>(index));
                } catch (const std::exception& e) {
                    failed_files++;
                    emit logMessage(QString::fromStdString("Error extracting file " + std::to_string(index + 1) + ": " + e.what()));
                    emit logMessage("Continuing with next file...");
                } catch (...) {
                    failed_files++;
                    emit logMessage(QString::fromStdString("Unknown error extracting file " + std::to_string(index + 1)));
                    emit logMessage("Continuing with next file...");
                }

                emit updateProgress(index + 1);
                QThread::msleep(1);  // Allow UI to update
            }

            if (failed_files == 0) {
                emit logMessage("=== Extraction complete! ===");
            } else {
                emit logMessage(QString::fromStdString("=== Extraction finished with " + std::to_string(failed_files) + " file errors ==="));
            }
            
            emit logMessage("Opening destination folder...");
            try {
                std::filesystem::path folder_path(m_outputPath);
                QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(folder_path.string())));
            } catch (const std::exception& e) {
                emit logMessage(QString::fromStdString("Warning: Could not open folder: " + std::string(e.what())));
            }
            
            clearProgressCheckpoint();
            emit finished();
            
        } catch (const std::filesystem::filesystem_error& e) {
            emit logMessage(QString::fromStdString("Filesystem error: " + std::string(e.what())));
            emit logMessage(QString::fromStdString("Path: " + e.path1().string()));
            emit finished();
        } catch (const std::exception& e) {
            emit logMessage(QString::fromStdString("Fatal error: " + std::string(e.what())));
            emit finished();
        } catch (...) {
            emit logMessage("Fatal error: Unknown exception occurred during extraction");
            emit finished();
        }
    }

signals:
    void logMessage(const QString& msg);
    void updateProgress(int value);
    void setMaxProgress(int max);
    void finished();

private:
    void writeProgressCheckpoint(const u32 current, const u32 total) {
        if (m_progressPath.isEmpty()) {
            return;
        }

        QFile progressFile(m_progressPath);
        if (progressFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QTextStream stream(&progressFile);
            stream << "current=" << current << "\n";
            stream << "total=" << total << "\n";
            stream << "pkg=" << QString::fromStdString(m_pkgPath) << "\n";
            stream << "timestamp=" << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
                   << "\n";
            stream.flush();
        }
    }

    void clearProgressCheckpoint() {
        if (!m_progressPath.isEmpty()) {
            QFile::remove(m_progressPath);
        }
    }

    std::string m_pkgPath;
    std::string m_outputPath;
    QString m_progressPath;
    bool m_previewOnly = false;
};

PkgXtractorWindow::PkgXtractorWindow(QWidget* parent)
    : QMainWindow(parent), ui(new Ui::PkgXtractorWindow) {
    ui->setupUi(this);
    setAcceptDrops(true);

    // Open log file next to executable
    QString exePath = QCoreApplication::applicationDirPath();
    QString logPath = exePath + "/pkgxtractor-qt.log";
    m_logFile.setFileName(logPath);
    if (m_logFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        m_logStream.setDevice(&m_logFile);
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        m_logStream << "\n=== Session started at " << timestamp << " ===\n";
        m_logStream.flush();
    }

    // Crash marker: if marker exists on startup, previous run did not exit cleanly.
    m_crashMarkerPath = exePath + "/pkgxtractor-qt.state";
    m_progressPath = exePath + "/pkgxtractor-qt.progress";
    if (QFile::exists(m_crashMarkerPath)) {
        QString details;
        QFile progressFile(m_progressPath);
        if (progressFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            details = QString::fromUtf8(progressFile.readAll());
        }

        QString warningMessage =
            "pkgxtractor-qt appears to have crashed during the previous run.\n"
            "Check pkgxtractor-qt.log for the last recorded step.";
        if (!details.isEmpty()) {
            warningMessage += "\n\nLast progress checkpoint:\n" + details;
        }

        QMessageBox::warning(
            this,
            "Previous crash detected",
            warningMessage);
    }
    QFile::remove(m_crashMarkerPath);

    // Create marker
    QFile crashMarker(m_crashMarkerPath);
    if (crashMarker.open(QIODevice::WriteOnly | QIODevice::Text)) {
        crashMarker.close();
    }

    // Connect signals
    connect(ui->m_browsePkgBtn, &QPushButton::clicked, this, &PkgXtractorWindow::onBrowsePkg);
    connect(ui->m_browseOutputBtn, &QPushButton::clicked, this, &PkgXtractorWindow::onBrowseOutput);
    connect(ui->m_previewBtn, &QPushButton::clicked, this, &PkgXtractorWindow::onPreviewOnly);
    connect(ui->m_extractBtn, &QPushButton::clicked, this, &PkgXtractorWindow::onExtract);
}

PkgXtractorWindow::~PkgXtractorWindow() {
    QFile::remove(m_crashMarkerPath);
    m_logFile.close();
    delete ui;
}

void PkgXtractorWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void PkgXtractorWindow::dropEvent(QDropEvent* event) {
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        if (!urls.isEmpty()) {
            QString filePath = urls.at(0).toLocalFile();
            ui->m_pkgPathInput->setText(filePath);
            updateSfoPreview(filePath);
        }
    }
}

void PkgXtractorWindow::onBrowsePkg() {
    QString fileName = QFileDialog::getOpenFileName(this, "Select PKG File", "",
                                                     "PS4 PKG Files (*.pkg);;All Files (*)");
    if (!fileName.isEmpty()) {
        ui->m_pkgPathInput->setText(fileName);
        updateSfoPreview(fileName);
    }
}

void PkgXtractorWindow::onBrowseOutput() {
    QString dirPath = QFileDialog::getExistingDirectory(this, "Select Destination Folder");
    if (!dirPath.isEmpty()) {
        ui->m_outputPathInput->setText(dirPath);
    }
}

void PkgXtractorWindow::onExtract() {
    if (ui->m_outputPathInput->text().isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a destination folder first!");
        return;
    }
    startWorker(false);
}

void PkgXtractorWindow::onPreviewOnly() {
    if (ui->m_outputPathInput->text().isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a destination folder first!");
        return;
    }
    startWorker(true);
}

void PkgXtractorWindow::startWorker(bool previewOnly) {
    if (ui->m_pkgPathInput->text().isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a PKG file first!");
        return;
    }

    ui->m_extractBtn->setEnabled(false);
    ui->m_previewBtn->setEnabled(false);
    ui->m_progressBar->setEnabled(true);
    ui->m_logOutput->clear();

    QFile::remove(m_crashMarkerPath);

    std::string pkgPath = ui->m_pkgPathInput->text().toStdString();
    std::string outputPath = ui->m_outputPathInput->text().toStdString();

    ExtractionWorker* worker = new ExtractionWorker(pkgPath, outputPath, m_progressPath, previewOnly);
    QThread* thread = new QThread;

    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &ExtractionWorker::extract);
    connect(worker, &ExtractionWorker::logMessage, this, &PkgXtractorWindow::onLogMessage);
    connect(worker, &ExtractionWorker::updateProgress, this, &PkgXtractorWindow::onUpdateProgress);
    connect(worker, &ExtractionWorker::setMaxProgress, this, &PkgXtractorWindow::onSetMaxProgress);
    connect(worker, &ExtractionWorker::finished, this, &PkgXtractorWindow::onFinished);
    connect(worker, QOverload<>::of(&ExtractionWorker::finished), thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    thread->start();
}

void PkgXtractorWindow::onLogMessage(const QString& msg) {
    ui->m_logOutput->append(msg);

    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_logStream << "[" << timestamp << "] " << msg << "\n";
    m_logStream.flush();
}

void PkgXtractorWindow::onUpdateProgress(int value) {
    ui->m_progressBar->setValue(value);
}

void PkgXtractorWindow::onSetMaxProgress(int max) {
    ui->m_progressBar->setMaximum(max);
}

void PkgXtractorWindow::onFinished() {
    ui->m_extractBtn->setEnabled(true);
    ui->m_previewBtn->setEnabled(true);
    ui->m_progressBar->setEnabled(false);
    QFile::remove(m_crashMarkerPath);
}

void PkgXtractorWindow::updateSfoPreview(QString pkgPath) {
    ui->m_sfoPreview->setPlainText("Loading...");
    ui->m_iconPreview->setPixmap(QPixmap());
    ui->m_iconPreview->setText("Loading...");

    try {
        std::filesystem::path file(pkgPath.toStdString());

        if (!std::filesystem::exists(file)) {
            ui->m_sfoPreview->setPlainText("PKG file not found");
            ui->m_iconPreview->setText("File not found");
            return;
        }

        PKG pkg{};
        std::string failreason;

        if (!pkg.Open(file, failreason)) {
            ui->m_sfoPreview->setPlainText(QString::fromStdString("Failed to open PKG: " + failreason));
            ui->m_iconPreview->setText("Failed to open");
            return;
        }

        PSF psf{};
        if (!psf.Open(pkg.sfo)) {
            ui->m_sfoPreview->setPlainText("Failed to parse SFO metadata");
            ui->m_iconPreview->setText("SFO parse error");
            return;
        }

        // Category mapping function
        auto mapCategory = [](const std::string& code) -> std::string {
            if (code == "DG" || code == "gd") return "DG Disc Game (Blu-ray)";
            if (code == "GP") return "GP Game Patch";
            if (code == "AC") return "AC Add-on Content (DLC)";
            if (code == "TL") return "TL TV/Live Service";
            if (code == "CT") return "CT Content (Avatar/Theme)";
            if (code == "AV") return "AV Avatar";
            if (code == "MK") return "MK Music/Movie";
            return code + " (Unknown)";
        };

        // Build SFO preview text
        QString sfoText;
        
        // Title
        if (auto title = psf.GetString("TITLE")) {
            sfoText += "Title: " + QString::fromStdString(std::string(title.value())) + "\n";
        }

        // Title ID
        if (auto title_id = psf.GetString("TITLE_ID")) {
            std::string id(title_id.value());
            id.erase(std::remove(id.begin(), id.end(), '\0'), id.end());
            sfoText += "Title ID: " + QString::fromStdString(id) + "\n";
        }

        // Content ID
        if (auto content_id = psf.GetString("CONTENT_ID")) {
            std::string id(content_id.value());
            id.erase(std::remove(id.begin(), id.end(), '\0'), id.end());
            sfoText += "Content ID: " + QString::fromStdString(id) + "\n";
        }

        // Version
        if (auto version = psf.GetInteger("APP_VER")) {
            sfoText += "App Version: " + QString::number(version.value()) + "\n";
        }

        // Content Version
        if (auto version = psf.GetInteger("VERSION")) {
            sfoText += "Content Version: " + QString::number(version.value()) + "\n";
        }

        // Category
        if (auto category = psf.GetString("CATEGORY")) {
            std::string cat(category.value());
            cat.erase(std::remove(cat.begin(), cat.end(), '\0'), cat.end());
            sfoText += "Category: " + QString::fromStdString(mapCategory(cat)) + "\n";
        }

        // Parental Level
        std::string parental_level;
        if (auto parental = psf.GetInteger("PARENTAL_LEVEL")) {
            parental_level = std::to_string(parental.value());
        } else if (auto parental = psf.GetInteger("PARENTAL_CONTROL")) {
            parental_level = std::to_string(parental.value());
        } else {
            parental_level = "-";
        }
        if (parental_level != "-") {
            sfoText += "Parental Lock Level: " + QString::fromStdString(parental_level) + "\n";
        }

        ui->m_sfoPreview->setPlainText(sfoText);

        // Load icon0.png from PKG entry 0x1200
        std::vector<u8> icon_data;
        if (pkg.GetEntryDataById(0x1200, icon_data, failreason)) {
            QImage img;
            if (img.loadFromData(icon_data.data(), icon_data.size(), "PNG")) {
                QPixmap pix = QPixmap::fromImage(img).scaled(m_iconPreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                ui->m_iconPreview->setPixmap(pix);
                ui->m_iconPreview->setText("");
            } else {
                ui->m_iconPreview->setText("Invalid icon0.png");
            }
        } else {
            ui->m_iconPreview->setText("icon0.png not found");
        }
    } catch (const std::exception& e) {
        ui->m_sfoPreview->setPlainText(QString::fromStdString("SFO preview error: " + std::string(e.what())));
    } catch (...) {
        ui->m_sfoPreview->setPlainText("SFO preview error: unknown exception");
    }
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    PkgXtractorWindow window;
    window.show();

    return app.exec();
}

#include "mainwindow.moc"
