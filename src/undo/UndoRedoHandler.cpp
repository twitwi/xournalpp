
#include "UndoRedoHandler.h"

#include <algorithm>
#include <cinttypes>

#include "control/Control.h"

#include "XojMsgBox.h"
#include "config.h"
#include "i18n.h"

#include "AddUndoAction.h"
#include "ColorUndoAction.h"
#include "InsertUndoAction.h"
#include "serializing/BinObjectEncoding.h"
#include "serializing/HexObjectEncoding.h"
#include "serializing/ObjectInputStream.h"
#include "serializing/ObjectOutputStream.h"
#include "model/Text.h"
#include "model/TexImage.h"
#include "model/Image.h"
#include "model/Stroke.h"
#include "control/tools/EditSelection.h"
#include "gui/XournalView.h"

template <typename T>
T* GetPtr(T* ptr) {
    return ptr;
}

template <typename T>
T* GetPtr(std::unique_ptr<T> ptr) {
    return ptr.get();
}

template <typename PtrType>
inline void printAction(PtrType& action) {
    if (action) {
        //g_message("%" PRIu64 " / %s", static_cast<uint64_t>(GetPtr(action)), action->getClassName());
        g_message("%" PRIu64 " / %s", (uint64_t)(action.get()), action->getClassName().c_str());
    } else {
        g_message("(null)");
    }
}

template <typename PtrType>
inline void printUndoList(std::deque<PtrType>& list) {
    for (auto&& action: list) {
        printAction(action);
    }
}

#ifdef UNDO_TRACE
constexpr bool UNDO_TRACE = true;
#else
constexpr bool UNDO_TRACE = false;
#endif

void UndoRedoHandler::printContents() {
    if constexpr (UNDO_TRACE)  // NOLINT
    {
        g_message("redoList");             // NOLINT
        printUndoList(this->redoList);     // NOLINT
        g_message("undoList");             // NOLINT
        printUndoList(this->undoList);     // NOLINT
        g_message("savedUndo");            // NOLINT
        if (this->savedUndo)               // NOLINT
        {                                  // NOLINT
            //printAction(this->savedUndo);  // NOLINT
        }                                  // NOLINT
    }
}

