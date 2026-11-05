#include "win_dragdrop.h"
#include "../../core/panel.h"
#include "../../core/platform_tracks.h"

namespace jsp::win {

namespace {

template <class I>
class ComObject : public I {
public:
    virtual ~ComObject() = default;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        if (iid == IID_IUnknown || iid == __uuidof(I)) {
            *out = static_cast<I*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG n = InterlockedDecrement(&m_refs);
        if (!n) delete this;
        return (ULONG)n;
    }

private:
    LONG m_refs = 1;
};

class DropTarget : public ComObject<IDropTarget> {
public:
    DropTarget(HWND hwnd, std::function<Panel*()> panel) : m_hwnd(hwnd), m_panel(std::move(panel)) {}

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD keys, POINTL pt, DWORD* effect) override {
        m_data = data;
        m_allowed = DROPEFFECT_NONE;
        bool native = false;
        DWORD check = *effect;
        if (data && SUCCEEDED(ole_interaction::get()->check_dataobject(data, check, native))) m_allowed = check & *effect;
        return send(Drag::Enter, keys, pt, effect);
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL pt, DWORD* effect) override { return send(Drag::Over, keys, pt, effect); }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        DropAction action;
        if (Panel* p = m_panel()) p->drag(Drag::Leave, 0, 0, 0, action);
        m_data.release();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD keys, POINTL pt, DWORD* effect) override {
        m_data = data;
        DropAction action = run(Drag::Drop, keys, pt, *effect);
        *effect = (DWORD)action.effect & m_allowed;
        Panel* p = m_panel();
        if (p && *effect != DROPEFFECT_NONE && data && action.playlist >= 0)
            playlist_incoming_item_filter_v2::get()->process_dropped_files_async(data, playlist_incoming_item_filter_v2::op_flag_delay_ui,
                                                                                 GetAncestor(m_hwnd, GA_ROOT), p->drop_notify(action));
        m_data.release();
        return S_OK;
    }

private:
    DropAction run(Drag kind, DWORD keys, POINTL pt, DWORD offered) {
        DropAction action;
        action.effect = (int)(offered & m_allowed);
        POINT p{ pt.x, pt.y };
        ScreenToClient(m_hwnd, &p);
        if (Panel* panel = m_panel()) panel->drag(kind, p.x, p.y, keys, action);
        else action.effect = 0;
        return action;
    }
    HRESULT send(Drag kind, DWORD keys, POINTL pt, DWORD* effect) {
        *effect = (DWORD)run(kind, keys, pt, *effect).effect & m_allowed;
        return S_OK;
    }

    HWND m_hwnd;
    std::function<Panel*()> m_panel;
    pfc::com_ptr_t<IDataObject> m_data;
    DWORD m_allowed = DROPEFFECT_NONE;
};

class DropSource : public ComObject<IDropSource> {
public:
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override {
        if (escape) return DRAGDROP_S_CANCEL;
        return keys & (MK_LBUTTON | MK_RBUTTON) ? S_OK : DRAGDROP_S_DROP;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
};

}

IDropTarget* create_drop_target(HWND hwnd, std::function<Panel*()> panel) { return new DropTarget(hwnd, std::move(panel)); }

int drag_tracks(metadb_handle_list_cref tracks, int effects) {
    pfc::com_ptr_t<IDataObject> data = ole_interaction::get()->create_dataobject(tracks);
    if (data.is_empty()) return 0;
    if (GetCapture()) ReleaseCapture();
    DropSource* source = new DropSource();
    DWORD done = DROPEFFECT_NONE;
    const HRESULT hr = DoDragDrop(data.get_ptr(), source, (DWORD)effects, &done);
    source->Release();
    return hr == DRAGDROP_S_DROP ? (int)done : 0;
}

}

namespace jsp::ui {

bool copy_tracks(metadb_handle_list_cref tracks) {
    pfc::com_ptr_t<IDataObject> data = ole_interaction::get()->create_dataobject(tracks);
    return data.is_valid() && SUCCEEDED(OleSetClipboard(data.get_ptr()));
}

bool clipboard_has_tracks() {
    pfc::com_ptr_t<IDataObject> data;
    if (FAILED(OleGetClipboard(data.receive_ptr())) || data.is_empty()) return false;
    DWORD effect = DROPEFFECT_COPY;
    bool native = false;
    return SUCCEEDED(ole_interaction::get()->check_dataobject(data, effect, native));
}

bool clipboard_tracks(metadb_handle_list_ref out) {
    pfc::com_ptr_t<IDataObject> data;
    if (FAILED(OleGetClipboard(data.receive_ptr())) || data.is_empty()) return false;
    return playlist_incoming_item_filter::get()->process_dropped_files(data.get_ptr(), out, true, core_api::get_main_window());
}

}
