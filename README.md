<div align="center">

<img src=".github/logo.png" width="96" alt="SoulMeter">

# SoulMeter

**A real-time DPS meter and combat overlay for [SoulWorker](https://store.steampowered.com/app/1377580/Soulworker/).**

Live damage tables, per-player breakdowns, buff uptime, DPS graphs and a combat log — drawn straight over the game.

![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078D6)
![Language](https://img.shields.io/badge/C%2B%2B-20-00599C)
![UI](https://img.shields.io/badge/UI-ImGui%20%2B%20DirectX%2011-5C2D91)
![Version](https://img.shields.io/badge/version-1.7.1.8-brightgreen)

</div>

---

## What it does

SoulMeter reads the game's network traffic and turns it into a live picture of your run — who is doing what damage, which buffs are up, when the boss died and why your numbers look the way they do.

| Feature | What you get |
|---|---|
| **Damage table** | Live DPS, total damage, share %, crit rate and hit counts for every player in your party |
| **Per-player detail** | Click a name to break a player down by skill — damage, casts, crit rate, average hit |
| **Buff tracking** | Uptime for armour break, attack speed, boss damage and more |
| **DPS graphs** | Damage-over-time plots per player, powered by ImPlot |
| **Combat log** | A recorded blow-by-blow of the encounter |
| **History** | The last 50 runs are kept and can be reopened and compared |
| **Ping** | Live latency, measured from the game's own heartbeat exchange |
| **Localised** | English, 日本語, 한국어, 繁體中文 |

---

## Getting started

> [!IMPORTANT]
> **No external loader is needed.** Earlier releases required a separate injector — this one injects itself.

1. Grab a release (or [build it yourself](#building-from-source)).
2. Keep the folder layout intact:
   ```
   SoulMeter.exe
   SoulMeterHook.dll     <- capture DLL, injected into the game
   sqlite3.dll
   SWDB.db               <- skill / monster / map names
   Lang/                 <- en, jp, kr, zh_tw
   Font/                 <- optional, drop .ttf files here
   ```
3. **Run `SoulMeter.exe` as Administrator**, then launch the game.

The meter waits for the game process, injects automatically the moment it appears, and switches from *Waiting for SoulWorker* to your world name once packets start flowing.

### Hotkeys

| Keys | Action |
|---|---|
| `Ctrl` + `End` | Show / hide the overlay |
| `Ctrl` + `Del` | Reset the current run |

Both are rebindable in `option.xml` using [DirectInput key codes](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee418641(v=vs.85)).

### Tips

- **Right-click the title bar** for the full feature menu.
- **Left-click a character's row** to open their detailed breakdown.
- Non-Latin text not rendering? Drop a font covering your language into `Font/` and pick it in the Font Selector.
- User settings live in `option.xml` and `imgui.ini`; saved history in `SoulMeter.dat`.

---

## How it works

Capture happens **inside the game process** rather than on the wire, which means no packets are missed and no traffic is intercepted at the network layer.

```
SoulMeter.exe                              SoulWorker (game process)
├── Injector ──── injects ───────────────► SoulMeterHook.dll
│                                          └── detours the game's own
│                                              packet (de)serialisers
└── PipeReceiver ◄── \\.\pipe\SoulMeterHook ──┘  (complete, plaintext frames)
         │
         └──► SWPacketMaker → damage / buff / combat state → ImGui overlay
```

The hook attaches to the client's own serialiser and deserialiser rather than to `ws2_32`. That matters: the client is an IOCP application issuing overlapped `WSARecv`, so socket-level hooks never observe a single byte. Hooking the game's own functions also means the client has already reassembled the TCP stream and decrypted the packet body by the time we see it — so frames arrive complete, in order, and exactly once, with no reassembly on our side to fall out of sync.

**Built with** ImGui + ImPlot on DirectX 11 · SQLite for game data · FlatBuffers for history · MinHook for the detours · nlohmann/json for i18n · tinyxml2 for settings.

---

## Building from source

Requires **Visual Studio 2022** (v143 toolset, C++20).

```bash
MSBuild "Soulworker Utility.sln" -m -p:Configuration=Release -p:Platform=x64
```

Output lands in `x64\Release\`. The solution builds two projects — `Soulworker Utility` (the meter) and `SoulMeterHook` (the injected DLL) — and copies `Lang\*.json` into the output folder automatically.

> [!NOTE]
> The hook resolves the game's packet functions from a pinned offset into `SoulWorker64.dll`. A game patch can move it, in which case the address in `SoulMeterHook/sockethooks.cpp` needs re-extracting.

---

## Credits

This project stands on the work of the people who built and maintained SoulMeter before it.

| Author | Contribution |
|---|---|
| **[FeAr](https://github.com/fearek/DPSMeter/)** | `fearek/DPSMeter` — the Global-server meter this repository continues from |
| **[AFNGP](https://github.com/AFNGP/SoulMeter)** | `AFNGP/SoulMeter` — long-running maintenance and feature work |

Original project by **Park3740**. Special thanks to **@Nyanchii** and **@ga0321**.