//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021-2022 SpriteOvO
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

#include "MainWindow.h"

#include <QAbstractButton>
#include <QScreen>
#include <QCursor>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QMessageBox>
#include <QPaintEvent>
#include <QFontDatabase>
#include <QResizeEvent>

#include <Config.h>
#include "../Helper.h"
#include "../Error.h"
#include "../Core/AppleCP.h"
#if defined APD_OS_WIN
    #include "../Core/GlobalMedia.h"
#endif
#include "../Core/Settings.h"
#include "DownloadWindow.h"
#include "SelectWindow.h"
#include "Widget/PopupControlPanel.h"

using namespace std::chrono_literals;

namespace Gui {

class CloseButton : public QAbstractButton
{
public:
    CloseButton(QWidget *parent = nullptr)
        : QAbstractButton{parent}
    {
        setFixedSize(30, 30);
        setFocusPolicy(Qt::StrongFocus);
        setObjectName("closeButton");
        setAccessibleName(QObject::tr("Sluiten"));
        setToolTip(QObject::tr("Sluiten"));
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPainter painter{this};
        painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

        DrawBackground(painter);
        DrawX(painter);
    }

    void DrawBackground(QPainter &painter)
    {
        painter.save();
        {
            painter.setPen(Qt::NoPen);

            const bool dark = qGray(palette().color(QPalette::Window).rgb()) < 128;
            QColor color = dark ? QColor{255, 255, 255, 22} : QColor{60, 60, 67, 18};
            if (isDown()) {
                color = dark ? QColor{255, 255, 255, 48} : QColor{60, 60, 67, 38};
            }
            else if (underMouse()) {
                color = dark ? QColor{255, 255, 255, 34} : QColor{60, 60, 67, 24};
            }
            painter.setBrush(QBrush{color});
            painter.drawEllipse(rect());

            if (_showKeyboardFocus) {
                painter.setPen(QPen{QColor{0, 122, 255}, 2});
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(rect().adjusted(1, 1, -1, -1));
            }
        }
        painter.restore();
    }

    void DrawX(QPainter &painter)
    {
        painter.save();
        {
            const bool dark = qGray(palette().color(QPalette::Window).rgb()) < 128;
            painter.setPen(
                QPen{dark ? QColor{174, 174, 178} : QColor{142, 142, 147}, 1.25, Qt::SolidLine,
                     Qt::RoundCap});
            painter.setBrush(Qt::NoBrush);

            QSize size = this->size();

            constexpr int margin = 9;

            painter.drawLine(margin, margin, size.width() - margin, size.height() - margin);

            painter.drawLine(size.width() - margin, margin, margin, size.height() - margin);
        }
        painter.restore();
    }

    void focusInEvent(QFocusEvent *event) override
    {
        // Qt can give the first button focus when a tool window is shown.  That is useful for
        // assistive technology, but it must not look like a pointer hover or a user action.
        // Reserve the visible ring for explicit keyboard traversal.
        _showKeyboardFocus = event->reason() == Qt::TabFocusReason ||
            event->reason() == Qt::BacktabFocusReason;
        QAbstractButton::focusInEvent(event);
        update();
    }

    void focusOutEvent(QFocusEvent *event) override
    {
        _showKeyboardFocus = false;
        QAbstractButton::focusOutEvent(event);
        update();
    }

private:
    bool _showKeyboardFocus{false};
};

//////////////////////////////////////////////////

class VideoWidget : public QVideoWidget
{
    Q_OBJECT

public:
    using QVideoWidget::QVideoWidget;

Q_SIGNALS:
    void Clicked();

private:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        Q_EMIT Clicked();
    }
};

//////////////////////////////////////////////////

enum class NewVersionAction {
    Update,
    Skip,
    Later,
};

