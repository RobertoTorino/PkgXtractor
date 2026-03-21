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
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QCheckBox>
#include <QMenu>
#include <QWidgetAction>
#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <QPointer>
#include <QWindow>
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

static bool DirectoryHasEntries(const std::filesystem::path& directory,
                                std::vector<std::string>& sample_entries,
                                std::string& error_message) {
    sample_entries.clear();
    std::error_code ec;

    if (!std::filesystem::exists(directory, ec) || ec) {
        return false;
    }

    if (!std::filesystem::is_directory(directory, ec) || ec) {
        error_message = "Target path exists but is not a directory";
        return true;
    }

    std::filesystem::directory_iterator iter(directory, ec);
    std::filesystem::directory_iterator end;
    if (ec) {
        error_message = "Failed to inspect target directory";
        return true;
    }

    for (; iter != end; iter.increment(ec)) {
        if (ec) {
            error_message = "Failed while enumerating target directory";
            return true;
        }
        if (sample_entries.size() < 5) {
            sample_entries.push_back(iter->path().filename().string());
        }
        return true;
    }

    return false;
}

static bool OpenPkgWithRetry(PKG& pkg, const std::filesystem::path& path, std::string& failreason,
                             int maxAttempts = 3, int delayMs = 120) {
    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        failreason.clear();
        if (pkg.Open(path, failreason)) {
            return true;
        }
        if (attempt < maxAttempts) {
            QThread::msleep(static_cast<unsigned long>(delayMs));
        }
    }
    return false;
}

// Full-screen borderless image viewer
class FullScreenImageViewer : public QWidget {
    Q_OBJECT

public:
    explicit FullScreenImageViewer(const QString& imagePath, QWidget* parent = nullptr)
        : QWidget(parent) {
        initializeUi();

        QImage img(imagePath);
        if (!img.isNull()) {
            m_originalPixmap = QPixmap::fromImage(img);
            updateScaledImage();
        } else {
            showMessage("Preview image missing or not available");
        }
    }

    explicit FullScreenImageViewer(const QImage& image, QWidget* parent = nullptr)
        : QWidget(parent) {
        initializeUi();

        if (!image.isNull()) {
            m_originalPixmap = QPixmap::fromImage(image);
            updateScaledImage();
        } else {
            showMessage("Preview image missing or not available");
        }
    }

    void setContentId(const QString& contentId) {
        m_contentId = contentId;
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            close();
        } else {
            QWidget::keyPressEvent(event);
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::RightButton) {
            event->ignore();
            return;
        }

        // Preserve the original behavior for closing fullscreen.
        close();
        event->accept();
    }

    void contextMenuEvent(QContextMenuEvent* event) override {
        QMenu menu(this);

        auto* saveAction = new QWidgetAction(&menu);
        auto* saveButton = new QPushButton("save as wallpaper");
        saveButton->setEnabled(!m_originalPixmap.isNull());
        saveButton->setCursor(Qt::PointingHandCursor);
        saveButton->setMinimumSize(260, 52);

        QFont buttonFont = saveButton->font();
        if (buttonFont.pointSize() > 0) {
            buttonFont.setPointSize(buttonFont.pointSize() + 2);
        } else if (buttonFont.pixelSize() > 0) {
            buttonFont.setPixelSize(buttonFont.pixelSize() + 2);
        }
        saveButton->setFont(buttonFont);
        saveButton->setStyleSheet("QPushButton { text-align: center; }");

        connect(saveButton, &QPushButton::clicked, this,
                &FullScreenImageViewer::saveCurrentImageAsWallpaper);
        connect(saveButton, &QPushButton::clicked, &menu, &QMenu::close);

        saveAction->setDefaultWidget(saveButton);
        menu.addAction(saveAction);
        menu.exec(event->globalPos());
        event->accept();
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        updateScaledImage();
    }

