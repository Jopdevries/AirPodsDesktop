#include "PopupControlPanel.h"

#include <algorithm>

#include <QButtonGroup>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
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

QString ModeButtonText(PopupControlPanel::NoiseControlMode mode)
{
    return ModeName(mode);
}

void DrawNoiseGlyph(QPainter &painter, PopupControlPanel::NoiseControlMode mode,
                    const QRectF &bounds, const QColor &color)
{
    // These deliberately use a single, rounded vector vocabulary rather than copied SF artwork.
    const QPointF center = bounds.center();
    const qreal unit = qMin(bounds.width(), bounds.height()) / 24.0;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen{color, 1.75 * unit, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin});

    switch (mode) {
    case PopupControlPanel::NoiseControlMode::Off: {
        painter.drawEllipse(QRectF{center.x() - 5.1 * unit, center.y() - 6.2 * unit,
                                  8.2 * unit, 12.4 * unit});
        painter.drawArc(QRectF{center.x() - 2.5 * unit, center.y() - 2.9 * unit,
                               5.6 * unit, 7.1 * unit}, -94 * 16, 210 * 16);
        painter.drawLine(center + QPointF{-8.2 * unit, -8.2 * unit},
                         center + QPointF{8.2 * unit, 8.2 * unit});
        break;
    }
    case PopupControlPanel::NoiseControlMode::Transparency: {
        painter.drawEllipse(QRectF{center.x() - 4.7 * unit, center.y() - 5.7 * unit,
                                  7.5 * unit, 11.4 * unit});
        painter.drawArc(QRectF{center.x() - 7.4 * unit, center.y() - 7.8 * unit,
                               13.7 * unit, 15.5 * unit}, -52 * 16, 104 * 16);
        painter.drawArc(QRectF{center.x() - 10.0 * unit, center.y() - 10.5 * unit,
                               18.9 * unit, 21.0 * unit}, -45 * 16, 90 * 16);
        break;
    }
    case PopupControlPanel::NoiseControlMode::Adaptive: {
        painter.drawEllipse(QRectF{center.x() - 4.2 * unit, center.y() - 5.3 * unit,
                                  7.0 * unit, 10.6 * unit});
        painter.drawArc(QRectF{center.x() - 8.0 * unit, center.y() - 8.8 * unit,
                               15.3 * unit, 17.6 * unit}, -56 * 16, 112 * 16);
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        QPainterPath sparkle;
        sparkle.moveTo(center + QPointF{6.8 * unit, -8.4 * unit});
        sparkle.lineTo(center + QPointF{8.0 * unit, -4.3 * unit});
        sparkle.lineTo(center + QPointF{11.5 * unit, -3.1 * unit});
        sparkle.lineTo(center + QPointF{8.0 * unit, -1.9 * unit});
        sparkle.lineTo(center + QPointF{6.8 * unit, 2.0 * unit});
        sparkle.lineTo(center + QPointF{5.6 * unit, -1.9 * unit});
        sparkle.lineTo(center + QPointF{2.3 * unit, -3.1 * unit});
        sparkle.lineTo(center + QPointF{5.6 * unit, -4.3 * unit});
        sparkle.closeSubpath();
        painter.fillPath(sparkle, color);
        break;
    }
    case PopupControlPanel::NoiseControlMode::ANC: {
        painter.drawEllipse(QRectF{center.x() - 3.7 * unit, center.y() - 5.4 * unit,
                                  6.4 * unit, 10.8 * unit});
        painter.drawArc(QRectF{center.x() - 7.6 * unit, center.y() - 8.7 * unit,
                               14.4 * unit, 17.5 * unit}, 112 * 16, 136 * 16);
        painter.drawArc(QRectF{center.x() - 7.6 * unit, center.y() - 8.7 * unit,
                               14.4 * unit, 17.5 * unit}, -68 * 16, 136 * 16);
        break;
    }
    }
    painter.restore();
}

class AppleVerticalVolumeSlider final : public QSlider
{
public:
    explicit AppleVerticalVolumeSlider(QWidget *parent = nullptr) : QSlider{Qt::Vertical, parent}
    {
        setFixedSize(76, 128);
        setFocusPolicy(Qt::StrongFocus);
        setInvertedAppearance(true);
    }

