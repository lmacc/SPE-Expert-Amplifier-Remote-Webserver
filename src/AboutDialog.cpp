#include "AboutDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("About SPE Remote"));

    auto* layout = new QVBoxLayout(this);

    auto* screenshot = new QLabel(this);
    screenshot->setPixmap(QPixmap(QStringLiteral(":/SPE_Remote.png")));
    screenshot->setAlignment(Qt::AlignCenter);
    layout->addWidget(screenshot);

    auto* text = new QLabel(this);
    text->setTextFormat(Qt::RichText);
    text->setOpenExternalLinks(true);
    text->setWordWrap(true);
    text->setText(QStringLiteral(
        "<h3>SPE Expert Amplifier Remote Webserver</h3>"
        "<p>Native Qt6 / C++20 WebSocket amplifier bridge for SPE Expert "
        "linear amplifiers, built against the SPE Application "
        "Programmer's Guide.</p>"
        "<p><a href=\"https://github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver\">"
        "github.com/lmacc/SPE-Expert-Amplifier-Remote-Webserver</a></p>"));
    layout->addWidget(text);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);
}
