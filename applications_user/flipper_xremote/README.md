<p align="center">Advanced IR Remote App for Flipper Device</p>

<p align="center">
  Tumo XRemote 1.13.1
</p>

<p align="center">
    <img src="https://github.com/kala13x/flipper-xremote/blob/main/.flipcorg/banner.png" alt="XRemote">
</p>

## Idea

Navigation to the menu to press each button individually can be often uncomfortable because it requires scrolling to the desired button and selecting it. The idea behind `XRemote` is that all physical buttons are pre-mapped to specific category buttons, and a physical button directly sends an infrared signal. This allows the flipper device to be used as a remote rather than as a tool that has a remote.

## Learn new remote

`XRemote` also introduces a more user-friendly learning approach. Instead of having to manually name each button on the flipper when cloning a remote, the learning tool informs you upfront which buttons it will record. All you need to do is press the corresponding button on your existing remote, eliminating the need to name them individually.

## Custom Layout

To customize your layout, open the saved remote file, select `Edit` in the menu, and configure which infrared commands should be transmitted when physical buttons are pressed or held. These changes will be stored in the existing remote file, which means that the configuration of custom buttons can be different for all remotes.

<table align="center">
    <tr>
        <td align="center">Edit custom page buttons</td>
    </tr>
    <tr>
        <td><img src="https://github.com/kala13x/flipper-xremote/blob/main/screens/custom_layout.png" alt="XRemote edit layout"></td>
    </tr>
</table>

## IR + RF Remotes

`IR + RF Remotes` combines an existing `.ir` remote with saved static Sub-GHz
commands in one remote that works entirely on Flipper:

1. Use `Create from IR` or `Create from Sub-GHz` to create a remote.
2. Use `Add Sub-GHz to Remote` to add up to eight saved `.sub` commands.
3. Open `My Remotes`, select a remote, and press OK to use it.
4. For Sub-GHz commands, hold OK to switch between the internal and external
   CC1101. Hold Right to cycle B1-B4 only for reviewed 24-bit Princeton commands.

Horizontal mode shows four command buttons per page in a 2x2 grid. Left/Right
moves through commands and Up/Down moves between rows. Vertical mode keeps four
full-width buttons per page; Up/Down selects a command and Left/Right changes
pages. The header always shows the current page and selected IR/RF output.

`My Remotes` shows each remote's IR/RF button counts and reports missing sources
or invalid files before launch. Press Right for Open, Edit, Repair, Copy, Backup,
and Delete. Copy creates a separate remote while keeping the same source references.

`Repair` is available only for a remote marked `Missing`. It finds the first
missing or unusable IR source, or the first missing Sub-GHz source, then opens
the matching file picker. A replacement IR file must contain at least one valid
command. A replacement Sub-GHz file must parse successfully and must not use a
changing-code protocol. One source is repaired per explicit operation so every
replacement remains visible and deliberate. Cancelled or rejected replacements
leave the profile unchanged.

`Backup` is available for a Ready remote and creates a portable folder under
`/ext/apps_data/tumoflip_xremote/bundles`. The folder contains a versioned
`manifest.tdeck`, the `.tdevice` record, and validated copies of every linked
`.ir` and static `.sub` source. `Restore Backup` validates the complete package
before copying its sources into a managed import folder and adding a separate
remote to the library. Repeated restores use new names and never overwrite the
previous remote. Invalid, incomplete, or cancelled restores do not create a
partial library record.

`Edit` changes the selected remote directly on the Flipper:

- Up/Down selects the remote name, linked IR remote, or an RF command.
- OK renames the selected label or chooses a replacement IR remote.
- Left/Right moves the selected RF command up or down.
- Long OK detaches the IR link or removes the selected RF command after confirmation.

Remotes are stored under `/ext/apps_data/tumoflip_xremote/devices` and reference
the original `.ir` and `.sub` files without modifying them. Missing source files
are reported in the editor and runtime UI. Updates use a temporary file and
backup rename so a failed write leaves the previously saved remote intact.
Removing a command or detaching IR removes only the remote link; the source
signal remains on the SD card.

Sub-GHz transmission keeps the firmware region gate and radio-broker ownership.
Changing-code protocols are rejected. Other protocols replay the saved command
unchanged. Reviewed 24-bit Princeton commands additionally support B1-B4
selection with a long Right press; only the documented command nibble changes.

## Standard file support

The application is compatible with standard `.ir` files. However, to ensure functionality, names within these files must align with the predefined naming scheme. If the button is not highlighted when pressed or the notification LED does not light up, the button with the appropriate name cannot be found in the file.

Button name | Description
------------|-------------------
`Power`     | Power
`Eject`     | Eject
`Setup`     | Setup/Settings
`Input`     | Input/Source
`Menu`      | Menu
`List`      | List
`Info`      | Info
`Mode`      | Mode
`Back`      | Back
`Ok`        | Enter/Ok
`Up`        | Up
`Down`      | Down
`Left`      | Left
`Right`     | Right
`Mute`      | Mute
`Vol_up`    | Volume up
`Vol_dn`    | Volume down
`Ch_next`   | Next channel
`Ch_prev`   | Previous channel
`Next`      | Jump forward
`Prev`      | Jump backward
`Fast_fo`   | Fast forward
`Fast_ba`   | Fast backward
`Play_pa`   | Play/Pause
`Pause`     | Pause
`Play`      | Play
`Stop`      | Stop

