#include "PopupControlPanel.h"

#include <algorithm>

#include <QButtonGroup>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>

#if defined APD_OS_WIN
#include "../../Core/GlobalMedia.h"
#endif

namespace Gui {
namespace {
QColor ColorFor(bool dark, const QColor &light, const QColor &darkColor)
{
    return dark ? darkColor : light;
}

bool IsDark(const QWidget *widget)
{
    return qGray(widget->palette().color(QPalette::Window).rgb()) < 128;
}

class AppleVolumeSlider final : public QSlider
{
public:
    explicit AppleVolumeSlider(QWidget *parent = nullptr) : QSlider{Qt::Horizontal, parent}
    {
        setMinimumHeight(32);
        setFocusPolicy(Qt::StrongFocus);
    }

    QSize sizeHint() const override { return {210, 32}; }
    QSize minimumSizeHint() const override { return {120, 32}; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const bool dark = IsDark(this);
        const bool enabled = isEnabled();
        const QColor accent = dark ? QColor{"#0A84FF"} : QColor{"#007AFF"};
        const QColor inactive = ColorFor(dark, QColor{"#D1D1D6"}, QColor{"#636366"});
        const QColor thumb = ColorFor(dark, QColor{"#FFFFFF"}, QColor{"#F2F2F7"});
        const QColor disabled = ColorFor(dark, QColor{"#E5E5EA"}, QColor{"#48484A"});

        constexpr qreal thumbDiameter = 20.0;
        constexpr qreal trackHeight = 5.0;
        const QRectF bounds = rect();
        const qreal left = thumbDiameter / 2.0;
        const qreal span = qMax<qreal>(0.0, bounds.width() - thumbDiameter);
        const qreal fraction = maximum() == minimum()
                                   ? 0.0
                                   : (value() - minimum()) / qreal(maximum() - minimum());
        const qreal visualFraction = invertedAppearance() ? 1.0 - fraction : fraction;
        const qreal thumbX = bounds.left() + left + span * visualFraction;
        const QRectF track{bounds.left() + left, bounds.center().y() - trackHeight / 2.0, span,
                           trackHeight};

        painter.setPen(Qt::NoPen);
        painter.setBrush(enabled ? inactive : disabled);
        painter.drawRoundedRect(track, trackHeight / 2.0, trackHeight / 2.0);

        QRectF progress = track;
        if (invertedAppearance()) {
            progress.setLeft(thumbX);
        }
        else {
            progress.setRight(thumbX);
        }
        painter.setBrush(enabled ? accent : disabled);
        painter.drawRoundedRect(progress, trackHeight / 2.0, trackHeight / 2.0);

        const QPointF center{thumbX, bounds.center().y()};
        if (hasFocus() && enabled) {
            QColor ring = accent;
            ring.setAlpha(70);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen{ring, 5.0});
            painter.drawEllipse(center, thumbDiameter / 2.0 + 3.0, thumbDiameter / 2.0 + 3.0);
        }

        if (enabled) {
            QColor shadow{0, 0, 0, dark ? 105 : 55};
            painter.setPen(Qt::NoPen);
            painter.setBrush(shadow);
            painter.drawEllipse(center + QPointF{0.0, 1.25}, thumbDiameter / 2.0,
                                thumbDiameter / 2.0);
        }
        painter.setBrush(enabled ? thumb : disabled);
        painter.drawEllipse(center, thumbDiameter / 2.0, thumbDiameter / 2.0);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            setSliderDown(true);
            SetValueFromPosition(event->pos().x());
            event->accept();
            return;
        }
        QSlider::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (isSliderDown()) {
            SetValueFromPosition(event->pos().x());
            event->accept();
            return;
        }
        QSlider::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && isSliderDown()) {
            SetValueFromPosition(event->pos().x());
            setSliderDown(false);
            event->accept();
            return;
        }
        QSlider::mouseReleaseEvent(event);
    }

private:
    void SetValueFromPosition(int x)
    {
        constexpr int thumbDiameter = 20;
        const int span = qMax(0, width() - thumbDiameter);
        const int position = qBound(0, x - thumbDiameter / 2, span);
        const int newValue = QStyle::sliderValueFromPosition(minimum(), maximum(), position, span,
                                                              invertedAppearance());
        if (newValue != value()) {
            setValue(newValue);
            Q_EMIT sliderMoved(newValue);
        }
    }
};

class ModeButton final : public QPushButton
{
public:
    explicit ModeButton(const QString &text, QWidget *parent = nullptr) : QPushButton{text, parent}
    {
        setMinimumHeight(44);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);

        const bool dark = IsDark(this);
        const bool enabled = isEnabled();
        const QColor accent = dark ? QColor{"#0A84FF"} : QColor{"#007AFF"};
        const QColor surface = ColorFor(dark, QColor{"#E9E9ED"}, QColor{"#3A3A3C"});
        const QColor disabledSurface = ColorFor(dark, QColor{"#E5E5EA"}, QColor{"#3A3A3C"});
        const QColor textColor = ColorFor(dark, QColor{"#1D1D1F"}, QColor{"#F5F5F7"});
        const QColor disabledText = ColorFor(dark, QColor{"#5F5F63"}, QColor{"#AEAEB2"});