private:
    void initializeUi() {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground, false);
        setAttribute(Qt::WA_DeleteOnClose, true);
        setStyleSheet("background-color: black;");

        QVBoxLayout* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        m_imageLabel = new QLabel();
        m_imageLabel->setAlignment(Qt::AlignCenter);
        m_imageLabel->setStyleSheet("QLabel { background-color: black; border: none; margin: 0px; padding: 0px; }");
        m_imageLabel->setContentsMargins(0, 0, 0, 0);
        m_imageLabel->setMargin(0);
        layout->addWidget(m_imageLabel);
        setLayout(layout);
    }

    void updateScaledImage() {
        if (m_originalPixmap.isNull()) {
            return;
        }

        const QSize targetSize = size();
        if (targetSize.width() <= 0 || targetSize.height() <= 0) {
            return;
        }

        // Fill the entire fullscreen surface and center-crop to avoid any borders.
        QPixmap expanded = m_originalPixmap.scaled(
            targetSize,
            Qt::KeepAspectRatioByExpanding,
            Qt::SmoothTransformation);

        const int cropX = expanded.width() > targetSize.width()
            ? (expanded.width() - targetSize.width()) / 2
            : 0;
        const int cropY = expanded.height() > targetSize.height()
            ? (expanded.height() - targetSize.height()) / 2
            : 0;

        m_imageLabel->setPixmap(
            expanded.copy(cropX, cropY, targetSize.width(), targetSize.height()));
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

    void saveCurrentImageAsWallpaper() {
        if (m_originalPixmap.isNull()) {
            QMessageBox::information(this, "Nothing to save",
                                     "No image is currently available.");
            return;
        }

        QString baseDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        if (baseDir.isEmpty()) {
            baseDir = QDir::homePath();
        }

        QString defaultFilename = m_contentId.isEmpty() ? "pkgxtractor-wallpaper.png" : m_contentId + ".png";
        const QString defaultPath = QDir(baseDir).filePath(defaultFilename);
        const QString selectedPath = QFileDialog::getSaveFileName(
            this,
            "Save as wallpaper",
            defaultPath,
            "PNG Image (*.png);;JPEG Image (*.jpg *.jpeg);;All Files (*)");

        if (selectedPath.isEmpty()) {
            return;
        }

        if (!m_originalPixmap.toImage().save(selectedPath)) {
            QMessageBox::warning(this, "Save failed",
                                 "Could not save wallpaper image.");
            return;
        }

        QMessageBox::information(this, "Wallpaper saved",
                                 "Saved to:\n" + selectedPath);
    }

    QLabel* m_imageLabel;
    QPixmap m_originalPixmap;
    QString m_contentId;
};

// Custom clickable label for icon preview
class ClickableIconLabel : public QLabel {
    Q_OBJECT

public:
    explicit ClickableIconLabel(QWidget* parent = nullptr)
        : QLabel(parent), m_outputPath(""), m_previewImagePath(""),
          m_pkgPath(""), m_pkgLoaded(false) {
        setCursor(Qt::PointingHandCursor);
    }

    void setOutputPath(const QString& path) {
        m_outputPath = path;
    }

    void setPreviewImagePath(const QString& path) {
        m_previewImagePath = path;
    }

    void setPkgPath(const QString& path) {
        m_pkgPath = path;
    }

