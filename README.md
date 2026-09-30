# PS5 Trophy Unlocker ELF

PC-side tool for sending a Trophy Unlocker payload to a compatible PS5 while a game is already running.

Trophy selection is controlled from the PC launcher. The project keeps the spirit of the original payload, but has been heavily modified and extended with a PC launcher, multiple send modes, debug reports, and PS4/PS5 detection.

## Demo video

[![Watch the demo](https://img.youtube.com/vi/amzFqTmyxbs/maxresdefault.jpg)](https://www.youtube.com/watch?v=amzFqTmyxbs)

> Experimental project intended for developers and homebrew users. Use at your own risk. All scripts and the C source are included here.
>
> Confirmed working on FW 6.02. FW 13.60 now has an experimental safe profile that never reuses the FW 6.02 ShellCore offsets. It continues through the Trophy2/UDS path for diagnostics and testing. This is not a fork.

## Background and modifications

This project is based only on the original `main.c` obtained from SonicISO on X. Everything else was reverse-engineered live on a PS5 running firmware 6.02 with debugging enabled, with SDK 10 also used as an information reference.

The original source has been heavily modified and extended to add:

- a PC launcher in `.bat` and `.ps1`;
- `all`, `id`, `wave`, `range`, and `list` modes;
- a debug mode with PC-side reports;
- TCP logging on port `9022`;
- temporary configuration-file handling;
- PS4 / PS5 detection;
- Trophy1 / Trophy2 / UDS tests;
- more detailed validation and error reporting.

## Important files

```text
PS5 Unlocker.elf              Normal payload injected into the game process
PS5 Unlocker DEBUG.elf        Separate debug payload with TCP logging
LANCER_UNLOCKER.bat           Simple double-click launcher
LANCER_UNLOCKER.ps1           Interactive PowerShell menu
INSTALLER_PYTHON_DEPENDANCES.bat
_support/                     Internal scripts, portable Python, and dependencies
debug_logs/                   Debug reports created on the PC
```

## Console requirements

Before running the tool:

- The console must be powered on.
- A game must already be running.
- PS5Debug must be active on port `744`.
- FTP must be active on port `2121`.
- The PC must be able to reach the console IP address.
- The payload loader must be ready on the console.

Ports used:

```text
744     PS5Debug
2121    FTP
9021    Payload send / loader, depending on the selected mode
9022    Debug payload logs
```

Note: in the PC launcher, port `9021` is mainly used to capture or manage payload return traffic when running `Debug report PC` / `-DebugReport`. In normal mode, payload logs are mainly exposed by the debug payload on port `9022`.

Configuration used during original testing:

```text
Console: PS5
Tested firmware: 6.02
PC: Windows 11 / Windows 10 VM
Debug: PS5 Debug 1.05 / kstuff 1.6.7
```

Experimental profile currently included:

```text
Firmware: 13.60
Strategy: Trophy2/UDS safe path
ShellCore writes: disabled
Status: experimental / diagnostics-first
```

The 13.60 profile intentionally does not contain guessed offsets. A firmware-specific
ShellCore patch should only be added after the exact code signature is independently
identified and validated.

## Python / dependencies

There are two supported methods.

### Method 1: portable, recommended

The package includes:

```text
_support\python
```

A portable Python runtime is already included, so the tool can be launched without installing Python system-wide.

Simply double-click:

```text
LANCER_UNLOCKER.bat
```

### Method 2: install or repair dependencies

Double-click:

```text
INSTALLER_PYTHON_DEPENDANCES.bat
```

Or run this from PowerShell in the tool directory:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\_support\install_dependencies_core.ps1"
```

Once dependencies are ready, use the launcher.

## Launching the menu

Double-click:

```text
LANCER_UNLOCKER.bat
```

Or run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\LANCER_UNLOCKER.ps1"
```

The menu then asks for:

1. the console IP address;
2. the send mode;
3. an ID, range, or list depending on the selected mode.

## Firmware profiles

The launcher accepts a `-Firmware` argument.

Examples:

```powershell
# Original verified 6.02 ShellCore patch path
powershell -NoProfile -ExecutionPolicy Bypass -File ".\_support\run_unlocker_core.ps1" -PS5 192.168.1.94 -Firmware 6.02 -Mode id -Id 1

# FW 13.60 safe path: no ShellCore writes, Trophy2/UDS diagnostics only
powershell -NoProfile -ExecutionPolicy Bypass -File ".\_support\run_unlocker_core.ps1" -PS5 192.168.1.94 -Firmware 13.60 -DebugReport -PayloadLogPort 9022 -Mode id -Id 1
```

Behavior:

- `6.02`: uses the known byte-signature-checked ShellCore gate patch.
- `13.60`: never applies the 6.02 offsets; continues with the Trophy2/UDS payload path.
- `auto` or unknown firmware: no ShellCore write is permitted.
- `-NoPatch`: explicitly disables any ShellCore patch regardless of profile.

## Available modes

### 1. Unlock all

Sends every detected trophy.

Equivalent command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\_support\run_unlocker_core.ps1" -PS5 192.168.1.94 -Mode all
```

### 2. Unlock one trophy

Example: unlock trophy ID `8`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\_support\run_unlocker_core.ps1" -PS5 192.168.1.94 -Mode id -Id 8
```

### 3. Quick wave

Example: unlock 10 trophies, IDs `1` through `10`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\_support\run_unlocker_core.ps1" -PS5 192.168.1.94 -Mode wave -Start 1 -Wave 10
```

Another example: `-Start 5 -Wave 5` targets IDs `5` through `9`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\_support\run_unlocker_core.ps1" -PS5 192.168.1.94 -Mode wave -Start 5 -Wave 5
```

### 4. Exact range

Example: unlock IDs `5` through `8`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\_support\run_unlocker_core.ps1" -PS5 192.168.1.XX -Mode range -Range 5-8
```

### 5. ID list

Example: unlock IDs `5`, `8`, and `21`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\_support\run_unlocker_core.ps1" -PS5 192.168.1.XX -Mode list -Ids "5,8,21"
```

### 6. PC debug report

Debug mode accepts a range of up to 10 trophies.

Examples:

```text
5       -> 1 to 5
6 9     -> 6 to 9
6-9     -> 6 to 9
6 a 9   -> 6 to 9
```

The report is written to:

```text
debug_logs/
```

Equivalent command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\_support\run_unlocker_core.ps1" -PS5 192.168.1.XX -DebugReport -Elf "PS5 Unlocker DEBUG.elf" -PayloadLogPort 9022 -Mode wave -Start 1 -Wave 5
```

The report helps verify:

- detection of the running game;
- detected platform, PS4 or PS5;
- whether the PS5 FW 6.02 patch was applied;
- the configuration that was sent;
- ELF injection;
- payload TCP logs;
- the exact error when a step fails.

## Useful commands

All commands below should be run from the tool directory.

### Install or repair dependencies

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\_support\install_dependencies_core.ps1"
```

### Launch the PowerShell menu

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\LANCER_UNLOCKER.ps1"
```

### Unlock all

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\_support\run_unlocker_core.ps1" -PS5 192.168.1.94 -Mode all
```

### Unlock one trophy

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\_support\run_unlocker_core.ps1" -PS5 192.168.1.94 -Mode id -Id 8
```

### Unlock 10 trophies, IDs 1 through 10

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\_support\run_unlocker_core.ps1" -PS5 192.168.1.94 -Mode wave -Start 1 -Wave 10
```

### Wave starting at ID 5, length 5

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\_support\run_unlocker_core.ps1" -PS5 192.168.1.94 -Mode wave -Start 5 -Wave 5
```

### Exact range 5 through 8

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\_support\run_unlocker_core.ps1" -PS5 192.168.1.94 -Mode range -Range 5-8
```

### Exact list

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\_support\run_unlocker_core.ps1" -PS5 192.168.1.94 -Mode list -Ids "5,8,21"
```

### Debug ELF on port 9022, example IDs 1 through 5

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\_support\run_unlocker_core.ps1" -PS5 192.168.1.94 -DebugReport -Elf "PS5 Unlocker DEBUG.elf" -PayloadLogPort 9022 -Mode wave -Start 1 -Wave 5
```

## How it works

The general flow is:

1. A game is launched on the console.
2. The PC prepares a configuration for the selected mode.
3. The launcher sends the ELF payload to the console.
4. The payload executes inside the game process.
5. It detects the platform and the available trophy context.
6. It attempts the appropriate route: PS4/Trophy1 or PS5/Trophy2/UDS.
7. In debug mode, logs are collected on the PC and a report is written to `debug_logs/`.

## PS4 / PS5 behavior

### PS4

- The script detects the running CUSA game.
- It attempts to retrieve NPWR/NPSIG information over FTP when available.
- It then injects the ELF into the game process.

### PS5

- The script detects the running PPSA game.
- The selected firmware profile decides whether a ShellCore RAM patch is allowed.
- FW 6.02 uses the known signature-checked patch.
- FW 13.60 is currently a safe no-write profile and proceeds through Trophy2/UDS diagnostics.
- Unknown firmware never receives a ShellCore write.
- If no verified patch is available, ELF injection can still continue so the debug payload can report the available Trophy2/UDS context and symbols.

## Logs and reports

Debug reports are created on the PC in:

```text
debug_logs/
```

The debug payload can also expose a TCP log on:

```text
9022
```

The code may also use temporary files on the console, depending on the selected mode:

```text
/data/trophy_unlocker_log.txt
/data/trophy_unlocker_id.txt
/data/trophy_unlocker_config.txt
/data/trophy_unlocker_npcomm.txt
/data/trophy_unlocker_npsig.bin
/data/trophy_unlocker_count.txt
/data/trophy_unlocker_platform.txt
```

## Known issues

This project is experimental.

Possible issues include:

- bugs;
- unsupported offsets, with multi-firmware support planned;
- detection problems;
- injection errors;
- game-specific behavior differences.

## Disclaimer

This project is provided for educational, experimental, and homebrew purposes.

## Credits

Main base / inspiration:

- SonicISO `main.c` included in the repository. The rest of this project grew from hands-on reverse engineering and experimentation on a personal console.
- PS5 Debug by Sistro / ctn — thanks to the GoldHEN community.
- P55 SDK by John Tornblom.
- SDK 10 as an information reference.
- Reverse engineering, scripts, and ELF analysis.

Major modifications include:

- extended code;
- PC launcher;
- trophy-selection modes;
- debug reports;
- PS4/PS5 handling.

Thanks also to the PS4/PS5 homebrew developers and testers who share research and helped make this learning process possible.

For additional developer references:

https://github.com/ArkSama/PS5-PHU-Trophy-System

https://git.etawen.dev/soniciso/uds-trophy-unlocker
