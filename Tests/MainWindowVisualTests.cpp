#include <QApplication>
#include <QAbstractButton>
#include <QColor>
#include <QDir>
#include <QFontMetrics>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QMediaPlayer>
#include <QPalette>
#include <QPixmap>
#include <QScreen>
#include <QStringList>
#include <QPushButton>
#include <QSlider>
#include <QTest>
#include <QVideoWidget>
#include <QWindow>
#include <QtMath>

#include "Source/Core/AirPods.h"
#if defined APD_OS_WIN
#include "Source/Core/OS/Windows.h"
#endif
#include "Source/Gui/MainWindow.h"
#include "Source/Gui/Widget/PopupControlPanel.h"

class MainWindowVisualTests final : public QObject
{
    Q_OBJECT

    static void ShowAndWaitForDecodedAnimation(Gui::MainWindow &window)
    {
        // QVideoWidget renders through the native multimedia surface on Windows.  A QWidget
        // grab before the dialog is shown omits that surface, yielding a misleading empty box.
        window.show();
        window.raise();
        window.activateWindow();

        QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
        QTRY_VERIFY_WITH_TIMEOUT(window.windowHandle() != nullptr, 1000);

        auto *player = window.findChild<QMediaPlayer *>();
        auto *videoWidget = window.findChild<QVideoWidget *>();
        QVERIFY(player != nullptr);
        QVERIFY(videoWidget != nullptr);

        QTRY_VERIFY_WITH_TIMEOUT(videoWidget->isVisible(), 1000);
        QTRY_VERIFY_WITH_TIMEOUT(player->state() == QMediaPlayer::PlayingState, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(player->isVideoAvailable(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(player->position() > 100, 3000);

        // Allow the show transition and a decoded frame to be presented before capture.
        QTest::qWait(300);
    }

    static void GrabVisiblePopup(Gui::MainWindow &window, QPixmap &popup)
    {
        auto *screen = window.windowHandle()->screen();
        QVERIFY(screen != nullptr);

        // Screen capture, rather than QWidget::grab(), includes the native QVideoWidget
        // surface used by Qt 5 Multimedia on Windows.
        popup = screen->grabWindow(window.winId());
        QVERIFY(!popup.isNull());
    }

    static bool HasVisibleVideoFrame(
        const QPixmap &popup, const QWidget &videoWidget, const QWidget &window)
    {
        const auto image = popup.toImage().convertToFormat(QImage::Format_RGB32);
        const auto videoOrigin = videoWidget.mapTo(&window, QPoint{});
        const auto scale = popup.devicePixelRatio();
        const QRect videoRect{
            qRound(videoOrigin.x() * scale), qRound(videoOrigin.y() * scale),
            qRound(videoWidget.width() * scale), qRound(videoWidget.height() * scale)};
        const auto cropped = image.copy(videoRect.intersected(image.rect()));
        if (cropped.isNull()) {
            return false;
        }

        // An uninitialised DirectShow surface is solid black.  The shipped AVI has a bright
        // background, so a single bright sample proves that the native capture contains video.
        constexpr int sampleStep = 4;
        int samples = 0;
        int brightSamples = 0;
        for (int y = 0; y < cropped.height(); y += sampleStep) {
            for (int x = 0; x < cropped.width(); x += sampleStep) {
                const auto color = QColor::fromRgb(cropped.pixel(x, y));
                ++samples;
                if (color.red() > 180 && color.green() > 180 && color.blue() > 180) {
                    ++brightSamples;
                }
            }
        }
        return samples > 0 && brightSamples * 2 > samples;
    }

private Q_SLOTS:
    void convertsEndpointVolumeToPercent()
    {
        QCOMPARE(Gui::PopupControlPanel::PercentFromVolume(-0.1f), 0);
        QCOMPARE(Gui::PopupControlPanel::PercentFromVolume(0.0f), 0);
        QCOMPARE(Gui::PopupControlPanel::PercentFromVolume(0.504f), 50);
        QCOMPARE(Gui::PopupControlPanel::PercentFromVolume(0.505f), 51);
        QCOMPARE(Gui::PopupControlPanel::PercentFromVolume(1.0f), 100);
        QCOMPARE(Gui::PopupControlPanel::PercentFromVolume(1.1f), 100);
    }

    void popupHasAppleControlSurface()
    {
        const QString output = qEnvironmentVariable("APD_VISUAL_OUTPUT");
        const QString dir = qEnvironmentVariable("APD_VISUAL_DIR");
        if (!dir.isEmpty()) {
            QVERIFY(QDir{}.mkpath(dir));
        }

        for (const bool dark : {false, true}) {
            QPalette palette;
            palette.setColor(QPalette::Window, dark ? QColor{30, 30, 32} : QColor{250, 250, 252});
            palette.setColor(QPalette::WindowText, dark ? Qt::white : QColor{29, 29, 31});
            qApp->setPalette(palette);

            Gui::PopupControlPanel panel;
            panel.setPalette(palette);
            panel.resize(300, panel.sizeHint().height());
            panel.show();
            QCoreApplication::processEvents();

            QVERIFY(panel.minimumSizeHint().width() <= 300);
            QVERIFY(panel.height() >= panel.minimumSizeHint().height());
            QCOMPARE(panel.findChildren<QPushButton *>().size(), 4);
            QCOMPARE(panel.findChild<QLabel *>("sectionLabel")->text(), QString{"Sound"});
            QCOMPARE(
                panel.findChild<QLabel *>("noiseControlLabel")->text(), QString{"Noise Control"});
            QCOMPARE(panel.findChild<QLabel *>("unavailableStatus")->isVisible(), true);
            const auto appearanceCapture =
                panel.grab().toImage().convertToFormat(QImage::Format_RGB32);
            const auto surfaceSample = QColor::fromRgb(appearanceCapture.pixel(
                appearanceCapture.width() - 10, appearanceCapture.height() / 2));
            // One saturated cyan-teal control surface in either host appearance: reject both the
            // earlier pale panel and the charcoal nested card while preserving white-text contrast.
            QVERIFY(surfaceSample.green() >= surfaceSample.red() + 70);
            QVERIFY(surfaceSample.blue() >= surfaceSample.red() + 90);
            QVERIFY(surfaceSample.blue() > surfaceSample.green());
            QVERIFY(qGray(surfaceSample.rgb()) >= 70);
            QVERIFY(qGray(surfaceSample.rgb()) <= 120);
            auto *volumeSlider = panel.findChild<QSlider *>("volumeSlider");
            QVERIFY(volumeSlider != nullptr);
            QCOMPARE(volumeSlider->orientation(), Qt::Horizontal);
            QVERIFY(volumeSlider->minimumHeight() >= 28);
            // The compact macOS geometry is a 4 pt rail, but the 20 pt thumb remains
            // comfortably inside this taller interactive hit target.
            QVERIFY(volumeSlider->height() >= 20);
            QVERIFY(volumeSlider->sizeHint().width() <= 176);
            QVERIFY(panel.findChild<QFrame *>("controlSeparator") != nullptr);
            for (auto *button : panel.findChildren<QPushButton *>()) {
                QVERIFY(!button->isChecked());
                QVERIFY(!button->isEnabled());
                QVERIFY(button->minimumHeight() >= 28);
                QVERIFY(!button->accessibleName().isEmpty());
            }

            const QStringList expectedLabels{
                "Noise Cancellation", "Transparency", "Adaptive", "Off"};
            const auto buttons = panel.findChildren<QPushButton *>();
            for (int i = 0; i < buttons.size(); ++i) {
                QCOMPARE(buttons[i]->text(), expectedLabels[i]);
            }

            for (const int volume : {0, 50, 100}) {
                panel.SetPreviewVolume(volume);
                QCoreApplication::processEvents();
                QCOMPARE(
                    panel.findChild<QLabel *>("volumeValue")->text(),
                    QString{"%1%"}.arg(volume));
                if (!dir.isEmpty()) {
                    QVERIFY(panel.grab().save(QDir{dir}.filePath(
                        QString{"popup-%1-volume-%2.png"}
                            .arg(dark ? "dark" : "light")
                            .arg(volume))));
                }
            }

            const QStringList modeNames{"anc", "transparency", "adaptive", "off"};
            for (int i = 0; i < modeNames.size(); ++i) {
                panel.SetNoiseControlState(
                    static_cast<Gui::PopupControlPanel::NoiseControlMode>(i), true);
                QCoreApplication::processEvents();
                QCOMPARE(panel.findChildren<QPushButton *>()[i]->isChecked(), true);
                if (!dir.isEmpty()) {
                    QVERIFY(panel.grab().save(QDir{dir}.filePath(
                        QString{"popup-%1-mode-%2.png"}.arg(
                            dark ? "dark" : "light", modeNames[i]))));
                }
            }

            if (!output.isEmpty() && !dark) {
                QVERIFY(panel.grab().save(output));
            }
        }
    }

    void completePopupPreservesAnimationAndDesign()
    {
        const QString dir = qEnvironmentVariable("APD_VISUAL_DIR");
        if (!dir.isEmpty()) {
            QVERIFY(QDir{}.mkpath(dir));
        }

        const QStringList deviceNames{
            "Jop's AirPods Pro 2 - Aerospace Engineering",
            "AirPods Pro",
        };
        const QStringList fileNames{
            "complete-popup-light-long-name.png",
            "complete-popup-light-airpods-pro.png",
        };
        const QColor expectedPopupSurface{250, 250, 252};

        for (const bool dark : {false, true}) {
            QPalette palette;
            palette.setColor(QPalette::Window, dark ? QColor{28, 28, 30} : QColor{250, 250, 252});
            palette.setColor(
                QPalette::WindowText, dark ? QColor{242, 242, 247} : QColor{28, 28, 30});
            qApp->setPalette(palette);

            for (int i = 0; i < deviceNames.size(); ++i) {
                Gui::MainWindow window{nullptr, false};
                Core::AirPods::State state;
                state.displayName = deviceNames[i];
                state.model = Core::AirPods::Model::AirPods_Pro_2;
                state.pods.left.battery = 100;
                state.pods.right.battery = 86;
                state.caseBox.battery = 54;
                state.pods.right.isCharging = true;
                window.UpdateState(state);

                auto *controls = window.findChild<Gui::PopupControlPanel *>("popupControlPanel");
                QVERIFY(controls != nullptr);
                controls->SetPreviewVolume(50);
                window.ensurePolished();
                window.resize(window.sizeHint().expandedTo(QSize{320, 480}));
                QCoreApplication::processEvents();

                // The AVI is opaque light artwork, so the whole pairing surface deliberately
                // remains light even while the host application uses a dark palette.
                QCOMPARE(window.palette().color(QPalette::Window), expectedPopupSurface);
                QVERIFY(window.width() >= 320);
                QVERIFY(window.height() >= window.minimumSizeHint().height());
                auto *deviceLabel = window.findChild<QLabel *>("deviceLabel");
                QVERIFY(deviceLabel != nullptr);
                QVERIFY(deviceLabel->geometry().right() <= window.width());
                QVERIFY(
                    QFontMetrics{deviceLabel->font()}.horizontalAdvance(deviceLabel->text()) <=
                    deviceLabel->contentsRect().width());
                QCOMPARE(deviceLabel->toolTip(), state.displayName);

                ShowAndWaitForDecodedAnimation(window);
                auto *videoWidget = window.findChild<QVideoWidget *>();
                QVERIFY(videoWidget != nullptr);
                auto *closeButton = window.findChild<QAbstractButton *>("closeButton");
                QVERIFY(closeButton != nullptr);

                // An automatic/assistive focus must not suggest that the close affordance is
                // already active.  Explicit keyboard traversal still gets the visible ring.
                closeButton->setFocus(Qt::OtherFocusReason);
                QTRY_VERIFY_WITH_TIMEOUT(closeButton->hasFocus(), 1000);
                QCOMPARE(closeButton->focusPolicy(), Qt::StrongFocus);

                QPixmap popup;
                GrabVisiblePopup(window, popup);
                QVERIFY2(
                    HasVisibleVideoFrame(popup, *videoWidget, window),
                    "The native popup screenshot did not contain a decoded AirPods AVI frame.");
                if (!dir.isEmpty() && !dark) {
                    QVERIFY(popup.save(QDir{dir}.filePath(fileNames[i])));
                }
            }
        }
    }
};

int main(int argc, char *argv[])
{
    // Qt initialises the GUI thread as STA.  The product initialises its WinRT apartment before
    // constructing QApplication, which avoids RPC_E_CHANGED_MODE and lets the real multimedia
    // pipeline decode the bundled AirPods animation under the same conditions as the app.
#if defined APD_OS_WIN
    Core::OS::Windows::Winrt::Initialize();
#endif

    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication application{argc, argv};
    MainWindowVisualTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "MainWindowVisualTests.moc"
