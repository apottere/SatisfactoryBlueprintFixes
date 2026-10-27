# Notes for agents

## Repository rules

- This repository is public. Keep personal data out of everything committed, writeups and log excerpts especially: no real names, emails, usernames, local file paths, IP addresses, server, session or player names, or world coordinates. Use placeholders such as `<project>` or `<Satisfactory>`.
- Don't add AI attribution to commits or PRs: no `Co-Authored-By` trailers, no "Generated with" lines.
- Writeups go in `writeups/`, one Markdown file per bug. Add a row to the README's bug table for each one.

## Building the mods

The folders under `mods/` are SML mods, and this repository is their source of truth. They build from a checkout of the [SML project](https://github.com/satisfactorymodding/SatisfactoryModLoader), set up per the [modding docs](https://docs.ficsit.app/satisfactory-modding/latest/Development/BeginnersGuide/index.html), using the CSS Unreal Engine build.

1. Copy the mod folder from `mods/` into the SML project's `Mods/` folder. Treat that copy as disposable build output and make changes here.
2. Package it with Alpakit's UAT command. The editor can stay open:

   ```
   RunUAT.bat -ScriptsForProject=<project>\FactoryGame.uproject PackagePlugin -project=<project>\FactoryGame.uproject ^
     -clientconfig=Shipping -serverconfig=Shipping -utf8output -DLCName=<ModName> ^
     -build -platform=Win64 -nocompileeditor -installed -unattended
   ```

   The zip ends up in `<project>/Saved/ArchivedPlugins/<ModName>/<ModName>-Windows.zip`, with builds for both the Steam and Epic targets. The first shipping build of a project compiles all of FactoryGame and takes about 20 minutes; later builds take a couple of minutes.

3. To install, extract the zip into `<Satisfactory>/FactoryGame/Mods/<ModName>/`. The game must be closed, or the DLL is locked.

### Gotchas

- **Path length:** if the SML project sits in a deep folder, UnrealBuildTool fails with *"The following action paths are longer than 260 characters"*. This is a hard check in UBT's `ActionGraph.CheckPathLengths`; enabling long paths in Windows doesn't help. Map a drive letter to the project's *parent* folder and build through it:

  ```
  subst S: <parent-of-project>
  RunUAT.bat ... -project=S:\<project-folder>\FactoryGame.uproject ...
  subst S: /d
  ```

  Don't map the project folder itself. With the project at the drive root, UAT passes `-remoteini="S:\"`, and the trailing backslash swallows the closing quote.

- **Editor builds:** each mod's module sets `"TargetDenyList": [ "Editor" ]`. The SML project's editor only has stub implementations of FactoryGame, so the hooks have nothing to hook there, and this lets packaging skip compiling the module for the editor. The hook registration is also wrapped in `#if !WITH_EDITOR`.

- **Client-only:** keep `"RequiredOnRemote": false` in each `.uplugin`. The point of this repository is fixes that work against unmodded dedicated servers.

## Investigating game code

- FactoryGame's C++ headers are in the SML project's `Source/FactoryGame/Public`. The `.cpp` files there are stubs.
- The game's Blueprint and widget assets in the SML project are stubs too: variables, functions and widget trees are real, but graph bodies are empty. To see real behaviour, hook it at runtime with `DesignerLoadTracer`-style logging.
- The shipping game includes PDBs next to its DLLs, so native stack dumps (`FDebug::DumpStackTraceToLog`) resolve symbols. The first dump loads them and causes a hitch of a few seconds.
- `FFrame::GetScriptCallstack` doesn't work in shipping builds (`DO_BLUEPRINT_GUARD` is 0). Walk `FFrame::GetThreadLocalTopStackFrame()` through `PreviousTrackingFrame` instead; that chain also crosses delegate broadcasts.
- The client log is `%LOCALAPPDATA%\FactoryGame\Saved\Logs\FactoryGame.log`. The game rotates it on the next launch, so copy it before relaunching.
