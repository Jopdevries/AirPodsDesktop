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
#include <QPushButton>
#include <QRadialGradient>
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
QString ModeName(PopupControlPanel::NoiseControlMode mode)
{
    switch (mode) {
    case PopupControlPanel::NoiseControlMode::ANC:
        return PopupControlPanel::tr("Noise Cancellation");
    case PopupControlPanel::NoiseControlMode::Transparency:
        return PopupControlPanel::tr("Transparency");
    case PopupControlPanel::NoiseControlMode::Adaptive:
        return PopupControlPanel::tr("Adaptive");
    case PopupControlPanel::NoiseControlMode::Off:
        return PopupControlPanel::tr("Off");
    }
    return {};
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

class AppleHorizontalVolumeSlider final : public QSlider
{
public:
    explicit AppleHorizontalVolumeSlider(QWidget *parent = nullptr)
        : QSlider{Qt::Horizontal, parent}
    {
        setMinimumHeight(28);
        setFocusPolicy(Qt::StrongFocus);
    }

    QSize sizeHint() const override { return {176, 28}; }
    QSize minimumSizeHint() const override { return {120, 28}; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        // Tahoe's compact Sound control is deliberately a slender 4 pt rail with a
        // 20 pt thumb.  The widget stays 28 pt tall, leaving a comfortable hit target.
        constexpr qreal thumbDiameter = 20.0;
        constexpr qreal trackHeight = 4.0;
        const QRectF bounds = rect();
        const qreal left = bounds.left() + thumbDiameter / 2.0;
        const qreal span = qMax<qreal>(0.0, bounds.width() - thumbDiameter);
        const qreal fraction = maximum() == minimum()
            ? 0.0
            : (value() - minimum()) / qreal(maximum() - minimum());
        const qreal thumbX = left + span * fraction;
        const QRectF track{left, bounds.center().y() - trackHeight / 2.0, span, trackHeight};

        if (hasFocus() && isEnabled()) {
            QColor focus{255, 255, 255, 190};
            painter.setPen(QPen{focus, 3.0, Qt::SolidLine, Qt::RoundCap});
            painter.setBrush(Qt::NoBrush);
            painter.drawLine(QPointF{track.left(), track.center().y()},
                             QPointF{track.right(), track.center().y()});
        }

        const QColor inactive{255, 255, 255, 104};
        const QColor active{255, 255, 255, 225};
        painter.setPen(Qt::NoPen);
        painter.setBrush(isEnabled() ? inactive : QColor{255, 255, 255, 52});
        painter.drawRoundedRect(track, trackHeight / 2.0, trackHeight / 2.0);

        QRectF filled = track;
        filled.setRight(thumbX);
        painter.setBrush(isEnabled() ? active : QColor{255, 255, 255, 76});
        painter.drawRoundedRect(filled, trackHeight / 2.0, trackHeight / 2.0);

        const QPointF thumb{thumbX, bounds.center().y()};
        if (isEnabled()) {
            painter.setPen(Qt::NoPen);
            // A one-point, low-opacity shadow separates the white thumb from the
            // glass without turning it into a separate control card.
            painter.setBrush(QColor{0, 0, 0, 42});
            painter.drawEllipse(thumb + QPointF{0.0, 1.0}, thumbDiameter / 2.0,
                                thumbDiameter / 2.0);
        }
        painter.setBrush(isEnabled() ? Qt::white : QColor{255, 255, 255, 125});
        painter.drawEllipse(thumb, thumbDiameter / 2.0, thumbDiameter / 2.0);
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
        const int span = qMax(1, width() - thumbDiameter);
        const int position = qBound(0, x - thumbDiameter / 2, span);
        const int newValue = QStyle::sliderValueFromPosition(minimum(), maximum(), position, span,
                                                              invertedAppearance());
        if (newValue != value()) {
            setValue(newValue);
            Q_EMIT sliderMoved(newValue);
        }
    }
};

class AirPodsGlyph final : public QWidget
{
public:
    explicit AirPodsGlyph(QWidget *parent = nullptr) : QWidget{parent}
    {
        setFixedSize(24, 24);
        setAccessibleName(QObject::tr("AirPods"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor color{255, 255, 255, 235};
        painter.setPen(QPen{color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin});
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QRectF{3.0, 3.0, 7.5, 9.5});
        painter.drawEllipse(QRectF{13.5, 3.0, 7.5, 9.5});
        painter.drawLine(QPointF{5.0, 11.0}, QPointF{5.0, 19.0});
        painter.drawLine(QPointF{8.5, 11.0}, QPointF{8.5, 20.5});
        painter.drawLine(QPointF{15.5, 11.0}, QPointF{15.5, 20.5});
        painter.drawLine(QPointF{19.0, 11.0}, QPointF{19.0, 19.0});
    }
};

class NoiseControlButton final : public QPushButton
{
public:
    NoiseControlButton(PopupControlPanel::NoiseControlMode mode, QWidget *parent = nullptr)
        : QPushButton{parent}, _mode{mode}
    {
        setText(ModeName(mode));
        setMinimumHeight(30);
        setMinimumWidth(160);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        const bool enabled = isEnabled();
        const QColor primary{255, 255, 255, 245};
        const QColor unavailable{255, 255, 255, 132};
        const QColor accent{255, 255, 255, 210};
        const QRectF glyphBounds{32.0, 4.0, 22.0, 22.0};

        if (isChecked()) {
            painter.setPen(QPen{QColor{177, 245, 255, 92}, 1.0});
            painter.setBrush(QColor{13, 177, 202, 34});
            painter.drawRoundedRect(QRectF{1.0, 1.0, width() - 2.0, height() - 2.0}, 6.0, 6.0);
        }
        else if (underMouse() && enabled) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor{19, 171, 196, 20});
            painter.drawRoundedRect(QRectF{1.0, 1.0, width() - 2.0, height() - 2.0}, 6.0, 6.0);
        }

        if (hasFocus() && enabled) {
            QColor focus = accent;
            focus.setAlpha(190);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen{focus, 2.0});
            painter.drawRoundedRect(QRectF{1.0, 1.0, width() - 2.0, height() - 2.0}, 5.0, 5.0);
        }

        const QColor content = !enabled ? unavailable : primary;
        if (isChecked()) {
            painter.setPen(QPen{content, 2.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin});
            painter.drawLine(QPointF{8.0, height() / 2.0}, QPointF{12.0, height() / 2.0 + 4.0});
            painter.drawLine(QPointF{12.0, height() / 2.0 + 4.0}, QPointF{20.0, height() / 2.0 - 5.0});
        }
        DrawNoiseGlyph(painter, _mode, glyphBounds, content);

        QFont labelFont = font();
        labelFont.setPointSizeF(12.0);
        labelFont.setWeight(isChecked() ? QFont::DemiBold : QFont::Medium);
        const auto label = ModeName(_mode);
        const QRect textRect{64, 0, width() - 68, height()};
        while (labelFont.pointSizeF() > 10.0 &&
               QFontMetrics{labelFont}.horizontalAdvance(label) > textRect.width())
        {
            labelFont.setPointSizeF(labelFont.pointSizeF() - 0.5);
        }
        painter.setFont(labelFont);
        painter.setPen(content);
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, label);
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
        const QColor color = isEnabled() ? QColor{255, 255, 255, 235}
                                         : QColor{255, 255, 255, 125};
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
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(7);

    auto *volumeHeader = new QHBoxLayout{};
    auto *volumeLabel = new QLabel{tr("Sound"), this};
    volumeLabel->setObjectName("sectionLabel");
    _volumeValue = new QLabel{this};
    _volumeValue->setObjectName("volumeValue");
    _volumeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    _volumeValue->setMinimumWidth(38);
    volumeHeader->addWidget(volumeLabel);
    volumeHeader->addStretch();
    volumeHeader->addWidget(_volumeValue);
    root->addLayout(volumeHeader);

    auto *volumeStage = new QHBoxLayout{};
    volumeStage->setContentsMargins(0, 0, 0, 0);
    volumeStage->setSpacing(8);
    auto *airPodsGlyph = new AirPodsGlyph{this};
    volumeStage->addWidget(airPodsGlyph);
    _slider = new AppleHorizontalVolumeSlider{this};
    _slider->setObjectName("volumeSlider");
    _slider->setRange(0, 100);
    _slider->setSingleStep(1);
    _slider->setPageStep(10);
    _slider->setAccessibleName(tr("Volume"));
    _slider->setAccessibleDescription(tr("Windows output volume"));
    _slider->setToolTip(tr("Windows output volume"));
    volumeStage->addWidget(_slider, 1);
    _speakerGlyph = new SpeakerGlyph{this};
    volumeStage->addWidget(_speakerGlyph);
    root->addLayout(volumeStage);

    auto *separator = new QFrame{this};
    separator->setObjectName("controlSeparator");
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Plain);
    separator->setFixedHeight(1);
    root->addWidget(separator);

    auto *modeLabel = new QLabel{tr("Noise Control"), this};
    modeLabel->setObjectName("noiseControlLabel");
    root->addWidget(modeLabel);

    auto *modes = new QVBoxLayout{};
    modes->setContentsMargins(0, 0, 0, 0);
    modes->setSpacing(1);
    _modeGroup = new QButtonGroup{this};
    _modeGroup->setExclusive(true);

    constexpr NoiseControlMode visualModeOrder[] = {
        NoiseControlMode::Transparency,
        NoiseControlMode::Adaptive,
        NoiseControlMode::ANC,
        NoiseControlMode::Off,
    };
    // Keep construction in enum order so programmatic clients retain their original ordering;
    // the presentation mirrors macOS: Transparency, Adaptive, Noise Cancellation, then Off.
    for (int i = 0; i < 4; ++i) {
        const auto mode = static_cast<NoiseControlMode>(i);
        auto *button = new NoiseControlButton{mode, this};
        button->setCheckable(true);
        button->setEnabled(false);
        button->setAccessibleName(tr("Noise Control: %1").arg(ModeName(mode)));
        button->setAccessibleDescription(tr("Unavailable"));
        button->setToolTip(tr("Noise Control is unavailable on Windows"));
        _modeGroup->addButton(button);
        connect(button, &QPushButton::clicked, this, [this, mode] {
            Q_EMIT NoiseControlRequested(mode);
        });
        _modeButtons.push_back(button);
        _buttonModes.push_back(mode);
    }
    for (const auto mode : visualModeOrder) {
        modes->addWidget(_modeButtons[static_cast<int>(mode)]);
    }
    root->addLayout(modes);

    _unavailableStatus = new QLabel{tr("Noise Control is unavailable on Windows"), this};
    _unavailableStatus->setObjectName("unavailableStatus");
    _unavailableStatus->setAccessibleName(tr("Noise Control is unavailable on Windows"));
    _unavailableStatus->setWordWrap(true);
    root->addWidget(_unavailableStatus);

    SetPreviewVolume(50);
    connect(_slider, &QSlider::valueChanged, this, [this](int value) {
        _volumeValue->setText(tr("%1%").arg(value));
        _speakerGlyph->SetVolume(value);
        _slider->setAccessibleDescription(
            tr("Windows output volume, %1 percent").arg(value));
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
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    const QRectF surface = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    QLinearGradient material{surface.topLeft(), surface.bottomRight()};
    material.setColorAt(0.0, QColor{"#0A7F97"});
    material.setColorAt(0.54, QColor{"#00748C"});
    material.setColorAt(1.0, QColor{"#00637B"});
    painter.setPen(Qt::NoPen);
    painter.setBrush(material);
    painter.drawRoundedRect(surface, 18.0, 18.0);

    QPainterPath clip;
    clip.addRoundedRect(surface, 18.0, 18.0);
    QRadialGradient bloom{
        QPointF{surface.right() - surface.width() * 0.12,
                surface.top() + surface.height() * 0.08},
        qMax(surface.width(), surface.height()) * 0.62};
    bloom.setColorAt(0.0, QColor{196, 248, 255, 44});
    bloom.setColorAt(0.45, QColor{95, 219, 240, 16});
    bloom.setColorAt(1.0, Qt::transparent);
    painter.save();
    painter.setClipPath(clip);
    painter.fillRect(surface, bloom);
    painter.restore();

    painter.setPen(QPen{QColor{187, 246, 255, 224}, 1.0});
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(surface.adjusted(0.5, 0.5, -0.5, -0.5), 17.5, 17.5);

    painter.setPen(QPen{QColor{117, 225, 242, 110}, 0.75});
    painter.drawRoundedRect(surface.adjusted(1.5, 1.5, -1.5, -1.5), 16.5, 16.5);
}

void PopupControlPanel::ApplyStyle()
{
    setStyleSheet(QStringLiteral(
        "QFrame#popupControlPanel { background: transparent; border: none; }"
                       "QLabel { color: rgba(255,255,255,0.96); font-size: 13px; "
                       "background: transparent; }"
                       "QLabel#sectionLabel, QLabel#noiseControlLabel { "
                       "color: #FFFFFF; font-size: 13px; font-weight: 600; }"
                       "QLabel#volumeValue { color: rgba(255,255,255,0.88); "
                       "font-size: 13px; font-weight: 600; }"
                       "QLabel#unavailableStatus { color: rgba(255,255,255,0.72); "
                       "font-size: 11px; }"
                       "QFrame#controlSeparator { color: rgba(255,255,255,0.42); "
                       "background: rgba(255,255,255,0.42); border: none; }"
                       "QPushButton { background: transparent; border: none; }"));
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
        tr("Windows output volume, %1 percent").arg(_slider->value()));
    _slider->setToolTip(tr("Windows output volume"));
}

void PopupControlPanel::SetVolumeUnavailable()
{
    _slider->setEnabled(false);
    _speakerGlyph->setEnabled(false);
    _volumeValue->setText(QStringLiteral("\u2014"));
    _slider->setAccessibleDescription(tr("Volume unavailable"));
    _slider->setToolTip(tr("Volume unavailable"));
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
            available ? (isSelected ? tr("Selected") : tr("Not selected"))
                      : tr("Unavailable"));
        button->setToolTip(available ? ModeName(_buttonModes[i])
                                     : tr("Noise Control is unavailable on Windows"));
    }
    _modeGroup->setExclusive(true);
    _unavailableStatus->setVisible(!available);
}
} // namespace Gui
