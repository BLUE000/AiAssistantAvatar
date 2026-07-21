#include <QApplication>
#include "ui/avatar_skin_builder_dialog.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("AvatarSkinBuilder");

    QString editingSkinName = "";
    if (argc > 1) {
        editingSkinName = QString::fromLocal8Bit(argv[1]);
    }

    AvatarSkinBuilderDialog dialog(nullptr, editingSkinName);
    dialog.show();

    return app.exec();
}
