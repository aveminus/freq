#ifndef SELECTIONMODEL_H
#define SELECTIONMODEL_H

#include "signal/operation.h"
#include <vector>

#include <QObject>

namespace Sawe {
    class Project;
}


namespace Tools
{
    class SelectionModel: public QObject
    {
        Q_OBJECT
    public:
        SelectionModel(Sawe::Project* p);
        virtual ~SelectionModel();

        enum SaveInside
        {
            SaveInside_FALSE,
            SaveInside_TRUE,
            SaveInside_UNCHANGED
        };

        Signal::pOperation current_selection_copy(SaveInside si = SaveInside_UNCHANGED);
        void               set_current_selection(Signal::pOperation o);
        const Signal::pOperation& current_selection() { return current_selection_; }

        Sawe::Project* project() { return project_; }

        // TODO How should 'all_selections' be used?
        // Should SelectionModel only describe one selection?
        std::vector<Signal::pOperation> all_selections;

    signals:
        void selectionChanged();

    private:
        Sawe::Project* project_;
        Signal::pOperation current_selection_;

        static Signal::pOperation copy_selection(Signal::pOperation, SaveInside si = SaveInside_UNCHANGED);

        template<typename T>
        static Signal::pOperation copy_selection_type(T*p, SaveInside si);
    };
} // namespace Tools

#endif // SELECTIONMODEL_H