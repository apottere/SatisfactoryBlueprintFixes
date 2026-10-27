# Satisfactory Blueprint Fixes

Client-side fixes and investigations for bugs in Satisfactory's blueprint and Blueprint Designer features. Everything here is an [SML](https://github.com/satisfactorymodding/SatisfactoryModLoader) mod that runs on the client only, so it works on unmodded dedicated servers.

## Contents

| Path | What it is |
| --- | --- |
| [`mods/BlueprintFixes`](mods/BlueprintFixes) | The fix mod. Install this one to get the fixes. |
| [`mods/DesignerLoadTracer`](mods/DesignerLoadTracer) | Diagnostic mod used for the investigation. It only logs; it doesn't change game behaviour. |
| [`writeups/`](writeups) | Root-cause write-ups, one per bug. |

## Fixed bugs

| Bug | Write-up | Fixed by |
| --- | --- | --- |
| A Blueprint Designer clears itself and loads the blueprint you pick in the build gun's menu | [designer-loads-blueprint-from-build-menu.md](writeups/designer-loads-blueprint-from-build-menu.md) | `BlueprintFixes` 0.1.0 (verified in game, CL 513885) |

## Installing

1. Install SML 3.12 or later, for example through [Satisfactory Mod Manager](https://smm.ficsit.app/).
2. Extract `BlueprintFixes-Windows.zip` into `<Satisfactory>/FactoryGame/Mods/BlueprintFixes/`, so the `.uplugin` sits directly in that folder.
3. Launch the game. `FactoryGame.log` should contain `LogBlueprintFixes: Display: Blueprint Fixes hooks installed`.

The server doesn't need the mod (`RequiredOnRemote` is `false`).

## Using the tracer

`DesignerLoadTracer` writes `LogDesignerLoadTracer` warnings to `FactoryGame.log` for:

- designer loads, clears and the matching server requests;
- build gun blueprint selection;
- designer state replicated from the server;
- blueprint buttons being placed into panels.

Each entry includes the Blueprint call stack, which widgets contain each widget on that stack, and every event dispatcher binding on the clicked button. Use it to confirm the fix or to investigate related bugs. It's noisy, and its native stack dumps cause a hitch, so remove it when you're done.
