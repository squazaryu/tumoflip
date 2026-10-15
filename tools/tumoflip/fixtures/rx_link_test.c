#include "rx_link.h"
#include <assert.h>
#include <string.h>

static void wire(void) {
    uint8_t bytes[RX_LINK_FRAME_MAX];
    RxLinkFrame f = {.type = RxLinkEdges, .sequence = 65535}, out;
    for(unsigned length = 0; length <= RX_LINK_PAYLOAD_MAX; length++) {
        f.length = length;
        for(unsigned i = 0; i < length; i++) f.data[i] = (uint8_t)(i * 57);
        size_t n = rx_link_encode(&f, bytes, sizeof(bytes));
        assert(n == length + 9);
        assert(rx_link_encode(&f, bytes, n - 1) == 0);
        RxLinkParser parser = {0};
        assert(!rx_link_feed(&parser, 0, &out));
        assert(!rx_link_feed(&parser, 'T', &out));
        for(size_t i = 0; i < n; i++) {
            bool done = rx_link_feed(&parser, bytes[i], &out);
            assert(done == (i == n - 1));
        }
        assert(out.type == f.type && out.sequence == f.sequence && out.length == length);
        assert(memcmp(out.data, f.data, length) == 0);
        bytes[n - 1] ^= 1;
        for(size_t i = 0; i < n; i++) assert(!rx_link_feed(&parser, bytes[i], &out));
        assert(parser.errors > 0);
    }
    f.length = 129;
    assert(!rx_link_encode(&f, bytes, sizeof(bytes)));
    RxLinkParser parser = {0};
    const uint8_t bad[] = {'T','R',2,1,0,0,0,'T','R',1,1,0,0,255};
    for(size_t i=0;i<sizeof(bad);i++) assert(!rx_link_feed(&parser,bad[i],&out));
    assert(parser.errors == 2);
    // Arbitrary noise must never overrun the bounded parser.
    uint32_t seed = 5;
    for(unsigned i=0;i<100000;i++) {
        seed = seed * 1664525u + 1013904223u;
        rx_link_feed(&parser, seed >> 24, &out);
        assert(parser.used <= RX_LINK_FRAME_MAX);
    }
}

static void hello(RxLinkBackend* b, uint32_t session) {
    RxLinkFrame f = {.type=RxLinkHello,.length=8}, reply;
    memcpy(f.data,"RXA1",4);rx_link_write32(f.data+4,session);
    assert(rx_link_backend_handle(b,&f,&reply));
    assert(reply.type==RxLinkReady && reply.length==9 && reply.data[8]==1);
    assert(!memcmp(reply.data,f.data,8));
}

