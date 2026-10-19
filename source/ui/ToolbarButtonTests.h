#ifndef TOOLBARBUTTONTESTS_H
#define TOOLBARBUTTONTESTS_H

#include <QObject>
#include <QCoreApplication>
#include <QResizeEvent>
#include <QTest>
#include <QSignalSpy>
#include <QButtonGroup>
#include "ToolbarButtons.h"
#include "Toolbar.h"
#include "widgets/ExpandableToolButton.h"
#include "widgets/IconPresetButton.h"
#include "subtoolbars/EraserSubToolbar.h"
#include "widgets/ThicknessPresetButton.h"
#include <QSettings>

/**
 * Unit tests for ToolbarButton classes.
 * Run with: speedynote --test-buttons
 */
class ToolbarButtonTests : public QObject {
    Q_OBJECT

private slots:
    // Test ActionButton
    void testActionButton() {
        ActionButton btn;
        
        // Should not be checkable
        QVERIFY(!btn.isCheckable());
        
        // Should have correct objectName for QSS
        QCOMPARE(btn.objectName(), QString("ActionButton"));
        
        // Should be 36x36
        QCOMPARE(btn.size(), QSize(36, 36));
        
        // Click should emit clicked signal
        QSignalSpy spy(&btn, &QPushButton::clicked);
        btn.click();
        QCOMPARE(spy.count(), 1);
    }
    
    // Test ToggleButton
    void testToggleButton() {
        ToggleButton btn;
        
        // Should be checkable
        QVERIFY(btn.isCheckable());
        
        // Should have correct objectName
        QCOMPARE(btn.objectName(), QString("ToggleButton"));
        
        // Should toggle on click
        QVERIFY(!btn.isChecked());
        btn.click();
        QVERIFY(btn.isChecked());
        btn.click();
        QVERIFY(!btn.isChecked());
    }
    
    // Test ThreeStateButton
    void testThreeStateButton() {
        ThreeStateButton btn;
        
        // Should have correct objectName
        QCOMPARE(btn.objectName(), QString("ThreeStateButton"));
        
        // Initial state should be 0
        QCOMPARE(btn.state(), 0);
        
        // Should cycle through states on click
        QSignalSpy spy(&btn, &ThreeStateButton::stateChanged);
        
        btn.click();
        QCOMPARE(btn.state(), 1);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 1);
        
        btn.click();
        QCOMPARE(btn.state(), 2);
        
        btn.click();
        QCOMPARE(btn.state(), 0); // Wraps around
        
        // setState should clamp to valid range
        btn.setState(5);
        QCOMPARE(btn.state(), 2); // Clamped to max
        
