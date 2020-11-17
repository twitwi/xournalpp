
#include "LiveSynchronizer.h"

#include "i18n.h"

#include "serializing/BinObjectEncoding.h"
//#include "serializing/HexObjectEncoding.h"
#include "serializing/ObjectInputStream.h"
#include "serializing/ObjectOutputStream.h"
#include "control/tools/EditSelection.h"
#include "gui/XournalView.h"
#include "gui/MainWindow.h"
#include "control/Control.h"
#include "model/DocumentChangeType.h"
#include "control/jobs/XournalScheduler.h"

#include <libsoup/soup.h>
SoupWebsocketConnection* shareConn = NULL;
#include "undo/PageLayerPosEntry.h"

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
        g_message("RECV... %x", user_data);
        ObjectInputStream in;
        if (in.read(buffer, lSize)) {
            string version = in.readString();
            if (version != PROJECT_STRING) {
                g_warning("Sync from Xournal Version %s to Xournal Version %s", version.c_str(), PROJECT_STRING);
            }
            auto page = control->getCurrentPage();
            auto layer = page->getSelectedLayer();
            while (layer->getElements()->size() > 0) {
                layer->removeElement((*layer->getElements())[0], true);
            }
            layer->readSerialized(in);
            control->firePageChanged(control->getCurrentPageNo());
            // TODO action? -> not really because no sync of ids anyway
        }
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

LiveSynchronizer::LiveSynchronizer(Control* control) : control(control), registeredUndoRedoListener(false) {

    DocumentListener::registerListener(control);

    // TEST websocket client
    auto session = soup_session_new();
    auto url = soup_message_new("GET", "ws://localhost:14444");

    soup_session_websocket_connect_async(session, url, NULL, NULL, NULL, &cbConnect, this->control);

}

void LiveSynchronizer::documentChanged(DocumentChangeType type) {
    g_message("DOC CHANGED %d", type);
}

void LiveSynchronizer::pageSizeChanged(size_t page) {
    g_message("pageSizeChanged ... %d", page);
}
void LiveSynchronizer::pageChanged(size_t page)  {
    g_message("pageChanged ... %d", page);
}
void LiveSynchronizer::pageInserted(size_t page) {
    g_message("pageInserted ... %d %d", page);
}
void LiveSynchronizer::pageDeleted(size_t page) {
    g_message("pageDeleted ... %d", page);
}
void LiveSynchronizer::pageSelected(size_t page) {
    g_message("pageSelected ... %d", page);
    PageListener::unregisterListener();
    this->currentPage = page;
    PageListener::registerListener(control->getDocument()->getPage(page));
    if (!this->registeredUndoRedoListener) {
        this->registeredUndoRedoListener = true;
        control->getUndoRedoHandler()->addUndoRedoListener(this);
    }
}


class SendJob: public Job {
    public:
    ~SendJob() = default;

    LiveSynchronizer* livesync;
    SendJob(LiveSynchronizer* livesync) : livesync(livesync) {
        livesync->control->getScheduler()->addJob(this, JOB_PRIORITY_NONE);
    }
    virtual void run() {
        livesync->sendPageUpdate();
    }
    auto getType() -> JobType { return JOB_TYPE_AUTOSAVE; }

};
/*
bool scheduleJob(LiveSynchronizer* that) {
    return that->scheduleJob();
}*/
bool LiveSynchronizer::scheduleJob() {
    auto sel = control->getWindow()->getXournal()->getSelection();
    if (sel != nullptr && sel->getElements()->size() > 0) {
        g_message("wait...");
        return true;
    }
    auto* job = new SendJob(this);
    job->unref();
    return false;
}

void LiveSynchronizer::maybeSendPageUpdate() {
    g_timeout_add(100, (GSourceFunc) &LiveSynchronizer::scheduleJob, this);
    //this->sendPageUpdate();
}
void LiveSynchronizer::sendPageUpdate() {

    auto page = this->control->getDocument()->getPage(this->currentPage);
    auto layer = page->getSelectedLayer();
    g_message("SEND... %x", layer);

    ObjectOutputStream out(new BinObjectEncoding());
    out.writeString(PROJECT_STRING);

    /*
    if (sel) sel->mouseUp();
    if (sel) for (Element* e: *sel->getElements()) {
        layer->addElement(e);
    }*/
    layer->serialize(out);
    /*
    if (sel) for (Element* e: *sel->getElements()) {
        layer->removeElement(e, false);
    }*/
    auto* o = out.getStr();
    g_message("%d", o->len); // TODO should then g_free()?
    if (shareConn) {
        soup_websocket_connection_send_binary(shareConn, (gconstpointer)o->str, (gsize)o->len);
    }
}
void LiveSynchronizer::rectChanged(Rectangle<double>& rect) {
    g_message("rectChanged...");
}
void LiveSynchronizer::rangeChanged(Range& range) {
    g_message("rangeChanged...");
}
void LiveSynchronizer::elementChanged(Element* elem) {
    g_message("elementChanged...");
    this->maybeSendPageUpdate();
}
void LiveSynchronizer::pageChanged() {
    g_message("pageChanged...");
}



void LiveSynchronizer::undoRedoChanged() {
    g_message("undoRedoChanged---");
}
void LiveSynchronizer::undoRedoPageChanged(PageRef page) {
    g_message("undoRedoPageChanged---");
    this->maybeSendPageUpdate();
}
