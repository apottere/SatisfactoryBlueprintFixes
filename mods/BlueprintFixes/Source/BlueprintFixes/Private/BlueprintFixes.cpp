#include "BlueprintFixes.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Patching/NativeHookManager.h"

DEFINE_LOG_CATEGORY(LogBlueprintFixes);

/**
 * Fix for the Blueprint Designer loading whatever blueprint you pick in the build gun's menu.
 *
 * The build menu's Blueprints tab and the designer's "Load Blueprint" picker are two instances of BPW_BlueprintMenu.
 * They reuse each other's blueprint buttons and subcategory sections, and each one binds its handlers to a reused widget
 * without removing the other menu's bindings. A click on such a button in the build menu then also runs the hidden
 * designer picker's OnBlueprintButtonClicked, which loads the blueprint into the designer.
 * See writeups/designer-loads-blueprint-from-build-menu.md in the repository for the full analysis.
 *
 * The fix: whenever one of these widgets is placed into a panel, drop every binding it has to a BPW_BlueprintMenu other
 * than the one it is being placed into. A widget belongs to exactly one menu, so no binding that is still needed is lost.
 */
namespace BlueprintFixes
{
	static const FName BlueprintMenuClassName(TEXT("BPW_BlueprintMenu_C"));
	static const FName BlueprintButtonClassName(TEXT("BPW_BuildMenuBlueprintButton_C"));
	static const FName SubCategoryButtonClassName(TEXT("Widget_BuildMenu_SubCategoryButton_C"));

	static bool IsOfClassNamed(const UObject* Object, FName ClassName)
	{
		for (const UClass* Class = Object ? Object->GetClass() : nullptr; Class; Class = Class->GetSuperClass())
		{
			if (Class->GetFName() == ClassName)
			{
				return true;
			}
		}
		return false;
	}

	/** Returns the user widget whose widget tree contains this widget, walking up through panels. */
	static UUserWidget* GetOwningUserWidget(const UWidget* Widget)
	{
		const UWidget* Current = Widget;
		while (Current)
		{
			if (const UPanelWidget* Parent = Current->GetParent())
			{
				Current = Parent;
				continue;
			}
			const UWidgetTree* Tree = Cast<UWidgetTree>(Current->GetOuter());
			return Tree ? Cast<UUserWidget>(Tree->GetOuter()) : nullptr;
		}
		return nullptr;
	}

	static UUserWidget* FindOwningBlueprintMenu(const UWidget* Widget)
	{
		for (UUserWidget* Owner = GetOwningUserWidget(Widget); Owner; Owner = GetOwningUserWidget(Owner))
		{
			if (IsOfClassNamed(Owner, BlueprintMenuClassName))
			{
				return Owner;
			}
		}
		return nullptr;
	}

	/** Removes every event dispatcher binding on Widget that points at a BPW_BlueprintMenu other than OwningMenu. */
	static void RemoveStaleMenuBindings(UWidget* Widget, const UUserWidget* OwningMenu)
	{
		int32 NumRemoved = 0;
		TSet<const UObject*> StaleMenus;
		for (TFieldIterator<FMulticastInlineDelegateProperty> It(Widget->GetClass()); It; ++It)
		{
			FMulticastScriptDelegate* Delegate = It->ContainerPtrToValuePtr<FMulticastScriptDelegate>(Widget);
			for (UObject* Bound : Delegate->GetAllObjects())
			{
				if (Bound != OwningMenu && IsOfClassNamed(Bound, BlueprintMenuClassName))
				{
					Delegate->RemoveAll(Bound);
					StaleMenus.Add(Bound);
					++NumRemoved;
				}
			}
		}
		for (const UObject* StaleMenu : StaleMenus)
		{
			UE_LOG(LogBlueprintFixes, Log, TEXT("Removed stale bindings from %s to %s (widget now belongs to %s); %d binding(s) removed in total"),
				*Widget->GetName(), *StaleMenu->GetPathName(), *OwningMenu->GetPathName(), NumRemoved);
		}
	}
}

void FBlueprintFixesModule::StartupModule()
{
	// The editor only has stub implementations of FactoryGame's widgets, so there is nothing to fix there.
#if !WITH_EDITOR
	RegisterHooks();
#endif
}

void FBlueprintFixesModule::ShutdownModule()
{
}

void FBlueprintFixesModule::RegisterHooks()
{
	using namespace BlueprintFixes;

	SUBSCRIBE_METHOD_EXPLICIT(UPanelSlot*(UPanelWidget::*)(UWidget*), UPanelWidget::AddChild, [](auto& Scope, UPanelWidget* Panel, UWidget* Content)
	{
		const bool bIsBlueprintButton = IsOfClassNamed(Content, BlueprintButtonClassName);
		const bool bIsSubCategory = !bIsBlueprintButton && IsOfClassNamed(Content, SubCategoryButtonClassName);
		if (!bIsBlueprintButton && !bIsSubCategory)
		{
			return;
		}

		const UUserWidget* OwningMenu = FindOwningBlueprintMenu(Panel);
		if (!OwningMenu)
		{
			return;
		}

		RemoveStaleMenuBindings(Content, OwningMenu);

		// A reused subcategory section brings its blueprint buttons along with it.
		if (bIsSubCategory)
		{
			if (const UWidgetTree* Tree = CastChecked<UUserWidget>(Content)->WidgetTree)
			{
				Tree->ForEachWidget([OwningMenu](UWidget* Child)
				{
					if (IsOfClassNamed(Child, BlueprintButtonClassName))
					{
						RemoveStaleMenuBindings(Child, OwningMenu);
					}
				});
			}
		}
	});

	UE_LOG(LogBlueprintFixes, Display, TEXT("Blueprint Fixes hooks installed"));
}

IMPLEMENT_MODULE(FBlueprintFixesModule, BlueprintFixes)
