# FeliCa Balance Reader for Flipper Zero

An application (.fap) for Flipper Zero that reads and displays the balance of Japanese FeliCa (NFC-F) smart cards—including Transit IC cards (Suica, PASMO, ICOCA, etc.) and popular e-money cards (Rakuten Edy, nanaco, WAON).

## Features
- **Auto-Detection & Polling**: Just launch the app and hold your card against the back of your Flipper Zero. It will automatically detect the card and read the balance.
- **Multi-Card Support**:
  - **Transit IC Cards (Suica, PASMO, ICOCA, etc.)**: Reads the latest transaction block from service `0x090f`.
  - **Rakuten Edy**: Reads the balance block from service `0x170f`.
  - **nanaco**: Reads the balance block from service `0x5597`.
  - **WAON**: Reads the balance block from service `0x6817`.
- **Anti-Crash & Anti-Freeze Design**: Intercepts the FeliCa poller state machine to bypass the standard full service traversal (which scans `0xFFFF` combinations). By reading the required balance service blocks directly, it consumes negligible memory, completes instantly, and completely avoids watchdog resets or device reboots.
- **Clear Status Display**: Displays "Hold FeliCa card to back", "Reading card...", card type, and balance.
- **Smart Card Identification**: Displays the name of the recognized card type. If a FeliCa card is successfully read but does not contain recognized balance services, it clearly displays "No balance info".
- **One-Touch Scan Reset**: Press the **OK** button to reset the scanning state and scan another card.

## Build Requirements
This application requires the Flipper Build Tool (`ufbt`). 

If you have `uv` (a fast Python package installer and resolver) installed, you can build without any manual setup.

## Building the App

To compile the application to a `.fap` package:

```bash
uvx ufbt faps
```

The compiled application will be generated as `dist/felica_balance.fap`.

## Deploying & Running on Device

With your Flipper Zero connected via USB, run the following command to build, flash, and launch the application automatically:

```bash
uvx ufbt launch
```

If you prefer to install it manually, copy the compiled `dist/felica_balance.fap` file to your Flipper Zero's SD card directory `/apps/NFC/`.

## Usage
1. Open the application (**FeliCa Balance**) from the Flipper Zero application menu.
2. When the screen says "**Hold FeliCa card to back**", place your card against the back of the Flipper Zero (where the NFC antenna is located).
3. The app will detect the card and display "**Reading card...**".
4. Once the read is complete, it will display the card name and the balance formatted with thousands separators (e.g., `1,000` or `12,500`) in large numbers.
5. If the card is a FeliCa card but not one of the supported payment/transit cards, it will display "**No balance info**".
6. Press the **OK** button to clear the current result and scan a new card.
7. Press the **Back** button to exit the application.

## Files
- `felica_balance.c`: Main source code containing UI drawing, NFC polling worker thread, direct FeliCa block reader, and currency parser.
- `application.fam`: FAP manifest file describing metadata, category (`NFC`), stack size, and icon.
- `felica_balance.png`: 10x10 pixel 1-bit menu icon representing a contactless card.