void handle(const char* what, const UndoAction& action, Control* control) {

    // one small problem is that pasting moves the elements at the center

    auto* ap = &action;
    g_message(what);
    g_message(action.getClassName().c_str());
    if (auto a = dynamic_cast<const ColorUndoAction*>(ap)) {
        g_message("OK COLOR");

        FILE* pFile = fopen(".xournal-test-binary.bin5", "rb");
        //if (pFile==NULL) {fputs ("File error",stderr); exit (1);}
        // I'd rather crash

        // obtain file size:
        fseek(pFile , 0 , SEEK_END);
        long lSize = ftell(pFile);
        rewind(pFile);

        // allocate memory to contain the whole file:
        char* buffer = (char*) malloc (sizeof(char)*lSize);
        if (buffer == NULL) {fputs ("Memory error",stderr); exit (2);}

        // copy the file into the buffer:
        size_t result = fread(buffer, 1, lSize, pFile);
        if (result != lSize) {fputs ("Reading error",stderr); exit (3);}

        ObjectInputStream in;
        if (in.read(buffer, lSize)) {

            //control->getDocument()->lock();
            int pageNr = control->getCurrentPageNo();
            PageRef page = control->getCurrentPage();
            MainWindow* win = control->getWindow();
            XojPageView* view = win->getXournal()->getViewFor(pageNr);
            auto undoRedo = control->getUndoRedoHandler();
            auto selection = new EditSelection(undoRedo, page, view);

            win->getXournal()->setSelection(selection);

            std::unique_ptr<Element> element;
            string version = in.readString();
            if (version != PROJECT_STRING) {
                g_warning("Sync from Xournal Version %s to Xournal Version %s", version.c_str(), PROJECT_STRING);
            }

            selection->readSerialized(in);
            //control->getDocument()->unlock();

            int count = in.readInt();
            Layer* layer = page->getSelectedLayer();
            auto syncAddUndoAction = std::make_unique<AddUndoAction>(page, false);

            for (int i = 0; i < count; i++) {
                string name = in.getNextObjectName();
                element.reset();

                if (name == "Stroke") {
                    element = std::make_unique<Stroke>();
                } else if (name == "Image") {
                    element = std::make_unique<Image>();
                } else if (name == "TexImage") {
                    element = std::make_unique<TexImage>();
                } else if (name == "Text") {
                    element = std::make_unique<Text>();
                } else {
                    throw InputStreamException(FS(FORMAT_STR("Get unknown object {1}") % name), __FILE__, __LINE__);
                }

                element->readSerialized(in);

                syncAddUndoAction->addElement(layer, element.get(), layer->indexOf(element.get()));
                // Todo: unique_ptr
                selection->addElement(element.release(), Layer::InvalidElementIndex);
            }
            g_message("ADD UNDO ACTION");
            undoRedo->addUndoAction(std::move(syncAddUndoAction));

            g_message("CLEAR SEL");
            control->clearSelection();
            //selection->moveSelection(0, 0);
            //selection->mouseUp();
            //win->getXournal()->repaintSelection();
            control->clearSelection(); // causes a refresh, ok

            //control->clipboardPasteXournal(in); // why not refreshing???
            //control->clearSelection(); // incurs a refresh, ok
            //control->getWindow()->getXournal()->getSelection();
            // this->page->fireElementChanged(this->element);
        }
        fclose (pFile);
        free (buffer);

    } else if (auto a = dynamic_cast<const InsertUndoAction*>(ap)) {
        g_message("OK INSERT");
        auto e = a->__element();

        ObjectOutputStream out(new BinObjectEncoding());
        out.writeString(PROJECT_STRING);

        int pageNr = control->getCurrentPageNo();
        MainWindow* win = control->getWindow();
        XojPageView* view = win->getXournal()->getViewFor(pageNr);
        PageRef page = control->getCurrentPage();
        auto sel = new EditSelection(control->getUndoRedoHandler(), page, view);
        sel->addElement(e);
        sel->serialize(out);

        //auto view = control->getWindow()->getXournal()->getViewFor(0);
        //EditSelection sel(control->getUndoRedoHandler(), e, (XojPageView*)NULL, control->getCurrentPage());
        //sel.addElement(e);
        //auto sel = control->getWindow()->getXournal()->getSelection();
        //sel->addElement(e);
        //sel->serialize(out);
        //char tmp[100048]="";
        //strcpy(tmp, g_string_free(out.getStr(), FALSE));
        auto* o = out.getStr();
        g_message("%d", o->len); // TODO should then g_free()

        FILE * fp = fopen(".xournal-test-binary.bin", "w");
    	fwrite(o->str, 1, o->len, fp);
    	fclose(fp);
    }
    g_message("---- ----");
}

void handlePtr(const char* what, UndoActionPtr& action, Control* control) {
    handle(what, *action.get(), control);
}

UndoRedoHandler::UndoRedoHandler(Control* control): control(control) {}

UndoRedoHandler::~UndoRedoHandler() { clearContents(); }

void UndoRedoHandler::clearContents() {
#ifdef UNDO_TRACE
    for (auto const& undoAction: this->undoList) {
        g_message("clearContents()::Delete UndoAction: %" PRIu64 " / %s", (size_t)*undoAction,
                  undoAction.getClassName());
    }
#endif  // UNDO_TRACE

    undoList.clear();
    clearRedo();

    this->savedUndo = nullptr;
    this->autosavedUndo = nullptr;

    printContents();
}

void UndoRedoHandler::clearRedo() {
#ifdef UNDO_TRACE
    for (auto const& undoAction: this->redoList) {
        g_message("clearRedo()::Delete UndoAction: %" PRIu64 " / %s", (size_t)&undoAction, undoAction.getClassName());
    }
#endif
    redoList.clear();
    printContents();
}

void UndoRedoHandler::undo() {
    if (this->undoList.empty()) {
        return;
    }

    g_assert_true(this->undoList.back());

    auto& undoAction = *this->undoList.back();
    //handle("UNDOING", undoAction, control);

    this->redoList.emplace_back(std::move(this->undoList.back()));
    this->undoList.pop_back();

    Document* doc = control->getDocument();
    doc->lock();
    bool undoResult = undoAction.undo(this->control);
    doc->unlock();

    if (!undoResult) {
        string msg = FS(_F("Could not undo \"{1}\"\n"
                           "Something went wrong… Please write a bug report…") %
                        undoAction.getText());
        XojMsgBox::showErrorToUser(control->getGtkWindow(), msg);
    }

    fireUpdateUndoRedoButtons(undoAction.getPages());

    printContents();
}

