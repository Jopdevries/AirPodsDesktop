#include "PopupControlPanel.h"

#include <algorithm>

#include <QButtonGroup>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>

#if defined APD_OS_WIN
#include "../../Core/GlobalMedia.h"
#endif

namespace Gui {
namespace {
class SpeakerGlyph final : public QWidget
{
public:
    explicit SpeakerGlyph(QWidget *parent = nullptr) : QWidget{parent}
    {
        setFixedSize(20, 28);
        setAccessibleName(tr("Speaker"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);

        const auto color = palette().color(QPalette::WindowText);
        QPainterPath speaker;
        speaker.moveTo(2.5, 11.0);
        speaker.lineTo(6.5, 11.0);
        speaker.lineTo(11.0, 7.0);
        speaker.lineTo(11.0, 21.0);
        speaker.lineTo(6.5, 17.0);
        speaker.lineTo(2.5, 17.0);
        speaker.closeSubpath();

        painter.setPen(Qt::NoPen);
        painter.fillPath(speaker, color);
        painter.setPen(QPen{color, 1.5, Qt::SolidLine, Qt::RoundCap});
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(QRectF{9.5, 9.0, 8.0, 10.0}, -60 * 16, 120 * 16);
    }
};

QString ModeName(PopupControlPanel::NoiseControlMode mode)
{
    switch (mode) {
    case PopupControlPanel::NoiseControlMode::ANC:
        return PopupControlPanel::tr("ANC");
    case PopupControlPanel::NoiseControlMode::Transparency:
        return PopupControlPanel::tr("Transparency");
    case PopupControlPanel::NoiseControlMode::Adaptive:
        return PopupControlPanel::tr("Adaptive");
    case PopupControlPanel::NoiseControlMode::Off:
        return PopupControlPanel::tr("Off");
    }
    return {};
}
}

int PopupControlPanel::PercentFromVolume(float value)
{
    return qBound(0, qRound(std::clamp(value, 0.f, 1.f) * 100.f), 100);
}

PopupControlPanel::PopupControlPanel(QWidget *parent) : QFrame{parent}
{
    setObjectName("popupControlPanel");
    setAccessibleName(tr("AirPods controls"));
    setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));

    auto *root = new QVBoxLayout{this};
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(8);

    auto *volumeHeader = new QHBoxLayout{};
    auto *volumeLabel = new QLabel{tr("Geluidsniveau"), this};
    volumeLabel->setObjectName("sectionLabel");
    _volumeValue = new QLabel{this};
    _volumeValue->setObjectName("volumeValue");
    _volumeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    _volumeValue->setMinimumWidth(38);
    volumeHeader->addWidget(volumeLabel);
    volumeHeader->addStretch();
    volumeHeader->addWidget(_volumeValue);
    root->addLayout(volumeHeader);

    auto *volume = new QHBoxLayout{};
    volume->setSpacing(10);
    volume->addWidget(new SpeakerGlyph{this});
    _slider = new QSlider{Qt::Horizontal, this};
    _slider->setRange(0, 100);
    _slider->setSingleStep(1);
    _slider->setPageStep(10);
    _slider->setMinimumHeight(28);
    _slider->setFocusPolicy(Qt::StrongFocus);
    _slider->setAccessibleName(tr("Geluidsniveau"));
    _slider->setAccessibleDescription(tr("Windows-uitvoervolume"));
    _slider->setToolTip(tr("Windows-uitvoervolume"));
    volume->addWidget(_slider, 1);
    root->addLayout(volume);

    auto *modeLabel = new QLabel{tr("Luistermodus"), this};
    modeLabel->setObjectName("sectionLabel");
    root->addWidget(modeLabel);

    auto *grid = new QGridLayout{};
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);
    _modeGroup = new QButtonGroup{this};
    _modeGroup->setExclusive(true);

    for (int i = 0; i < 4; ++i) {
        const auto mode = static_cast<NoiseControlMode>(i);
        auto *button = new QPushButton{ModeName(mode), this};
        button->setMinimumHeight(44);
        button->setCheckable(true);
        button->setEnabled(false);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setAccessibleName(tr("Luistermodus: %1").arg(ModeName(mode)));
        button->setAccessibleDescription(tr("Niet beschikbaar"));
        button->setToolTip(tr("AirPods-bediening niet beschikbaar"));
        _modeGroup->addButton(button, i);
        connect(button, &QPushButton::clicked, this, [this, mode] {
            Q_EMIT NoiseControlRequested(mode);
        });
        grid->addWidget(button, i / 2, i % 2);
        _modeButtons.push_back(button);
    }
    root->addLayout(grid);

    _unavailableStatus = new QLabel{tr("AirPods-bediening niet beschikbaar"), this};
    _unavailableStatus->setObjectName("unavailableStatus");
    _unavailableStatus->setAccessibleName(tr("AirPods-bediening niet beschikbaar"));
    _unavailableStatus->setWordWrap(true);
    root->addWidget(_unavailableStatus);

    SetPreviewVolume(50);
    connect(_slider, &QSlider::valueChanged, this, [this](int value) {
        _volumeValue->setText(tr("%1%").arg(value));
        _slider->setAccessibleDescription(
            tr("Windows-uitvoervolume, %1 procent").arg(value));
#if defined APD_OS_WIN
        if (!Core::GlobalMedia::SetOutputVolume(value / 100.f)) {
            SetVolumeUnavailable();
        }
#endif
    });