        QColor fill = !enabled ? disabledSurface : (isChecked() ? accent : surface);
        if (enabled && isDown()) {
            fill = fill.darker(dark ? 108 : 104);
        }
        else if (enabled && underMouse() && !isChecked()) {
            fill = fill.lighter(dark ? 110 : 103);
        }

        const QRectF buttonRect = rect().adjusted(0.75, 0.75, -0.75, -0.75);
        constexpr qreal radius = 12.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(buttonRect, radius, radius);

        if (hasFocus() && enabled) {
            QColor focus = accent;
            focus.setAlpha(190);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen{focus, 2.0});
            painter.drawRoundedRect(buttonRect.adjusted(1.0, 1.0, -1.0, -1.0), radius - 1.0,
                                    radius - 1.0);
        }

        QFont textFont = font();
        textFont.setWeight(isChecked() ? QFont::DemiBold : QFont::Medium);
        painter.setFont(textFont);
        painter.setPen(!enabled ? disabledText : (isChecked() ? Qt::white : textColor));
        const QFontMetrics textMetrics{textFont};
        painter.drawText(rect().adjusted(8, 0, -8, 0), Qt::AlignCenter,
                         textMetrics.elidedText(text(), Qt::ElideRight, width() - 16));
    }
};
} // namespace

class SpeakerGlyph final : public QWidget
{
public:
    explicit SpeakerGlyph(QWidget *parent = nullptr) : QWidget{parent}
    {
        setFixedSize(20, 28);
        setAccessibleName(QObject::tr("Speaker"));
    }

    void SetVolume(int volume)
    {
        const int next = qBound(0, volume, 100);
        if (_volume == next) {
            return;
        }
        _volume = next;
        setAccessibleName(_volume == 0 ? QObject::tr("Muted speaker") : QObject::tr("Speaker"));
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);

        const QColor color = !isEnabled()
            ? (IsDark(this) ? QColor{"#AEAEB2"} : QColor{"#6E6E73"})
            : (IsDark(this) ? QColor{"#F5F5F7"} : QColor{"#1D1D1F"});
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

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen{color, 1.55, Qt::SolidLine, Qt::RoundCap});
        if (_volume == 0) {
            painter.drawLine(QPointF{13.5, 10.2}, QPointF{18.2, 17.8});
            painter.drawLine(QPointF{18.2, 10.2}, QPointF{13.5, 17.8});
        }
        else {
            painter.drawArc(QRectF{9.3, 9.0, 8.2, 10.0}, -60 * 16, 120 * 16);
            if (_volume >= 58) {
                painter.drawArc(QRectF{9.2, 6.3, 11.2, 15.4}, -55 * 16, 110 * 16);
            }
        }
    }

private:
    int _volume{50};
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
    _speakerGlyph = new SpeakerGlyph{this};
    volume->addWidget(_speakerGlyph);
    _slider = new AppleVolumeSlider{this};
    _slider->setRange(0, 100);
    _slider->setSingleStep(1);
    _slider->setPageStep(10);
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
        auto *button = new ModeButton{ModeName(mode), this};
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
        _speakerGlyph->SetVolume(value);
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
    const bool dark = qGray(palette().color(QPalette::Window).rgb()) < 128;
    const QString bg = dark ? "#2c2c2e" : "#f2f2f7";
    const QString fg = dark ? "#f5f5f7" : "#1d1d1f";
    const QString secondary = dark ? "#aeaeb2" : "#6e6e73";

    setStyleSheet(
        QStringLiteral(
            "QFrame#popupControlPanel { background: %1; border: none; border-radius: 20px; }"
            "QLabel { color: %2; font-size: 13px; }"
            "QLabel#sectionLabel { color: %2; font-size: 13px; font-weight: 600; }"
            "QLabel#unavailableStatus { color: %3; font-size: 11px; }"
            "QSlider { background: transparent; border: none; }")
            .arg(bg, fg, secondary));
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
    _speakerGlyph->setEnabled(true);
    _slider->setValue(qBound(0, percent, 100));
    _volumeValue->setText(tr("%1%").arg(_slider->value()));
    _speakerGlyph->SetVolume(_slider->value());
    _slider->setAccessibleDescription(
        tr("Windows-uitvoervolume, %1 procent").arg(_slider->value()));
    _slider->setToolTip(tr("Windows-uitvoervolume"));
}

void PopupControlPanel::SetVolumeUnavailable()
{
    _slider->setEnabled(false);
    _speakerGlyph->setEnabled(false);
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
        button->setCursor(available ? Qt::PointingHandCursor : Qt::ArrowCursor);
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
