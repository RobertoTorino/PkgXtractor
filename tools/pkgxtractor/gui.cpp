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
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QDateTime>
#include <QCoreApplication>
#include <QImage>
#include <QPixmap>
#include <QDesktopServices>
#include <QUrl>
#include <Qt>
#include <QScreen>
#include <QKeyEvent>
#include <QScrollArea>
#include <QCheckBox>
#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <QGuiApplication>

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

// Full-screen borderless image viewer
class FullScreenImageViewer : public QWidget {
    Q_OBJECT

public:
    explicit FullScreenImageViewer(const QString& imagePath, QWidget* parent = nullptr)
        : QWidget(parent), m_imagePath(imagePath) {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground, false);
        setStyleSheet("background-color: black;");

        QVBoxLayout* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        m_imageLabel = new QLabel();
        m_imageLabel->setAlignment(Qt::AlignCenter);
        m_imageLabel->setStyleSheet("background-color: black;");

        QScrollArea* scrollArea = new QScrollArea();
        scrollArea->setWidget(m_imageLabel);
        scrollArea->setWidgetResizable(true);
        scrollArea->setStyleSheet("QScrollArea { background-color: black; border: none; }");

        layout->addWidget(scrollArea);
        setLayout(layout);

        // Load and display image
        QImage img(imagePath);
        if (!img.isNull()) {
            displayImage(img);
        } else {
            showMessage("pic0.png missing or not available");
        }
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            close();
        } else {
            QWidget::keyPressEvent(event);
        }
    }

private:
    void displayImage(const QImage& img) {
        QPixmap pixmap = QPixmap::fromImage(img);
        QScreen* screen = QGuiApplication::primaryScreen();
        QRect screenGeometry = screen->geometry();

        // Scale image to fit screen while maintaining aspect ratio
        QPixmap scaledPixmap = pixmap.scaledToHeight(
            screenGeometry.height(),
            Qt::SmoothTransformation);

        if (scaledPixmap.width() > screenGeometry.width()) {
            scaledPixmap = pixmap.scaledToWidth(
                screenGeometry.width(),
                Qt::SmoothTransformation);
        }

        m_imageLabel->setPixmap(scaledPixmap);
    }

    void showMessage(const QString& message) {
        QLabel* msgLabel = new QLabel(message);
        msgLabel->setAlignment(Qt::AlignCenter);
        msgLabel->setStyleSheet("color: white; font-size: 16px; background-color: black;");

        QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(this->layout());
        if (layout) {
            // Clear existing widgets
            QLayoutItem* item;
            while ((item = layout->takeAt(0)) != nullptr) {
                delete item->widget();
                delete item;
            }
            layout->addWidget(msgLabel);
        }
    }

    QString m_imagePath;
    QLabel* m_imageLabel;
};

// Custom clickable label for icon preview
class ClickableIconLabel : public QLabel {
    Q_OBJECT

public:
    explicit ClickableIconLabel(QWidget* parent = nullptr)
        : QLabel(parent), m_outputPath("") {
        setCursor(Qt::PointingHandCursor);
    }

    void setOutputPath(const QString& path) {
        m_outputPath = path;
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        Q_UNUSED(event);
        if (m_outputPath.isEmpty()) {
            QMessageBox::information(this, "No PKG loaded",
                                   "Please select a PKG file to preview pic0.png.");
            return;
        }

        QString pic0Path = m_outputPath + "/sce_sys/pic0.png";
        if (!QFile::exists(pic0Path)) {
            QMessageBox::information(this, "Image not found",
                                   "pic0.png is not available for this PKG.");
            return;
        }

        FullScreenImageViewer* viewer = new FullScreenImageViewer(pic0Path);
        viewer->showFullScreen();
    }

private:
    QString m_outputPath;
};

class ExtractionWorker : public QObject {
    Q_OBJECT

public:
    ExtractionWorker(const std::string& pkgPath, const std::string& outputPath,
                                         const QString& progressPath, bool previewOnly, bool salvageMode)
        : m_pkgPath(pkgPath), m_outputPath(outputPath), m_progressPath(progressPath),
                    m_previewOnly(previewOnly), m_salvageMode(salvageMode) {}

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

