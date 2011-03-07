#include "fantrackercontroller.h"
#include "fantrackerview.h"

//#include "sawe/project.h"
#include "ui_mainwindow.h"
#include "ui/mainwindow.h"
#include "tools/support/fantrackerfilter.h"
#include "tfr/cepstrum.h"

namespace Tools {

    FanTrackerController::FanTrackerController(FanTrackerView* view, RenderView* render_view)
{
    render_view_ = render_view;
    view_ = view;
    setupGui();
}


void FanTrackerController::
        setupGui()
{
    ::Ui::MainWindow* ui = render_view_->model->project()->mainWindow()->getItems();

    TaskTimer tt("Cepstrum peak filter is bound \n");

    connect(ui->actionFanTracker, SIGNAL(triggered()), SLOT(receiveFanTracker()));
    connect(render_view_, SIGNAL(painting()), view_, SLOT(draw()));

}


void FanTrackerController::
        receiveFanTracker()
{

    Sawe::Project* project_ = render_view_->model->project();

    project_->appendOperation( Signal::pOperation(new Tools::Support::FanTrackerFilter()) );

    render_view_->userinput_update();
    TaskTimer tt("Cepstrum peak filter is applied \n");

}

} // namespace Tools
