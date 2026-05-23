#include "lde-log-uploader.hpp"

#include "obs-app.hpp"

#include "qt-wrappers.hpp"

#include <QMessageBox>

#include <fstream>
#include <iomanip>

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/PutObjectRequest.h>

namespace {

// -------------------------------------------------------------------------------------------------
// MARK: - AWS config

const auto kAwsRegion = Aws::Region::US_EAST_1; // todo value
const Aws::String kAwsBucketName = "BUCKET"; // todo value

// -------------------------------------------------------------------------------------------------
// MARK: - Private types

struct Context final {
    bool uploading{};
    std::atomic<bool> atomicStop{};
    std::thread threadQnj{};
};

// -------------------------------------------------------------------------------------------------
// MARK: - Static vars

Aws::SDKOptions sAwsSdkOptions{};

// -------------------------------------------------------------------------------------------------
// MARK: - Private functions

auto MakeMinLogFileTimestamp(QComboBox* uploadCombo) -> uint64_t
{
    int64_t hours{};
    //
    auto uploadComboIndex = uploadCombo->currentIndex();
    if (0 == uploadComboIndex) {
        hours = 24;
    } else if (1 == uploadComboIndex) {
        hours = 48;
    } else if (2 == uploadComboIndex) {
        hours = 7 * 24;
    } else if (3 == uploadComboIndex) {
        hours = 30 * 24;
    } else {
        hours = 24;
    }

    auto now = time(nullptr);
    time_t minUnixTimeInt = now - (hours * 3600);
    //
    struct tm tmValue {
    };
#if defined(__APPLE__)
    localtime_r(&minUnixTimeInt, &tmValue);
#elif defined(_WIN32)
    localtime_s(&tmValue, &minUnixTimeInt);
#else
#error Not implemented
#endif

    std::ostringstream stream{};
#define iTWO_DIGIT std::setw(2) << std::setfill('0')
    stream
        << (tmValue.tm_year + 1900)
        << iTWO_DIGIT << (tmValue.tm_mon + 1)
        << iTWO_DIGIT << tmValue.tm_mday
        << iTWO_DIGIT << tmValue.tm_hour
        << iTWO_DIGIT << tmValue.tm_min
        << iTWO_DIGIT << tmValue.tm_sec;
#undef iTWO_DIGIT
    return std::stoull(stream.str());
}

auto DoesStdStringViewHaveSuffix(std::string_view whole, std::string_view suffix) -> bool
{
    if (whole.size() < suffix.size()) {
        return false;
    }
    return (0 == whole.compare(whole.size() - suffix.size(), suffix.size(), suffix));
}

auto MakeLogFilePaths(uint64_t minLogFileTimestamp, std::vector<std::string>* pathsPtr, std::unordered_map<std::string, std::string>* pathToNameMapPtr) -> void
{
    auto& paths = *pathsPtr;
    auto& pathToNameMap = *pathToNameMapPtr;

    auto dirPathCString = GetConfigPathPtr("Louper/LDE/logs");
    std::string dirPathStdString{dirPathCString};
    //
    BPtr<char> logDir{dirPathCString};
    auto dirQ = os_opendir(logDir);
    if (!dirQ) {
        return;
    }

    constexpr auto kNameSuffix = std::string_view{".txt"};
    //
#if defined(__APPLE__)
    const std::string kSeparator{"/"};
#elif defined(_WIN32)
    const std::string kSeparator{"\\"};
#endif

    for (;;) {
        auto entryQ = os_readdir(dirQ);
        if (!entryQ) {
            break;
        }
        if (entryQ->directory) {
            continue;
        }
        auto nameStdStringView = std::string_view{entryQ->d_name};
        if (!DoesStdStringViewHaveSuffix(nameStdStringView, kNameSuffix)) {
            continue;
        }

        auto nameTimestamp = convert_log_name(/*has_prefix*/ false, entryQ->d_name);
        if (nameTimestamp < minLogFileTimestamp) {
            continue;
        }

        auto nameStdString = std::string{nameStdStringView};
        auto pathStdString = dirPathStdString + kSeparator + nameStdString;
        //
        paths.push_back(pathStdString);
        pathToNameMap[pathStdString] = nameStdString;
    }

    os_closedir(dirQ);

    std::sort(paths.begin(), paths.end(), [](const std::string& pathA, const std::string& pathB) {
        return pathB < pathA;
    });
}

auto RunThread(Context* contextPtr, std::vector<std::string> paths, std::unordered_map<std::string, std::string> pathToNameMap, std::string localId, LdeLogUploader* dialog) -> void
{
    auto& context = *contextPtr;

    Aws::Client::ClientConfiguration awsClientConfig{};
    awsClientConfig.region = kAwsRegion;
    awsClientConfig.requestTimeoutMs = 20000;
    //
    auto s3Client = std::make_shared<Aws::S3::S3Client>(awsClientConfig);

    auto finished = true;
    int uploadedFileCount = 0;
    auto pathCount = paths.size();

    for (size_t pathIndex = 0; pathIndex < pathCount; pathIndex += 1) {
        if (context.atomicStop) {
            finished = false;
            break;
        }

        auto& path = paths.at(pathIndex);
        auto& name = pathToNameMap.at(path);

        std::string objectKeyStdString{};
        {
            std::ostringstream stream{};
            stream << "LdeLogs/" << localId << "/" << name;
            objectKeyStdString = stream.str();
        }
        Aws::String objectKeyAwsString = objectKeyStdString.c_str();

        Aws::S3::Model::PutObjectRequest s3Request{};
        s3Request.SetBucket(kAwsBucketName);
        s3Request.SetKey(objectKeyAwsString);

        do {
            std::shared_ptr<Aws::FStream> inputData = Aws::MakeShared<Aws::FStream>("PutObjectInputStream", path, std::ios_base::in | std::ios_base::binary);

            if (!inputData->is_open()) {
                blog(LOG_ERROR, "LdeLogUploader: Unable to open log file \"%s\"", path.c_str());
                break;
            }

            s3Request.SetBody(inputData);
            Aws::S3::Model::PutObjectOutcome s3Outcome = s3Client->PutObject(s3Request);

            if (!s3Outcome.IsSuccess()) {
                blog(LOG_ERROR, "LdeLogUploader: Unable to upload log file \"%s\": %s", path.c_str(), s3Outcome.GetError().GetMessage().c_str());
                // NOTE: Currently we don't invoke a DeleteS3ObjectFromBucket() here.
                break;
            }

#if 1
            blog(LOG_INFO, "LdeLogUploader: Uploaded log file \"%s\" as \"%s\"", path.c_str(), objectKeyAwsString.c_str());
#endif

            uploadedFileCount += 1;
        } while (false);

        auto fraction = static_cast<float>(pathIndex + 1) / static_cast<float>(pathCount);
        QMetaObject::invokeMethod(dialog, "HandleProgress", Qt::AutoConnection, Q_ARG(float, fraction));
    }

    QMetaObject::invokeMethod(
        dialog, "HandleEnd", Qt::AutoConnection,
        Q_ARG(bool, finished),
        Q_ARG(int, uploadedFileCount),
        Q_ARG(int, static_cast<int>(pathCount)));
}

} // namespace

