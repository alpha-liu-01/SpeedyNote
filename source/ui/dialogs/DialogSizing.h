#ifndef DIALOGSIZING_H
#define DIALOGSIZING_H

/**
 * Sizing helpers for dialogs that have to hold up under more than one
 * platform's idea of a default font.
 *
 * Header-only and free of state: these are three short answers to the same
 * mistake, which is treating a size measured on one desktop as a property of
 * the dialog rather than of the font it was measured with.
 */

#include <QAbstractScrollArea>
#include <QDialog>
#include <QFont>
#include <QFrame>
#include <QScreen>
#include <QScrollArea>
#include <QScroller>
#include <QSize>
#include <QWidget>

namespace DialogSizing {

/// `base` scaled by `factor`, in whichever unit it happens to carry.
///
/// For text that should sit a little above or below the body size. A hardcoded
/// px in a stylesheet cannot do that job, because it is a constant next to a
/// body size that is not: at 96dpi `font-size: 13px` is a shade under a 10pt
/// default and reads as the small print it was meant to be, while on a platform
/// reporting 72dpi against a 12pt default the same declaration comes out level
/// with the body text.
inline QFont scaledFont(const QFont& base, qreal factor)
{
    QFont font = base;
    // A font carries one or the other, and the unused one reads as -1. Scaling
    // that yields a font size of -1, which is to say no text.
    if (font.pointSizeF() > 0.0) {
        font.setPointSizeF(font.pointSizeF() * factor);
    } else if (font.pixelSize() > 0) {
        font.setPixelSize(qRound(font.pixelSize() * factor));
    }
    return font;
}

/// Opens `dialog` at `preferred`, grown to what its layout asks for, and capped
/// at what the screen can show.
///
/// Sets no minimum, on purpose. QLayout::activate() already gives a window the
/// minimum its own layout reports -- but only on the axes that have not been
/// given one explicitly, so a hardcoded minimum does not raise a floor, it
/// replaces the layout's requirement. Where the platform font is larger than
/// the one those numbers were measured with, the shortfall comes out of the
/// content: widgets are squeezed below their own minimums and whatever sits
/// last in the layout is clipped.
inline void openAtSize(QDialog* dialog, QSize preferred)
{
    QSize size = preferred.expandedTo(dialog->sizeHint());
    if (const QScreen* screen = dialog->screen()) {
        // Inset so a dialog that wants the whole screen still reads as a dialog,
        // and so its button row cannot land under a status bar: availableGeometry()
        // is the full display on HarmonyOS, reserved strips and all. Unlike the
        // sizes above, this does not vary with the font, which is why it can be a
        // constant at all.
        size = size.boundedTo(screen->availableGeometry().size() - QSize(48, 96));
    }
    dialog->resize(size);
}

/// `content`, in a scroll area sized to be shrinkable.
///
/// For content whose height is the sum of a dozen font-dependent widgets and so
/// cannot be promised to fit any particular screen. What it changes is the
/// dialog's *minimum*: a scroll area reports a few lines rather than what it
/// holds, which is what lets the dialog be smaller than its content and scroll
/// instead of crushing it.
///
/// The hint is asked to follow the content, but do not rely on it doing so from a
/// constructor: a scroll area caches that hint against the size its widget
/// currently has, and before the first layout that is not the size the widget
/// wants. Measured on the export dialog, the hint came back 486px against content
/// that needed 966, so what a dialog built on this opens at is whatever
/// openAtSize() was given, not its content's height.
inline QScrollArea* inScrollArea(QWidget* content, QWidget* parent = nullptr)
{
    auto* area = new QScrollArea(parent);
    area->setWidget(content);
    area->setWidgetResizable(true);
    area->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    area->setFrameShape(QFrame::NoFrame);
    // Vertical only. Every wrapping label in here reflows to the width it is
    // given, so a horizontal bar would not be content needing more width, it
    // would be a sign the width was wrong.
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

#ifdef Q_OS_HARMONY
    // Dragging inside the viewport does not scroll it on this platform -- verified
    // on the emulator, where a swipe across the export options left the view
    // exactly where it was -- so without this the only way to reach the content
    // below the fold is the scrollbar, which is a few pixels wide and no way to
    // ask anyone to work a tablet. The objection to QScroller elsewhere in this
    // codebase is that it fights QListView's and QTreeWidget's own touch
    // handling; a plain QScrollArea has none to fight.
    QScroller::grabGesture(area->viewport(), QScroller::LeftMouseButtonGesture);
#endif

    return area;
}

}  // namespace DialogSizing

#endif  // DIALOGSIZING_H
