#ifndef KEYCAPTUREDIALOG_H
#define KEYCAPTUREDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QEvent>

class KeyCaptureDialog : public QDialog {
    Q_OBJECT

public:
    explicit KeyCaptureDialog(QWidget *parent = nullptr);
    QString getCapturedKeySequence() const { return capturedSequence; }

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;  // For capturing Tab key

private slots:
    void clearSequence();

private:
    QLabel *instructionLabel;
    QLabel *capturedLabel;
    QPushButton *clearButton;
    QPushButton *okButton;
    QPushButton *cancelButton;
    QString capturedSequence;
    
    void updateDisplay();
};

#endif // KEYCAPTUREDIALOG_H 