static void backend(void) {
    RxLinkBackend b={0}; RxLinkFrame f={.type=RxLinkQuery,.length=4},reply;
    rx_link_write32(f.data,123);
    assert(!rx_link_backend_handle(&b,&f,&reply));
    hello(&b,123);
    assert(rx_link_backend_handle(&b,&f,&reply));
    RxLinkSnapshot s;
    assert(rx_link_snapshot_decode(&reply,123,&s) && s.count==0);
    assert(!rx_link_snapshot_decode(&reply,124,&s));
    f.type=RxLinkEdges;f.length=16;
    rx_link_write32(f.data+4,7);
    rx_link_write32(f.data+8,100 | 0x80000000u);
    rx_link_write32(f.data+12,300);
    assert(!rx_link_backend_handle(&b,&f,&reply));
    assert(b.snapshot.count==2 && b.snapshot.minimum==100 && b.snapshot.maximum==300);
    assert(b.duration_sum==400 && b.high_sum==100);
    hello(&b,123); // Retransmitted handshake must not reset active statistics.
    assert(b.snapshot.count==2);
    rx_link_backend_handle(&b,&f,&reply); // Duplicate batch ignored.
    assert(b.snapshot.count==2);
    f.sequence=2; rx_link_backend_handle(&b,&f,&reply);
    assert(b.snapshot.count==4 && b.snapshot.lost_batches==1);
    f.sequence=3;f.length=13;rx_link_backend_handle(&b,&f,&reply);
    assert(b.snapshot.count==4 && b.snapshot.rejected_batches==1);
    f.length=16;rx_link_write32(f.data+12,0);rx_link_backend_handle(&b,&f,&reply);
    assert(b.snapshot.count==4 && b.snapshot.rejected_batches==2);
    rx_link_write32(f.data+12,RX_LINK_DURATION_MAX+1);
    rx_link_backend_handle(&b,&f,&reply);assert(b.snapshot.count==4);
    rx_link_write32(f.data+12,300);rx_link_write32(f.data,999);
    rx_link_backend_handle(&b,&f,&reply);assert(b.snapshot.count==4);
    rx_link_write32(f.data,123);f.type=RxLinkQuery;f.length=4;
    assert(rx_link_backend_handle(&b,&f,&reply));
    assert(rx_link_snapshot_decode(&reply,123,&s));
    assert(s.count==4 && s.mean==200 && s.high_permille==250 && s.dropped_edges==7);
    reply.data[reply.length-1] ^= 0x80;
    assert(!rx_link_snapshot_decode(&reply,123,&s)); // Invalid histogram sum.
    f.type=RxLinkStop;assert(!rx_link_backend_handle(&b,&f,&reply));assert(!b.active);
    f.type=RxLinkQuery;assert(!rx_link_backend_handle(&b,&f,&reply));
    hello(&b,456);assert(b.snapshot.count==0);
    b.expected_sequence=65535;
    f.type=RxLinkEdges;f.length=12;f.sequence=65535;
    rx_link_write32(f.data,456);rx_link_write32(f.data+4,0);
    rx_link_write32(f.data+8,RX_LINK_DURATION_MAX | 0x80000000u);
    rx_link_backend_handle(&b,&f,&reply);assert(b.snapshot.count==1);
    f.sequence=0;rx_link_backend_handle(&b,&f,&reply);
    assert(b.snapshot.count==2 && b.snapshot.lost_batches==0);
    b.snapshot.count=RX_LINK_SAMPLE_LIMIT;
    f.sequence=1;rx_link_backend_handle(&b,&f,&reply);
    assert(b.snapshot.count==RX_LINK_SAMPLE_LIMIT);
    // Unknown commands and invalid hello/session leave state untouched.
    f.type=255;assert(!rx_link_backend_handle(&b,&f,&reply));
    f.type=RxLinkHello;f.length=8;memset(f.data,0,8);
    assert(!rx_link_backend_handle(&b,&f,&reply));assert(b.session==456);
}

static void client(void) {
    RxLinkClient c={0}; RxLinkBackend b={0}; RxLinkFrame f={.type=RxLinkHello,.length=8},reply;
    memcpy(f.data,"RXA1",4);rx_link_write32(f.data+4,42);
    assert(rx_link_backend_handle(&b,&f,&reply));
    assert(rx_link_client_accept(&c,&reply,0)==RxLinkClientIgnored);
    rx_link_client_begin(&c,42,UINT32_MAX-100);
    assert(c.waiting && !c.connected);
    assert(!rx_link_client_expired(&c,100));
    assert(rx_link_client_expired(&c,5000));
    reply.data[8]=2; // A different board must not enable the radio.
    assert(rx_link_client_accept(&c,&reply,0)==RxLinkClientIgnored);
    reply.data[8]=1;rx_link_write32(reply.data+4,99);
    assert(rx_link_client_accept(&c,&reply,0)==RxLinkClientIgnored);
    rx_link_write32(reply.data+4,42);
    assert(rx_link_client_accept(&c,&reply,200)==RxLinkClientReady);
    assert(c.connected && !c.waiting);
    assert(rx_link_client_accept(&c,&reply,1500)==RxLinkClientIgnored);
    assert(rx_link_client_expired(&c,2200));
    f.type=RxLinkQuery;f.length=4;rx_link_write32(f.data,42);
    assert(rx_link_backend_handle(&b,&f,&reply));
    assert(rx_link_client_accept(&c,&reply,2100)==RxLinkClientSnapshot);
    assert(!rx_link_client_expired(&c,2200));
    rx_link_client_stop(&c);
    assert(!c.connected && !c.waiting && !rx_link_client_expired(&c,999999));
    assert(rx_link_client_accept(&c,&reply,2200)==RxLinkClientIgnored);
    rx_link_client_begin(&c,0,0);assert(!c.waiting && !c.connected);
}

int main(void) {wire();backend();client();return 0;}