            if (m_previewOnly) {
                emit logMessage("Preview mode: reading PKG metadata...");
                
                try {
                    std::filesystem::create_directories(output_folder_path);
                } catch (const std::exception& e) {
                    emit logMessage(QString::fromStdString("Warning: Failed to create preview folder: " + std::string(e.what())));
                }

                try {
                    if (!pkg.Extract(file, output_folder_path, failreason)) {
                        emit logMessage(QString::fromStdString("Cannot extract PKG: " + failreason));
                        emit finished();
                        return;
                    }
                } catch (const std::exception& e) {
                    emit logMessage(QString::fromStdString("CRITICAL: PKG::Extract() threw exception: " + std::string(e.what())));
                    emit logMessage("Extraction aborted due to critical error");
                    emit finished();
                    return;
                }
                
                const u32 files_count = pkg.GetNumberOfFiles();
                emit logMessage(QString::fromStdString("Preview: " + std::to_string(files_count) + " files in PKG"));
                
                emit logMessage("Exporting file list...");
                auto filelist_path = output_folder_path / "filelist.txt";
                if (pkg.ExportFileList(filelist_path)) {
                    emit logMessage(QString::fromStdString("File list exported to: " + filelist_path.string()));
                } else {
                    emit logMessage("Warning: Failed to export file list");
                }

                emit setMaxProgress(1);
                emit updateProgress(1);
                emit logMessage("=== Preview complete (metadata extracted) ===");
                clearProgressCheckpoint();
                emit finished();
                return;
            }

            try {
                std::filesystem::create_directories(output_folder_path);
            } catch (const std::exception& e) {
                emit logMessage(QString::fromStdString("Warning: Failed to create extraction folder: " + std::string(e.what())));
            }

            emit logMessage("Running PKG extraction to populate file table...");

            try {
                if (!pkg.Extract(file, output_folder_path, failreason)) {
                    emit logMessage(QString::fromStdString("Cannot extract PKG: " + failreason));
                    emit finished();
                    return;
                }
            } catch (const std::exception& e) {
                emit logMessage(QString::fromStdString("CRITICAL: PKG::Extract() threw exception: " + std::string(e.what())));
                emit logMessage("Extraction aborted due to critical error");
                emit finished();
                return;
            } catch (...) {
                emit logMessage("CRITICAL: PKG::Extract() threw unknown exception");
                emit logMessage("Extraction aborted due to critical error");
                emit finished();
                return;
            }

            emit logMessage("Getting file count from PKG...");
            const u32 files_count = pkg.GetNumberOfFiles();
            emit logMessage(QString::fromStdString("Total files to extract: " + std::to_string(files_count)));
            
            // Export file list for debugging (always to the real output path)
            emit logMessage("Exporting file list...");
            auto filelist_path = output_folder_path / "filelist.txt";
            if (pkg.ExportFileList(filelist_path)) {
                emit logMessage(QString::fromStdString("File list exported to: " + filelist_path.string()));
            } else {
                emit logMessage("Warning: Failed to export file list");
            }
            
            emit logMessage(QString::fromStdString("Extraction target: " + output_folder_path.string()));

            emit logMessage("Setting up progress bar...");
            emit setMaxProgress(files_count);