## Alternative button names
In addition to the predefined names, `XRemote` uses alternative button names to make it as easy as possible to interact with different types of IR dumps. That means if a button with the appropriate name is not found in the file, the application will try to find the same button with alternative names. Ensure this feature is enabled in the application settings before you use it.

The application stores and reads alternate names from the following file:
```
SD Card/apps_data/flipper_xremote/alt_names.txt
```

If the `Alt-Names` option is enabled in the config and the file does not exist, it will be created automatically with default values during the application's startup. You can edit, add, or remove any button or alternate name values from this file. Button names must either have only the first uppercase or be entirely lowercase. As for alternate names, they are case-insensitive. The button can have one or several comma-separated alternate names.

This is the default `alt_names.txt` file:

```
Filetype: XRemote Alt-Names
Version: 1
# 
Power: shutdown,off,on,standby
Setup: settings,config,cfg
Input: source,select
Menu: osd,gui
List: guide
Info: display
Mode: aspect,format
Back: return,exit
Ok: enter,select
Up: uparrow
Down: downarrow
Left: leftarrow
Right: rightarrow
Mute: silence,silent,unmute
Vol_up: vol+,volume+,volup,+
Vol_dn: vol-,volume-,voldown,-
Ch_next: ch+,channel+,chup
Ch_prev: ch-,channel-,chdown
Next: next,skip,ffwd
Prev: prev,back,rewind,rew
Fast_fo: fastfwd,fastforward,ff
Fast_ba: fastback,fastrewind,fb
Play_pa: playpause,play,pause
```

## Installation options

1. Install the latest stable version directly from the official [application catalog](https://lab.flipper.net/apps/flipper_xremote).
2. Install on any firmware from the community driven application catalog [FlipC](https://flipc.org/kala13x/flipper-xremote).
3. Manually install using `.fap` file:  
   - Download the `.fap` file from the [Releases](https://github.com/kala13x/flipper-xremote/releases) section of this repository.
   - Write the `.fap` file to an SD card using [qFlipper](https://docs.flipper.net/qflipper) or any your preferred SD card writer.

## Build options

1. If you already have the flipper zero firmware cloned on the Linux:
   - Connect your Flipper device to your computer using a USB cable.
   - Use deploy script from this repository to build and run the application on the device:

    ```bash
    ./deploy.sh --fw=/path/to/the/firmware -b -l
    ```

    Do not use `-l` (link) option of you are building the project directly from the `applications_user` directory of the firmware.
2. If you don't have the firmware or the Linux please refer to the [official documentation](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/documentation/AppsOnSDCard.md) for build instructions.

## Progress

- [x] Application menu
- [x] Learn new remote
- [x] Signal analyzer
- [x] Use saved remote
  - [x] General button page
  - [x] Control buttons page
  - [x] Navigation buttons page
  - [x] Player buttons page
  - [x] Custom buttons page
  - [x] Edit custom layout
  - [x] Alternative button names
  - [ ] Add or remove button
  - [ ] All buttons page
- [x] Application settings
  - [x] GUI to change settings
  - [x] Load settings from the file
  - [x] Store settings to the file
  - [x] Vertical/horizontal views
  - [x] IR command repeat count
  - [x] Exit button behavior
  - [x] Enable/disable alt names

## Screens

<table align="center">
    <tr>
        <td align="center">Main menu</td>
        <td align="center">Saved remote menu</td>
    </tr>
    <tr>
        <td><img src="https://github.com/kala13x/flipper-xremote/blob/main/screens/app_menu.png" alt="XRemote main menu"></td>
        <td><img src="https://github.com/kala13x/flipper-xremote/blob/main/screens/saved_remote_menu.png" alt="XRemote saved remote menu"></td>
    </tr>
</table>

<table align="center">
    <tr>
        <td align="center">Saved remote control apps</td>
    </tr>
    <tr>
        <td><img src="https://github.com/kala13x/flipper-xremote/blob/main/screens/saved_remote_apps.png" alt="XRemote IR applicatoions"></td>
    </tr>
</table>

<table align="center">
    <tr>
        <td align="center">Learn mode</td>
        <td align="center">Received signal</td>
    </tr>
    <tr>
        <td><img src="https://github.com/kala13x/flipper-xremote/blob/main/screens/learn_mode.png" alt="XRemote learn mode"></td>
        <td><img src="https://github.com/kala13x/flipper-xremote/blob/main/screens/signal_view.png" alt="XRemote received signal"></td>
    </tr>
</table>

<table align="center">
    <tr>
        <td align="center">Settings</td>
    </tr>
    <tr>
        <td><img src="https://github.com/kala13x/flipper-xremote/blob/main/screens/settings_menu.png" alt="XRemote settings menu"></td>
    </tr>
</table>
