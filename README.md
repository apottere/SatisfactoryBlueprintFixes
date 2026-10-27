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

## Building

The mods build like any other SML mod, from a checkout of the [SML project](https://github.com/satisfactorymodding/SatisfactoryModLoader) set up per the [modding docs](https://docs.ficsit.app/satisfactory-modding/latest/Development/BeginnersGuide/index.html).

1. Copy (or symlink) a folder from `mods/` into the SML project's `Mods/` folder.
2. Package it with Alpakit in the editor, or from the command line:

   ```
   RunUAT.bat -ScriptsForProject=<project>\FactoryGame.uproject PackagePlugin -project=<project>\FactoryGame.uproject ^
     -clientconfig=Shipping -serverconfig=Shipping -utf8output -DLCName=BlueprintFixes ^
     -build -platform=Win64 -nocompileeditor -installed -unattended
   ```

   The zip ends up in `<project>/Saved/ArchivedPlugins/<ModName>/`.

Both mods set `"TargetDenyList": [ "Editor" ]` on their module, because the editor only has stub implementations of the game classes they hook. That way the editor doesn't need to compile them.

**Windows path length:** if the SML project sits in a deep folder, UnrealBuildTool fails with *"The following action paths are longer than 260 characters"*, even with long paths enabled in Windows. Map a short drive letter to the project's *parent* folder and build through it:

```
subst S: C:\path\to\parent-of-project
RunUAT.bat ... -project=S:\SatisfactoryModLoader\FactoryGame.uproject ...
subst S: /d
```

Mapping the project folder itself, so the project ends up at the drive root, breaks UAT's argument quoting: `-remoteini="S:\"` swallows the closing quote.

## Using the tracer

`DesignerLoadTracer` writes `LogDesignerLoadTracer` warnings to `FactoryGame.log` for:

- designer loads, clears and the matching server requests;
- build gun blueprint selection;
- designer state replicated from the server;
- blueprint buttons being placed into panels.

Each entry includes the Blueprint call stack, which widgets contain each widget on that stack, and every event dispatcher binding on the clicked button. Use it to confirm the fix or to investigate related bugs. It's noisy, so remove it when you're done.
