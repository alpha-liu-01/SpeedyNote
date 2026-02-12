#ifndef TOOLBARBUTTONTESTS_H
#define TOOLBARBUTTONTESTS_H

#include <QObject>
#include <QTest>
#include <QSignalSpy>
#include <QButtonGroup>
#include "ToolbarButtons.h"

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