void UndoRedoHandler::redo() {
    if (this->redoList.empty()) {
        return;
    }

    g_assert_true(this->redoList.back());

    UndoAction& redoAction = *this->redoList.back();
    //handle("REDOING", redoAction, control);

    this->undoList.emplace_back(std::move(this->redoList.back()));
    this->redoList.pop_back();

    Document* doc = control->getDocument();
    doc->lock();
    bool redoResult = redoAction.redo(this->control);
    doc->unlock();

    if (!redoResult) {
        string msg = FS(_F("Could not redo \"{1}\"\n"
                           "Something went wrong… Please write a bug report…") %
                        redoAction.getText());
        XojMsgBox::showErrorToUser(control->getGtkWindow(), msg);
    }

    fireUpdateUndoRedoButtons(redoAction.getPages());

    printContents();
}

auto UndoRedoHandler::canUndo() -> bool { return !this->undoList.empty(); }

auto UndoRedoHandler::canRedo() -> bool { return !this->redoList.empty(); }

/**
 * Adds an undo Action to the list, or if nullptr does nothing
 */
void UndoRedoHandler::addUndoAction(UndoActionPtr action) {
    if (!action) {
        return;
    }
    
    handlePtr("DONE", action, control);


    this->undoList.emplace_back(std::move(action));
    clearRedo();
    fireUpdateUndoRedoButtons(this->undoList.back()->getPages());

    printContents();
}

void UndoRedoHandler::addUndoActionBefore(UndoActionPtr action, UndoAction* before) {
    auto iter = std::find_if(begin(this->undoList), end(this->undoList),
                             [before](UndoActionPtr const& smtr_ptr) { return (smtr_ptr.get() == before); });

    if (iter == end(this->undoList)) {
        addUndoAction(std::move(action));
        return;
    }
    this->undoList.emplace(iter, std::move(action));
    clearRedo();
    fireUpdateUndoRedoButtons(this->undoList.back()->getPages());

    printContents();
}

auto UndoRedoHandler::removeUndoAction(UndoAction* action) -> bool {
    auto iter = std::find_if(begin(this->undoList), end(this->undoList),
                             [action](UndoActionPtr const& smtr_ptr) { return smtr_ptr.get() == action; });
    if (iter == end(this->undoList)) {
        return false;
    }
    this->undoList.erase(iter);
    clearRedo();
    fireUpdateUndoRedoButtons(action->getPages());
    return true;
}

auto UndoRedoHandler::undoDescription() -> string {
    if (!this->undoList.empty()) {
        UndoAction& a = *this->undoList.back();
        if (!a.getText().empty()) {
            string txt = _("Undo: ");
            txt += a.getText();
            return txt;
        }
    }
    return _("Undo");
}

auto UndoRedoHandler::redoDescription() -> string {
    if (!this->redoList.empty()) {
        UndoAction& a = *this->redoList.back();
        if (!a.getText().empty()) {
            string txt = _("Redo: ");
            txt += a.getText();
            return txt;
        }
    }
    return _("Redo");
}

void UndoRedoHandler::fireUpdateUndoRedoButtons(const vector<PageRef>& pages) {
    for (auto&& undoRedoListener: this->listener) {
        undoRedoListener->undoRedoChanged();
    }

    for (PageRef page: pages) {
        if (!page) {
            continue;
        }

        for (auto&& undoRedoListener: this->listener) {
            undoRedoListener->undoRedoPageChanged(page);
        }
    }
}

void UndoRedoHandler::addUndoRedoListener(UndoRedoListener* listener) { this->listener.emplace_back(listener); }

auto UndoRedoHandler::isChanged() -> bool {
    if (this->undoList.empty()) {
        return this->savedUndo;
    }

    return this->savedUndo != this->undoList.back().get();
}

auto UndoRedoHandler::isChangedAutosave() -> bool {
    if (this->undoList.empty()) {
        return this->autosavedUndo;
    }
    return this->autosavedUndo != this->undoList.back().get();
}

void UndoRedoHandler::documentAutosaved() {
    this->autosavedUndo = this->undoList.empty() ? nullptr : this->undoList.back().get();
}

void UndoRedoHandler::documentSaved() {
    this->savedUndo = this->undoList.empty() ? nullptr : this->undoList.back().get();
}