#if defined APD_OS_WIN
    RefreshVolume();
    _volumePoller = new QTimer{this};
    _volumePoller->setInterval(1000);
    connect(_volumePoller, &QTimer::timeout, this, &PopupControlPanel::RefreshVolume);
    _volumePoller->start();
#endif

    SetNoiseControlState(std::nullopt, false);
    ApplyStyle();
}

void PopupControlPanel::ApplyStyle()
{
    const bool dark = qGray(palette().color(QPalette::Window)) < 128;
    const QString bg = dark ? "rgba(44,44,46,235)" : "rgba(246,246,248,235)";
    const QString fg = dark ? "#f5f5f7" : "#1d1d1f";
    const QString secondary = dark ? "#aeaeb2" : "#6e6e73";
    const QString inactiveTrack = dark ? "#636366" : "#d1d1d6";
    const QString thumb = dark ? "#f2f2f7" : "#ffffff";

    setStyleSheet(
        QStringLiteral(
            "QFrame#popupControlPanel { background: %1; border: 1px solid "
            "rgba(128,128,128,70); border-radius: 18px; }"
            "QLabel { color: %2; font-size: 13px; }"
            "QLabel#sectionLabel { color: %2; font-size: 13px; font-weight: 600; }"
            "QLabel#unavailableStatus { color: %3; font-size: 11px; }"
            "QSlider::groove:horizontal { height: 4px; border-radius: 2px; background: %4; }"
            "QSlider::sub-page:horizontal { background: #007aff; border-radius: 2px; }"
            "QSlider::handle:horizontal { width: 20px; margin: -8px 0; border-radius: 10px; "
            "background: %5; border: 1px solid rgba(60,60,67,80); }"
            "QSlider:focus::handle:horizontal { border: 2px solid #007aff; }"
            "QPushButton { min-height: 44px; border: 1px solid %4; border-radius: 11px; "
            "background: transparent; color: %2; font-size: 12px; padding: 0 8px; }"
            "QPushButton:hover:enabled { background: rgba(120,120,128,28); }"
            "QPushButton:focus { border: 2px solid #007aff; }"
            "QPushButton:checked { color: white; background: #007aff; border-color: #007aff; "
            "font-weight: 600; }"
            "QPushButton:disabled { color: %3; background: transparent; }")
            .arg(bg, fg, secondary, inactiveTrack, thumb));
}

void PopupControlPanel::SetPreviewVolume(int percent)
{
    if (!_slider) {
        return;
    }

    if (_volumePoller) {
        _volumePoller->stop();
    }
    SetVolumePercent(percent);
}

void PopupControlPanel::SetVolumePercent(int percent)
{
    const QSignalBlocker blocker{_slider};
    _slider->setEnabled(true);
    _slider->setValue(qBound(0, percent, 100));
    _volumeValue->setText(tr("%1%").arg(_slider->value()));
    _slider->setAccessibleDescription(
        tr("Windows-uitvoervolume, %1 procent").arg(_slider->value()));
    _slider->setToolTip(tr("Windows-uitvoervolume"));
}

void PopupControlPanel::SetVolumeUnavailable()
{
    _slider->setEnabled(false);
    _volumeValue->setText(QStringLiteral("\u2014"));
    _slider->setAccessibleDescription(tr("Geluidsniveau niet beschikbaar"));
    _slider->setToolTip(tr("Geluidsniveau niet beschikbaar"));
}

void PopupControlPanel::RefreshVolume()
{
#if defined APD_OS_WIN
    if (_slider->isSliderDown()) {
        return;
    }

    if (auto v = Core::GlobalMedia::GetOutputVolume(); v.has_value()) {
        SetVolumePercent(PercentFromVolume(*v));
    }
    else {
        SetVolumeUnavailable();
    }
#endif
}

void PopupControlPanel::SetNoiseControlState(std::optional<NoiseControlMode> mode, bool available)
{
    const bool hasSelection = available && mode.has_value();
    if (!hasSelection) {
        _modeGroup->setExclusive(false);
    }

    for (int i = 0; i < _modeButtons.size(); ++i) {
        auto *button = _modeButtons[i];
        const bool isSelected = hasSelection && static_cast<int>(*mode) == i;
        button->setEnabled(available);
        button->setChecked(isSelected);
        button->setAccessibleDescription(
            available ? (isSelected ? tr("Geselecteerd") : tr("Niet geselecteerd"))
                      : tr("Niet beschikbaar"));
        button->setToolTip(
            available ? ModeName(static_cast<NoiseControlMode>(i))
                      : tr("AirPods-bediening niet beschikbaar"));
    }
    _modeGroup->setExclusive(true);
    _unavailableStatus->setVisible(!available);
}
} // namespace Gui
