#include <mbgl/storage/asset_request_baton.hpp>
#include <mbgl/storage/asset_request.hpp>
#include <mbgl/storage/response.hpp>

namespace mbgl {

void AssetRequestBaton::run(AssetRequestBaton *ptr) {
    assert(ptr->threadId == uv_thread_self());

    if (ptr->canceled || !ptr->request) {
        // Either the AssetRequest object has been destructed, or the
        // request was canceled.
        cleanup(ptr);
        return;
    }

    // Just return a 500 error until implemented properly
    notify_error(ptr, 500, "Assets not implemented on this platform.");
    cleanup(ptr);
}

}