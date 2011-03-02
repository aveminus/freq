#include "graphicsview.h"
#include "renderview.h"

#include <TaskTimer.h>
#include <demangle.h>

#include <QEvent>
#include <QTimerEvent>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QGraphicsProxyWidget>

//#define DEBUG_EVENTS
#define DEBUG_EVENTS if(0)

namespace Tools
{

GraphicsView::
        GraphicsView(QGraphicsScene* scene)
    :   QGraphicsView(scene),
        pressed_control_(false)
{
    setWindowTitle(tr("Sonic AWE"));
    //setRenderHints(QPainter::SmoothPixmapTransform);
    //setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
    setMouseTracking( true );

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);


    setRenderHints(renderHints() | QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

    QGraphicsProxyWidget* toolProxy = new QGraphicsProxyWidget();
    layout_widget_ = new QWidget();

    // Make all child widgets occupy the entire area
    layout_widget_->setLayout(new QBoxLayout(QBoxLayout::TopToBottom));
    layout_widget_->layout()->setMargin(0);
    layout_widget_->layout()->setSpacing(0);

    toolProxy->setWidget( layout_widget_ );
    toolProxy->setWindowFlags( Qt::FramelessWindowHint | Qt::WindowSystemMenuHint );
    toolProxy->setZValue( -1e30 );
    layout_widget_->setWindowOpacity( 0 );
    scene->addItem( toolProxy );
}


GraphicsView::
        ~GraphicsView()
{
    if (scene())
        delete scene();
}


bool GraphicsView::
        event ( QEvent * e )
{
    DEBUG_EVENTS TaskTimer tt("GraphicsView event %s %d", vartype(*e).c_str(), e->isAccepted());
    bool r = QGraphicsView::event(e);
    DEBUG_EVENTS TaskTimer("GraphicsView event %s info %d %d", vartype(*e).c_str(), r, e->isAccepted()).suppressTiming();
    return r;
}


bool GraphicsView::
        eventFilter(QObject* o, QEvent* e)
{
    DEBUG_EVENTS TaskTimer tt("GraphicsView eventFilter %s %s %d", vartype(*o).c_str(), vartype(*e).c_str(), e->isAccepted());
    bool r = QGraphicsView::eventFilter(o, e);
    DEBUG_EVENTS TaskTimer("GraphicsView eventFilter %s %s info %d %d", vartype(*o).c_str(), vartype(*e).c_str(), r, e->isAccepted()).suppressTiming();
    return r;
}


void GraphicsView::timerEvent(QTimerEvent *e){
    DEBUG_EVENTS TaskTimer tt("GraphicsView timerEvent %s %d", vartype(*e).c_str(), e->isAccepted());
    QGraphicsView::timerEvent(e);
    DEBUG_EVENTS TaskTimer("GraphicsView timerEvent %s info %d", vartype(*e).c_str(), e->isAccepted()).suppressTiming();
}

void GraphicsView::childEvent(QChildEvent *e){
    DEBUG_EVENTS TaskTimer tt("GraphicsView childEvent %s %d", vartype(*e).c_str(), e->isAccepted());
    QGraphicsView::childEvent(e);
    DEBUG_EVENTS TaskTimer("GraphicsView childEvent %s info %d", vartype(*e).c_str(), e->isAccepted()).suppressTiming();
}

void GraphicsView::customEvent(QEvent *e){
    DEBUG_EVENTS TaskTimer tt("GraphicsView customEvent %s %d", vartype(*e).c_str(), e->isAccepted());
    QGraphicsView::customEvent(e);
    DEBUG_EVENTS TaskTimer("GraphicsView customEvent %s info %d", vartype(*e).c_str(), e->isAccepted()).suppressTiming();
}

void GraphicsView::keyPressEvent(QKeyEvent *event) {
    if (event->key() != Qt::Key_Control)
    {
        event->setAccepted( false );
        return;
    }

    pressed_control_ = true;

    unsigned u = toolWindows();
    for (unsigned i=0; i<u; ++i)
    {
        Support::ToolSelector* ts = toolSelector(i);
        ts->temp_tool = ts->currentTool();
        ts->setCurrentTool( ts->default_tool, true );
    }
}

void GraphicsView::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() != Qt::Key_Control)
    {
        event->setAccepted( false );
        return;
    }

    if (!pressed_control_ )
        return;

    pressed_control_ = false;

    unsigned u = toolWindows();
    for (unsigned i=0; i<u; ++i)
    {
        Support::ToolSelector* ts = toolSelector(i);
        ts->setCurrentTool( ts->temp_tool, true );
        ts->temp_tool = 0;
    }
}

