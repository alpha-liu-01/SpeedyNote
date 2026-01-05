#ifndef STYLELOADER_H
#define STYLELOADER_H

#include <QString>
#include <QColor>
#include <QMap>

/**
 * @brief Helper class for loading QSS stylesheets with placeholder substitution
 * 
 * Stylesheets in resources/styles/ use {{PLACEHOLDER}} syntax for dynamic values.
 * This class loads the QSS file and replaces placeholders with runtime values.
 * 
 * Usage:
 *   QString qss = StyleLoader::loadTabStylesheet(darkMode, accentColor, ...);
 *   tabBar->setStyleSheet(qss);
 */
class StyleLoader
{
public:
    /**
     * @brief Load tab bar stylesheet with dynamic color substitution
     * @param darkMode Whether dark mode is active
     * @param accentColor The accent color (tab bar background)
     * @param washedAccent Desaturated accent color (inactive tabs)
     * @param textColor Tab text color
     * @param selectedBg Selected tab background (system gray)
     * @param hoverBg Tab hover background
     * @return Complete stylesheet string ready to apply
     */
    static QString loadTabStylesheet(
        bool darkMode,
        const QColor &accentColor,
        const QColor &washedAccent,
        const QColor &textColor,
        const QColor &selectedBg,
        const QColor &hoverBg
    );
    
    /**
     * @brief Load a QSS file from resources with placeholder substitution
     * @param resourcePath Path to QSS file (e.g., ":/resources/styles/tabs.qss")
     * @param replacements Map of placeholder names to values (without {{ }})
     * @return Stylesheet with placeholders replaced
     */
    static QString loadStylesheet(
        const QString &resourcePath,
        const QMap<QString, QString> &replacements
    );

private:
    // Prevent instantiation - all methods are static
    StyleLoader() = default;
};

#endif // STYLELOADER_H

