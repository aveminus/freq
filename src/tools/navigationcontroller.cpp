#include "navigationcontroller.h"

// Sonic AWE
#include "ui_mainwindow.h"
#include "ui/mainwindow.h"
#include "heightmap/render/renderer.h"
#include "tools/commands/movecameracommand.h"
#include "tools/commands/zoomcameracommand.h"
#include "tools/commands/rotatecameracommand.h"
#include "tools/support/renderviewinfo.h"
#include "tools/support/toolselector.h"

// Qt
#include <QMouseEvent>

// Todo remove
#include "sawe/project.h"
#include "toolfactory.h"

#include "demangle.h"

namespace Tools
{

using Support::RenderViewInfo;
using Support::RenderCamera;

NavigationController::
        NavigationController(RenderView* view, Sawe::Project* project, Support::ToolSelector* tool_selector)
            :
            _view(view),
            _project(project),
            _tool_selector(tool_selector),
            zoom_only_(false)
{
    connectGui();

    setAttribute( Qt::WA_DontShowOnScreen, true );
}


NavigationController::
        ~NavigationController()
{
    TaskTimer(__FUNCTION__).suppressTiming();
}


void NavigationController::
        receiveToggleNavigation(bool active)
{
    if (active || zoom_only_ == false)
        _tool_selector->setCurrentTool( this, active );
    if (active)
        zoom_only_ = false;
}


void NavigationController::
        receiveToggleZoom(bool active)
{
    if (active || zoom_only_ == true)
        _tool_selector->setCurrentTool( this, active );
    if (active)
        zoom_only_ = true;
}


void NavigationController::
        moveUp()
{
    moveCamera(0, 0.1f/_view->model->camera->zscale);
    _view->redraw();
}


void NavigationController::
        moveDown()
{
    moveCamera(0, -0.1f/_view->model->camera->zscale);
    _view->redraw();
}


void NavigationController::
        moveLeft()
{
    moveCamera(-0.1f/_view->model->camera->xscale, 0);
    _view->redraw();
}


void NavigationController::
        moveRight()
{
    moveCamera(0.1f/_view->model->camera->xscale, 0);
    _view->redraw();
}


void NavigationController::
        scaleUp()
{
    zoom( -2, ScaleZ );
    _view->redraw();
}


void NavigationController::
        scaleDown()
{
    zoom( 2, ScaleZ );
    _view->redraw();
}


void NavigationController::
        scaleLeft()
{
    zoom( 2, ScaleX );
    _view->redraw();
}


void NavigationController::
        scaleRight()
{
    zoom( -2, ScaleX );
    _view->redraw();
}


void NavigationController::
        rotateUp()
{
    rotateCamera( 0, 10 );
}


void NavigationController::
        rotateDown()
{
    rotateCamera( 0, -10 );
}


void NavigationController::
        rotateLeft()
{
    rotateCamera( 10, 0 );
}


void NavigationController::
        rotateRight()
{
    rotateCamera( -10, 0 );
}


void NavigationController::
        mousePressEvent ( QMouseEvent * e )
{
    //TaskTimer("NavigationController mousePressEvent %s %d", vartype(*e).c_str(), e->isAccepted()).suppressTiming();

    /*switch ( e->button() )
    {
        case Qt::LeftButton:
            if(' '==lastKey)
                selectionButton.press( e->x(), e->y() );
            else
                leftButton.press( e->x(), e->y() );
            //printf("LeftButton: Press\n");
            break;

        case Qt::MidButton:
            middleButton.press( e->x(), e->y() );
            //printf("MidButton: Press\n");
            break;

        case Qt::RightButton:
        {
            rightButton.press( e->x(), e->y() );
            //printf("RightButton: Press\n");
        }
            break;

        default:
            break;
    }*/

    if(isEnabled()) {
        if (e->buttons().testFlag(Qt::LeftButton))
            moveButton.press( e->x(), e->y() );

        if (e->buttons().testFlag(Qt::RightButton))
            rotateButton.press( e->x(), e->y() );

    }

//    if(leftButton.isDown() && rightButton.isDown())
//        selectionButton.press( e->x(), e->y() );

    _view->redraw();
}

void NavigationController::
        mouseReleaseEvent ( QMouseEvent * e )
{
    //TaskTimer("NavigationController mouseReleaseEvent %s %d", vartype(*e).c_str(), e->isAccepted()).suppressTiming();
    if ( !e->buttons().testFlag(Qt::LeftButton))
        moveButton.release();

    if ( !e->buttons().testFlag(Qt::RightButton))
    {
        scaleButton.release();
        rotateButton.release();
    }

    _view->redraw();
}


void NavigationController::
        wheelEvent ( QWheelEvent *e )
{
    static bool canScrollHorizontal = false;
    if( e->orientation() == Qt::Horizontal && 0 == e->modifiers())
        canScrollHorizontal = true;

    if (!canScrollHorizontal)
    {
        if (e->modifiers().testFlag(Qt::ControlModifier))
            zoom( e->delta(), ScaleX );
        else if (e->modifiers().testFlag(Qt::AltModifier))
            zoom( e->delta(), ScaleZ );
        else
            zoom( -e->delta(), Zoom );
    }
    else
    {
        if (e->modifiers().testFlag(Qt::AltModifier))
        {
            zoom( -e->delta(), Zoom );
        }
        else
        {
            bool success1, success2;
            float s = -0.125f;

            QPointF prev = e->pos();
            if( e->orientation() == Qt::Horizontal )
                prev.setX( prev.x() + s*e->delta() );
            else
                prev.setY( prev.y() + s*e->delta() );

            Heightmap::Position last = RenderViewInfo(_view->model).getPlanePos( prev, &success1);
            Heightmap::Position current = RenderViewInfo(_view->model).getPlanePos( e->pos(), &success2);
            if (success1 && success2)
            {
                const RenderCamera c = *_view->model->camera.read ();
                if (e->modifiers().testFlag(Qt::ControlModifier))
                    zoomCamera( 4*(current.time - last.time)*c.xscale / c.p[2],
                                4*(current.scale - last.scale)*c.zscale / c.p[2],
                                0 );
                else
                    moveCamera( last.time - current.time, last.scale - current.scale);
            }
        }
    }

    _view->redraw();
}


void NavigationController::
        zoom(int delta, ZoomMode mode)
{
    float ds = 0, dt = 0, dz = 0;
    switch(mode)
    {
    case Zoom: dz = delta; break;
    case ScaleX: dt = delta; break;
    case ScaleZ: ds = delta; break;
    }

    zoomCamera( dt, ds, dz);
}


void NavigationController::
        mouseMoveEvent ( QMouseEvent * e )
{
    // update currently not pressed mouse buttons
    mouseReleaseEvent(e);

    //TaskTimer("NavigationController mouseMoveEvent %s %d", vartype(*e).c_str(), e->isAccepted()).suppressTiming();
    RenderViewInfo r(_view->model);

    int x = e->x(), y = e->y();
//    TaskTimer tt("moving");

    if (scaleButton.isDown()) {
        // TODO scale selection
    }

    Ui::MouseControl *zoomCommand = 0, *rescaleCommand = 0, *rotateCommand = 0, *navigateCommand = 0;

    bool zoomAnyways = e->modifiers().testFlag(Qt::AltModifier) || e->modifiers().testFlag(Qt::ControlModifier);
    if (zoom_only_ || zoomAnyways)
    {
        if( rotateButton.isDown() )
            zoomCommand = &rotateButton;

        if( moveButton.isDown() )
            rescaleCommand = &moveButton;
    }
    else
    {
        if( rotateButton.isDown() )
            rotateCommand = &rotateButton;

        if( moveButton.isDown() )
            navigateCommand = &moveButton;
    }

    if (zoomCommand)
        zoomCamera( 0, 0, -zoomCommand->deltaX( x ) + zoomCommand->deltaY( y ) );

    if (rescaleCommand)
    {
        bool success1, success2;
        Heightmap::Position last = r.getPlanePos( QPointF(rescaleCommand->getLastx(), rescaleCommand->getLasty()), &success1);
        Heightmap::Position current = r.getPlanePos( e->localPos (), &success2);
        if (success1 && success2)
        {
            const RenderCamera c = *_view->model->camera.read ();
            zoomCamera( 4*(current.time - last.time)*c.xscale/c.p[2],
                        4*(current.scale - last.scale)*c.zscale/c.p[2],
                        0 );
        }
    }

    if (navigateCommand)
    {
        //Controlling the position with the left button.
        bool success1, success2;
        Heightmap::Position last = r.getPlanePos( QPointF(moveButton.getLastx(), moveButton.getLasty()), &success1);
        Heightmap::Position current = r.getPlanePos( e->localPos(), &success2);
        if (success1 && success2)
        {
            moveCamera( last.time - current.time, last.scale - current.scale);
        }
    }

    if (rotateCommand)
    {
        //Controlling the rotation with the right button.
        rotateCamera( rotateCommand->deltaX( x ), rotateCommand->deltaY( y ) );
    }


    //Updating the buttons
    moveButton.update(x, y);
    rotateButton.update(x, y);
    scaleButton.update(x, y);

    _view->redraw ();
}


void NavigationController::
        changeEvent(QEvent * event)
{
    if (event->type() == QEvent::EnabledChange)
    {
        if (!isEnabled())
            emit enabledChanged(isEnabled());
        else
        {
            one_action_at_a_time_->defaultAction()->setChecked(0!=parent());
        }
    }
}


void NavigationController::
        connectGui()
{
    Ui::SaweMainWindow* main = _project->mainWindow();
    Ui::MainWindow* ui = main->getItems();

    //connect(ui->actionToggleNavigationToolBox, SIGNAL(toggled(bool)), ui->toolBarOperation, SLOT(setVisible(bool)));
    //connect(ui->toolBarOperation, SIGNAL(visibleChanged(bool)), ui->actionToggleNavigationToolBox, SLOT(setChecked(bool)));

//    QList<QAction*> a = ui->toolBarOperation->actions();
//    QAction* first = 0;
//    //if (!a.empty()) first = a.first();
//    ui->toolBarOperation->insertAction(first, ui->actionActivateNavigation);
//    ui->toolBarOperation->insertAction(first, ui->actionZoom);
//    if (first) ui->toolBarOperation->insertSeparator(first);

    connect(ui->actionActivateNavigation, SIGNAL(toggled(bool)), this, SLOT(receiveToggleNavigation(bool)));
    connect(ui->actionZoom, SIGNAL(toggled(bool)), this, SLOT(receiveToggleZoom(bool)));
    connect(this, SIGNAL(enabledChanged(bool)), ui->actionActivateNavigation, SLOT(setChecked(bool)));
    connect(this, SIGNAL(enabledChanged(bool)), ui->actionZoom, SLOT(setChecked(bool)));

    one_action_at_a_time_.reset( new Ui::ComboBoxAction() );
    one_action_at_a_time_->decheckable( false );
    one_action_at_a_time_->addActionItem( ui->actionActivateNavigation );
    one_action_at_a_time_->addActionItem( ui->actionZoom );

    _tool_selector->default_tool = this;

    QList<QKeySequence> shortcuts = ui->actionActivateNavigation->shortcuts();
    shortcuts.push_back( Qt::Key_Escape );
    ui->actionActivateNavigation->setShortcuts( shortcuts );

    _tool_selector->setCurrentToolCommand( this );
    ui->actionActivateNavigation->setChecked(true);

    bindKeyToSlot( main, "Up", this, SLOT(moveUp()) );
    bindKeyToSlot( main, "Down", this, SLOT(moveDown()) );
    bindKeyToSlot( main, "Left", this, SLOT(moveLeft()) );
    bindKeyToSlot( main, "Right", this, SLOT(moveRight()) );
    bindKeyToSlot( main, "Ctrl+Up", this, SLOT(scaleUp()) );
    bindKeyToSlot( main, "Ctrl+Down", this, SLOT(scaleDown()) );
    bindKeyToSlot( main, "Ctrl+Left", this, SLOT(scaleLeft()) );
    bindKeyToSlot( main, "Ctrl+Right", this, SLOT(scaleRight()) );
    bindKeyToSlot( main, "Shift+Up", this, SLOT(rotateUp()) );
    bindKeyToSlot( main, "Shift+Down", this, SLOT(rotateDown()) );
    bindKeyToSlot( main, "Shift+Left", this, SLOT(rotateLeft()) );
    bindKeyToSlot( main, "Shift+Right", this, SLOT(rotateRight()) );
}


void NavigationController::bindKeyToSlot( QWidget* owner, const char* keySequence, const QObject* receiver, const char* slot )
{
    QAction* a = new QAction(owner);
    a->setShortcut(QString(keySequence));
    QObject::connect(a, SIGNAL(triggered()), receiver, slot);
    owner->addAction( a );
}


void NavigationController::
        moveCamera( float dt, float ds )
{
    Tools::Commands::pCommand cmd( new Tools::Commands::MoveCameraCommand(_view->model, dt, ds ));
    _project->commandInvoker()->invokeCommand( cmd );
}


void NavigationController::
        zoomCamera( float dt, float ds, float dz )
{
    Tools::Commands::pCommand cmd( new Tools::Commands::ZoomCameraCommand(_view->model, dt, ds, dz*0.005 ));
    _project->commandInvoker()->invokeCommand( cmd );
}


void NavigationController::
        rotateCamera( float dx, float dy )
{
    Tools::Commands::pCommand cmd( new Tools::Commands::RotateCameraCommand(_view->model, dx, dy ));
    _project->commandInvoker()->invokeCommand( cmd );
}


} // namespace Tools
