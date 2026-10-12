<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Octop Passport — Turn your AI Passport into a pocket terminal for your self-hosted Octop

Hold OK and speak; read the reply on the 240×320 screen. The device turns your
speech into text, sends it to an agent on **your own self-hosted**
[Octop](https://github.com/TencentCloud/Octop) instance, and streams the answer
onto the display with automatic paging for long articles. No phone, no companion
app — provisioning and Octop account binding happen in the device's own hotspot
portal.

> Status: P-M1 skeleton in progress (the protocol chain is verified against a
> live Octop instance from a PC simulator; voice capture/STT and article
> sanitizing render land in P-M2). Current buttons: short-press OK sends a
> preset test prompt.

## What it does (target behavior)

| You | Device |
| --- | --- |
| Hold OK, speak, release | Recording streams to speech-to-text; the transcript is shown (reviewable) |
| Wait | Text goes to your bound Octop agent; the status bar shows the link state |
| Read | Replies stream in token by token: Markdown is reduced for reading, pages auto-follow while streaming, UP/DOWN page through, short-press OK cancels the turn |
| Hold DOWN | Opens the provisioning portal: Wi-Fi, Octop server URL and account, agent binding — one form |

Conversations live in your own Octop (one dedicated thread per device); the web
console always has the full text. The device keeps only a recent-reading buffer
(about 48 pages) and points you to the console for anything longer.

## Security boundaries

- The device talks straight to your own Octop (plain http/ws on a LAN, or your
  own HTTPS reverse proxy); speech and chats never pass through a third-party
  relay.
- The Octop account is stored in device NVS and is only used to mint a JWT
  (24 h, auto re-login). The firmware ships with no embedded credentials.
- Device NVS is not encrypted at rest: rebind the account in the portal before
  handing the device to someone else.

## Flash / build

ESP-IDF 5.5.3, target `esp32c3`:

```bash
idf.py set-target esp32c3
idf.py build                      # daily
./tools/validate.sh               # full gate + byte-verified merged image
```

Flash `build/FoloToy-AI-Passport-full.bin` from `0x0` (web flasher or
`esptool.py --chip esp32c3 write_flash 0x0 ...`). On first boot the device
opens the hotspot `OctopPassport-XXXX`; open `192.168.4.1` on a phone and fill
in the form.

## Read more

- Design notes: the workspace-side design document (Chinese) carries the
  P-M0 measured protocol facts; this repository holds the engineering docs
  under [`docs/development/`](docs/development/README.md).
- [`docs/development/ai-guide.md`](docs/development/ai-guide.md): AI workflow
- [`AGENTS.md`](AGENTS.md): repository rules and task routing

## License and credits

Code follows the family baseline [Apache-2.0](LICENSE). The BSP, build gate and
engineering docs come from the FoloToy AI Passport family repositories; the
Wi-Fi provisioning component
[esp-wifi-connect](https://github.com/78/esp-wifi-connect) (Apache-2.0) carries
this repository's portal-field extension patch; the Chinese font is a Source
Han Sans subset (SIL OFL 1.1). Octop is Tencent's MIT-licensed open source;
this project is not affiliated with it.
