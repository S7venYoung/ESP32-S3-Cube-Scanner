#include "studio_transport.h"
#include <cassert>

int main() {
    cube_dongle::studio::ProgressDeadline deadline;
    assert(!deadline.Expired(0));
    assert(!deadline.Expired(4999999));
    assert(deadline.Expired(5000000));
    deadline.Reset();
    assert(!deadline.Expired(6000000));
    assert(!deadline.Expired(10999999));
    assert(deadline.Expired(11000000));
    cube_dongle::studio::BlePipe pipe;
    pipe.Reset(7);
    uint8_t bytes[512]{};
    for (size_t i = 0; i < sizeof(bytes); ++i)
        bytes[i] = static_cast<uint8_t>(i);
    assert(!pipe.Write(bytes, sizeof(bytes), 6));
    assert(pipe.Write(bytes, sizeof(bytes), 7));
    uint8_t out[512];
    assert(pipe.Next(out, 23) == 20 && out[19] == 19);
    assert(pipe.Next(out, 23) == 0);
    pipe.Retry();
    assert(pipe.Next(out, 23) == 20 && out[0] == 0);
    // Retrying a chunk must not reset the adapter's progress timeout.
    deadline.Reset();
    assert(!deadline.Expired(12000000));
    for (int64_t now = 13000000; now < 17000000; now += 1000000) {
        pipe.Retry();
        assert(pipe.Next(out, 23) == 20 && out[0] == 0);
        assert(!deadline.Expired(now));
    }
    assert(deadline.Expired(17000000));
    pipe.Confirm();
    assert(pipe.Next(out, 185) == 182 && out[0] == 20);
    pipe.Reset(8);
    assert(!pipe.Pending() && pipe.Next(out, 185) == 0);
    assert(!pipe.Write(bytes, 1, 7));
    for (unsigned i = 0; i < 8; ++i)
        assert(pipe.Receive(bytes, sizeof(bytes)));
    assert(!pipe.Receive(bytes, 1));
    assert(pipe.Read(out, sizeof(out)) == sizeof(out));
    assert(pipe.Receive(bytes, sizeof(bytes)));
    pipe.Reset(9);
    assert(pipe.Read(out, sizeof(out)) == 0);
    for (unsigned i = 0; i < 4; ++i)
        assert(pipe.Write(bytes, sizeof(bytes), 9));
    assert(!pipe.Write(bytes, 1, 9));
}
