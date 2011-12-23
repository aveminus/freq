#include "sawe/project_header.h"
#include <QtCore/QString>
#include <QtTest/QtTest>
#include <QtCore/QCoreApplication>
#include <iostream>
#include <QGLWidget> // libsonicawe uses gl, so we need to include a gl header in this project as well
#include <QTimer>
#include <QImage>
#include <QPainter>
#include <QRgb>

#include "sawe/application.h"
#include "sawe/project.h"
#include "tools/renderview.h"

using namespace std;
using namespace Tfr;
using namespace Signal;

class OpenGui : public QObject
{
    Q_OBJECT

public:
    OpenGui();

private slots:
    void initTestCase();
    void cleanupTestCase();
    void openGui();
    void compareImages();

protected slots:
    void setInitialized();
    void saveImage();

private:
    int n;
    Sawe::Project* p;
    QImage resultImage, goldImage;

    QString resultFileName, goldFileName, diffFileName;

    bool initialized;
};


OpenGui::
        OpenGui()
            :
            initialized(false)
{
#ifdef USE_CUDA
    resultFileName = "opengui-result-cuda.png";
    goldFileName = "opengui-gold-cuda.png";
    diffFileName = "opengui-diff-cuda.png";
#else
    resultFileName = "opengui-result-cpu.png";
    goldFileName = "opengui-gold-cpu.png";
    diffFileName = "opengui-diff-cpu.png";
#endif
}


void OpenGui::
        initTestCase()
{
    p = Sawe::Application::global_ptr()->slotNew_recording( -1 ).get();
    connect( p->tools().render_view(), SIGNAL(postPaint()), SLOT(setInitialized()));
    QFile::remove(resultFileName);
}


void OpenGui::
        setInitialized()
{
    if (initialized)
        return;

    initialized = true;

    QTimer::singleShot(0, this, SLOT(saveImage()));
}


void OpenGui::
        saveImage()
{
    TaskTimer ti("saveImage");

    QGLWidget* glwidget = p->tools().render_view()->glwidget;
    glwidget->makeCurrent();

    QPixmap pixmap(p->mainWindowWidget()->size());
    QGL::setPreferredPaintEngine(QPaintEngine::OpenGL);
    QPainter painter(&pixmap);
    p->mainWindowWidget()->render(&painter);
    QImage glimage = glwidget->grabFrameBuffer();

    QPoint p2 = p->mainWindow()->centralWidget()->pos();
    painter.drawImage(p2 + QPoint(1,1), glimage);

    resultImage = pixmap.toImage();
    resultImage.save(resultFileName);

    Sawe::Application::global_ptr()->slotClosed_window( p->mainWindowWidget() );
}


void OpenGui::
        compareImages()
{
    QImage openguigold(goldFileName);

    QCOMPARE( openguigold.size(), resultImage.size() );

    QImage diffImage( openguigold.size(), openguigold.format() );

    double diff = 0;
    for (int y=0; y<openguigold.height(); ++y)
    {
        for (int x=0; x<openguigold.width(); ++x)
        {
            float gold = QColor(openguigold.pixel(x,y)).lightnessF();
            float result = QColor(resultImage.pixel(x,y)).lightnessF();
            diff += std::fabs( gold - result );
            float hue = fmod(10 + (gold - result)*0.5f, 1.f);
            diffImage.setPixel( x, y, QColor::fromHsvF( hue, std::min(1.f, gold - result == 0 ? 0 : 0.25f+0.75f*std::fabs( gold - result )), 0.25f+0.75f*gold ).rgba() );
        }
    }

    diffImage.save( diffFileName );

    double limit = 10.;
    if (! (diff < limit))
    {
        TaskInfo("OpenGui::compareImages, ligtness difference between '%s' and '%s' was %g, tolerated max difference is %g. Saved diff image in '%s'",
                 goldFileName.toStdString().c_str(), resultFileName.toStdString().c_str(),
                 diff, limit, diffFileName.toStdString().c_str() );
    }

    QVERIFY(diff < limit);
}


void OpenGui::
        cleanupTestCase()
{
}


void OpenGui::
        openGui()
{
    TaskTimer ti("openGui");

    Sawe::Application::global_ptr()->exec();
}

// expanded QTEST_MAIN but for Sawe::Application
int main(int argc, char *argv[])
{
    Sawe::Application application(argc, argv, false);
    QTEST_DISABLE_KEYPAD_NAVIGATION
    OpenGui tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "opengui.moc"