    void setPkgLoaded(bool loaded) {
        m_pkgLoaded = loaded;
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        Q_UNUSED(event);

        if (!m_pkgLoaded && m_outputPath.isEmpty() && m_pkgPath.isEmpty()) {
            QMessageBox::information(this, "No package loaded",
                                     "Select a PKG file first.");
            return;
        }

        const QString previewPath = resolvePreviewImagePath();

        if (previewPath.isEmpty() && !m_pkgPath.isEmpty()) {
            try {
                PKG pkg{};
                std::string failreason;
                if (OpenPkgWithRetry(pkg, Common::FS::PathFromQString(m_pkgPath), failreason,
                                     2, 60)) {
                    constexpr size_t kMaxPreviewSize = 64 * 1024 * 1024;
                    for (const u32 entryId : {0x1220u, 0x1006u}) { // pic0.png, pic1.png
                        std::vector<u8> imageData;
                        std::string imageFail;
                        if (!pkg.GetEntryDataById(entryId, imageData, imageFail) || imageData.empty()) {
                            continue;
                        }
                        if (imageData.size() > kMaxPreviewSize) {
                            continue;
                        }

                        QImage image;
                        if (image.loadFromData(imageData.data(), static_cast<int>(imageData.size()), "PNG")) {
                            if (m_activeViewer && m_activeViewer->isVisible()) {
                                m_activeViewer->raise();
                                m_activeViewer->activateWindow();
                                return;
                            }

                            m_activeViewer = new FullScreenImageViewer(image);
                            QString contentId = QString::fromStdString(std::string(pkg.GetContentID()));
                            m_activeViewer->setContentId(contentId);
                            connect(m_activeViewer, &QObject::destroyed, this,
                                    [this]() { m_activeViewer = nullptr; });

                            QScreen* targetScreen = nullptr;
                            if (QWidget* topLevel = window()) {
                                if (QWindow* sourceWindow = topLevel->windowHandle()) {
                                    targetScreen = sourceWindow->screen();
                                }
                            }
                            if (!targetScreen) {
                                targetScreen = QGuiApplication::primaryScreen();
                            }

                            if (targetScreen) {
                                m_activeViewer->winId();
                                if (QWindow* viewerWindow = m_activeViewer->windowHandle()) {
                                    viewerWindow->setScreen(targetScreen);
                                }
                                m_activeViewer->setGeometry(targetScreen->geometry());
                            }

                            m_activeViewer->showFullScreen();
                            return;
                        }
                    }
                }
            } catch (...) {
                // Fall through to message below.
            }
        }

        if (previewPath.isEmpty()) {
            QMessageBox::information(this, "Image not found",
                                     "pic0.png or pic1.png is not available in this package.");
            return;
        }

        if (m_activeViewer && m_activeViewer->isVisible()) {
            m_activeViewer->raise();
            m_activeViewer->activateWindow();
            return;
        }

        m_activeViewer = new FullScreenImageViewer(previewPath);
        
        if (!m_pkgPath.isEmpty()) {
            try {
                PKG pkg{};
                std::string failreason;
                if (OpenPkgWithRetry(pkg, Common::FS::PathFromQString(m_pkgPath), failreason, 1, 30)) {
                    QString contentId = QString::fromStdString(std::string(pkg.GetContentID()));
                    m_activeViewer->setContentId(contentId);
                }
            } catch (...) {
                // Content-id extraction failed, viewer will use default filename
            }
        }
        
        connect(m_activeViewer, &QObject::destroyed, this, [this]() { m_activeViewer = nullptr; });

        QScreen* targetScreen = nullptr;
        if (QWidget* topLevel = window()) {
            if (QWindow* sourceWindow = topLevel->windowHandle()) {
                targetScreen = sourceWindow->screen();
            }
        }
        if (!targetScreen) {
            targetScreen = QGuiApplication::primaryScreen();
        }

        if (targetScreen) {
            m_activeViewer->winId();
            if (QWindow* viewerWindow = m_activeViewer->windowHandle()) {
                viewerWindow->setScreen(targetScreen);
            }
            m_activeViewer->setGeometry(targetScreen->geometry());
        }

        m_activeViewer->showFullScreen();
    }

private:
    QString resolvePreviewImagePath() const {
        if (!m_previewImagePath.isEmpty() && QFileInfo::exists(m_previewImagePath)) {
            return m_previewImagePath;
        }

        if (!m_outputPath.isEmpty()) {
            const QString pic0Path = QDir(m_outputPath).filePath("sce_sys/pic0.png");
            if (QFileInfo::exists(pic0Path)) {
                return pic0Path;
            }

            const QString pic1Path = QDir(m_outputPath).filePath("sce_sys/pic1.png");
            if (QFileInfo::exists(pic1Path)) {
                return pic1Path;
            }
        }

        return QString();
    }

    QString m_outputPath;
    QString m_previewImagePath;
    QString m_pkgPath;
    bool m_pkgLoaded;
    QPointer<FullScreenImageViewer> m_activeViewer;
};

