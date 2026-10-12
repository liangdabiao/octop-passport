<p align="right">
  <a href="raw-socket-websocket-client.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# A WebSocket Client over Raw Sockets on ESP32-C3

Recorded while building the Octop Passport app (a voice terminal that streams
an LLM agent's replies onto the device screen through a self-hosted Octop
server). The transport is a hand-written WebSocket client over a raw lwip
socket — the family's `esp_http_client` lesson made the component route a
non-starter, and the frame protocol itself is thin. The client survived a full
real-device debugging day; these are the pitfalls it hit, in the order they
were caught. Every one of them produced a symptom that looked like "the server
is broken" or "the button does nothing" rather than a socket bug.

## 1. Delivering a frame must keep the tail bytes (TCP coalescing)

When a received WS frame is delivered to the application, the receive buffer
often holds `[header][payload][next frame's header bytes]` — TCP coalesces
frames that arrive in one segment. The delivery code moved the payload to the
front and set `rx_len = payload_len`, **silently discarding the tail** — the
next frame's header bytes. The stream then desyncs: every subsequent decode
reads payload bytes as a header and fails.

Symptom: the link dies with a frame-header error right after the **first**
message is processed; everything before that looked healthy.

Fix: strip only the header — `memmove(rx, rx + hl, rx_len - hl); rx_len -= hl;`
— payload and tail stay, the consumer reads the payload from `rx[0]` before the
next `poll()` call.

## 2. Oversize frames: discard-before-completeness, or the link dies every turn

The server (Octop) pushes `state_snapshot` frames whose size grows with the
conversation history — unbounded, and quickly past a 4 KB receive buffer. The
naive frame loop checks completeness first
(`rx_len < header + payload → wait for more`) and only then considers
discarding. A frame larger than the buffer can **never** pass that check, and
the fill function refuses to read into a full buffer → one of the two returns
an error → link death. Symptom: **every single turn dies** right after the
server starts replying (the snapshot), while small frames look fine.

Fix: check oversize **before** completeness. An oversize frame consumes only
its header, records `discard_left = payload_len`, and the loop drains payload
bytes as they arrive (`discard_fin_pending` tracks a fragmented message until
its `fin` segment). The connection stays up; the device only cares about
`token`/`done`/`error` frames anyway.

## 3. The fill function must always block for new bytes

A "fill the receive buffer" helper that returns immediately when the buffer is
non-empty turns the poll loop into a **pure hot spin**: a partial frame sits in
the buffer, `consume` returns "need more", `fill` returns "already have bytes",
forever — at full CPU, starving lower-priority tasks until the task watchdog
fires. Symptom: total input starvation, no logs, the board feels frozen.

Fix: `fill_rx` always performs one blocking `read()` (with the caller's
timeout); progress is "new bytes arrived", not "buffer has data".

## 4. A queued user action must survive a link death

The server closes idle dashboard WebSocket connections after a few tens of
seconds, so the device spends much of its life in a reconnect window. If the
queued "send this turn" flag is consumed by `run_turn` and that turn's
connection dies before any reply frame arrives, the user's key press is lost —
symptom: "pressed the button, nothing happened" while the status bar shows a
healthy *Connected*.

Fix: when a turn dies with **zero frames received**, re-arm the turn request —
the reconnect loop resends it on the fresh connection. Also send a protocol
`cancel` for turns the user cancelled, and consider starting a fresh thread
(server-side turns are processed sequentially per thread; a backlog makes every
new turn wait minutes with no frames).

## 5. Keep-alive: change one variable at a time, and log the death reason

A 25 s client-initiated ping **killed** the connection on schedule (every death
exactly 25 s apart) while the server's own ping/pong kept it alive — removed,
the link survived indefinitely. Whatever the server-side reason, the debugging
method matters more: every `-1` return path writes a human-readable reason into
`ws->last_err`, and the reason is shown on the device screen. Without that,
"socket error" tells you nothing.

## Debugging methods that made this tractable

- **The screen is the debug console.** The USB-Serial-JTAG console VFS is
  rerouted by the screenshot driver and becomes unreliable for live logs; the
  same screenshots that verify the UI can carry diagnostic counters instead
  (frames received, last frame type, token count, link-death reason).
- **App-only flash preserves NVS.** While iterating on network code, flash only
  the app partition (`0x10000`) so Wi-Fi credentials and the Octop account
  survive — no re-provisioning per iteration. (A full merged image wipes NVS.)
- **Replicate the client on the PC.** A ~100-line Python script with the same
  handshake and framing semantics, run against the real server, reproduced the
  exact failure and made frames visible — the device could then be fixed
  against a reproduced failure instead of a hunch.
