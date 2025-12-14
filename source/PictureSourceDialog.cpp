#include "PictureSourceDialog.h"
#include <QHBoxLayout>

PictureSourceDialog::PictureSourceDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Add Picture"));
    setModal(true);
    setMinimumWidth(280);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    
    // Title label
    QLabel *titleLabel = new QLabel(tr("Choose picture source:"), this);
    titleLabel->setStyleSheet("font-weight: bold; font-size: 13px;");
    mainLayout->addWidget(titleLabel);
    
    mainLayout->addSpacing(8);
    
    // Load from disk button
    loadFromDiskButton = new QPushButton(tr("📁  Load from disk..."), this);
    loadFromDiskButton->setMinimumHeight(40);
    loadFromDiskButton->setCursor(Qt::PointingHandCursor);
    connect(loadFromDiskButton, &QPushButton::clicked, this, &PictureSourceDialog::onLoadFromDisk);
    mainLayout->addWidget(loadFromDiskButton);
    
    // Use existing button
    useExistingButton = new QPushButton(tr("🖼️  Use existing picture..."), this);
    useExistingButton->setMinimumHeight(40);
    useExistingButton->setCursor(Qt::PointingHandCursor);
    connect(useExistingButton, &QPushButton::clicked, this, &PictureSourceDialog::onUseExisting);
    mainLayout->addWidget(useExistingButton);
    
    mainLayout->addSpacing(8);
    
    // Cancel button
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    cancelButton = new QPushButton(tr("Cancel"), this);
    cancelButton->setMinimumWidth(80);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(cancelButton);
    
    mainLayout->addLayout(buttonLayout);
}

void PictureSourceDialog::onLoadFromDisk() {
    result = LoadFromDisk;
    accept();
}

void PictureSourceDialog::onUseExisting() {
    result = UseExisting;
    accept();
}