void GraphicsView::mousePressEvent( QMouseEvent* e )
{
    DEBUG_EVENTS TaskTimer tt("GraphicsView mousePressEvent %s %d", vartype(*e).c_str(), e->isAccepted());
    QGraphicsView::mousePressEvent(e);
    DEBUG_EVENTS TaskTimer("GraphicsView mousePressEvent %s info %d", vartype(*e).c_str(), e->isAccepted()).suppressTiming();
}

void GraphicsView::mouseMoveEvent(QMouseEvent *e)
{
    if (!hasFocus())
    {
        setFocus();
    }
    DEBUG_EVENTS TaskTimer tt("GraphicsView mouseMoveEvent %s %d", vartype(*e).c_str(), e->isAccepted());
    QGraphicsView::mouseMoveEvent(e);
    DEBUG_EVENTS TaskTimer("GraphicsView mouseMoveEvent %s info %d", vartype(*e).c_str(), e->isAccepted()).suppressTiming();
}

void GraphicsView::mouseReleaseEvent(QMouseEvent *e)
{
    DEBUG_EVENTS TaskTimer tt("GraphicsView mouseReleaseEvent %s %d", vartype(*e).c_str(), e->isAccepted());
    QGraphicsView::mouseReleaseEvent(e);
    DEBUG_EVENTS TaskTimer("GraphicsView mouseReleaseEvent %s info %d", vartype(*e).c_str(), e->isAccepted()).suppressTiming();
}

void GraphicsView::drawBackground(QPainter *painter, const QRectF &rect)
{
    QGraphicsView::drawBackground( painter, rect );
}


void GraphicsView::resizeEvent(QResizeEvent *event) {
    //float h = event->size().height();
    if (scene())
        scene()->setSceneRect(QRectF(0, 0, event->size().width(), event->size().height()));

    layout_widget_->resize( event->size() );
}


Support::ToolSelector* GraphicsView::
        toolSelector(int index)
{
    while (index >= layout_widget_->layout()->count())
    {
        QWidget* parent = new QWidget();
        parent->setLayout(new QVBoxLayout());
        parent->layout()->setMargin(0);
        parent->setWindowOpacity( 0 );

        Support::ToolSelector* tool_selector = new Support::ToolSelector( parent );
        tool_selector->setParent( parent );

        layout_widget_->layout()->addWidget( parent );
    }

    QWidget* parent = layout_widget_->layout()->itemAt(index)->widget();
    for (int i = 0; i<parent->children().count(); ++i)
    {
        Support::ToolSelector* t = dynamic_cast<Support::ToolSelector*>( parent->children().at(i) );
        if (t)
            return t;
    }

    throw std::logic_error("Support::ToolSelector* GraphicsView::toolSelector");
}


unsigned GraphicsView::
        toolWindows()
{
    return layout_widget_->layout()->count();
}


void GraphicsView::
        setLayoutDirection( QBoxLayout::Direction direction )
{
    QBoxLayout* bl = dynamic_cast<QBoxLayout*>(layout_widget_->layout());
    if (bl->direction() != direction)
    {
        bl->setDirection( direction );

        emit layoutChanged( direction );
    }
}


QBoxLayout::Direction GraphicsView::
        layoutDirection()
{
    QBoxLayout* bl = dynamic_cast<QBoxLayout*>(layout_widget_->layout());
    return bl->direction();
}


} // namespace Tools
