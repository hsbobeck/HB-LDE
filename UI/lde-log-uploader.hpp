#pragma once

#include <QDialog>

#include "ui_LdeLogUploader.h"

class LdeLogUploader : public QDialog {
    Q_OBJECT

    std::unique_ptr<Ui::LdeLogUploader> ui;

private:
    void* _contextVp{};

public:
    LdeLogUploader(QWidget* parent = 0);
    ~LdeLogUploader();

private:
    auto UpdateProgressBarVisibility() -> void;
    auto UpdateActionButtonTitle() -> void;
    auto UpdateActionButtonEnabled() -> void;

private slots:
    void on_actionButton_clicked();
    void HandleProgress(float fraction);
    void HandleEnd(bool finished, int uploadedFileCount, int totalFileCount);

protected:
    auto showEvent(QShowEvent* event) -> void override;
    auto reject() -> void override;

public:
    static auto InitOnce() -> void;
    static auto DeinitOnce() -> void;
};
