# macOS Installation

Thank you for downloading Sevenpack Alpha.

## Installation

1. Open the downloaded disk image (.dmg).
2. Copy what you need:

**VST3 plugin**: drag `SevenpackAlpha.vst3` onto the `VST3 (drag plugin here)` shortcut, or copy it to:

```
/Library/Audio/Plug-Ins/VST3/
```

**Standalone application**: drag `SevenpackAlpha.app` onto the `Applications (drag app here)` shortcut.

3. If prompted, enter your administrator password.
4. Launch your DAW and rescan your plugins if necessary.

## Apple Security

If macOS prevents the plugin or the application from opening because it is from an unidentified developer:

1. Open **System Settings → Privacy & Security**.
2. Scroll to the **Security** section.
3. Click **Open Anyway** if the option appears.
4. Restart your DAW (or open the application again).

If the option does not appear, open **Terminal** and run:

```
sudo xattr -cr /Library/Audio/Plug-Ins/VST3/SevenpackAlpha.vst3
sudo xattr -cr /Applications/SevenpackAlpha.app
```

## Requirements

* macOS 11 (Big Sur) or later
* Apple Silicon or Intel Mac
* VST3 compatible host, or the standalone application

If you experience any issues, feel free to contact us.

Enjoy!

**SIDE KEY**
