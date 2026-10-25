#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QStyleFactory>

#include "MainWindow.h"

int main(int argc, char* argv[]) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  QGuiApplication::setAttribute(Qt::AA_DisableWindowContextHelpButton);
#endif
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
      Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

  QApplication app(argc, argv);

  QApplication::setOrganizationName("NetTool");
  QApplication::setApplicationName("Network Test Tool");
  app.setWindowIcon(QIcon(":/network_tool.png"));

  auto translator = new QTranslator();
  if (translator->load(QStringLiteral(":/network_tool.qm")))
    app.installTranslator(translator);

  MainWindow w;
  w.show();

  return app.exec();
}