            emit logMessage("Starting file extraction loop...");
            u32 failed_files = 0;
            bool extraction_error = false;
            std::vector<std::string> salvage_skipped_entries;
            std::vector<std::string> extraction_error_entries;
            for (u32 index = 0; index < files_count; ++index) {
                writeProgressCheckpoint(index + 1, files_count);
                emit logMessage(QString::fromStdString("Extracting file " + std::to_string(index + 1) + "/" + std::to_string(files_count)));

                if (m_salvageMode) {
                    std::string preflight_reason;
                    if (!pkg.CanExtractFile(static_cast<int>(index), preflight_reason)) {
                        failed_files++;
                        extraction_error = true;
                        const std::string skip_msg =
                            "Salvage: skipping file " + std::to_string(index + 1) +
                            " due to invalid metadata (" + preflight_reason + ")";
                        salvage_skipped_entries.push_back(skip_msg);
                        extraction_error_entries.push_back(skip_msg);
                        emit logMessage(QString::fromStdString(skip_msg));
                        emit updateProgress(index + 1);
                        QThread::msleep(1);
                        continue;
                    }
                }

                try {
                    pkg.ExtractFiles(static_cast<int>(index));
                } catch (const std::exception& e) {
                    failed_files++;
                    extraction_error = true;
                    const std::string runtime_error_msg =
                        "Error extracting file " + std::to_string(index + 1) + ": " + e.what();
                    extraction_error_entries.push_back(runtime_error_msg);
                    emit logMessage(QString::fromStdString(runtime_error_msg));
                    emit logMessage("Continuing with next file...");
                } catch (...) {
                    failed_files++;
                    extraction_error = true;
                    const std::string unknown_error_msg =
                        "Unknown error extracting file " + std::to_string(index + 1);
                    extraction_error_entries.push_back(unknown_error_msg);
                    emit logMessage(QString::fromStdString(unknown_error_msg));
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

                if (m_salvageMode) {
                    emit logMessage(QString::fromStdString("Recovery Summary: skipped entries = " +
                                                          std::to_string(salvage_skipped_entries.size())));

                    constexpr size_t preview_limit = 20;
                    const size_t shown = std::min(preview_limit, salvage_skipped_entries.size());
                    for (size_t i = 0; i < shown; ++i) {
                        emit logMessage(QString::fromStdString("  [" + std::to_string(i + 1) + "] " +
                                                              salvage_skipped_entries[i]));
                    }
                    if (salvage_skipped_entries.size() > shown) {
                        emit logMessage(QString::fromStdString(
                            "  ... " + std::to_string(salvage_skipped_entries.size() - shown) +
                            " more (see salvage_report.txt)"));
                    }
                }

                if (!extraction_error_entries.empty()) {
                    const auto report_path = output_folder_path / "extraction_errors.txt";
                    std::ofstream report(report_path, std::ios::trunc);
                    if (report.is_open()) {
                        report << "PkgXtractor Extraction Error Report\n";
                        report << "================================\n";
                        report << "PKG: " << m_pkgPath << "\n";
                        report << "Mode: " << (m_salvageMode ? "salvage" : "normal") << "\n";
                        report << "Total files: " << files_count << "\n";
                        report << "Error entries: " << extraction_error_entries.size() << "\n\n";
                        for (const auto& line : extraction_error_entries) {
                            report << line << "\n";
                        }
                        report.close();
                        emit logMessage(QString::fromStdString("Extraction error report written: " + report_path.string()));
                    } else {
                        emit logMessage("Warning: Could not write extraction_errors.txt");
                    }
                }

                if (m_salvageMode && !salvage_skipped_entries.empty()) {
                    const auto report_path = output_folder_path / "salvage_report.txt";
                    std::ofstream report(report_path, std::ios::trunc);
                    if (report.is_open()) {
                        report << "PkgXtractor Salvage Report\n";
                        report << "========================\n";
                        report << "PKG: " << m_pkgPath << "\n";
                        report << "Skipped entries: " << salvage_skipped_entries.size() << "\n\n";
                        for (const auto& line : salvage_skipped_entries) {
                            report << line << "\n";
                        }
                        report.close();
                        emit logMessage(QString::fromStdString("Salvage report written: " + report_path.string()));
                    } else {
                        emit logMessage("Warning: Could not write salvage_report.txt");
                    }
                }
            
                // Only try to open folder if extraction had no errors to avoid crashes
                if (!extraction_error) {
                    emit logMessage("Opening destination folder...");
                    try {
                        std::filesystem::path folder_path(output_folder_path);
                        // Verify the path exists before opening
                        if (std::filesystem::exists(folder_path)) {
                            QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(folder_path.string())));
                        } else {
                            emit logMessage("Warning: Output folder path does not exist");
                        }
                    } catch (const std::exception& e) {
                        emit logMessage(QString::fromStdString("Warning: Could not open folder: " + std::string(e.what())));
                    } catch (...) {
                        emit logMessage("Warning: Could not open folder (unknown error)");
                    }
                } else {
                    emit logMessage("Skipping folder open due to extraction errors");
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
    bool m_salvageMode = false;
};

class PkgXtractorWindow : public QMainWindow {
    Q_OBJECT

public:
    PkgXtractorWindow() : QMainWindow() {
        setWindowTitle("pkgxtractor-qt");
        setGeometry(100, 100, 700, 600);
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
            if (m_logFile.isOpen()) {
                m_logStream << "WARNING: Previous unclean shutdown detected.\n";
                m_logStream.flush();
            }
        }

