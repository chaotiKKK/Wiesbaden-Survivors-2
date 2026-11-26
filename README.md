# Wiesbaden Survivors 2.0

Top-down arena roguelite built in Unreal Engine 5.8. Seven Wiesbaden-inspired arenas, 28 enemy archetypes, eight three-phase bosses, 40 weapons with 160 tiers, 44 items and a wave/shop loop.

## Windows build

Download the latest `WiesbadenSurvivors2-Windows-*.zip` from the repository's **Releases** page. Unpack the complete archive and start:

```text
WiesbadenSurvivors2.exe
```

The `Engine/` and `WiesbadenSurvivors2/` folders must stay next to the launcher. The build is a 64-bit Unreal Engine Shipping build for Windows.

### Controls

- **WASD / arrow keys / left stick**: move
- **Mouse**: aim; weapons fire automatically
- **1 / 2 / 3**: pick a shop item
- **R**: reroll the shop
- **F11**: toggle fullscreen
- **Esc**: pause

## Source layout

- `Source/WS2Core`, `Source/WS2Gameplay`, `Source/WS2UI` — C++ game modules
- `Content/Data` — heroes, enemies, bosses, weapons, items, arenas, VAT metadata
- `Art/scripts` — headless Blender generators, LOD chains and turnarounds
- `Tools` — UE import, VAT and budget-validator scripts

## Verification

The shipped Windows build comes from the same commit and content set that produced the recorded proof runs:

- 7 arenas, all headless proof runs exit 0
- 28/28 enemy types with spawn and kill in every run
- 8/8 bosses with phases 1 → 2 → 3
- 160/160 weapon tiers, 44 items, zero overlap pairs
- frame budget pass in every arena; no sustained window below 60 fps
- 80 content assets validated with 0 warnings and 0 failures, including the four-level `SM_Leonidas` LOD chain

## Build requirements

- Unreal Engine 5.8
- Visual Studio 2022 with the Windows C++ toolchain
- Windows 10/11, DirectX 12 capable GPU

Generated `Binaries/`, `Intermediate/`, `Saved/`, `Build/` and `Dist/` directories are intentionally not committed.
