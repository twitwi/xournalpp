
#include "LiveSynchronizer.h"

#include "i18n.h"

#include "undo/UndoAction.h"
#include "undo/AddUndoAction.h"
#include "undo/ColorUndoAction.h"
#include "undo/InsertUndoAction.h"
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
#include "gui/MainWindow.h"
#include "control/Control.h"

#include <libsoup/soup.h>
SoupWebsocketConnection* shareConn = NULL;
#include "undo/PageLayerPosEntry.h"


void LiveSynchronizer::handle(const char* what, const UndoAction& action, Control* control) {

    auto* ap = &action;
    g_message(what);
    g_message(action.getClassName().c_str());
    if (auto a = dynamic_cast<const ColorUndoAction*>(ap)) {
        //g_message("INSERT %s: %x", what, e);
    } else if (auto a = dynamic_cast<const AddUndoAction*>(ap)) {

        ObjectOutputStream out(new BinObjectEncoding());
        out.writeString(PROJECT_STRING);

        int pageNr = control->getCurrentPageNo();
        MainWindow* win = control->getWindow();
        XojPageView* view = win->getXournal()->getViewFor(pageNr);
        PageRef page = control->getCurrentPage();
        auto sel = new EditSelection(control->getUndoRedoHandler(), page, view);

        for (GList* l = a->__elements(); l != nullptr; l = l->next) {
            auto e = static_cast<PageLayerPosEntry<Element>*>(l->data);
            g_message("ADD %s: %x", what, e);
            sel->addElement(e->element);
        }

        //sel->addElement(e);
        sel->serialize(out);

        auto* o = out.getStr();
        g_message("%d", o->len); // TODO should then g_free()
        /*
        FILE * fp = fopen(".xournal-test-binary.bin", "w");
    	fwrite(o->str, 1, o->len, fp);
    	fclose(fp);
        */
        if (shareConn) {
            soup_websocket_connection_send_binary(shareConn, (gconstpointer)o->str, (gsize)o->len);
        }
    } else if (auto a = dynamic_cast<const InsertUndoAction*>(ap)) {
        auto e = a->__element();

        ObjectOutputStream out(new BinObjectEncoding());
        out.writeString(PROJECT_STRING);

        int pageNr = control->getCurrentPageNo();
        MainWindow* win = control->getWindow();
        XojPageView* view = win->getXournal()->getViewFor(pageNr);
        PageRef page = control->getCurrentPage();
        g_message("INSERT %s: %x", what, e);
        auto sel = new EditSelection(control->getUndoRedoHandler(), page, view);
        sel->addElement(e);
        sel->serialize(out);

        auto* o = out.getStr();
        g_message("%d", o->len); // TODO should then g_free()
        /*
        FILE * fp = fopen(".xournal-test-binary.bin", "w");
    	fwrite(o->str, 1, o->len, fp);
    	fclose(fp);
        */
        if (shareConn) {
            soup_websocket_connection_send_binary(shareConn, (gconstpointer)o->str, (gsize)o->len);
        }
    }
    g_message("---- ----");
}

void LiveSynchronizer::handlePtr(const char* what, UndoActionPtr& action, Control* control) {
    handle(what, *action.get(), control);
}
void cbMessage(SoupWebsocketConnection *self, gint type, GBytes* message, gpointer user_data) {
    //g_message("MESSAGE!!!! %d %d %d", type, SOUP_WEBSOCKET_DATA_TEXT, SOUP_WEBSOCKET_DATA_BINARY);
    //g_message(*(const char**) message);
    //g_message((const char*)user_data);
    auto* control = (Control*) user_data;
    const char* buffer = NULL;
    char* tofree = NULL;
    long unsigned lSize = 0;

    if (type == SOUP_WEBSOCKET_DATA_TEXT
    && ! strcmp("GO", (const char*) g_bytes_get_data(message, NULL))) {
        FILE* pFile = fopen(".xournal-test-binary.bin5", "rb");
        //if (pFile==NULL) {fputs ("File error",stderr); exit (1);}
        // I'd rather crash

        // obtain file size:
        fseek(pFile , 0 , SEEK_END);
        lSize = ftell(pFile);
        rewind(pFile);

        // allocate memory to contain the whole file:
        char* buf;
        buffer = tofree = buf = (char*) malloc (sizeof(char)*lSize);
        if (buffer == NULL) {fputs ("Memory error",stderr); exit (2);}

        // copy the file into the buffer:
        size_t result = fread(buf, 1, lSize, pFile);
        if (result != lSize) {fputs ("Reading error",stderr); exit (3);}
        fclose (pFile);
    }
    if (type == SOUP_WEBSOCKET_DATA_BINARY) {
        buffer = (const char*) g_bytes_get_data(message, &lSize);
    }

    if (buffer) {
        ObjectInputStream in;
        if (in.read(buffer, lSize)) {

            //control->getDocument()->lock();
            int pageNr = control->getCurrentPageNo();
            PageRef page = control->getCurrentPage();
            MainWindow* win = control->getWindow();
            XojPageView* view = win->getXournal()->getViewFor(pageNr);
            auto undoRedo = control->getUndoRedoHandler();
            auto selection = new EditSelection(undoRedo, page, view);
            vector<Element*> elements;

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

                layer->addElement(element.get());
                syncAddUndoAction->addElement(layer, element.get(), layer->indexOf(element.get()));
                // Todo: unique_ptr
                //selection->addElement(element.release(), Layer::InvalidElementIndex);
                elements.emplace_back(element.release());
            }

            // recreate a selection so that the bounding box is properly computed (bug?)
            selection = new EditSelection(undoRedo, elements, view, page);
            win->getXournal()->setSelection(selection); // add to the document
            control->clearSelection(); // deselect
            undoRedo->addUndoActionSYNC(std::move(syncAddUndoAction)); // allow undo of it
            //view->rerenderPage();
            //control->getScheduler()->addRerenderPage(view);
            //selection->

            //control->clipboardPasteXournal(in); // why not refreshing???
            //control->clearSelection(); // incurs a refresh, ok
            //control->getWindow()->getXournal()->getSelection();
            // this->page->fireElementChanged(this->element);
        }
        if (tofree) free(tofree);
    }
}
void cbConnect(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    g_message("WEBSO!!!!");
    SoupWebsocketConnection* conn = soup_session_websocket_connect_finish((SoupSession*)source_object, res, NULL);
    shareConn = conn;
    //soup_websocket_connection_send_text(conn, "HELLO SOUP");
    //soup_websocket_connection_send_binary(conn, "HI\0HI", 5);
    g_signal_connect(conn, "message", GCallback(cbMessage), user_data);
}

LiveSynchronizer::LiveSynchronizer(Control* control) : control(control) {


    // TEST websocket client
    auto session = soup_session_new();
    auto url = soup_message_new("GET", "ws://localhost:14444");

    soup_session_websocket_connect_async(session, url, NULL, NULL, NULL, &cbConnect, this->control);


}