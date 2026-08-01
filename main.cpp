#include <QtWidgets/QApplication>
#include <QtGui/QIcon>
#include "color_bottle_game.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const QIcon appIcon(QStringLiteral(":/resources/hola.ico"));
    if (!appIcon.isNull())
        app.setWindowIcon(appIcon);

    ColorBottleGame game;
    if (!appIcon.isNull())
        game.setWindowIcon(appIcon);
    game.show();

    return app.exec();
}