        btn.setState(-1);
        QCOMPARE(btn.state(), 0); // Clamped to min
    }
    
    // Test ToolButton
    void testToolButton() {
        ToolButton btn;
        
        // Should be checkable (like ToggleButton)
        QVERIFY(btn.isCheckable());
        
        // Should have correct objectName
        QCOMPARE(btn.objectName(), QString("ToolButton"));
    }
    
    // Test ToolButton exclusive selection with QButtonGroup
    void testToolButtonGroup() {
        ToolButton btn1, btn2, btn3;
        QButtonGroup group;
        
        group.addButton(&btn1);
        group.addButton(&btn2);
        group.addButton(&btn3);
        group.setExclusive(true);
        
        // Initially none checked
        QVERIFY(!btn1.isChecked());
        QVERIFY(!btn2.isChecked());
        QVERIFY(!btn3.isChecked());
        
        // Click btn1 - only btn1 should be checked
        btn1.click();
        QVERIFY(btn1.isChecked());
        QVERIFY(!btn2.isChecked());
        QVERIFY(!btn3.isChecked());
        
        // Click btn2 - only btn2 should be checked
        btn2.click();
        QVERIFY(!btn1.isChecked());
        QVERIFY(btn2.isChecked());
        QVERIFY(!btn3.isChecked());
    }
    
    // Test icon loading
    void testIconLoading() {
        ActionButton btn;
        
        // Set a themed icon that exists in resources
        btn.setThemedIcon("save");
        
        // Icon should not be null
        QVERIFY(!btn.icon().isNull());
        
        // Test dark mode switching
        btn.setDarkMode(false);
        QVERIFY(!btn.isDarkMode());
        
        btn.setDarkMode(true);
        QVERIFY(btn.isDarkMode());
        QVERIFY(!btn.icon().isNull()); // Should still have icon
    }
    
    // Test ButtonStyles loading
    void testButtonStyles() {
        // Light mode stylesheet should not be empty
        QString lightStyle = ButtonStyles::getStylesheet(false);
        QVERIFY(!lightStyle.isEmpty());
        QVERIFY(lightStyle.contains("ActionButton"));
        QVERIFY(lightStyle.contains("ToggleButton"));
        
        // Dark mode stylesheet should not be empty
        QString darkStyle = ButtonStyles::getStylesheet(true);
        QVERIFY(!darkStyle.isEmpty());
        QVERIFY(darkStyle.contains("ActionButton"));
        
        // They should be different (different colors)
        QVERIFY(lightStyle != darkStyle);
    }

    // Overflow paging: the trailing button group moves to a second page when
    // the row no longer fits, and comes back when it does.
    void testToolbarPagination() {
        // Parented, as MainWindow hosts it. A parentless Toolbar would be a
        // window, and QLayout pins a window's minimum size to the full row
        // width, so it could never be resized narrow enough to page.
        QWidget host;
        Toolbar toolbar(&host);
        QVERIFY(!toolbar.m_page1Widgets.isEmpty());
        QVERIFY(!toolbar.m_page2Widgets.isEmpty());

        const int natural = toolbar.naturalContentWidth();
        QVERIFY(natural > 0);

        // A widget that has never been shown is not created yet, so resize()
        // only assigns the geometry and never generates an event. Deliver one
        // so the real resizeEvent path still gets exercised.
        auto resizeTo = [&toolbar](int width) {
            const QSize before = toolbar.size();
            toolbar.resize(width, 44);
            QResizeEvent event(toolbar.size(), before);
            QCoreApplication::sendEvent(&toolbar, &event);
        };

        // The toolbar must not report the whole row as its floor, or the
        // window could never shrink far enough to page in the first place.
        QVERIFY(toolbar.minimumSizeHint().width() < natural);

        resizeTo(natural + 200);
        QVERIFY(!toolbar.m_paged);
        QVERIFY(!toolbar.m_pagerNextButton->isVisibleTo(&toolbar));
        QVERIFY(!toolbar.m_pagerBackButton->isVisibleTo(&toolbar));
        for (QWidget* widget : toolbar.m_page2Widgets)
            QVERIFY(widget->isVisibleTo(&toolbar));

        resizeTo(natural / 2);
        QVERIFY(toolbar.m_paged);
        QCOMPARE(toolbar.m_currentPage, 0);
        QVERIFY(toolbar.m_pagerNextButton->isVisibleTo(&toolbar));
        QVERIFY(!toolbar.m_pagerBackButton->isVisibleTo(&toolbar));
        for (QWidget* widget : toolbar.m_page1Widgets)
            QVERIFY(widget->isVisibleTo(&toolbar));
        for (QWidget* widget : toolbar.m_page2Widgets)
            QVERIFY(!widget->isVisibleTo(&toolbar));

        // Forward on the right, back on the left.
        toolbar.m_pagerNextButton->click();
        QCOMPARE(toolbar.m_currentPage, 1);
        QVERIFY(toolbar.m_pagerBackButton->isVisibleTo(&toolbar));
        QVERIFY(!toolbar.m_pagerNextButton->isVisibleTo(&toolbar));
        for (QWidget* widget : toolbar.m_page1Widgets)
            QVERIFY(!widget->isVisibleTo(&toolbar));
        for (QWidget* widget : toolbar.m_page2Widgets)
            QVERIFY(widget->isVisibleTo(&toolbar));

        // Pan lives on page 2, so selecting it from page 1 has to follow it
        // over rather than check a button nobody can see.
        toolbar.m_pagerBackButton->click();
        QCOMPARE(toolbar.m_currentPage, 0);
        toolbar.setCurrentTool(ToolType::Pan);
        QCOMPARE(toolbar.m_currentPage, 1);
        QVERIFY(toolbar.m_panButton->isVisibleTo(&toolbar));

        // Widening restores the single row and drops both pagers.
        resizeTo(natural + 200);
        QVERIFY(!toolbar.m_paged);
        QCOMPARE(toolbar.m_currentPage, 0);
        QVERIFY(!toolbar.m_pagerNextButton->isVisibleTo(&toolbar));
        QVERIFY(!toolbar.m_pagerBackButton->isVisibleTo(&toolbar));
        for (QWidget* widget : toolbar.m_page1Widgets)
            QVERIFY(widget->isVisibleTo(&toolbar));
        for (QWidget* widget : toolbar.m_page2Widgets)
            QVERIFY(widget->isVisibleTo(&toolbar));
    }

    // An expanded subtoolbar must keep its width while its page is hidden,
    // otherwise the measurement driving paging collapses and the strip comes
    // back a sliver wide.
    void testExpandedWidthSurvivesHiding() {
        QWidget host;
        Toolbar toolbar(&host);

        toolbar.setCurrentTool(ToolType::Pen);
        ExpandableToolButton* pen = toolbar.m_penExpandable;
        QVERIFY(pen->isExpanded());
        const int expandedWidth = pen->sizeHint().width();
        QVERIFY(expandedWidth > 36);

        pen->hide();
        QCOMPARE(pen->sizeHint().width(), expandedWidth);

        pen->show();
        QCOMPARE(pen->sizeHint().width(), expandedWidth);

        // Selecting a tool with no subtoolbar and coming straight back must
        // restore the full strip width rather than the bare icon.
        toolbar.setObjectInsertMode(DocumentViewport::ObjectInsertMode::Image);
        toolbar.setCurrentTool(ToolType::ObjectSelect);
        QVERIFY(!pen->isExpanded());
        QCOMPARE(pen->sizeHint().width(), 36);

        toolbar.setCurrentTool(ToolType::Pen);
        QVERIFY(pen->isExpanded());
        QCOMPARE(pen->sizeHint().width(), expandedWidth);
    }

    // Size presets belong to a mode: switching modes must swap the three
    // values (and the selection), and lasso - which has no size - must hide the
    // row instead of showing dead controls.
    void testEraserSizePresetsArePerMode() {
        // The bar reads the real QSettings, so snapshot the eraser group and
        // put it back afterwards: a test run must not wipe the developer's
        // saved presets.
        auto snapshotEraserGroup = []() {
            QMap<QString, QVariant> saved;
            QSettings settings;
            for (const QString& key : settings.allKeys()) {
                if (key.startsWith(QStringLiteral("eraser/"))) {
                    saved.insert(key, settings.value(key));
                }
            }
            return saved;
        };
        auto restoreEraserGroup = [](const QMap<QString, QVariant>& saved) {
            QSettings settings;
            settings.beginGroup(QStringLiteral("eraser"));
            settings.remove(QString());
            settings.endGroup();
            for (auto it = saved.constBegin(); it != saved.constEnd(); ++it) {
                settings.setValue(it.key(), it.value());
            }
        };

        const QMap<QString, QVariant> savedSettings = snapshotEraserGroup();

        // Start from a clean slate so the built-in defaults are what we see.
        {
            QSettings settings;
            settings.beginGroup(QStringLiteral("eraser"));
            settings.remove(QString());
            settings.endGroup();
        }

        EraserSubToolbar bar;
        const QList<ThicknessPresetButton*> sizes =
            bar.findChildren<ThicknessPresetButton*>();
        QCOMPARE(sizes.size(), 3);

        auto thicknesses = [&]() {
            QList<qreal> values;
            for (ThicknessPresetButton* button : sizes) {
                values << button->thickness();
            }
            return values;
        };

        auto indexOf = [&](ThicknessPresetButton* button) {
            return sizes.indexOf(button);
        };

        // Whole-stroke is the default mode and shows its own three sizes.
        QCOMPARE(bar.currentModeIndex(), 0);
        QCOMPARE(thicknesses(), QList<qreal>({5.0, 15.0, 40.0}));
        for (ThicknessPresetButton* button : sizes) {
            QVERIFY(!button->isHidden());
        }

        // Edit the whole-stroke presets through the selection path, then pick
        // the third one, so the mode has a distinctive set + selection.
        QTest::mouseClick(sizes.at(indexOf(sizes.at(2))), Qt::LeftButton);
        QCOMPARE(bar.currentSize(), 40.0);

        // Switching to partial brings its own values, not the shared ones.
        const QList<IconPresetButton*> modes = bar.findChildren<IconPresetButton*>();
        QCOMPARE(modes.size(), 4);
        QTest::mouseClick(modes.at(1), Qt::LeftButton);
        QCOMPARE(bar.currentModeIndex(), 1);
        QCOMPARE(thicknesses(), QList<qreal>({8.0, 15.0, 25.0}));
        QCOMPARE(bar.currentSize(), 15.0);   // partial starts on its middle preset

        // Remember the partial selection, then bounce back: whole-stroke must
        // still have its own values and its third preset selected.
        QTest::mouseClick(sizes.at(0), Qt::LeftButton);
        QCOMPARE(bar.currentSize(), 8.0);
        QTest::mouseClick(modes.at(0), Qt::LeftButton);
        QCOMPARE(bar.currentModeIndex(), 0);
        QCOMPARE(thicknesses(), QList<qreal>({5.0, 15.0, 40.0}));
        QCOMPARE(bar.currentSize(), 40.0);

        // And back to partial: its first preset is still selected.
        QTest::mouseClick(modes.at(1), Qt::LeftButton);
        QCOMPARE(bar.currentSize(), 8.0);

        // Lasso has no size, so the whole row disappears.
        QTest::mouseClick(modes.at(2), Qt::LeftButton);
        QCOMPARE(bar.currentModeIndex(), 2);
        for (ThicknessPresetButton* button : sizes) {
            QVERIFY(button->isHidden());
        }

        // Cycling the size does nothing while lasso is active.
        const int beforeCycle = bar.currentModeIndex();
        bar.cycleSize();
        QCOMPARE(bar.currentModeIndex(), beforeCycle);

        // The partial lasso also works on a region, so it has no size either.
        QTest::mouseClick(modes.at(3), Qt::LeftButton);
        QCOMPARE(bar.currentModeIndex(), 3);
        for (ThicknessPresetButton* button : sizes) {
            QVERIFY(button->isHidden());
        }

        // Returning to a disc-based mode brings the row back with that mode's
        // values intact.
        QTest::mouseClick(modes.at(1), Qt::LeftButton);
        for (ThicknessPresetButton* button : sizes) {
            QVERIFY(!button->isHidden());
        }
        QCOMPARE(thicknesses(), QList<qreal>({8.0, 15.0, 25.0}));
        QCOMPARE(bar.currentSize(), 8.0);

        // Leave the developer's settings exactly as they were.
        restoreEraserGroup(savedSettings);
    }

    // The eraser mode picker must be three side-by-side choices, with exactly
    // one selected - not a single button that cycles through hidden states.
    void testEraserModeButtons() {
        EraserSubToolbar bar;

        const QList<IconPresetButton*> buttons = bar.findChildren<IconPresetButton*>();
        // whole / partial / lasso / lasso-partial
        QCOMPARE(buttons.size(), 4);

        auto selectedCount = [&]() {
            int count = 0;
            for (IconPresetButton* button : buttons) {
                if (button->isSelected()) {
                    ++count;
                }
            }
            return count;
        };

        // Whole-stroke is the default and must be the visible selection.
        QCOMPARE(bar.currentModeIndex(), 0);
        QCOMPARE(selectedCount(), 1);
        QVERIFY(buttons.at(0)->isSelected());

        // Picking a mode selects it, deselects the rest, and reports the change.
        for (int mode : {1, 2, 3, 0}) {
            QSignalSpy spy(&bar, &EraserSubToolbar::eraserModeChanged);
            QTest::mouseClick(buttons.at(mode), Qt::LeftButton);
            QCOMPARE(bar.currentModeIndex(), mode);
            QCOMPARE(spy.count(), 1);
            QCOMPARE(spy.first().first().toInt(), mode);
            QCOMPARE(selectedCount(), 1);
            QVERIFY(buttons.at(mode)->isSelected());
        }

        // Clicking the already-active mode is a no-op, not a toggle-off.
        QSignalSpy repeat(&bar, &EraserSubToolbar::eraserModeChanged);
        QTest::mouseClick(buttons.at(0), Qt::LeftButton);
        QCOMPARE(repeat.count(), 0);
        QCOMPARE(bar.currentModeIndex(), 0);
        QCOMPARE(selectedCount(), 1);

        // External state changes (e.g. shortcut) move the ring with them.
        bar.setModeState(2);
        QCOMPARE(bar.currentModeIndex(), 2);
        QCOMPARE(selectedCount(), 1);
        QVERIFY(buttons.at(2)->isSelected());
    }
};

/**
 * Run button tests.
 * @return 0 if all tests pass, non-zero otherwise
 */
inline int runButtonTests() {
    ToolbarButtonTests tests;
    return QTest::qExec(&tests);
}

#endif // TOOLBARBUTTONTESTS_H

