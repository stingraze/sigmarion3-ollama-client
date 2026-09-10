# Ollama Client for CE
*Made with Cursor, so some of the README might be wrong.

Native Windows CE GUI client for [Ollama](https://ollama.com). Built with **CeGCC** (`arm-mingw32ce-gcc`). 
Aimed at Handheld PC 2000 and newer devices such as the **NEC Sigmarion III** (800×480, Windows CE 4.1).

## Features

- Set the Ollama server URL (`http://host:11434`)
- **Models** button loads `/api/tags` into a combo box (you can also type a model name)
- Send a prompt via `/api/generate` with streamed output
- **Stop**, **Clear**, and **Save** (writes `ollama-ce.ini` next to the `.exe`)

Ollama talks HTTP on the LAN. This client does not use HTTPS.

## Build (Windows CE EXE)

Use the Brainux / [brain-hackers/cegcc-build](https://github.com/brain-hackers/cegcc-build/releases) `arm-mingw32ce` toolchain (GCC 9.3.0):

```bash
# example: 2026-04-14-154823
unzip cegcc-x86_64-*.zip
export PATH="$PWD/cegcc/bin:$PATH"   # or copy cegcc to /opt/cegcc
make          # -> ollama-ce.exe      (links WINSOCK.dll, CE 3.0 / Sigmarion III)
make alt      # -> ollama-ce-ws2.exe  (links WS2.dll, CE 4+ / SHARP Brain)
make strip
```

Copy the EXE to the device (`\Storage Card\` or `\Program Files\`). It links only `COREDLL.dll`, `COMMCTRL.dll`, and Winsock — no `cegcc.dll` needed on the device.

## Ollama server

The handheld is not `localhost`. Ollama binds to loopback by default and also rejects requests whose `Host` header is not local, so it must be started with:

```bash
export OLLAMA_HOST=0.0.0.0:11434
ollama serve
```

Verify from another machine on the LAN before touching the PDA:

```bash
curl http://192.168.1.4:11434/api/tags
```

Allow TCP 11434 through the server's firewall, and make sure the device and PC share an IP network (Wi-Fi, USB RNDIS, or CF Ethernet).

In the GUI: set **Server** to `http://192.168.1.4:11434`, tap **Save**, then **Models**.

## Troubleshooting

The status line at the bottom reports the exact failure, including the Winsock error number.

| Status message | Meaning |
| --- | --- |
| `Cannot reach 192.168.1.4:11434 (err 10061)` | Nothing listening there. Ollama is bound to loopback only, or a firewall blocks 11434. Start it with `OLLAMA_HOST=0.0.0.0:11434`. |
| `Cannot reach ... (err 10065 / 10051)` | No route from the device. Check the handheld's IP, netmask, and gateway. |
| `Timed out connecting to ...` | Packets are being dropped silently, usually a firewall. |
| `Cannot resolve <name> (err ...)` | No DNS on the device. Use a numeric IP such as `http://192.168.1.4:11434`. |
| `HTTP 403: set OLLAMA_HOST=0.0.0.0 and restart Ollama` | Ollama refused the `Host` header (its DNS-rebinding guard). |
| `Server closed connection with no reply` | Something answered but is not Ollama (wrong port). |

The URL is normalized before use: a missing `http://` is added and a missing port becomes `:11434`, so `192.168.1.4` is treated as `http://192.168.1.4:11434`. The effective URL is written back into the **Server** box.

If `ollama-ce.exe` connects to nothing at all but the network otherwise works, try `ollama-ce-ws2.exe` — it uses the Winsock 2 stack instead of Winsock 1.1.

## PC helper (optional)

The same protocol stack, built for Linux, to test a server before copying the EXE:

```bash
make test          # unit tests: JSON, UTF-8, URL parsing/normalization
make host
./ollama-ce-host --url http://192.168.1.4:11434 --list
./ollama-ce-host --url http://192.168.1.4:11434 --model llama3.2 --prompt "Hello"
```

`test/fake_ollama.py` is a small stub of `/api/tags` and streaming `/api/generate` for exercising the client without a model:

```bash
python3 test/fake_ollama.py 18080 &
./ollama-ce-host --url http://127.0.0.1:18080 --list
```

## Layout

```
Server [ http://192.168.1.4:11434 ] [Models] [Save]
Model  [ llama3.2:latest          ]
Prompt
[                                 ]
[Send] [Stop] [Clear]
Response
[                                 ]
status line
```

Settings persist in `ollama-ce.ini` beside the executable:

```
url=http://192.168.1.4:11434
model=llama3.2:latest
```

## Notes for CE 3.0

- Unicode GUI (`WinMain` + `WCHAR` controls)
- Manual UTF-8 conversion (Windows CE 3.0 has no reliable `CP_UTF8`)
- Winsock 1.1, HTTP/1.0, IPv4 only
- Numeric IPs bypass `gethostbyname()`, which fails on CE 3.0 without DNS
- Non-blocking connect with a 15 s timeout, verified through `SO_ERROR`
- Network work runs on a background thread so **Stop** stays responsive