// -------------------------------------------------------------------------------------------------
// MARK: - Macros

#define iPROVIDE_CONTEXT auto& context = *static_cast<Context*>(_contextVp)

// -------------------------------------------------------------------------------------------------
// MARK: - Basics

LdeLogUploader::LdeLogUploader(QWidget* parent)
    : QDialog{parent},
      ui{new Ui::LdeLogUploader}
{
    _contextVp = new Context{};

    setWindowFlags(windowFlags() & ~Qt::WindowMaximizeButtonHint & ~Qt::WindowContextHelpButtonHint);
    setWindowModality(Qt::ApplicationModal);
    setFixedSize(480, 160);
    ui->setupUi(this);

    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->reset();
}

LdeLogUploader::~LdeLogUploader()
{
    delete static_cast<Context*>(_contextVp);
}

// -------------------------------------------------------------------------------------------------
// MARK: - Private

auto LdeLogUploader::UpdateProgressBarVisibility() -> void
{
    iPROVIDE_CONTEXT;
    ui->progressBar->setVisible(context.uploading);
}

auto LdeLogUploader::UpdateActionButtonTitle() -> void
{
    iPROVIDE_CONTEXT;
    if (!context.uploading) {
        ui->actionButton->setText("Upload");
    } else {
        ui->actionButton->setText("Cancel");
    }
}