    QSize sizeHint() const override { return {76, 128}; }
    QSize minimumSizeHint() const override { return {76, 112}; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const bool dark = IsDark(this);
        const QRectF track{8.0, 2.0, width() - 16.0, height() - 4.0};
        const qreal fraction = maximum() == minimum()
            ? 0.0
            : (value() - minimum()) / qreal(maximum() - minimum());
        const qreal fillTop = track.bottom() - track.height() * fraction;
        QPainterPath clipping;
        clipping.addRoundedRect(track, track.width() / 2.0, track.width() / 2.0);

        if (hasFocus() && isEnabled()) {
            QColor focus = dark ? QColor{"#0A84FF"} : QColor{"#007AFF"};
            focus.setAlpha(105);
            painter.setPen(QPen{focus, 3.0});
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(track.adjusted(-3.0, -3.0, 3.0, 3.0),
                                    track.width() / 2.0 + 3.0, track.width() / 2.0 + 3.0);
        }

        painter.save();
        painter.setClipPath(clipping);
        QLinearGradient upper{track.topLeft(), track.bottomRight()};
        upper.setColorAt(0.0, ColorFor(dark, QColor{"#7891A5"}, QColor{"#555B63"}));
        upper.setColorAt(1.0, ColorFor(dark, QColor{"#4D6172"}, QColor{"#32363C"}));
        painter.fillRect(track, upper);

        if (isEnabled()) {
            QLinearGradient fill{QPointF{track.center().x(), fillTop}, track.bottomLeft()};
            fill.setColorAt(0.0, ColorFor(dark, QColor{"#FFFFFF"}, QColor{"#F5F5F7"}));
            fill.setColorAt(1.0, ColorFor(dark, QColor{"#F4FAFD"}, QColor{"#E5E5EA"}));
            painter.fillRect(QRectF{track.left(), fillTop, track.width(), track.bottom() - fillTop},
                             fill);
            if (fraction > 0.0 && fraction < 1.0) {
                painter.setPen(QPen{QColor{255, 255, 255, 115}, 1.0});
                painter.drawLine(QPointF{track.left(), fillTop}, QPointF{track.right(), fillTop});
            }
        }
        else {
            painter.fillRect(track, ColorFor(dark, QColor{"#C7C7CC"}, QColor{"#48484A"}));
        }
        painter.restore();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            setSliderDown(true);
            SetValueFromPosition(event->pos().y());
            event->accept();
            return;
        }
        QSlider::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (isSliderDown()) {
            SetValueFromPosition(event->pos().y());
            event->accept();
            return;
        }
        QSlider::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && isSliderDown()) {
            SetValueFromPosition(event->pos().y());
            setSliderDown(false);
            event->accept();
            return;
        }
        QSlider::mouseReleaseEvent(event);
    }

private:
    void SetValueFromPosition(int y)
    {
        constexpr int inset = 2;
        const int span = qMax(1, height() - inset * 2);
        const int position = qBound(0, y - inset, span);
        const int newValue = QStyle::sliderValueFromPosition(minimum(), maximum(), position, span,
                                                              true);
        if (newValue != value()) {
            setValue(newValue);
            Q_EMIT sliderMoved(newValue);
        }
    }
};

class NoiseControlSurface final : public QWidget
{
public:
    explicit NoiseControlSurface(QWidget *parent = nullptr) : QWidget{parent}
    {
        setMinimumHeight(82);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = IsDark(this);
        const QRectF surface = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        QLinearGradient material{surface.topLeft(), surface.bottomRight()};
        material.setColorAt(0.0, ColorFor(dark, QColor{"#DCE5EB"}, QColor{"#3A3A3C"}));
        material.setColorAt(1.0, ColorFor(dark, QColor{"#C8D3DC"}, QColor{"#29292B"}));
        painter.setPen(Qt::NoPen);
        painter.setBrush(material);
        painter.drawRoundedRect(surface, 18.0, 18.0);

        QColor highlight{255, 255, 255, dark ? 24 : 150};
        painter.setPen(QPen{highlight, 1.0});
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(surface.adjusted(0.5, 0.5, -0.5, -0.5), 17.5, 17.5);
    }
};

