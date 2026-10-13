<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Octop Passport

Turn the AI Passport into a pocket voice terminal for **your own self-hosted
Octop**: hold OK and speak, release, and the device uploads your recording for
speech-to-text, sends the transcript to your own Octop agent, and streams the
reply character by character onto the 240×320 screen with automatic paging for
long articles.

Built on [FoloToy AI Passport](https://github.com/FoloToy/ai-passport)
hardware (ESP32-C3, 240×320 display, microphone, speaker, three buttons,
520 mAh battery) and
[TencentCloud/Octop](https://github.com/TencentCloud/Octop) (MIT, a
self-hosted multi-user AI assistant platform). Not affiliated with or
endorsed by Meta, Muse, or FoloToy.

```mermaid
flowchart LR
    P[Device: hold OK and speak] <-- BLE pairing --> A["Phone (Octop app or web console)"]
    A <-- phone network --> S[Your VPS / home server: self-hosted Octop]
```

## What it does

| You | Device |
| --- | --- |
| **Hold OK and speak, release** | Recording uploads for speech-to-text while you talk (no waiting), status shows Recording → Recognizing |
| Wait a few seconds | Your words become text and go to your Octop agent; the reply streams onto the screen |
| Long replies | Automatic paging, footer shows page x/y; UP/DOWN page through |
| Punctuation | Full GB2312 hanzi plus CJK symbol coverage — no missing-glyph boxes in Chinese text |
| Continuous conversation | One dedicated thread per device; the web console always has the full history |

Recognition quality is decided by the speech-to-text service you enable in
your Octop console (see "Voice recognition setup" below).

## What you need

| Item | Requirement |
| --- | --- |
| Hardware | FoloToy AI Passport (8 MB flash), USB data cable |
| Server | Self-hosted [Octop](https://github.com/TencentCloud/Octop) at `http://IP:8088`, reachable from the phone/computer |
| Account | A dedicated Octop user plus one agent |
| Speech-to-text | Any STT enabled in the Octop console voice settings (MiMo / Tencent Cloud / OpenAI, with key) |
| Network | Device and phone/computer on the same 2.4 GHz Wi-Fi, able to reach your Octop address |

## Three steps to start

1. **Hold the DOWN key for ~1 second** — the device opens the provisioning
   hotspot `OctopPassport`
2. **Join that hotspot with your phone**, open `192.168.4.1` in a browser
3. Fill the form: **home Wi-Fi** (pick SSID, enter password) → **Octop server
   address** (e.g. `http://150.109.81.50:8088`, with or without `http://`) →
   **username/password** (a dedicated account is recommended) → Connect

The device verifies, reboots, and connects: the status bar shows *Octop
connected* and you are ready. While there, enable a speech-to-text service
for this account in the Octop console voice settings (MiMo STT / Tencent
ASR / OpenAI Whisper — any one, with its key).

## Buttons

| Action | Effect |
| --- | --- |
| **Hold OK and speak, release** | Record → recognize → the transcript is sent automatically (30 s cap; presses shorter than 0.4 s are discarded) |
| Short OK while a reply streams | Cancel the current reply |
| UP / DOWN | Page through the reply (page any time, even mid-stream) |
| Hold DOWN 1 s | Re-open provisioning (same hotspot) |

## Voice recognition setup (pick one)

**Option A: built into Octop (recommended — nothing to configure on the
device).** In the Octop console → Voice settings → enable any STT service
with its key. The device uses it automatically; **switching providers in the
console later needs no action on the device**.

**Option B: a custom STT endpoint.** Point the device at any endpoint that
speaks the OpenAI `/v1/audio/transcriptions` shape via the `stt_url`/
`stt_key` fields (portal form support is on the roadmap; the fields already
exist in the device config store).

## Troubleshooting

- **Status bar shows Connection failed · cannot reach server**: the device is
  retrying automatically. Check that your phone/computer can open the Octop
  address; the device reconnects on its own once the server responds.
- **Recognition failed**: check the STT service and key in the Octop voice
  settings; when the server receives audio but recognition fails, no
  transcript is shown.
- **Paging does nothing**: the current reply is shorter than one page
  (clamping at the last page is by design); long replies page normally.
- **Forgot Wi-Fi password / changing servers**: hold DOWN to re-open
  provisioning.

## Updating the firmware

- **Full image** (`FoloToy-AI-Passport-full.bin`, written from `0x0`): erases
  stored settings (Wi-Fi/Octop account) — re-provision after flashing. Use
  this for first installation.
- **App-only update** (writing only the `0x10000` app partition): keeps
  stored settings; suitable for version upgrades within this project.

## Privacy and security

- Audio and text go only to **your own Octop deployment** (and its
  speech-to-text service). No third-party relay.
- The Octop password is stored in device NVS **unencrypted**: before handing
  the device to someone else, re-provision over the account or change the
  password in Octop.
- Each device is an independent user/session in Octop; the console can view
  and delete history at any time.

## Building from source

ESP-IDF **5.5.3**, target `esp32c3`:

```sh
git clone <this repository> && cd octop-passport
source <your ESP-IDF>/export.sh
idf.py set-target esp32c3
idf.py build                # or ./tools/validate.sh for the full gate + release image
```

The Chinese font is derived by `tools/octop-passport/gen_font.py` from the
source strings plus the full GB2312 charset (requires Node lv_font_conv and
fontTools).

## License and credits

- Code licensed under the upstream [Apache-2.0](LICENSE); BSP and build
  skeleton come from the FoloToy AI Passport family repositories.
- Wi-Fi provisioning component
  [78/esp-wifi-connect](https://github.com/78/esp-wifi-connect)
  (Apache-2.0), with this repository's portal-field extension patch.
- Chinese font: Noto Sans CJK SC subset (SIL Open Font License 1.1).
- [Octop](https://github.com/TencentCloud/Octop) is Tencent's MIT-licensed
  open-source project; this project is not affiliated with it.
