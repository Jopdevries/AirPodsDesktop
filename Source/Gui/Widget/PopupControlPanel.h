#pragma once

#include <QFrame>
#include <QVector>

#include <optional>

class QLabel;
class QButtonGroup;
class QPushButton;
class QSlider;
class QTimer;

namespace Gui {

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
    QSlider *_slider{};
    QLabel *_volumeValue{};
    QLabel *_unavailableStatus{};
    QButtonGroup *_modeGroup{};
    QVector<QPushButton *> _modeButtons;
    QTimer *_volumePoller{};

    void ApplyStyle();
    void SetVolumePercent(int percent);
    void SetVolumeUnavailable();
    void RefreshVolume();
};
} // namespace Gui