NewVersionAction NewVersionMessageBox(
    QWidget *parent, const QString &title, const QString &text,
    const Core::Update::ReleaseInfo &releaseInfo)
{
    QMessageBox msgBox{QMessageBox::Question, title, text, QMessageBox::NoButton, parent};

    const auto buttonUpdate = msgBox.addButton(QMessageBox::tr("Update now"), QMessageBox::YesRole);
    const auto buttonSkip =
        msgBox.addButton(QMessageBox::tr("Skip this version"), QMessageBox::AcceptRole);
    const auto buttonView = msgBox.addButton(QMessageBox::tr("View release"), QMessageBox::NoRole);
    const auto buttonLater =
        msgBox.addButton(QMessageBox::tr("Remind me later"), QMessageBox::NoRole);

    msgBox.setDefaultButton(buttonUpdate);

    buttonView->disconnect();
    msgBox.connect(buttonView, &QPushButton::clicked, &msgBox, [&] { releaseInfo.OpenUrl(); });

    if (msgBox.exec() == -1) {
        return NewVersionAction::Later;
    }

    const auto clickedButton = msgBox.clickedButton();

    if (clickedButton == buttonUpdate) {
        return NewVersionAction::Update;
    }
    else if (clickedButton == buttonSkip) {
        return NewVersionAction::Skip;
    }
    else {
        return NewVersionAction::Later;
    }
}

//////////////////////////////////////////////////