class ExtractionWorker : public QObject {
    Q_OBJECT

public:
    ExtractionWorker(const std::filesystem::path& pkgPath,
                                         const std::filesystem::path& outputPath,
                                         const QString& progressPath, bool previewOnly, bool salvageMode)
        : m_pkgPath(pkgPath), m_outputPath(outputPath), m_progressPath(progressPath),
                    m_previewOnly(previewOnly), m_salvageMode(salvageMode) {}

public slots:
    void extract() {
        emit logMessage("=== Starting extraction ===");
        
        try {
            const std::filesystem::path& file = m_pkgPath;
            std::filesystem::path output_folder_path = m_outputPath;

            const std::string pkgPathUtf8 = Common::FS::PathToUTF8String(file);

            emit logMessage(QString::fromStdString("Checking file: " + pkgPathUtf8));
            
            if (!std::filesystem::exists(file)) {
                emit logMessage(QString::fromStdString("Error: File does not exist: " + pkgPathUtf8));
                emit finished();
                return;
            }

            emit logMessage("Detecting file type...");
            if (Loader::DetectFileType(file) != Loader::FileTypes::Pkg) {
                emit logMessage(QString::fromStdString("Error: " + pkgPathUtf8 + " is not a valid PKG file"));
                emit finished();
                return;
            }

            emit logMessage(QString::fromStdString(pkgPathUtf8 + " is a valid PKG"));

            emit logMessage("Initializing PKG extractor...");
            PKG pkg{};
            std::string failreason;

            emit logMessage("Opening PKG file...");
            if (!OpenPkgWithRetry(pkg, file, failreason)) {
                emit logMessage(QString::fromStdString("Cannot open PKG file: " + failreason));
                emit finished();
                return;
            }

            emit logMessage("PKG file opened successfully");

            emit logMessage("SFO logic disabled for stability test.");

            std::string folder_name = std::string(pkg.GetTitleID());
            folder_name.erase(std::remove(folder_name.begin(), folder_name.end(), '\0'),
                              folder_name.end());
            emit logMessage(QString::fromStdString("Using TITLE_ID from PKG header: " +
                                                   folder_name));

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

            {
                std::vector<std::string> sample_entries;
                std::string guardrail_error;
                if (DirectoryHasEntries(output_folder_path, sample_entries, guardrail_error)) {
                    emit logMessage("Guardrail: target output folder is not empty.");
                    emit logMessage(QString::fromStdString("Target folder: " + output_folder_path.string()));
                    emit logMessage("For stability, each preview/extraction requires a new empty folder.");
                    if (!guardrail_error.empty()) {
                        emit logMessage(QString::fromStdString("Guardrail detail: " + guardrail_error));
                    }
                    if (!sample_entries.empty()) {
                        std::string joined;
                        for (size_t i = 0; i < sample_entries.size(); ++i) {
                            if (i > 0) {
                                joined += ", ";
                            }
                            joined += sample_entries[i];
                        }
                        emit logMessage(QString::fromStdString("Existing entries: " + joined));
                    }
                    emit finished();
                    return;
                }
            }

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
                if (files_count == 0) {
                    emit logMessage("Preview: no extractable outer PFS entries detected; package appears metadata-only.");
                }
                
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
            if (files_count == 0) {
                emit logMessage("No extractable outer PFS entries detected; extracted metadata/sce_sys only.");
            }
            
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
            bool aborted_early = false;
            std::vector<std::string> salvage_skipped_entries;
            std::vector<std::string> extraction_error_entries;
            for (u32 index = 0; index < files_count; ++index) {
                writeProgressCheckpoint(index + 1, files_count);
                
                // Add file boundary message at every 100 files for debugging
                if (index % 100 == 0 && index > 0) {
                    emit logMessage(QString::fromStdString("[BOUNDARY] Processed " + std::to_string(index) + " files successfully"));
                }
                
                emit logMessage(QString::fromStdString("Extracting file " + std::to_string(index + 1) + "/" + std::to_string(files_count)));

                std::string preflight_reason;
                if (!pkg.CanExtractFile(static_cast<int>(index), preflight_reason)) {
                    failed_files++;
                    extraction_error = true;

                    const std::string preflight_msg =
                        "Preflight failed for file " + std::to_string(index + 1) +
                        " (" + preflight_reason + ")";
                    extraction_error_entries.push_back(preflight_msg);
                    emit logMessage(QString::fromStdString(preflight_msg));

                    if (m_salvageMode) {
                        const std::string skip_msg =
                            "Salvage: skipping file " + std::to_string(index + 1) +
                            " due to invalid metadata (" + preflight_reason + ")";
                        salvage_skipped_entries.push_back(skip_msg);
                        emit logMessage(QString::fromStdString(skip_msg));
                        emit updateProgress(index + 1);
                        QThread::msleep(1);
                        continue;
                    }

                    emit logMessage(
                        "Aborting extraction in normal mode due to invalid metadata. "
                        "Enable Salvage Mode to skip bad entries.");
                    emit updateProgress(index + 1);
                    aborted_early = true;
                    break;
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

            if (aborted_early) {
                    emit logMessage("=== Extraction aborted early due to invalid metadata ===");
                } else if (failed_files == 0) {
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
                        report << "PKG: " << Common::FS::PathToUTF8String(m_pkgPath) << "\n";
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
                        report << "PKG: " << Common::FS::PathToUTF8String(m_pkgPath) << "\n";
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
            
                // Signal the main window to open the folder if extraction succeeded
                if (!extraction_error) {
                    try {
                        std::filesystem::path folder_path(output_folder_path);
                        if (std::filesystem::exists(folder_path)) {
                            // Convert to QString and emit signal for main window to handle
                            QString folderPathStr;
                            Common::FS::PathToQString(folderPathStr, folder_path);
                            emit openFolder(folderPathStr);
                        } else {
                            emit logMessage("Warning: Output folder path does not exist");
                        }
                    } catch (const std::exception& e) {
                        emit logMessage(QString::fromStdString("Warning: Could not prepare folder path: " + std::string(e.what())));
                    } catch (...) {
                        emit logMessage("Warning: Could not prepare folder path (unknown error)");
                    }
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
    void openFolder(const QString& folderPath);

private:
    void writeProgressCheckpoint(const u32 current, const u32 total) {
        if (m_progressPath.isEmpty()) {
            return;
        }

        QString pkgPathQt;
        Common::FS::PathToQString(pkgPathQt, m_pkgPath);

        QFile progressFile(m_progressPath);
        if (progressFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QTextStream stream(&progressFile);
            stream << "current=" << current << "\n";
            stream << "total=" << total << "\n";
            stream << "pkg=" << pkgPathQt << "\n";
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

    std::filesystem::path m_pkgPath;
    std::filesystem::path m_outputPath;
    QString m_progressPath;
    bool m_previewOnly = false;
    bool m_salvageMode = false;
};

class PkgXtractorWindow : public QMainWindow {
    Q_OBJECT

public:
    PkgXtractorWindow() : QMainWindow() {
        // Format build timestamp from compile-time macros
        auto formatBuildDate = []() -> QString {
            QString date = __DATE__;  // "Mar  9 2026"
            QString time = __TIME__;  // "15:37:58"
            
            QMap<QString, QString> months = {
                {"Jan", "01"}, {"Feb", "02"}, {"Mar", "03"}, {"Apr", "04"},
                {"May", "05"}, {"Jun", "06"}, {"Jul", "07"}, {"Aug", "08"},
                {"Sep", "09"}, {"Oct", "10"}, {"Nov", "11"}, {"Dec", "12"}
            };
            
            QStringList parts = date.simplified().split(' ');
            if (parts.size() == 3) {
                QString month = months.value(parts[0], "00");
                QString day = parts[1].rightJustified(2, '0');
                QString year = parts[2];
                return QString("build: %1-%2-%3 %4").arg(year).arg(month).arg(day).arg(time.toLower());
            }
            return QString("build: %1 %2").arg(date).arg(time).toLower();
        };
        
        setWindowTitle(QString("pkgxtractor-qt - %1 - RT").arg(formatBuildDate()));
        setGeometry(100, 100, 700, 600);
        setFixedSize(700, 600);
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

        m_renamePkgBtn = new QPushButton("Rename to CONTENT_ID");
        connect(m_renamePkgBtn, &QPushButton::clicked, this, &PkgXtractorWindow::onRenamePkgToContentId);
        pkgLayout->addWidget(m_renamePkgBtn);

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
        m_sfoPreview->setMaximumHeight(220);
        m_sfoPreview->setStyleSheet("QTextEdit { border: none; }");
        m_sfoPreview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        sfoLayout->addWidget(m_sfoPreview);
        sfoLayout->addStretch();  // Push content to top, not centered
        previewLayout->addLayout(sfoLayout, 3);

        QVBoxLayout* iconLayout = new QVBoxLayout();
        iconLayout->addWidget(new QLabel("icon preview (click)"));
        m_iconPreview = new ClickableIconLabel();
        m_iconPreview->setFixedSize(200, 200);
        m_iconPreview->setAlignment(Qt::AlignCenter);
        m_iconPreview->setStyleSheet("QLabel { border: none; background: #111; }");
        showFallbackLogo();
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
        m_logOutput = new QTextEdit();
        m_logOutput->setReadOnly(true);
        m_logOutput->setStyleSheet("QTextEdit { border: none; }");
        m_logOutput->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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
        QString fileName = QFileDialog::getOpenFileName(
            this, "Select PKG File", QString(),
            "PKG Files (*.pkg *.PKG);;All Files (*)");
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
        m_renamePkgBtn->setEnabled(false);
        m_progressBar->setEnabled(true);
        m_progressBar->setValue(0);
        m_logOutput->clear();

        QThread* thread = new QThread();
        ExtractionWorker* worker = new ExtractionWorker(
            Common::FS::PathFromQString(m_pkgPathInput->text()),
            Common::FS::PathFromQString(m_outputPathInput->text()),
            m_progressPath,
            previewOnly,
            m_salvageCheck->isChecked());

        worker->moveToThread(thread);

        connect(thread, &QThread::started, worker, &ExtractionWorker::extract);
        connect(worker, &ExtractionWorker::logMessage, this, &PkgXtractorWindow::onLog);
        connect(worker, &ExtractionWorker::updateProgress, m_progressBar, &QProgressBar::setValue);
        connect(worker, &ExtractionWorker::setMaxProgress, m_progressBar, &QProgressBar::setMaximum);
        connect(worker, &ExtractionWorker::openFolder, this, [](const QString& folderPath) {
            try {
                if (!folderPath.isEmpty()) {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath));
                }
            } catch (...) {
                // Silently ignore folder opening errors
            }
        }, Qt::QueuedConnection);
        connect(worker, &ExtractionWorker::finished, thread, &QThread::quit);
        connect(worker, &ExtractionWorker::finished, worker, &QObject::deleteLater);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        connect(thread, &QThread::finished, this, [this]() {
            m_extractBtn->setEnabled(true);
            m_previewBtn->setEnabled(true);
            m_compareBtn->setEnabled(true);
            m_renamePkgBtn->setEnabled(true);
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

    void onRenamePkgToContentId() {
        const QString currentPath = m_pkgPathInput->text().trimmed();
        if (currentPath.isEmpty()) {
            QMessageBox::warning(this, "Rename PKG", "Please select a PKG file first.");
            return;
        }

        const std::filesystem::path pkgPath = Common::FS::PathFromQString(currentPath);
        if (!std::filesystem::exists(pkgPath)) {
            QMessageBox::warning(this, "Rename PKG", "Selected PKG file does not exist.");
            return;
        }

        if (Loader::DetectFileType(pkgPath) != Loader::FileTypes::Pkg) {
            QMessageBox::warning(this, "Rename PKG", "Selected file is not a valid PKG.");
            return;
        }

        // Extract rename fields in nested scope so file gets closed before rename
        QString contentIdQt;
        QString targetBaseName;
        QString targetScheme = "CONTENT_ID";
        {
            PKG pkg{};
            std::string failreason;
            if (!OpenPkgWithRetry(pkg, pkgPath, failreason, 2, 60)) {
                QMessageBox::warning(
                    this, "Rename PKG",
                    QString::fromStdString("Failed to open PKG: " + failreason));
                return;
            }

            // Read CONTENT_ID from PKG header (36-byte field at offset 0x40)
            std::string contentId(pkg.GetContentID());
            contentId.erase(std::remove(contentId.begin(), contentId.end(), '\0'), contentId.end());
            contentIdQt = QString::fromStdString(contentId).trimmed();
            
            if (contentIdQt.isEmpty()) {
                QMessageBox::warning(this, "Rename PKG", "CONTENT_ID is empty or not found in PKG header.");
                return;
            }

            targetBaseName = contentIdQt;

            // For game packages, append normalized APP_VER (01.00 -> 0100).
            // DLC/content packages (CATEGORY=AC) usually don't carry APP_VER and keep CONTENT_ID only.
            if (!pkg.sfo.empty()) {
                PSF psf{};
                if (psf.Open(pkg.sfo)) {
                    QString categoryQt;
                    if (auto categoryValue = psf.GetString("CATEGORY"); categoryValue) {
                        std::string category(categoryValue.value());
                        category.erase(std::remove(category.begin(), category.end(), '\0'),
                                       category.end());
                        categoryQt = QString::fromStdString(category).trimmed().toUpper();
                    }

                    const bool isAddOnContent = (categoryQt == "AC") || categoryQt.startsWith("AC ");
                    if (!isAddOnContent) {
                        QString appVerQt;
                        if (auto appVerValue = psf.GetString("APP_VER"); appVerValue) {
                            std::string appVer(appVerValue.value());
                            appVer.erase(std::remove(appVer.begin(), appVer.end(), '\0'),
                                         appVer.end());
                            appVerQt = QString::fromStdString(appVer).trimmed();
                            appVerQt.remove('.');
                        }

                        if (!appVerQt.isEmpty()) {
                            targetBaseName = contentIdQt + "-" + appVerQt;
                            targetScheme = "CONTENT_ID-APP_VER";
                        }
                    }
                }
            }
        } // PKG destructor called here - file is now closed

        QString extension;
        Common::FS::PathToQString(extension, pkgPath.extension());
        if (extension.isEmpty()) {
            extension = ".pkg";
        }

        QString parentDir;
        Common::FS::PathToQString(parentDir, pkgPath.parent_path());
        const QString targetPath = QDir(parentDir).filePath(targetBaseName + extension);

        Qt::CaseSensitivity pathCase = Qt::CaseSensitive;
#ifdef _WIN32
        pathCase = Qt::CaseInsensitive;
#endif
        if (QString::compare(QFileInfo(currentPath).absoluteFilePath(),
                             QFileInfo(targetPath).absoluteFilePath(),
                             pathCase) == 0) {
            QMessageBox::information(this, "Rename PKG",
                                     "PKG is already named after " + targetScheme + ".");
            return;
        }

        if (QFileInfo::exists(targetPath)) {
            QMessageBox::warning(this, "Rename PKG",
                                 "A file with that " + targetScheme + " name already exists in this folder.");
            return;
        }

        const std::filesystem::path targetFsPath = Common::FS::PathFromQString(targetPath);
        std::error_code renameEc;
        std::filesystem::rename(pkgPath, targetFsPath, renameEc);
        if (renameEc) {
            QMessageBox::warning(
                this, "Rename PKG",
                QString::fromStdString(
                    "Failed to rename file: " + renameEc.message() +
                    ". Ensure it is not in use and you have write permission."));
            return;
        }

        m_pkgPathInput->setText(targetPath);
        m_iconPreview->setPkgPath(targetPath);
        onLog("Renamed PKG to " + targetScheme + ": " + targetPath);
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
        onLog("Preview: loading icon metadata for " + pkgPath);
        m_sfoPreview->clear();
        showFallbackLogo();
        m_iconPreview->setPkgPath(pkgPath);
        m_iconPreview->setPkgLoaded(false);
        m_iconPreview->setOutputPath("");
        m_iconPreview->setPreviewImagePath("");

        auto sanitizeText = [](QString value) -> QString {
            value.remove(QChar('\0'));
            value = value.trimmed();
            return value.isEmpty() ? QString("not available") : value;
        };

        auto buildPreviewText = [](const QString& category, const QString& contentId,
                                   const QString& title, const QString& titleId,
                                   const QString& version) {
            return QString("CATEGORY: %1\n"
                           "CONTENT_ID: %2\n"
                           "TITLE: %3\n"
                           "TITLE_ID: %4\n"
                           "VERSION: %5")
                .arg(category, contentId, title, titleId, version);
        };

        QString category = "not available";
        QString contentId = "not available";
        QString title = "not available";
        QString titleId = "not available";
        QString version = "not available";

        if (pkgPath.isEmpty()) {
            m_sfoPreview->setPlainText(
                buildPreviewText(category, contentId, title, titleId, version));
            return;
        }

        try {
            PKG pkg{};
            std::string failreason;
            if (!OpenPkgWithRetry(pkg, Common::FS::PathFromQString(pkgPath), failreason, 2, 60)) {
                onLog(QString::fromStdString("Preview: failed to open PKG for icon: " + failreason));
                m_sfoPreview->setPlainText(
                    buildPreviewText(category, contentId, title, titleId, version));
                return;
            }

            m_iconPreview->setPkgLoaded(true);

            QString outputRoot = m_outputPathInput->text().trimmed();
            std::string headerTitleId = std::string(pkg.GetTitleID());
            headerTitleId.erase(std::remove(headerTitleId.begin(), headerTitleId.end(), '\0'),
                                headerTitleId.end());
            if (!outputRoot.isEmpty() && !headerTitleId.empty()) {
                const std::string sanitizedTitleId = SanitizeFolderName(headerTitleId);
                const QString fullOutputPath =
                    QDir(outputRoot).filePath(QString::fromStdString(sanitizedTitleId));
                m_iconPreview->setOutputPath(fullOutputPath);
            }

            std::vector<u8> iconData;
            std::string iconFail;
            if (pkg.GetEntryDataById(0x1200, iconData, iconFail) && !iconData.empty()) {
                constexpr size_t kMaxIconSize = 32 * 1024 * 1024;
                if (iconData.size() <= kMaxIconSize) {
                    QImage image;
                    if (image.loadFromData(iconData.data(), static_cast<int>(iconData.size()), "PNG")) {
                        QPixmap pix = QPixmap::fromImage(image).scaled(
                            m_iconPreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        m_iconPreview->setPixmap(pix);
                        m_iconPreview->setText("");
                        m_iconPreview->setPkgLoaded(true);
                        onLog("Preview: icon0.png loaded");
                    } else {
                        onLog("Preview: icon0.png invalid, using fallback logo");
                    }
                } else {
                    onLog("Preview: icon0.png too large, using fallback logo");
                }
            } else {
                onLog("Preview: icon0.png not found, using fallback logo");
            }

            if (!pkg.sfo.empty()) {
                PSF psf{};
                if (psf.Open(pkg.sfo)) {
                    auto readSfoString = [&](const char* key) {
                        if (auto value = psf.GetString(key); value) {
                            return sanitizeText(
                                QString::fromStdString(std::string(value.value())));
                        }
                        return QString("not available");
                    };

                    category = readSfoString("CATEGORY");
                    contentId = readSfoString("CONTENT_ID");
                    title = readSfoString("TITLE");
                    titleId = readSfoString("TITLE_ID");
                    version = readSfoString("VERSION");
                    onLog("Preview: SFO metadata loaded (5-key view)");
                } else {
                    onLog("Preview: failed to parse param.sfo, using fallback values");
                }
            } else {
                onLog("Preview: param.sfo missing, using fallback values");
            }

            if (contentId == "not available") {
                contentId = sanitizeText(QString::fromStdString(std::string(pkg.GetContentID())));
            }
            if (titleId == "not available") {
                titleId = sanitizeText(QString::fromStdString(std::string(pkg.GetTitleID())));
            }
        } catch (const std::exception& e) {
            onLog(QString::fromStdString("Preview: icon load exception: " + std::string(e.what())));
        } catch (...) {
            onLog("Preview: icon load unknown exception");
        }

        m_sfoPreview->setPlainText(
            buildPreviewText(category, contentId, title, titleId, version));
    }

private:
    void showFallbackLogo() {
        // Load embedded icon from Qt resources
        QPixmap logoPixmap(":/icon.png");
        if (!logoPixmap.isNull()) {
            QPixmap scaled = logoPixmap.scaled(
                m_iconPreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            m_iconPreview->setPixmap(scaled);
            m_iconPreview->setText("");
            return;
        }

        // Fallback if resource not found
        m_iconPreview->setPixmap(QPixmap());
        m_iconPreview->setText("No icon");
    }

    QLineEdit* m_pkgPathInput;
    QLineEdit* m_outputPathInput;
    QPushButton* m_extractBtn;
    QPushButton* m_previewBtn;
    QPushButton* m_compareBtn;
    QPushButton* m_renamePkgBtn;
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
