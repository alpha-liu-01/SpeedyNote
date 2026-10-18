// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "imageinsert.h"
#include "imagedocument.h"
#include <QLabel>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QTimer>

class LauncherImageImportController final : public QObject
{
public:
    explicit LauncherImageImportController(QWidget *launcher)
        : QObject(launcher), m_launcher(launcher)
    {
        for (auto *widget : launcher->findChildren<QWidget *>())
            if (widget->inherits("FloatingActionButton")) { m_fab = widget; break; }
        if (!m_fab || launcher->property("touchInkLauncherImagesInstalled").toBool()) return;
        auto nativeButtons = m_fab->findChildren<QPushButton *>(QString(), Qt::FindDirectChildrenOnly);
        if (nativeButtons.size() != 6) return; // Main button plus five actions in v1.6.3.
        for (auto *button : nativeButtons) if (button->width() == 56) m_main = button;
        for (auto *animation : m_fab->findChildren<QPropertyAnimation *>())
            if (animation->propertyName() == "expandProgress") m_animation = animation;
        if (!m_main || !m_animation) return;

        // Reserve a sixth native-sized row without touching the original action
        // list. The native animation positions its five buttons from the bottom.
        m_fab->setFixedHeight(m_fab->height() + 60);
        m_fab->move(m_fab->x(), m_fab->y() - 60);
        m_fab->setProperty("expandProgress", m_fab->property("expandProgress"));
        m_button = new QPushButton(m_fab);
        m_button->setObjectName(QStringLiteral("touchInkLauncherImportImage"));
        m_button->setAccessibleName(QStringLiteral("导入图片"));
        m_button->setToolTip(QStringLiteral("导入图片：以整张图片为页面，直接书写批注"));
        m_button->setFixedSize(48, 48);
        m_button->setCursor(Qt::PointingHandCursor);
        m_button->setFocusPolicy(Qt::NoFocus);
        m_button->setStyleSheet(nativeButtons.at(1)->styleSheet());
        const bool dark = nativeButtons.at(1)->styleSheet().contains("#2a2e32");
        m_button->setIcon(QIcon(dark ? ":/resources/icons/objectinsert_reversed.png"
                                    : ":/resources/icons/objectinsert.png"));
        m_button->setIconSize(QSize(24, 24));
        m_label = new QLabel(QStringLiteral("导入图片"), m_fab->parentWidget());
        m_label->setObjectName(QStringLiteral("touchInkLauncherImportImageLabel"));
        m_label->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_label->setStyleSheet(dark
            ? "QLabel { color: #ffffff; background: #2a2e32; border-radius: 5px; padding: 5px 8px; }"
            : "QLabel { color: #202124; background: #f5f5f5; border-radius: 5px; padding: 5px 8px; }");
        m_label->adjustSize();
        connect(m_animation, &QPropertyAnimation::valueChanged, this, [this] { positionButton(); });
        connect(m_button, &QPushButton::clicked, this, [this] { chooseImage(); });
        m_fab->installEventFilter(this);
        connect(&m_pendingTimer, &QTimer::timeout, this, [this] { prepareImagePage(); });
        launcher->setProperty("touchInkLauncherImagesInstalled", true);
        positionButton();
    }

protected:
    bool eventFilter(QObject *receiver, QEvent *event) override
    {
        if (receiver == m_fab && (event->type() == QEvent::Move || event->type() == QEvent::Resize ||
            event->type() == QEvent::Show || event->type() == QEvent::Hide)) positionButton();
        return false;
    }

private:
    QPointer<QWidget> m_launcher, m_fab;
    QPointer<QPushButton> m_button, m_main;
    QPointer<QLabel> m_label;
    QPointer<QPropertyAnimation> m_animation;
    QTimer m_pendingTimer;
    QSet<QWidget *> m_previousCanvases;
    QString m_pendingPath;
    int m_attempts = 0;
    bool m_busy = false;