auto LdeLogUploader::UpdateActionButtonEnabled() -> void
{
    iPROVIDE_CONTEXT;
    if (!context.uploading) {
        ui->actionButton->setEnabled(true);
    } else {
        if (context.atomicStop) {
            ui->actionButton->setEnabled(false);
        } else {
            ui->actionButton->setEnabled(true);
        }
    }
}

// -------------------------------------------------------------------------------------------------
// MARK: - Private slots

auto LdeLogUploader::on_actionButton_clicked() -> void
{
    iPROVIDE_CONTEXT;
    if (!context.uploading) {
        std::vector<std::string> paths{};
        std::unordered_map<std::string, std::string> pathToNameMap{};
        //
        auto minLogFileTimestamp = MakeMinLogFileTimestamp(ui->uploadCombo);
        MakeLogFilePaths(minLogFileTimestamp, &paths, &pathToNameMap);

        if (paths.empty()) {
            QMessageBox::information(
                this,
                "No logs to upload",
                "There are no log files to upload.",
                QMessageBox::Ok, QMessageBox::NoButton);
            return;
        }

        context.uploading = true;
        context.atomicStop = false;
        //
        UpdateProgressBarVisibility();
        ui->progressBar->setValue(0);
        //
        UpdateActionButtonTitle();
        UpdateActionButtonEnabled();

        auto localId = config_get_string(App()->GlobalConfig(), "General", "LocalId");
        context.threadQnj = std::thread{&RunThread, &context, std::move(paths), std::move(pathToNameMap), std::move(localId), this};
    } else {
        context.atomicStop = true;
        UpdateActionButtonEnabled();
    }
}

auto LdeLogUploader::HandleProgress(float fraction) -> void
{
    ui->progressBar->setValue(static_cast<int>(fraction * 100.0f));
}

auto LdeLogUploader::HandleEnd(bool finished, int uploadedFileCount, int totalFileCount) -> void
{
    iPROVIDE_CONTEXT;
    context.threadQnj.join();

    context.uploading = false;
    context.atomicStop = false;
    //
    UpdateProgressBarVisibility();
    UpdateActionButtonTitle();
    UpdateActionButtonEnabled();

    if (finished) {
        if (uploadedFileCount > 0) {
            App()->SendMixpanelEvent(MixpanelEventKind::LogsUploaded, /*service*/ {}, /*sourceTreeModel*/ {});
        }

        std::ostringstream stream{};
        stream << uploadedFileCount << " ";
        if (1 == uploadedFileCount) {
            stream << "log file ";
        } else {
            stream << "log files ";
        }
        if (uploadedFileCount != totalFileCount) {
            stream << "(out of " << totalFileCount << ") ";
        }
        if (1 == uploadedFileCount) {
            stream << "has been uploaded.";
        } else {
            stream << "have been uploaded.";
        }
        auto message = stream.str();

        QMessageBox::information(
            this,
            "Logs uploaded",
            message.c_str(),
            QMessageBox::Ok, QMessageBox::NoButton);
    }
}

// -------------------------------------------------------------------------------------------------
// MARK: - Protected

auto LdeLogUploader::showEvent(QShowEvent* event) -> void
{
    (void)event;

    auto localId = config_get_string(App()->GlobalConfig(), "General", "LocalId");
    ui->idEdit->setText(localId);

    UpdateProgressBarVisibility();
    UpdateActionButtonTitle();
    UpdateActionButtonEnabled();
}

auto LdeLogUploader::reject() -> void
{
    iPROVIDE_CONTEXT;
    if (context.uploading) {
        return;
    }
    return QDialog::reject();
}

// -------------------------------------------------------------------------------------------------
// MARK: - Interface

auto LdeLogUploader::InitOnce() -> void
{
    Aws::InitAPI(sAwsSdkOptions);
}

auto LdeLogUploader::DeinitOnce() -> void
{
    Aws::ShutdownAPI(sAwsSdkOptions);
}
