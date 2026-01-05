#include "StyleLoader.h"
#include <QFile>
#include <QDebug>

QString StyleLoader::loadTabStylesheet(
    bool darkMode,
    const QColor &accentColor,
    const QColor &washedAccent,
    const QColor &textColor,
    const QColor &selectedBg,
    const QColor &hoverBg)
{
    // Build replacement map
    QMap<QString, QString> replacements;
    replacements["TAB_BAR_BG"] = accentColor.name();
    replacements["TAB_BG"] = washedAccent.name();
    replacements["TAB_TEXT"] = textColor.name();
    replacements["TAB_SELECTED_BG"] = selectedBg.name();
    replacements["TAB_HOVER_BG"] = hoverBg.name();
    
    // Theme-aware icons
    replacements["CLOSE_ICON"] = darkMode ? "cross_reversed.png" : "cross.png";
    replacements["RIGHT_ARROW"] = darkMode ? "right_arrow_reversed.png" : "right_arrow.png";
    replacements["LEFT_ARROW"] = darkMode ? "left_arrow_reversed.png" : "left_arrow.png";
    
    // Load appropriate stylesheet
    QString resourcePath = darkMode 
        ? ":/resources/styles/tabs_dark.qss" 
        : ":/resources/styles/tabs.qss";
    
    return loadStylesheet(resourcePath, replacements);
}

QString StyleLoader::loadStylesheet(
    const QString &resourcePath,
    const QMap<QString, QString> &replacements)
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "StyleLoader: Failed to open stylesheet:" << resourcePath;
        return QString();
    }
    
    QString stylesheet = QString::fromUtf8(file.readAll());
    file.close();
    
    // Replace all placeholders
    for (auto it = replacements.constBegin(); it != replacements.constEnd(); ++it) {
        QString placeholder = QString("{{%1}}").arg(it.key());
        stylesheet.replace(placeholder, it.value());
    }
    
    return stylesheet;
}