    void positionButton()
    {
        if (!m_button || !m_fab || !m_label) return;
        const qreal progress = m_fab->property("expandProgress").toReal();
        const int mainY = m_fab->height() - 56;
        m_button->move((m_fab->width() - 48) / 2, mainY - qRound(360 * progress));
        m_button->setVisible(progress > 0.02);
        m_button->raise();
        m_label->move(m_fab->x() - m_label->width() - 8,
            m_fab->y() + m_button->y() + (48 - m_label->height()) / 2);
        m_label->setVisible(m_fab->isVisible() && progress > 0.98);
        m_label->raise();
    }

    void chooseImage()
    {
        if (m_busy || !m_pendingPath.isEmpty()) return;
        m_busy = true;
        if (m_animation->endValue().toReal() > 0) m_main->click();
        QSettings settings(QStringLiteral("SpeedyNote"), QStringLiteral("App"));
        QStringList patterns;
        for (const auto &format : QImageReader::supportedImageFormats())
            patterns.append(QStringLiteral("*.") + QString::fromLatin1(format));
        const QString path = QFileDialog::getOpenFileName(m_launcher, QStringLiteral("导入图片"),
            settings.value(QStringLiteral("touchInk/imageDirectory")).toString(),
            QStringLiteral("图片 (%1);;所有文件 (*)").arg(patterns.join(QLatin1Char(' '))));
        m_busy = false;
        if (path.isEmpty()) return;
        QImageReader reader(path);
        reader.setAutoTransform(true);
        QString problem;
        QImage image;
        if (QFileInfo(path).size() > 64LL * 1024 * 1024)
            problem = QStringLiteral("图片文件超过 64 MB，请先缩小图片。");
        else {
            image = reader.read();
            if (image.isNull()) problem = QStringLiteral("无法读取图片：%1").arg(reader.errorString());
        }
        if (!problem.isEmpty()) {
            QMessageBox::warning(m_launcher, QStringLiteral("导入图片"), problem);
            return;
        }
        const QString bundle = ImageDocument::create(path, image, problem);
        if (bundle.isEmpty()) {
            QMessageBox::warning(m_launcher, QStringLiteral("导入图片"), problem);
            return;
        }
        m_previousCanvases.clear();
        for (auto *widget : QApplication::allWidgets())
            if (widget->inherits("DocumentViewport")) m_previousCanvases.insert(widget);
        m_pendingPath = bundle;
        m_attempts = 0;
        m_launcher->setProperty("touchInkLastImportedImageBundle", bundle);
        settings.setValue(QStringLiteral("touchInk/imageDirectory"), QFileInfo(path).absolutePath());
        if (!QMetaObject::invokeMethod(m_launcher, "notebookSelected", Qt::DirectConnection,
            Q_ARG(QString, bundle))) {
            m_pendingPath.clear();
            return;
        }
        m_pendingTimer.start(50);
    }

    void prepareImagePage()
    {
        for (auto *canvas : QApplication::allWidgets()) {
            if (!canvas->inherits("DocumentViewport") || !canvas->isVisible() ||
                m_previousCanvases.contains(canvas)) continue;
            for (auto *widget : canvas->window()->findChildren<QWidget *>()) {
                QObject *controller = widget->property("touchInkImageController").value<QObject *>();
                if (!controller) continue;
                m_pendingTimer.stop();
                m_pendingPath.clear();
                canvas->setFocus(Qt::OtherFocusReason);
                for (auto *tool : widget->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
                    if (!tool->inherits("ExpandableToolButton")) continue;
                    if (auto *pen = tool->findChild<QAbstractButton *>()) pen->click();
                    break;
                }
                return;
            }
        }
        if (++m_attempts > 100) {
            m_pendingTimer.stop();
            m_pendingPath.clear();
            QMessageBox::warning(m_launcher, QStringLiteral("导入图片"),
                QStringLiteral("未能打开图片批注页面，请返回首页重新打开。"));
        }
    }
};
