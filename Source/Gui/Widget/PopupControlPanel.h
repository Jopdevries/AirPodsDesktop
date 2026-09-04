#pragma once

#include <QFrame>
#include <QVector>

#include <optional>

class QLabel;
class QButtonGroup;
class QPaintEvent;
class QPushButton;
class QSlider;
class QTimer;

namespace Gui {

class SpeakerGlyph;

class PopupControlPanel final : public QFrame
{
    Q_OBJECT

public:
    enum class NoiseControlMode { ANC, Transparency, Adaptive, Off };
    Q_ENUM(NoiseControlMode)

    explicit PopupControlPanel(QWidget *parent = nullptr);

    static int PercentFromVolume(float value);

    void SetPreviewVolume(int percent);
    void SetNoiseControlState(std::optional<NoiseControlMode> mode, bool available);

Q_SIGNALS:
    void NoiseControlRequested(Gui::PopupControlPanel::NoiseControlMode mode);

private:
    SpeakerGlyph *_speakerGlyph{};
    QSlider *_slider{};
    QLabel *_volumeValue{};
    QLabel *_unavailableStatus{};
    QButtonGroup *_modeGroup{};
    QVector<QPushButton *> _modeButtons;
    QVector<NoiseControlMode> _buttonModes;
    QTimer *_volumePoller{};

    void ApplyStyle();
    void paintEvent(QPaintEvent *event) override;
    void SetVolumePercent(int percent);
    void SetVolumeUnavailable();
    void RefreshVolume();
};
} // namespace Gui
