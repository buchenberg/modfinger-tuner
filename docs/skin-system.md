# Skin System

Colours are **data-driven at runtime** from JSON files — no recompile, no restart. Two
defaults are bundled into the binary; extras live in a per-user folder and show up
instantly the next time the skin menu opens.

## JSON schema

```json
{
  "name": "80s Neon",
  "colors": {
    "background": "0xff0a0a12",
    "panel":      "0xff15151f",
    "primary":    "0xffeaff00",
    "secondary":  "0xffff2db3",
    "muted":      "0xff8a5a9a",
    "zoneLit":    "0xff39ff14",
    "zoneIdle":   "0xff1a7a14",
    "marker":     "0xff8a8a96"
  }
}
```

Colours are `0xAARRGGBB` hex strings (8 digits). All 8 slots must be present.

### Semantic colour slots

| Slot | Used for |
|------|----------|
| `background` | plugin background |
| `panel` | cents-bar track, label edit background, skin button |
| `primary` | note letter, needle, cents number, label edit outline |
| `secondary` | frequency readout, reference label text |
| `muted` | "listening…", markings (-50/0/+50), non-confident note |
| `zoneLit` | in-tune green band when the needle is inside (±5¢) |
| `zoneIdle` | in-tune band otherwise |
| `marker` | center tick on the cents bar |

The `zoneLit` and `zoneIdle` colours are rendered with alpha 0.85 and 0.22 respectively
in the paint method — the palette stores the base colour, not the alpha.

## Runtime loading flow

```
Plugin editor constructor
  │
  └─ SkinLibrary::reload()
       │
       ├─ Add defaults from BinaryData
       │    └─ juce_add_binary_data(ModfingerSkinsData SOURCES skins/*.json)
       │       → BinaryData::dark_json / BinaryData::eighties_neon_json
       │       → JSON::parse → TunerPalette
       │
       └─ Scan user folder
            └─ ~/Library/Application Support/ModfingerTuner/skins/*.json  (macOS)
               %APPDATA%\ModfingerTuner\skins\*.json                      (Windows)
               ~/.config/ModfingerTuner/skins/*.json                      (Linux)
            └─ Parse → TunerPalette
            └─ User skin with duplicate name O V E R R I D E S bundled one
```

## Skin selector UI

- **"Skin: …" TextButton** (bottom-right) — themed pill, opens a PopupMenu.
- **Menu rescans on every open** — `showSkinMenu()` calls `skinLibrary_.reload()`, so a
  file dropped into the user folder appears next time the menu opens.
- **"Import skin…"** — async FileChooser, copies JSON to user folder, reloads, selects
  the imported skin.
- **"Open skins folder…"** — opens the user folder in the OS file manager.

## How the active skin is persisted

The active skin is stored **by name** as a property (`skinName`) on the APVTS
`ValueTree` state (not a host parameter). This keeps the skin-set dynamic — the plugin
never needs to know all skin names at compile time.

- `processorRef_.getSkinName()` — reads property, default "80s Neon"
- `processorRef_.setSkinName(name)` — sets property, persisted in host state
- On host state restore (`setStateInformation`), `skinName` is restored; the editor's
  timer detects a name mismatch and calls `applySkinByName`.

## Adding a new bundled skin

1. Add a JSON file in `skins/` (same schema).
2. Add the path to `juce_add_binary_data(ModfingerSkinsData SOURCES …)` in
   `CMakeLists.txt`.
3. Rebuild.

The `SkinLibrary` already loads all bundled entries from BinaryData — no C++ changes
needed (the bundled list is compiled in, but because the library scans BinaryData inline,
you must rebuild to pick up a new bundled skin).

## Adding a user skin (no rebuild)

1. Copy a `.json` file into the per-user skins folder.
2. Open the skin menu — it rescans and the new skin appears.
3. Select it.
