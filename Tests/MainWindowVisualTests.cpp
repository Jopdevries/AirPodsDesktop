#include <QApplication>
#include <QDir>
#include <QFontMetrics>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QTest>

#include "Source/Core/AirPods.h"
#if defined APD_OS_WIN
#include "Source/Core/OS/Windows.h"
#endif
#include "Source/Gui/MainWindow.h"
#include "Source/Gui/Widget/PopupControlPanel.h"

class MainWindowVisualTests final : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
#if defined APD_OS_WIN
        Core::OS::Windows::Winrt::Initialize();
#endif
    }

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
            panel.resize(320, panel.sizeHint().height());
            panel.show();
            QCoreApplication::processEvents();

            QVERIFY(panel.height() >= panel.minimumSizeHint().height());
            QCOMPARE(panel.findChildren<QPushButton *>().size(), 4);
            QCOMPARE(panel.findChild<QLabel *>("sectionLabel")->text(), QString{"Geluidsniveau"});
            QCOMPARE(panel.findChild<QLabel *>("unavailableStatus")->isVisible(), true);
            for (auto *button : panel.findChildren<QPushButton *>()) {
                QVERIFY(!button->isChecked());
                QVERIFY(!button->isEnabled());
                QVERIFY(button->minimumHeight() >= 44);
                QVERIFY(!button->accessibleName().isEmpty());
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

    void completePopupHandlesLongDeviceName()
    {
        const QString dir = qEnvironmentVariable("APD_VISUAL_DIR");
        if (!dir.isEmpty()) {
            QVERIFY(QDir{}.mkpath(dir));
        }

        for (const bool dark : {false, true}) {
            QPalette palette;
            palette.setColor(QPalette::Window, dark ? QColor{28, 28, 30} : QColor{250, 250, 252});
            palette.setColor(
                QPalette::WindowText, dark ? QColor{242, 242, 247} : QColor{28, 28, 30});
            qApp->setPalette(palette);

            Gui::MainWindow window{nullptr, false};
            Core::AirPods::State state;
            state.displayName = "Jop's AirPods Pro 2 - Aerospace Engineering";
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
            window.resize(window.sizeHint().expandedTo(QSize{360, 520}));
            QCoreApplication::processEvents();

            QVERIFY(window.width() >= 360);
            QVERIFY(window.height() >= window.minimumSizeHint().height());
            auto *deviceLabel = window.findChild<QLabel *>("deviceLabel");
            QVERIFY(deviceLabel != nullptr);
            QVERIFY(deviceLabel->geometry().right() <= window.width());
            QVERIFY(
                QFontMetrics{deviceLabel->font()}.horizontalAdvance(deviceLabel->text()) <=
                deviceLabel->contentsRect().width());
            QCOMPARE(deviceLabel->toolTip(), state.displayName);

            if (!dir.isEmpty()) {
                QVERIFY(window.grab().save(QDir{dir}.filePath(
                    QString{"complete-popup-%1-long-name.png"}.arg(dark ? "dark" : "light"))));
            }
        }
    }
};

QTEST_MAIN(MainWindowVisualTests)
#include "MainWindowVisualTests.moc"
