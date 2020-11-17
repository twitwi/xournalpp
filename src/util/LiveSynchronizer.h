

#pragma once

#include <memory>
#include "undo/UndoRedoHandler.h"
#include "model/DocumentListener.h"
#include "model/PageListener.h"


class Control;
class SendJob;

class LiveSynchronizer :
        public DocumentListener,
        public PageListener,
        public UndoRedoListener {

public:
    LiveSynchronizer(Control *control);


    // DocumentListener
public:
    virtual void documentChanged(DocumentChangeType type);
    virtual void pageSizeChanged(size_t page);
    virtual void pageChanged(size_t page);
    virtual void pageInserted(size_t page);
    virtual void pageDeleted(size_t page);
    virtual void pageSelected(size_t page);

    // PageListener
public:
    virtual void rectChanged(Rectangle<double>& rect);
    virtual void rangeChanged(Range& range);
    virtual void elementChanged(Element* elem);
    virtual void pageChanged();

    // UndoRedoListener
public:
    virtual void undoRedoChanged();
    virtual void undoRedoPageChanged(PageRef page);

protected:
    friend SendJob;

    void sendPageUpdate();
    void maybeSendPageUpdate();
    bool scheduleJob();
    Control* control;
    size_t currentPage;
    bool registeredUndoRedoListener;
};


