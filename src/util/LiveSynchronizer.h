

#pragma once

#include <memory>

class UndoAction;
using UndoActionPtr = std::unique_ptr<UndoAction>;
class Control;

class LiveSynchronizer {

public:
    LiveSynchronizer(Control *control);

    void handle(const char* what, const UndoAction& action, Control* control);
    void handlePtr(const char* what, UndoActionPtr& action, Control* control);

private:
    Control* control;
};


