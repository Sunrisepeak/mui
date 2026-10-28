# mgame.net

Rollback netcode for two peers, independent of the transport (D-85). Each peer runs the whole game and sends only its inputs; the other's input for a frame not yet received is predicted (its last known), and when the real one differs the session restores the state saved at that frame and plays forward again.

```cpp
mgame::net::Session session(game, local_player, {.delay = 2, .max_rollback = 8});
transport.send(mgame::net::encode(session.local(input)));        // this frame's input, for frame + delay
for (auto& bytes : transport.received())
    if (auto m = mgame::net::decode(bytes)) session.receive(*m);
session.advance();                                               // advanced, or waiting for the peer
```

- **A game** is anything that plays one frame from both players' inputs, saves and restores its state by a copy, and checksums a saved state (`mgame::net::Game`); gongfu's match is one.
- **Input delay** (`delay`) plays a local input that many frames after it is given: latency within it needs no rollback.
- **Rollback** reaches back at most `max_rollback` frames; a peer that far past the other's last input waits instead.
- **Messages** carry every local input the peer has not acknowledged — a lost one is sent again with the next — the acknowledgement of the peer's, the sender's frame (`ahead()`: what time sync evens out) and, every `checksum_every` confirmed frames, a checksum: `desync()` names the first frame the peers disagree on. `encode` / `decode` make them little-endian bytes for any transport (UDP, WebRTC).

`tests/net_test.cpp` plays gongfu's match on two peers through simulated links — perfect; latency inside the delay; 4, 6 ± 3 and 8 ± 4 frames with 0, 5 and 15 % loss; a peer 20 frames behind — and holds both to the bytes of an offline run of the same inputs, with no rollback where the delay covers the latency and rollbacks where it does not; a game altered on one peer is named at the frame they parted.
