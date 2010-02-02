#ifndef DISPLAYWIDGET_H
#define DISPLAYWIDGET_H

#include <QGLWidget>
#include "wavelettransform.h"
#include <boost/shared_ptr.hpp>
#include <TAni.h>

class MouseControl
{
private:
  float lastx;
  float lasty;
  bool down;
  unsigned int hold;
  
public:
  MouseControl(): down( false ), hold( 0 ){};
  
  float deltaX( float x );
  float deltaY( float y );
  
  bool worldPos(GLdouble &ox, GLdouble &oy);
  static bool worldPos(GLdouble x, GLdouble y, GLdouble &ox, GLdouble &oy);
  
  bool isDown(){return down;}
  bool isTouched();
  int getHold(){return hold;}
  
  void press( float x, float y );
  void update( float x, float y );
  void release();
  void touch(){hold = 0;}
  void untouch(){hold++;}
};

struct MyVector{
  float x, y, z;
};

class DisplayWidget : public QGLWidget
{
public:
    DisplayWidget( boost::shared_ptr<WavelettTransform> wavelett, int timerInterval=0 );
    ~DisplayWidget();
  static int lastKey;

  enum Yscale {
      Yscale_Linear,
      Yscale_ExpLinear,
      Yscale_LogLinear,
      Yscale_LogExpLinear
  } yscale;
  floatAni orthoview;

protected:
  virtual void initializeGL();
  virtual void resizeGL( int width, int height );
  virtual void paintGL();

  virtual void mousePressEvent ( QMouseEvent * e );
  virtual void mouseReleaseEvent ( QMouseEvent * e );
  virtual void wheelEvent ( QWheelEvent *event );
  virtual void mouseMoveEvent ( QMouseEvent * e );
  virtual void timeOut();
  void timerEvent( QTimerEvent *te);

protected slots:
  virtual void timeOutSlot();

private:
  boost::shared_ptr<WavelettTransform> wavelett;
  float px, py, pz,
        rx, ry, rz,
        qx, qy, qz;
  int prevX, prevY, targetQ;
  
  GLint viewport[4];
  GLdouble modelMatrix[16];
  GLdouble projectionMatrix[16];
  
  MyVector v1, v2;
  MyVector selection[2];
  bool selecting;

  void drawArrows();
  void drawColorFace();
  void drawWaveform(boost::shared_ptr<Waveform> waveform);
  void drawWavelett();
  void drawSelection();
  
  MouseControl leftButton;
  MouseControl rightButton;
  MouseControl middleButton;
};

#endif // DISPLAYWIDGET_H