class NoiseControlButton final : public QPushButton
{
public:
    NoiseControlButton(PopupControlPanel::NoiseControlMode mode, QWidget *parent = nullptr)
        : QPushButton{parent}, _mode{mode}
    {
        setMinimumSize(60, 82);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        const bool dark = IsDark(this);
        const bool enabled = isEnabled();
        const QColor accent = dark ? QColor{"#0A84FF"} : QColor{"#007AFF"};
        const QColor primary = ColorFor(dark, QColor{"#1D1D1F"}, QColor{"#F5F5F7"});
        const QColor unavailable = ColorFor(dark, QColor{"#707178"}, QColor{"#8E8E93"});
        const QRectF iconCircle{width() / 2.0 - 22.0, 5.0, 44.0, 44.0};

        if (isChecked()) {
            QColor selected = accent;
            if (isDown()) {
                selected = selected.darker(112);
            }
            painter.setPen(Qt::NoPen);
            painter.setBrush(selected);
            painter.drawEllipse(iconCircle);
        }
        else if (underMouse() && enabled) {
            QColor hover = primary;
            hover.setAlpha(dark ? 36 : 22);
            painter.setPen(Qt::NoPen);
            painter.setBrush(hover);
            painter.drawEllipse(iconCircle);
        }

        if (hasFocus() && enabled) {
            QColor focus = accent;
            focus.setAlpha(190);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen{focus, 2.0});
            painter.drawEllipse(iconCircle.adjusted(-2.0, -2.0, 2.0, 2.0));
        }

        const QColor content = !enabled ? unavailable : (isChecked() ? Qt::white : primary);
        DrawNoiseGlyph(painter, _mode, iconCircle.adjusted(10.0, 10.0, -10.0, -10.0), content);

        QFont labelFont = font();
        labelFont.setPointSizeF(9.0);
        labelFont.setWeight(isChecked() ? QFont::DemiBold : QFont::Medium);
        const auto label = ModeButtonText(_mode);
        const QRect textRect{2, 52, width() - 4, height() - 52};
        while (labelFont.pointSizeF() > 7.5 &&
               QFontMetrics{labelFont}.horizontalAdvance(label) > textRect.width())
        {
            labelFont.setPointSizeF(labelFont.pointSizeF() - 0.5);
        }
        painter.setFont(labelFont);
        painter.setPen(content);
        painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                         label);
    }

private:
    PopupControlPanel::NoiseControlMode _mode;
};
} // namespace

class SpeakerGlyph final : public QWidget
{
public:
    explicit SpeakerGlyph(QWidget *parent = nullptr) : QWidget{parent}
    {
        setFixedSize(24, 24);
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
            ? (IsDark(this) ? QColor{"#8E8E93"} : QColor{"#6E6E73"})
            : (IsDark(this) ? QColor{"#F5F5F7"} : QColor{"#1D1D1F"});
        QPainterPath speaker;
        speaker.moveTo(2.5, 9.0);
        speaker.lineTo(6.8, 9.0);
        speaker.lineTo(12.0, 5.0);
        speaker.lineTo(12.0, 19.0);
        speaker.lineTo(6.8, 15.0);
        speaker.lineTo(2.5, 15.0);
        speaker.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.fillPath(speaker, color);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen{color, 1.55, Qt::SolidLine, Qt::RoundCap});
        if (_volume == 0) {
            painter.drawLine(QPointF{14.7, 8.0}, QPointF{21.0, 16.0});
            painter.drawLine(QPointF{21.0, 8.0}, QPointF{14.7, 16.0});
        }
        else {
            painter.drawArc(QRectF{10.5, 7.0, 8.2, 10.0}, -60 * 16, 120 * 16);
            if (_volume >= 58) {
                painter.drawArc(QRectF{10.0, 4.4, 11.7, 15.2}, -55 * 16, 110 * 16);
            }
        }
    }

private:
    int _volume{50};
};

int PopupControlPanel::PercentFromVolume(float value)
{
    return qBound(0, qRound(std::clamp(value, 0.f, 1.f) * 100.f), 100);
}

