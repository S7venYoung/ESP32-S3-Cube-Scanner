#include <cassert>
#include "default_layers.h"
#include "fast_keymap.h"
using namespace cube_dongle;
using namespace cube_dongle::studio;
int main() {
    DefaultLayers defaults;
    Model model;
    Bytes stored, args, request;
    Number(args, 1, 1);
    Number(args, 2, 2);
    Message(request, 2, args);
    assert(Reader(defaults.Handle(request, false, model, 1, [](const Bytes&) {
               return true;
           })).Get(1));
    auto response = defaults.Handle(request, true, model, 1, [&](const Bytes& data) {
        stored = data;
        return true;
    });
    assert(Reader(response).Get(2) && defaults.Resolve(1, model) == 2);
    DefaultLayers restored;
    assert(restored.Load(stored) && restored.Resolve(1, model) == 2);
    assert(!restored.Load(Bytes(25)));
    defaults.endpoints[1] = -2;
    defaults.os_layers[2] = 1;
    defaults.overrides[0] = 3;
    assert(defaults.Resolve(1, model) == 1);
    model.order = {0, 2, 3, 1};
    model.count = 2;
    assert(defaults.Resolve(1, model) == -1);
    KeyboardState<61, 4> keys(model.map);
    keys.SetLayerOrder(model.order, model.count);
    keys.SetDefaultLayer(2);
    assert(keys.ActiveLayer() == 2);
    keys.SetDefaultLayer(1);
    assert(keys.ActiveLayer() == 0);  // Deleted default falls back.
    Rpc rpc;
    request.clear();
    Number(request, 1, 1);
    response = FastKeymap(request, rpc);
    assert(Reader(response).Get(2));
    args.clear();
    Number(args, 1, 2);
    request.clear();
    Message(request, 4, args);
    response = FastKeymap(request, rpc);
    Reader layer_response(response);
    const auto* layer = layer_response.Get(5);
    assert(layer);
    Reader decoded(layer->data, layer->size, {3});
    assert(decoded.Repeated(3).size() == 61);
    request.clear();
    Number(request, 10, 1);
    response = FastKeymap(request, rpc);
    assert(Reader(response).Get(11)->size == 0);
    rpc.model.map[1][4] = {Kind::Key, 4, 0};
    response = FastKeymap(request, rpc);
    assert(Reader(response).Get(11)->size > 0);
    args.clear();
    Number(args, 1, 1);
    Number(args, 1, 2);
    request.clear();
    Message(request, 9, args);
    response = FastKeymap(request, rpc);
    Reader batch_response(response);
    const auto* layers = batch_response.Get(10);
    assert(layers);
    Reader batch(layers->data, layers->size, {1});
    assert(batch.Repeated(1).size() == 2);
}
