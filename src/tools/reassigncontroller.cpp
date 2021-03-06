#include "reassigncontroller.h"

// Sonic AWE
#include "sawe/project.h"
#ifdef USE_CUDA
#include "filters/reassign.h"
#include "filters/ridge.h"
#endif
#include "ui_mainwindow.h"
#include "ui/mainwindow.h"

namespace Tools
{

Reassign::
        Reassign( Sawe::Project* project )
            :
            project_(project)
{
    setupGui();
}


void Reassign::
        setupGui()
{
    Ui::MainWindow* ui = project_->mainWindow()->getItems();

    connect(ui->actionTonalizeFilter, SIGNAL(triggered()), SLOT(receiveTonalizeFilter()));
    connect(ui->actionReassignFilter, SIGNAL(triggered()), SLOT(receiveReassignFilter()));
}


void Reassign::
        receiveTonalizeFilter()
{
#ifdef USE_CUDA
    Signal::pOperation tonalize( new Filters::Tonalize());
    project_->appendOperation( tonalize );
#endif
}


void Reassign::
        receiveReassignFilter()
{
#ifdef USE_CUDA
    Signal::pOperation reassign( new Filters::Reassign());
    project_->appendOperation( reassign );
#endif
}

} // namespace Tools