PopupControlPanel::PopupControlPanel(QWidget *parent) : QFrame{parent}
{
    setObjectName("popupControlPanel");
    setAccessibleName(tr("AirPods controls"));
    setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    setFrameShape(QFrame::NoFrame);

    auto *root = new QVBoxLayout{this};
    root->setContentsMargins(14, 13, 14, 13);
    root->setSpacing(6);

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

    auto *volumeStage = new QVBoxLayout{};
    volumeStage->setContentsMargins(0, 0, 0, 0);
    volumeStage->setSpacing(1);
    _slider = new AppleVerticalVolumeSlider{this};
    _slider->setRange(0, 100);
    _slider->setSingleStep(1);
    _slider->setPageStep(10);
    _slider->setAccessibleName(tr("Geluidsniveau"));
    _slider->setAccessibleDescription(tr("Windows-uitvoervolume"));
    _slider->setToolTip(tr("Windows-uitvoervolume"));
    volumeStage->addWidget(_slider, 0, Qt::AlignHCenter);
    _speakerGlyph = new SpeakerGlyph{this};
    volumeStage->addWidget(_speakerGlyph, 0, Qt::AlignHCenter);
    root->addLayout(volumeStage);

    auto *modeLabel = new QLabel{tr("Luistermodus"), this};
    modeLabel->setObjectName("sectionLabel");
    root->addWidget(modeLabel);

    auto *modeSurface = new NoiseControlSurface{this};
    auto *modes = new QHBoxLayout{modeSurface};
    modes->setContentsMargins(5, 0, 5, 0);
    modes->setSpacing(0);
    _modeGroup = new QButtonGroup{this};
    _modeGroup->setExclusive(true);

    constexpr NoiseControlMode visualModeOrder[] = {
        NoiseControlMode::Off,
        NoiseControlMode::Transparency,
        NoiseControlMode::Adaptive,
        NoiseControlMode::ANC,
    };
    // Keep construction in enum order so programmatic clients retain their original ordering;
    // the layout itself follows Apple's Off, Transparency, Adaptive, Noise Cancellation order.
    for (int i = 0; i < 4; ++i) {
        const auto mode = static_cast<NoiseControlMode>(i);
        auto *button = new NoiseControlButton{mode, modeSurface};
        button->setCheckable(true);
        button->setEnabled(false);
        button->setAccessibleName(tr("Luistermodus: %1").arg(ModeName(mode)));
        button->setAccessibleDescription(tr("Niet beschikbaar"));
        button->setToolTip(tr("AirPods-bediening niet beschikbaar"));
        _modeGroup->addButton(button);
        connect(button, &QPushButton::clicked, this, [this, mode] {
            Q_EMIT NoiseControlRequested(mode);
        });
        _modeButtons.push_back(button);
        _buttonModes.push_back(mode);
    }
    for (const auto mode : visualModeOrder) {
        modes->addWidget(_modeButtons[static_cast<int>(mode)], 1);
    }
    root->addWidget(modeSurface);

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

void PopupControlPanel::paintEvent(QPaintEvent *)
{
    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing);
    const bool dark = IsDark(this);
    const QRectF surface = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    QLinearGradient material{surface.topLeft(), surface.bottomRight()};
    material.setColorAt(0.0, ColorFor(dark, QColor{"#F8F9FC"}, QColor{"#38383A"}));
    material.setColorAt(1.0, ColorFor(dark, QColor{"#E9EDF4"}, QColor{"#242426"}));
    painter.setPen(Qt::NoPen);
    painter.setBrush(material);
    painter.drawRoundedRect(surface, 22.0, 22.0);

    QColor highlight{255, 255, 255, dark ? 18 : 185};
    painter.setPen(QPen{highlight, 1.0});
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(surface.adjusted(0.5, 0.5, -0.5, -0.5), 21.5, 21.5);
}

void PopupControlPanel::ApplyStyle()
{
    const bool dark = IsDark(this);
    const QString fg = dark ? "#F5F5F7" : "#1D1D1F";
    const QString secondary = dark ? "#AEAEB2" : "#5F6470";
    setStyleSheet(
        QStringLiteral("QFrame#popupControlPanel { background: transparent; border: none; }"
                       "QLabel { color: %1; font-size: 13px; background: transparent; }"
                       "QLabel#sectionLabel { color: %1; font-size: 13px; font-weight: 600; }"
                       "QLabel#volumeValue { color: %2; font-size: 13px; font-weight: 600; }"
                       "QLabel#unavailableStatus { color: %2; font-size: 11px; }"
                       "QSlider, QPushButton { background: transparent; border: none; }")
            .arg(fg, secondary));
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
        const bool isSelected = hasSelection && _buttonModes[i] == *mode;
        button->setEnabled(available);
        button->setCursor(available ? Qt::PointingHandCursor : Qt::ArrowCursor);
        button->setChecked(isSelected);
        button->setAccessibleDescription(
            available ? (isSelected ? tr("Geselecteerd") : tr("Niet geselecteerd"))
                      : tr("Niet beschikbaar"));
        button->setToolTip(available ? ModeName(_buttonModes[i])
                                     : tr("AirPods-bediening niet beschikbaar"));
    }
    _modeGroup->setExclusive(true);
    _unavailableStatus->setVisible(!available);
}
} // namespace Gui
