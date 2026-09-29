// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 David Eichendorf and FileClump contributors
#include <QApplication>
#include <QTimer>

#include "MainWindow.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("FileClump"));
    QApplication::setOrganizationName(QStringLiteral("FileClump"));
    QApplication::setDesktopFileName(QStringLiteral("fileclump"));

    MainWindow w;
    w.show();

    // Optional: path to scan immediately, e.g. `fileclump /home`
    const QStringList args = QApplication::arguments();
    if (args.size() > 1) {
        const QString path = args.at(1);
        QTimer::singleShot(0, &w, [&w, path] { w.startScan(path); });
    }
    return app.exec();
}