        // Write marker immediately; remove only on clean shutdown.
        {
            QFile markerFile(m_crashMarkerPath);
            if (markerFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                QTextStream markerStream(&markerFile);
                markerStream << "running\n";
                markerStream << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
                             << "\n";
                markerStream.flush();
            }
        }

        connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
            if (!m_crashMarkerPath.isEmpty()) {
                QFile::remove(m_crashMarkerPath);
            }
        });

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
        m_outputPathInput->setPlaceholderText("Select destination folder (required)");
        outputLayout->addWidget(m_outputPathInput);

        QPushButton* browseFolderBtn = new QPushButton("Browse");
        connect(browseFolderBtn, &QPushButton::clicked, this, &PkgXtractorWindow::onBrowseOutput);
        outputLayout->addWidget(browseFolderBtn);

        // SFO + Icon preview
        QHBoxLayout* previewLayout = new QHBoxLayout();
        mainLayout->addLayout(previewLayout);

        QVBoxLayout* sfoLayout = new QVBoxLayout();
        sfoLayout->addWidget(new QLabel("SFO Preview:"));
        m_sfoPreview = new QTextEdit();
        m_sfoPreview->setReadOnly(true);
        m_sfoPreview->setPlaceholderText("Select a PKG file to preview param.sfo metadata...");
        m_sfoPreview->setMaximumHeight(200);
        sfoLayout->addWidget(m_sfoPreview);
        previewLayout->addLayout(sfoLayout, 3);

        QVBoxLayout* iconLayout = new QVBoxLayout();
        iconLayout->addWidget(new QLabel("icon0.png Preview (click):"));
        m_iconPreview = new ClickableIconLabel();
        m_iconPreview->setFixedSize(200, 200);
        m_iconPreview->setAlignment(Qt::AlignCenter);
        m_iconPreview->setStyleSheet("QLabel { border: 1px solid #666; background: #111; cursor: pointer; }");
        m_iconPreview->setText("No icon");
        iconLayout->addWidget(m_iconPreview);
        iconLayout->addStretch();
        previewLayout->addLayout(iconLayout, 1);

        // Progress Bar
        m_progressBar = new QProgressBar();
        m_progressBar->setEnabled(false);
        mainLayout->addWidget(m_progressBar);

        // Action Buttons
        QHBoxLayout* actionLayout = new QHBoxLayout();
        m_salvageCheck = new QCheckBox("Salvage Mode (skip bad metadata)");
        m_salvageCheck->setChecked(false);
        actionLayout->addWidget(m_salvageCheck);

        m_previewBtn = new QPushButton("Preview Only");
        connect(m_previewBtn, &QPushButton::clicked, this, &PkgXtractorWindow::onPreviewOnly);
        actionLayout->addWidget(m_previewBtn);

        m_compareBtn = new QPushButton("Compare Extractions");
        connect(m_compareBtn, &QPushButton::clicked, this, &PkgXtractorWindow::onCompareExtractions);
        actionLayout->addWidget(m_compareBtn);

        m_extractBtn = new QPushButton("Extract PKG");
        connect(m_extractBtn, &QPushButton::clicked, this, &PkgXtractorWindow::onExtract);
        actionLayout->addWidget(m_extractBtn);
        mainLayout->addLayout(actionLayout);

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
                updateSfoPreview(filePath);
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
            updateSfoPreview(fileName);
        }
    }

    void onBrowseOutput() {
        QString folderPath = QFileDialog::getExistingDirectory(this, "Select Output Folder");
        if (!folderPath.isEmpty()) {
            m_outputPathInput->setText(folderPath);
        }
    }

    void startWorker(bool previewOnly) {
        if (m_pkgPathInput->text().isEmpty()) {
            QMessageBox::warning(this, "Error", "Please select a PKG file first.");
            return;
        }

        if (m_outputPathInput->text().isEmpty()) {
            QMessageBox::warning(this, "Error", "Please select a destination folder.");
            return;
        }

        m_extractBtn->setEnabled(false);
        m_previewBtn->setEnabled(false);
        m_compareBtn->setEnabled(false);
        m_progressBar->setEnabled(true);
        m_progressBar->setValue(0);
        m_logOutput->clear();

        QThread* thread = new QThread();
        ExtractionWorker* worker = new ExtractionWorker(
            m_pkgPathInput->text().toStdString(),
            m_outputPathInput->text().toStdString(),
            m_progressPath,
            previewOnly,
            m_salvageCheck->isChecked());

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
            m_previewBtn->setEnabled(true);
            m_compareBtn->setEnabled(true);
            m_salvageCheck->setEnabled(true);
            m_progressBar->setEnabled(false);
        });

        m_salvageCheck->setEnabled(false);

        thread->start();
    }

    void onExtract() {
        startWorker(false);
    }

    void onCompareExtractions() {
        const QString leftFolder =
            QFileDialog::getExistingDirectory(this, "Select PkgXtractor Extraction Folder");
        if (leftFolder.isEmpty()) {
            return;
        }

        const QString rightFolder =
            QFileDialog::getExistingDirectory(this, "Select LibOrbisPkg Extraction Folder");
        if (rightFolder.isEmpty()) {
            return;
        }

        const auto hashChoice = QMessageBox::question(
            this, "Deep Comparison",
            "Enable SHA-256 hash comparison?\n\n"
            "Yes = slower but more accurate\n"
            "No = fast path/size comparison");
        const bool useHash = (hashChoice == QMessageBox::Yes);

        QString appDir = QCoreApplication::applicationDirPath();
        QString scriptPath =
            QDir::cleanPath(appDir + "/../../scripts/compare_extractions.py");
        if (!QFileInfo::exists(scriptPath)) {
            scriptPath = QDir::cleanPath(appDir + "/../scripts/compare_extractions.py");
        }
        if (!QFileInfo::exists(scriptPath)) {
            scriptPath = QDir::cleanPath(appDir + "/compare_extractions.py");
        }
        if (!QFileInfo::exists(scriptPath)) {
            scriptPath = QDir::cleanPath(appDir + "/scripts/compare_extractions.py");
        }

        if (!QFileInfo::exists(scriptPath)) {
            QString msg = "compare_extractions.py not found.\n\nSearched locations:\n";
            msg += "1. " + QDir::cleanPath(appDir + "/../../scripts/compare_extractions.py") + "\n";
            msg += "2. " + QDir::cleanPath(appDir + "/../scripts/compare_extractions.py") + "\n";
            msg += "3. " + QDir::cleanPath(appDir + "/compare_extractions.py") + "\n";
            msg += "4. " + QDir::cleanPath(appDir + "/scripts/compare_extractions.py");
            QMessageBox::warning(this, "Compare Extractions", msg);
            onLog("Compare Extractions: script not found. App dir: " + appDir);
            return;
        }

        QString pythonExe = QStandardPaths::findExecutable("python");
        QStringList args;
        if (!pythonExe.isEmpty()) {
            args << scriptPath << leftFolder << rightFolder;
        } else {
            pythonExe = QStandardPaths::findExecutable("py");
            if (pythonExe.isEmpty()) {
                QMessageBox::warning(
                    this, "Compare Extractions",
                    "Python executable not found. Install Python 3 and ensure it is in PATH.");
                onLog("Compare Extractions: Python executable not found in PATH.");
                return;
            }
            args << "-3" << scriptPath << leftFolder << rightFolder;
        }

        const QString reportPath =
            QDir::cleanPath(leftFolder + "/compare_report_" +
                            QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".json");
        args << "--json-out" << reportPath;

        if (useHash) {
            args << "--hash";
        }

        const QString leftFilelist = QDir::cleanPath(leftFolder + "/filelist.txt");
        const QString rightFilelist = QDir::cleanPath(rightFolder + "/filelist.txt");
        if (QFileInfo::exists(leftFilelist) && QFileInfo::exists(rightFilelist)) {
            args << "--left-filelist" << leftFilelist << "--right-filelist" << rightFilelist;
        }

        onLog("=== Starting extraction comparison ===");
        onLog("Left: " + leftFolder);
        onLog("Right: " + rightFolder);
        onLog("Mode: " + QString(useHash ? "path + size + hash" : "path + size"));

        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(pythonExe, args);

        if (!process.waitForStarted(5000)) {
            QMessageBox::warning(this, "Compare Extractions",
                                 "Failed to start comparison process.");
            onLog("Compare Extractions: failed to start process.");
            return;
        }

        process.waitForFinished(-1);
        const QString output = QString::fromUtf8(process.readAllStandardOutput());
        if (!output.isEmpty()) {
            onLog(output.trimmed());
        }

        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            QMessageBox::warning(this, "Compare Extractions",
                                 "Comparison failed. See the log panel for details.");
            onLog("Compare Extractions: process failed.");
            return;
        }

        onLog("Comparison JSON report: " + reportPath);
        QMessageBox::information(this, "Compare Extractions",
                                 "Comparison completed successfully.\n\nReport:\n" + reportPath);
    }

    void onPreviewOnly() {
        startWorker(true);
    }

    void onLog(const QString& msg) {
        m_logOutput->append(msg);
        
        // Write to log file
        if (m_logFile.isOpen()) {
            m_logStream << msg << "\n";
            m_logStream.flush();  // Flush immediately so logs aren't lost on crash
        }
    }

    void updateSfoPreview(const QString& pkgPath) {
        m_sfoPreview->clear();
        m_iconPreview->setPixmap(QPixmap());
        m_iconPreview->setText("No icon");

        auto mapCategory = [](QString categoryCode) -> QString {
            const QString code = categoryCode.trimmed().toUpper();
            if (code == "DG" || code == "GD") return "DG Disc Game (Blu-ray)";
            if (code == "GP") return "GP Game Patch";
            if (code == "AC") return "AC Add-on Content (DLC)";
            if (code == "HG") return "HG HDD Game";
            if (code == "EG") return "EG External Game";
            if (code == "UP") return "UP Application Update";
            return code.isEmpty() ? "-" : QString("%1 (Unknown)").arg(code);
        };

        try {
            PKG pkg{};
            std::string failreason;
            if (!pkg.Open(std::filesystem::path(pkgPath.toStdString()), failreason)) {
                m_sfoPreview->setPlainText(QString::fromStdString("Failed to open PKG: " + failreason));
                return;
            }

            if (pkg.sfo.empty()) {
                m_sfoPreview->setPlainText("No param.sfo found in PKG.");
            } else {
                PSF psf{};
                if (!psf.Open(pkg.sfo)) {
                    m_sfoPreview->setPlainText("Failed to parse param.sfo.");
                } else {
                    auto getStr = [&](const char* key) -> QString {
                        auto value = psf.GetString(key);
                        if (!value) {
                            return "-";
                        }
                        std::string text(value.value());
                        text.erase(std::remove(text.begin(), text.end(), '\0'), text.end());
                        return QString::fromStdString(text);
                    };

                    auto getInt = [&](const char* key) -> QString {
                        auto value = psf.GetInteger(key);
                        return value ? QString::number(value.value()) : QString("-");
                    };

                    QString categoryCode = getStr("CATEGORY");
                    QString parental = getInt("PARENTAL_LEVEL");
                    if (parental == "-") {
                        parental = getInt("PARENTAL_CONTROL");
                    }

                    QString titleId = getStr("TITLE_ID");

                    QString preview;
                    preview += "TITLE_ID: " + titleId + "\n";
                    preview += "TITLE: " + getStr("TITLE") + "\n";
                    preview += "CONTENT_ID: " + getStr("CONTENT_ID") + "\n";
                    preview += "APP_VER: " + getStr("APP_VER") + "\n";
                    preview += "VERSION: " + getStr("VERSION") + "\n";
                    preview += "CATEGORY: " + mapCategory(categoryCode) + "\n";
                    preview += "PARENTAL_LEVEL: " + parental + "\n";
                    preview += "CONTENT_TYPE: " + getInt("CONTENT_TYPE") + "\n";
                    m_sfoPreview->setPlainText(preview);

                    // Set the output path for the icon label so it knows where to look for pic0.png
                    QString outputPath = m_outputPathInput->text();
                    if (!outputPath.isEmpty() && !titleId.isEmpty()) {
                        // Sanitize folder name (same as in extraction code)
                        std::string titleIdStr = titleId.toStdString();
                        std::string sanitized = SanitizeFolderName(titleIdStr) + "_" + titleIdStr;
                        QString fullOutputPath = outputPath + "/" + QString::fromStdString(sanitized);
                        m_iconPreview->setOutputPath(fullOutputPath);
                    }
                }
            }

            // Extract pic0.png to temp location for immediate preview
            // The correct entry ID for pic0.png is 0x1220 (verified in pkg_type.cpp)
            std::vector<u32> pic0_entry_ids = {
                0x1220, // CORRECT: pic0.png (verified in pkg_type.cpp)
                0x12A0, // FALLBACK: pic0.dds (variant)
                // Legacy/uncommon IDs in case some PKGs use different layouts
                0x1700, 0x1C00, 0x1701, 0x1702, 0x1703, 0x1704, 0x1705,
                0x1C01, 0x1C02, 0x1C03, 0x1800, 0x1801, 0x1802,
                0x1600, 0x1601, 0x1602, 0x1603,
                0x1300, 0x1400, 0x1500,
                0x0800, 0x0900, 0x0A00, 0x0B00,
                0x1900, 0x1A00, 0x1B00
            };
            
            std::vector<u8> pic0Data;
            std::string pic0Fail;
            bool pic0Found = false;
            
            // Try to find pic0.png by checking for PNG magic in common locations
            for (u32 entry_id : pic0_entry_ids) {
                pic0Data.clear();
                pic0Fail.clear();
                
                if (pkg.GetEntryDataById(entry_id, pic0Data, pic0Fail)) {
                    if (!pic0Data.empty() && pic0Data.size() >= 4) {
                        // Verify it's a valid PNG file (starts with PNG signature: 89 50 4E 47)
                        if (pic0Data[0] == 0x89 && pic0Data[1] == 0x50 && 
                            pic0Data[2] == 0x4E && pic0Data[3] == 0x47) {
                            pic0Found = true;
                            break;
                        }
                    }
                }
            }
            
            if (pic0Found && !pic0Data.empty()) {
                // Create temp directory for pic0
                std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "pkgxtractor_pic0";
                try {
                    std::filesystem::create_directories(tempDir);
                    std::filesystem::path sce_sys_dir = tempDir / "sce_sys";
                    std::filesystem::create_directories(sce_sys_dir);
                    std::filesystem::path pic0Path = sce_sys_dir / "pic0.png";
                    
                    // Write pic0.png to temp location
                    std::ofstream pic0File(pic0Path, std::ios::binary);
                    if (pic0File.is_open()) {
                        pic0File.write(reinterpret_cast<const char*>(pic0Data.data()), pic0Data.size());
                        pic0File.close();
                        
                        // Verify file was written correctly
                        if (std::filesystem::exists(pic0Path)) {
                            u64 fileSize = std::filesystem::file_size(pic0Path);
                            if (fileSize == pic0Data.size()) {
                                // Successfully written - update path to temp directory
                                m_iconPreview->setOutputPath(QString::fromStdString(tempDir.string()));
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    // Silently fail - pic0.png will be unavailable
                }
            }

            std::vector<u8> iconData;
            std::string iconFail;
            if (pkg.GetEntryDataById(0x1200, iconData, iconFail) && !iconData.empty()) {
                QImage image;
                if (image.loadFromData(iconData.data(), static_cast<int>(iconData.size()), "PNG")) {
                    QPixmap pix = QPixmap::fromImage(image).scaled(
                        m_iconPreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    m_iconPreview->setPixmap(pix);
                    m_iconPreview->setText("");
                } else {
                    m_iconPreview->setText("Invalid icon0.png");
                }
            } else {
                m_iconPreview->setText("icon0.png not found");
            }
        } catch (const std::exception& e) {
            m_sfoPreview->setPlainText(QString::fromStdString("SFO preview error: " + std::string(e.what())));
        } catch (...) {
            m_sfoPreview->setPlainText("SFO preview error: unknown exception");
        }
    }

private:
    QLineEdit* m_pkgPathInput;
    QLineEdit* m_outputPathInput;
    QPushButton* m_extractBtn;
    QPushButton* m_previewBtn;
    QPushButton* m_compareBtn;
    QCheckBox* m_salvageCheck;
    QProgressBar* m_progressBar;
    QTextEdit* m_logOutput;
    QTextEdit* m_sfoPreview;
    ClickableIconLabel* m_iconPreview;
    QFile m_logFile;
    QTextStream m_logStream;
    QString m_crashMarkerPath;
    QString m_progressPath;
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    PkgXtractorWindow window;
    window.show();

    return app.exec();
}

#include "gui.moc"