MainWindow::MainWindow(QWidget *parent, bool startUpdateChecker) : QDialog{parent}
{
    qRegisterMetaType<Core::AirPods::State>("Core::AirPods::State");
    qRegisterMetaType<Core::Update::ReleaseInfo>("Core::Update::ReleaseInfo");

    _videoWidget = new VideoWidget{this};
    _closeButton = new CloseButton{this};

    _ui.setupUi(this);
    setMinimumWidth(_windowMinimumWidth);
    setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlags(windowFlags() | Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    // The bundled Apple pairing AVI has an opaque light background.  Keep the complete pairing
    // surface light until we can ship alpha-capable artwork; otherwise Dark Mode produces a
    // conspicuous white video rectangle that breaks the visual continuity of the animation.
    // This also matches Apple's light pairing card rather than pretending that the AVI supports
    // a dark appearance.
    auto popupPalette = palette();
    popupPalette.setColor(QPalette::Window, QColor{250, 250, 252});
    popupPalette.setColor(QPalette::WindowText, QColor{28, 28, 30});
    popupPalette.setColor(QPalette::Base, Qt::white);
    popupPalette.setColor(QPalette::Text, QColor{28, 28, 30});
    popupPalette.setColor(QPalette::Button, QColor{229, 229, 234});
    popupPalette.setColor(QPalette::ButtonText, QColor{28, 28, 30});
    setPalette(popupPalette);
    setAutoFillBackground(false);
    Utils::Qt::SetPaletteColor(this, QPalette::Window, QColor{250, 250, 252});
    Utils::Qt::SetPaletteColor(_ui.deviceLabel, QPalette::WindowText, QColor{28, 28, 30});
    _ui.controlSeparator->setStyleSheet("color: rgba(60,60,67,51);");
    _ui.pushButton->setStyleSheet(
        "QPushButton { min-height: 41px; border: 0; border-radius: 10px; "
        "background: #e5e5ea; color: #1c1c1e; } QPushButton:hover { background: #d1d1d6; "
        "} QPushButton:focus { border: 2px solid #007aff; }");

    const auto configureBattery = [](Widget::Battery *battery) {
        auto font = battery->font();
        font.setPointSize(10);
        font.setWeight(QFont::Medium);
        battery->setFont(font);

        battery->setTextPadding(5.0);
        battery->setBatterySize(28, 12);
        battery->setBorderWidth(1.0);
        battery->setBorderRadius(3.0);
        battery->setBackgroundRadius(2.0);
        battery->setHeadRadius(1.5);
        battery->setBorderColor(QColor{142, 142, 147});
        battery->setNormalColor(QColor{52, 199, 89});
        battery->setChargingIconColor(QColor{52, 199, 89});

        auto batteryPalette = battery->palette();
        batteryPalette.setColor(QPalette::WindowText, QColor{29, 29, 31});
        battery->setPalette(batteryPalette);
    };
    configureBattery(_leftBattery);
    configureBattery(_rightBattery);
    configureBattery(_caseBattery);

    _controlPanel = new PopupControlPanel{this};
    _controlPanel->SetNoiseControlState(std::nullopt, false);
    _ui.layoutControls->addWidget(_controlPanel);

    connect(qApp, &QGuiApplication::applicationStateChanged, this, &MainWindow::OnAppStateChanged);
    connect(_ui.pushButton, &QPushButton::clicked, this, &MainWindow::OnButtonClicked);
    connect(&_posAnimation, &QPropertyAnimation::finished, this, &MainWindow::OnPosMoveFinished);
    connect(_videoWidget, &VideoWidget::Clicked, this, &MainWindow::OnAnimationClicked);
    connect(_closeButton, &QAbstractButton::clicked, this, &MainWindow::DoHide);
    connect(_mediaPlayer, &QMediaPlayer::stateChanged, this, &MainWindow::OnPlayerStateChanged);

    connect(this, &MainWindow::UpdateStateSafely, this, &MainWindow::UpdateState);
    connect(this, &MainWindow::AvailableSafely, this, &MainWindow::Available);
    connect(this, &MainWindow::UnavailableSafely, this, &MainWindow::Unavailable);
    connect(this, &MainWindow::DisconnectSafely, this, &MainWindow::Disconnect);
    connect(this, &MainWindow::BindSafely, this, &MainWindow::Bind);
    connect(this, &MainWindow::UnbindSafely, this, &MainWindow::Unbind);
    connect(this, &MainWindow::ShowSafely, this, &MainWindow::show);
    connect(this, &MainWindow::HideSafely, this, &MainWindow::DoHide);
    connect(
        this, &MainWindow::VersionUpdateAvailableSafely, this, &MainWindow::VersionUpdateAvailable);

    _posAnimation.setDuration(260);
    _posAnimation.setEasingCurve(QEasingCurve::OutCubic);
    _autoHideTimer->callOnTimeout([this] { DoHide(); });
    _mediaPlayer->setMuted(true);
    _mediaPlayer->setVideoOutput(_videoWidget);

    _ui.layoutAnimation->addWidget(_videoWidget);
    _ui.layoutAnimation->setAlignment(_videoWidget, Qt::AlignCenter);
    _ui.layoutPods->addWidget(_leftBattery, 0, Qt::AlignHCenter | Qt::AlignTop);
    _ui.layoutPods->addWidget(_rightBattery, 0, Qt::AlignHCenter | Qt::AlignTop);
    _ui.layoutCase->addWidget(_caseBattery, 0, Qt::AlignHCenter | Qt::AlignTop);
    _ui.layoutClose->addWidget(_closeButton);

    // For getting the correct initial height of `_videoWidget` later
    _ui.layoutAnimation->activate();
    _videoWidget->show();

    if (startUpdateChecker) {
        _updateChecker.Start();
    }
}

MainWindow::~MainWindow()
{
    _deviceQueryThread.request_stop();
    if (_deviceQueryThread.joinable()) {
        _deviceQueryThread.join();
    }
}

void MainWindow::UpdateState(const Core::AirPods::State &state)
{
    LOG(Info, "MainWindow::UpdateState");

    _viewModel.UpdateState(state);
    Repaint();
}

void MainWindow::Available()
{
    LOG(Info, "MainWindow::Available");

    _viewModel.Available();
    Repaint();
}

void MainWindow::Unavailable()
{
    LOG(Info, "MainWindow::Unavailable");

    _viewModel.Unavailable();
    Repaint();
}

void MainWindow::Disconnect()
{
    LOG(Info, "MainWindow::Disconnect");

    _viewModel.Disconnect();
    Repaint();
}

void MainWindow::Bind()
{
    LOG(Info, "MainWindow::Bind");

    _viewModel.Bind();
    Repaint();
}

void MainWindow::Unbind()
{
    LOG(Info, "MainWindow::Unbind");

    _viewModel.Unbind();
    Repaint();
}

void MainWindow::AskUserUpdate(const Core::Update::ReleaseInfo &releaseInfo)
{
    auto releaseVersion = releaseInfo.version.toString();

    QString changeLogBlock;
    if (!releaseInfo.changeLog.isEmpty()) {
        changeLogBlock = QString{"\n\n%1\n%2"}.arg(tr("Change log:")).arg(releaseInfo.changeLog);
    }

    auto action = NewVersionMessageBox(
        nullptr, Config::ProgramName,
        tr("Hey! I found a new version available!\n"
           "\n"
           "Current version: %1\n"
           "Latest version: %2"
           "%3")
            .arg(Core::Update::GetLocalVersion().toString())
            .arg(releaseVersion)
            .arg(changeLogBlock),
        releaseInfo);

    switch (action) {
    case Gui::NewVersionAction::Update:
        LOG(Info, "VersionUpdate: User clicked Update.");

        if (!releaseInfo.CanAutoUpdate()) {
            LOG(Info, "VersionUpdate: Cannot auto update. Popup latest url and quit.");
            releaseInfo.OpenUrl();
        }
        else {
            Gui::DownloadWindow{releaseInfo}.exec();
        }

        Utils::Qt::QuitApplicationSafely();
        return;

    case Gui::NewVersionAction::Skip:
        LOG(Info, "VersionUpdate: User clicked Skip.");

        Core::Settings::ModifiableAccess()->skipped_version = releaseVersion;

        // Continue checking for new versions after the skipped version
        break;

    case Gui::NewVersionAction::Later:
        LOG(Info, "VersionUpdate: User clicked Later.");

        _updateChecker.Stop();
        break;

    default:
        LOG(Warn, "VersionUpdate: Unhandled user clicked button.");
        break;
    }
}

void MainWindow::ChangeButtonAction(ButtonAction action)
{
    switch (action) {
    case ButtonAction::NoButton:
        _ui.pushButton->setText("");
        _ui.pushButton->hide();
        adjustSize();
        return;

    case ButtonAction::Bind:
        _ui.pushButton->setText(tr("Bind to AirPods"));
        break;

    default:
        FatalError(std::format("Unhandled ButtonAction: '{}'", Helper::ToUnderlying(action)), true);
    }

    _buttonAction = action;
    _ui.pushButton->show();
    adjustSize();
}

void MainWindow::SetAnimation(std::optional<Core::AirPods::Model> model)
{
    if (model == _cacheModel) {
        return;
    }

    if (!model.has_value()) {
        StopAnimation();
        _mediaPlayer->setMedia(QMediaContent{});
    }
    else {
        const auto presentation = GetAnimationPresentation(model.value());

        _mediaPlayer->setMedia(QUrl{presentation.resource});

        if (_isVisible) {
            PlayAnimation();
        }
        else {
            StopAnimation();
        }
    }

    _cacheModel = model;
    ResizeAnimationWidget();
}

void MainWindow::ResizeAnimationWidget()
{
    if (!_cacheModel.has_value()) {
        return;
    }

    const auto presentation = GetAnimationPresentation(_cacheModel.value());
    const auto containerSize = _ui.gridLayoutWidget->contentsRect().size();
    if (containerSize.height() <= 0 || presentation.sourceSize.height() <= 0) {
        return;
    }

    const auto aspectRatio = static_cast<double>(presentation.sourceSize.width()) /
        static_cast<double>(presentation.sourceSize.height());
    // The original 320 px pairing card presents the supplied 2:1 AirPods AVI at exactly
    // 240 x 120 logical pixels. This avoids an additional non-integer scaling step at 150% DPI.
    const auto height = qMin(containerSize.height(), 120);
    const auto width = qMin(qMin(containerSize.width(), 240), qRound(height * aspectRatio));
    if (width > 0) {
        _videoWidget->setFixedSize(width, height);
    }
}

void MainWindow::PlayAnimation()
{
    _isAnimationPlaying = true;
    _mediaPlayer->play();
    _videoWidget->show();
}

void MainWindow::StopAnimation()
{
    // The player will go black after stopping
    // I have no idea about this, so let's hide the widget here as a workaround
    _videoWidget->hide();

    _isAnimationPlaying = false;
    _mediaPlayer->stop();
}

void MainWindow::BindDevice()
{
    LOG(Info, "BindDevice");

    if (_deviceQueryRunning.exchange(true)) {
        LOG(Info, "Ignore duplicate device query while one is already running.");
        return;
    }

    if (_deviceQueryThread.joinable()) {
        _deviceQueryThread.join();
    }

    _deviceQueryThread = std::jthread{[this](std::stop_token stopToken) {
        Core::OS::Windows::Winrt::Initialize();
        auto devices = Core::AirPods::GetDevices();
        if (stopToken.stop_requested()) {
            return;
        }

        QMetaObject::invokeMethod(
            this,
            [this, devices = std::move(devices)]() mutable {
                _deviceQueryRunning = false;
                ShowDeviceSelector(std::move(devices));
            },
            Qt::QueuedConnection);
    }};
}

void MainWindow::ShowDeviceSelector(std::vector<Core::Bluetooth::Device> devices)
{
    if (devices.empty()) {
        QMessageBox::warning(
            this, Config::ProgramName,
            QMessageBox::tr("No paired device found.\n"
                            "You need to pair your AirPods in Windows Bluetooth Settings first."));
        return;
    }

    int selectedIndex = 0;

    if (devices.size() > 1) {
        QStringList deviceNames;
        for (const auto &device : devices) {
            auto deviceName = device.GetName();

            LOG(Trace, "Device name: '{}'", deviceName);
            LOG(Trace, "GetProductId: '{}' GetVendorId: '{}'", device.GetProductId(),
                device.GetVendorId());
            deviceNames.append(QString::fromStdString(deviceName));
        }

        SelectWindow selector{tr("Please select your AirPods device below."), deviceNames, this};
        if (selector.exec() == -1) {
            LOG(Warn, "selector.exec() == -1");
            return;
        }

        if (!selector.HasResult()) {
            LOG(Info, "No result for selector.");
            return;
        }

        selectedIndex = selector.GetSeletedIndex();
        APD_ASSERT(selectedIndex >= 0 && selectedIndex < devices.size());
    }

    const auto &selectedDevice = devices.at(selectedIndex);

    LOG(Info, "Selected device index: '{}', device name: '{}'. Bound to this device.",
        selectedIndex, selectedDevice.GetName());

    Core::Settings::ModifiableAccess()->device_address = selectedDevice.GetAddress();
}

void MainWindow::ControlAutoHideTimer(bool start)
{
    LOG(Trace, "ControlAutoHideTimer: start == '{}', _isVisible == '{}'", start, _isVisible);

    if (start && _isVisible) {
        _autoHideTimer->start(10s);
    }
    else {
        _autoHideTimer->stop();
    }
}

void MainWindow::VersionUpdateAvailable(const Core::Update::ReleaseInfo &releaseInfo, bool silent)
{
    LOG(Info, "MainWindow::VersionUpdateAvailable: silent: `{}`", silent);

    if (!silent) {
        AskUserUpdate(releaseInfo);
    }
    else {
        emit SilentUpdateAvailable(releaseInfo);
    }
}

void MainWindow::Repaint()
{
    const auto presentation = _viewModel.Present();
    _ui.deviceLabel->setText(presentation.title);
    FitDeviceLabelFont();
    ChangeButtonAction(presentation.buttonAction);
    SetAnimation(presentation.animationModel);

    const auto applyBattery = [](Widget::Battery *widget, const BatteryPresentation &battery) {
        if (!battery.visible) {
            widget->hide();
            return;
        }

        widget->setCharging(battery.charging);
        widget->setValue(battery.value);
        widget->show();
    };

    applyBattery(_leftBattery, presentation.leftBattery);
    applyBattery(_rightBattery, presentation.rightBattery);
    applyBattery(_caseBattery, presentation.caseBattery);
}

void MainWindow::FitDeviceLabelFont()
{
    const auto fullText = _ui.deviceLabel->text();
    _ui.deviceLabel->setToolTip(fullText);

    auto font = _ui.deviceLabel->font();
    font.setPointSize(_deviceLabelMaximumPointSize);

    const auto availableWidth = _ui.deviceLabel->contentsRect().width();
    while (font.pointSize() > _deviceLabelMinimumPointSize &&
           QFontMetrics{font}.horizontalAdvance(_ui.deviceLabel->text()) > availableWidth)
    {
        font.setPointSize(font.pointSize() - 1);
    }

    _ui.deviceLabel->setFont(font);
    const QFontMetrics metrics{font};
    if (metrics.horizontalAdvance(fullText) > availableWidth) {
        _ui.deviceLabel->setText(metrics.elidedText(fullText, Qt::ElideMiddle, availableWidth));
    }
}

void MainWindow::OnAppStateChanged(Qt::ApplicationState state)
{
    LOG(Trace, "OnAppStateChanged: '{}'", Helper::ToString(state));
    ControlAutoHideTimer(state != Qt::ApplicationActive);
}

void MainWindow::OnPosMoveFinished()
{
    if (!_isVisible) {
        hide();
        StopAnimation();
    }
}

void MainWindow::OnAnimationClicked()
{
#if defined APD_DEBUG
    using namespace Core::AirPods;

    static Model next = Model::AirPods_1;

    _ui.deviceLabel->setText(Helper::ToString(next));
    SetAnimation(next);

    next = static_cast<Model>(Helper::ToUnderlying(next) + 1);
    if (next >= Model::_Max) {
        next = Model::AirPods_1;
    }
#endif
}

void MainWindow::OnButtonClicked()
{
    switch (_buttonAction) {
    case ButtonAction::Bind:
        LOG(Info, "User clicked 'Bind'");
        BindDevice();
        break;

    default:
        FatalError(
            std::format("Unhandled ButtonAction: '{}'", Helper::ToUnderlying(_buttonAction)), true);
    }
}

// for loop play
void MainWindow::OnPlayerStateChanged(QMediaPlayer::State newState)
{
    if (newState == QMediaPlayer::StoppedState && _isAnimationPlaying) {
        _mediaPlayer->play();
    }
}

void MainWindow::DoHide()
{
    LOG(Trace, "MainWindow: Hide");

    if (!_isVisible) {
        return;
    }
    _isVisible = false;

    ControlAutoHideTimer(false);

    const auto screenGeometry = screen()->geometry();

    _posAnimation.stop();
    _posAnimation.setDuration(180);
    _posAnimation.setEasingCurve(QEasingCurve::InCubic);
    _posAnimation.setStartValue(pos());
    _posAnimation.setEndValue(QPoint{x(), screenGeometry.bottom() + 1});
    _posAnimation.start();
}

void MainWindow::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

    LOG(Trace, "MainWindow: Show");

    if (_isVisible) {
        return;
    }
    _isVisible = true;

    PlayAnimation();
    ControlAutoHideTimer(true);

    auto targetScreen = QGuiApplication::screenAt(QCursor::pos());
    if (targetScreen == nullptr) {
        targetScreen = screen();
    }

    const auto availableGeometry = targetScreen->availableGeometry();
    const auto screenGeometry = targetScreen->geometry();
    const auto targetX = availableGeometry.right() - width() + 1 - _screenMargin.width();
    const auto targetY = availableGeometry.bottom() - height() + 1 - _screenMargin.height();

    move(targetX, screenGeometry.bottom() + 1);

    _posAnimation.stop();
    _posAnimation.setDuration(260);
    _posAnimation.setEasingCurve(QEasingCurve::OutCubic);
    _posAnimation.setStartValue(pos());
    _posAnimation.setEndValue(QPoint{targetX, targetY});
    _posAnimation.start();
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    // A bitmap/window-region mask clips on whole physical pixels and leaves stair-stepped
    // corners at common Windows scale factors. A translucent top-level surface lets Qt blend
    // the curved edge in device pixels instead.
    QPainter painter{this};
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    const QRectF surface = QRectF{rect()}.adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen{QColor{60, 60, 67, 24}, 1.0});
    painter.setBrush(palette().color(QPalette::Window));
    painter.drawRoundedRect(surface, _windowCornerRadius, _windowCornerRadius);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    ResizeAnimationWidget();
}
} // namespace Gui

#include "MainWindow.moc"
