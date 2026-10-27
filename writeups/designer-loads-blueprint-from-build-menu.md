# Blueprint Designer loads the blueprint you pick in the build gun

**Status:** root cause found; client-side fix in [`mods/BlueprintFixes`](../mods/BlueprintFixes), verified in game
**Game version investigated:** 1.x, build `++FactoryGame+rel-main-anniversary-2026-CL-513885`, client on a dedicated server
**Existing report:** [Blueprint Designer Replaces contents with Blueprint selected from the build menu](https://questions.satisfactorygame.com/post/637c4eb6ca608e0803526288) (open since Update 7, 30+ duplicates)

## Symptom

You pick a blueprint from the Blueprints tab of the build gun's menu (the build menu). A Blueprint Designer in the world clears its contents and loads that blueprint, even though you aren't using the designer and may be far away from it. The build gun also selects the blueprint normally.

Earlier reports describe it as random: it only happens for some blueprints, restarting sometimes clears it, and dismantling the designer sometimes clears it.

## Reproduction

1. Build a Blueprint Designer and open its terminal.
2. Click **Load Blueprint**, open a blueprint category, and load a blueprint from it.
3. Close the designer.
4. Open the build menu, go to the Blueprints tab and open the same category.
5. Click a blueprint in that category. The designer loads it.

Blueprints in categories you never opened in the designer's picker aren't affected, which is why it looks random.

The investigated sessions always loaded a blueprint in step 2. The stale bindings are created when the picker builds its list, so just opening the picker is probably enough, but that wasn't tested.

## Root cause

The build menu's Blueprints tab and the designer's load picker are two separate instances of the same widget, `BPW_BlueprintMenu`:

| List | Owner chain |
| --- | --- |
| Build menu | `BPW_BlueprintMenu <- Widget_SlidingTabs <- Widget_BuildMenu_C` |
| Designer load picker | `BPW_BlueprintMenu <- BPW_BlueprintSelector <- Widget_BlueprintDesigner_C` |

When either list builds a category (`SetCurrentCategory` → `Generate Sub Catgeories` → `AddBlueprintButton`), it fills it with subcategory sections (`Widget_BuildMenu_SubCategoryButton`) and blueprint buttons (`BPW_BuildMenuBlueprintButton`), then binds its handlers to each button's event dispatchers (`OnClicked`, `OnHovered`, `OnSelect`, `OnSelectToggle`, `OnClearSelect`, `OnSettingsClicked`, `OnUnhovered`, `OnButtonMoved`, `OnDragBegin`, `OnControllerDragBegin`).

**The two lists reuse each other's widget instances.** When the build menu builds a category the designer's picker already built, it gets back the same subcategory section and button objects the designer's picker created, moves them into its own tree, and binds its handlers on top. Nothing removes the designer picker's bindings. Every reused button now has two subscribers on each dispatcher.

Clicking such a button in the build menu broadcasts `OnClicked` to both lists:

1. The designer picker's `OnBlueprintButtonClicked` → `BPW_BlueprintSelector.OnBlueprintClicked` → `Widget_BlueprintDesigner.OnBlueprintSelected` → `AttemptLoad` → `AFGBuildableBlueprintDesigner::LoadBlueprintIntoDesigner` → `UFGBlueprintRemoteCallObject::Server_LoadBlueprintInDesigner`. The server clears the designer and loads the blueprint.
2. The build menu's `OnBlueprintButtonClicked` → `Widget_BuildMenu.OnBlueprintClicked` → `UFGBuildGunStateBuild::SetActiveBlueprintDescriptor`. This is the expected behaviour.

The designer window and its picker are only hidden when closed, not destroyed, so their handlers keep working. Event dispatchers don't care whether the subscriber is visible.

### How this explains the reported behaviour

- **Only some blueprints:** only buttons in categories the designer's picker built are shared.
- **Distance doesn't matter:** the request comes from your client's UI; it isn't tied to where you stand.
- **Restarting the client helps until it doesn't:** a restart recreates the widgets, and the bug returns the next time you open those categories in the designer's picker and then in the build menu.
- **Dismantling the designer helps:** that destroys the designer window that holds the stale handlers.
- **Restarting the server "fixes" it:** probably because the client reconnects, which also recreates the widgets. This wasn't tested.

## Evidence

Collected with a client-only diagnostic mod ([`mods/DesignerLoadTracer`](../mods/DesignerLoadTracer)) that hooks the designer load path and the panel-add path, and logs Blueprint call stacks, widget ownership and dispatcher bindings. Excerpts from one session, trimmed.

**1. The designer's picker builds its list (frame 2775).** Button `_2147462839` goes into subcategory section `_2147462849`. In the same pass, the picker also fills subcategory section `_2147462943` (event #9). All of these widgets are owned by the designer:

```
[#16] Button BPW_BuildMenuBlueprintButton_C_2147462839 added to panel mBuildingsContainer in:
      Widget_BuildMenu_SubCategoryButton_C_2147462849 <- ... <- BPW_BlueprintMenu <- BPW_BlueprintSelector <- Widget_BlueprintDesigner_C_2147463355
  Blueprint stack:
    #0 BPW_BlueprintMenu_C:AddBlueprintButton
    #1 BPW_BlueprintMenu_C:Generate Sub Catgeories
    #2 BPW_BlueprintMenu_C:SetCurrentCategory
```

**2. The build menu builds the same category (frame 3626).** It doesn't create new widgets. It takes the button and subcategory section `_2147462943` that the designer's picker made, and puts the button into a different section than before:

```
[#81] Button BPW_BuildMenuBlueprintButton_C_2147462839 added to panel mBuildingsContainer in:
      Widget_BuildMenu_SubCategoryButton_C_2147462943 <- Widget_Scrollbox <- mBrowser <- BPW_BlueprintMenu <- Widget_SlidingTabs <- Widget_BuildMenu_C_2147457969
  Blueprint stack:
    #0 BPW_BlueprintMenu_C:AddBlueprintButton
    #1 BPW_BlueprintMenu_C:Generate Sub Catgeories
    #2 BPW_BlueprintMenu_C:SetCurrentCategory
```

**3. Clicking the button in the build menu (frame 3800).** It sits in the build menu, but both lists are bound to every dispatcher:

```
[#104] Clicked button BPW_BuildMenuBlueprintButton_C_2147462839 is in:
       Widget_BuildMenu_SubCategoryButton_C_2147462943 <- ... <- BPW_BlueprintMenu <- Widget_SlidingTabs <- Widget_BuildMenu_C_2147457969 (all visible)
    dispatcher bindings:
      OnClicked:
        -> BPW_BlueprintMenu  [in: Widget_Window_DarkMode(hidden) <- BPW_BlueprintSelector(hidden) <- Widget_BlueprintDesigner_C_2147463355(hidden)]
        -> BPW_BlueprintMenu  [in: Widget_SlidingTabs(visible) <- Widget_Window_DarkMode(visible) <- Widget_BuildMenu_C_2147457969(visible)]
      (same two subscribers on all 10 dispatchers)
```

**4. The hidden designer picker handles the click first and loads into the designer:**

```
[#104] execLoadBlueprintIntoDesigner on Build_BlueprintDesigner_MK2_C_2147471818 (record='[Rail2] 90deg', buildables=106)
  Blueprint stack:
    #0 Widget_BlueprintDesigner_C:AttemptLoad                     [visible=0]
    #2 Widget_BlueprintDesigner_C:OnBlueprintSelected             [visible=0]
    #4 BPW_BlueprintSelector_C:OnBlueprintClicked                 [visible=0]
    #5 BPW_BlueprintMenu_C:OnBlueprintButtonClicked               [designer picker, hidden]
    #6 BPW_BuildMenuBlueprintButton_C:OnMouseButtonUp             [button _2147462839, in the build menu, visible]
[#106] Server_LoadBlueprintInDesigner RPC: blueprint='1deg rotation'
[#108] BuildGun SetActiveBlueprintDescriptor: '1deg rotation'     <- the build menu's own handler, same click
[#111] OnRep_Buildables (from server): record='1deg rotation', buildables=2   <- the designer's 106 buildings are gone
```

### Not confirmed

How the build menu gets hold of the designer picker's widgets. A hook on `UWidgetBlueprintLibrary::Create` never fired for these widgets, so they aren't made by a plain Blueprint *Create Widget* node. The reuse pattern looks like a widget pool shared by both lists, which hands widgets out again without clearing their dispatcher bindings. The reuse and the double bindings are directly observed; the pool is an inference.

## Suggested fix for Coffee Stain

Any one of these fixes it:

- Unbind a list's handlers from its buttons and subcategory sections when the list clears or rebuilds a category, or when it releases them back to wherever they're shared from.
- Don't share these widget instances between the build menu and the designer's picker (separate pools or plain `CreateWidget`).
- In `BPW_BlueprintMenu.OnBlueprintButtonClicked`, ignore clicks from buttons that aren't in this list's own widget tree.

## Workaround: the `BlueprintFixes` mod

[`mods/BlueprintFixes`](../mods/BlueprintFixes) is a client-only SML mod; the server doesn't need it. It hooks `UPanelWidget::AddChild`. When a `BPW_BuildMenuBlueprintButton` or `Widget_BuildMenu_SubCategoryButton` is placed into a panel, it finds the `BPW_BlueprintMenu` that panel belongs to and removes the widget's dispatcher bindings to any other `BPW_BlueprintMenu`. For a subcategory section it also cleans the blueprint buttons inside it. A widget belongs to exactly one list, so this only removes leftover bindings. Each cleanup is logged under `LogBlueprintFixes`.

### Verification

The reproduction steps above were repeated with `BlueprintFixes` 0.1.0 and the tracer installed, on the same client and server.

When the build menu took over the designer picker's widgets, the fix removed the stale bindings. That was 14 buttons, each with all 10 of the designer picker's bindings:

```
LogBlueprintFixes: Removed stale bindings from BPW_BuildMenuBlueprintButton_C_2147464792
  to ...Widget_BlueprintDesigner_C_2147465308...BPW_BlueprintSelector...BPW_BlueprintMenu
  (widget now belongs to ...Widget_BuildMenu_C_2147459344...BPW_BlueprintMenu); 10 binding(s) removed in total
```

Clicking that button in the build menu then reached only the build menu's list. The designer didn't load anything and kept its contents:

```
[#104] BuildGun SetActiveBlueprintDescriptor: '1deg rotation'
[#104] Clicked button BPW_BuildMenuBlueprintButton_C_2147464792 is in: ... <- Widget_BuildMenu_C_2147459344 (visible)
      OnClicked:
        -> BPW_BlueprintMenu  [in: Widget_SlidingTabs(visible) <- Widget_Window_DarkMode(visible) <- Widget_BuildMenu_C_2147459344(visible)]
(no execLoadBlueprintIntoDesigner / Server_LoadBlueprintInDesigner follows; designer record stays '[Rail2] 90deg')
```
