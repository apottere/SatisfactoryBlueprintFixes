#include "DesignerLoadTracer.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Buildables/FGBuildableBlueprintDesigner.h"
#include "Components/PanelWidget.h"
#include "Equipment/FGBuildGunBuild.h"
#include "FGBlueprintSubsystem.h"
#include "FGFactoryBlueprintTypes.h"
#include "FGRemoteCallObject.h"
#include "Patching/NativeHookManager.h"
#include "UObject/Stack.h"

DEFINE_LOG_CATEGORY(LogDesignerLoadTracer);

/**
 * Diagnostic for the vanilla bug where selecting a blueprint in the build gun makes a Blueprint Designer
 * reset and load that blueprint. Every hook only logs; none of them change game behavior.
 * Search FactoryGame.log for "LogDesignerLoadTracer".
 */
namespace DesignerLoadTracer
{
	static int32 GEventCounter = 0;

	static const TCHAR* NetModeToString(const UObject* WorldContext)
	{
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		if (!World)
		{
			return TEXT("NoWorld");
		}
		switch (World->GetNetMode())
		{
		case NM_Standalone: return TEXT("Standalone");
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		case NM_ListenServer: return TEXT("ListenServer");
		case NM_Client: return TEXT("Client");
		default: return TEXT("Unknown");
		}
	}

	static FString DescribeDesigner(AFGBuildableBlueprintDesigner* Designer)
	{
		if (!IsValid(Designer))
		{
			return TEXT("<null designer>");
		}
		const FBlueprintRecord Record = Designer->GetCurrentBlueprintRecord();
		return FString::Printf(TEXT("%s (record='%s', buildables=%d, location=%s)"),
			*Designer->GetName(),
			*Record.BlueprintName,
			Designer->GetBuildablesInBlueprintDesigner().Num(),
			*Designer->GetActorLocation().ToCompactString());
	}

	static FString DescribeDescriptor(UFGBlueprintDescriptor* Descriptor)
	{
		return IsValid(Descriptor) ? FString::Printf(TEXT("'%s'"), *Descriptor->GetBlueprintNameAsString()) : TEXT("<null descriptor>");
	}

	static const FName BlueprintButtonClassName(TEXT("BPW_BuildMenuBlueprintButton_C"));

	static bool IsBlueprintButton(const UObject* Object)
	{
		for (const UClass* Class = Object ? Object->GetClass() : nullptr; Class; Class = Class->GetSuperClass())
		{
			if (Class->GetFName() == BlueprintButtonClassName)
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Describes where a widget sits as the chain of user widgets that contain it, e.g.
	 * "BPW_BlueprintMenu(visible) <- BPW_BlueprintSelector(hidden) <- Widget_BlueprintDesigner_C_123(hidden)".
	 * Several menus share the name BPW_BlueprintMenu, so the outer chain is what tells them apart.
	 */
	static FString DescribeWidgetOwners(const UWidget* Widget)
	{
		if (!Widget)
		{
			return TEXT("<null>");
		}
		TStringBuilder<512> Out;
		const UWidget* Current = Widget;
		for (int32 Guard = 0; Current && Guard < 128; ++Guard)
		{
			if (const UPanelWidget* Parent = Current->GetParent())
			{
				Current = Parent;
				continue;
			}
			const UWidgetTree* Tree = Cast<UWidgetTree>(Current->GetOuter());
			const UUserWidget* Owner = Tree ? Cast<UUserWidget>(Tree->GetOuter()) : nullptr;
			if (!Owner)
			{
				break;
			}
			Out.Appendf(TEXT("%s%s(%s)"), Out.Len() ? TEXT(" <- ") : TEXT(""), *Owner->GetName(), Owner->IsVisible() ? TEXT("visible") : TEXT("hidden"));
			Current = Owner;
		}
		if (Out.Len() == 0)
		{
			return Widget->GetParent() ? TEXT("<panel outside any user widget>") : TEXT("<detached: not in any panel>");
		}
		return FString(Out.ToString());
	}

	/** Lists every object bound to each event dispatcher on the object, with the function each one runs. */
	static FString DescribeDispatcherBindings(UObject* Object)
	{
		TStringBuilder<2048> Out;
		for (TFieldIterator<FMulticastDelegateProperty> It(Object->GetClass()); It; ++It)
		{
			const FMulticastScriptDelegate* Delegate = It->GetMulticastDelegate(It->ContainerPtrToValuePtr<void>(Object));
			if (!Delegate || !Delegate->IsBound())
			{
				continue;
			}
			Out.Appendf(TEXT("\n      %s:"), *It->GetName());
			for (UObject* Bound : Delegate->GetAllObjects())
			{
				Out.Appendf(TEXT("\n        -> %s  [in: %s]"), *Bound->GetName(), *DescribeWidgetOwners(Cast<UWidget>(Bound)));
			}
			Out.Appendf(TEXT("\n        functions: %s"), *Delegate->ToString<UObject>());
		}
		return Out.Len() ? FString(Out.ToString()) : TEXT("\n      <no bound dispatchers>");
	}

	/**
	 * Walks the thread-local script frame chain. Unlike FFrame::GetScriptCallstack this works in shipping builds,
	 * and PreviousTrackingFrame links across ProcessEvent boundaries (delegate broadcasts, input events).
	 * Blueprint buttons found on the stack are collected so their placement and bindings can be dumped.
	 */
	static FString DescribeScriptStack(int32 MaxDepth = 64, TArray<UObject*>* OutButtons = nullptr)
	{
		TStringBuilder<2048> Out;
		int32 Depth = 0;
		for (const FFrame* Frame = FFrame::GetThreadLocalTopStackFrame(); Frame && Depth < MaxDepth; Frame = Frame->PreviousTrackingFrame, ++Depth)
		{
			Out.Appendf(TEXT("\n    #%d %s  [object %s]"), Depth, Frame->Node ? *Frame->Node->GetPathName() : TEXT("<null function>"), *GetNameSafe(Frame->Object));
			if (const UWidget* Widget = Cast<UWidget>(Frame->Object))
			{
				Out.Appendf(TEXT(" [visible=%d in: %s]"), Widget->IsVisible(), *DescribeWidgetOwners(Widget));
			}
			if (OutButtons && IsBlueprintButton(Frame->Object))
			{
				OutButtons->AddUnique(Frame->Object);
			}
		}
		if (Depth == 0)
		{
			return TEXT("\n    <no Blueprint frames: called from native code>");
		}
		return FString(Out.ToString());
	}

	static void LogEvent(const FString& What, const UObject* WorldContext, bool bDumpNativeStack, bool bDumpButtons = false, int32 MaxScriptDepth = 64)
	{
		const int32 EventId = ++GEventCounter;
		TArray<UObject*> Buttons;
		const FString ScriptStack = DescribeScriptStack(MaxScriptDepth, bDumpButtons ? &Buttons : nullptr);
		UE_LOG(LogDesignerLoadTracer, Warning, TEXT("[#%d] [%s] [frame %llu] %s\n  Blueprint stack:%s"),
			EventId, NetModeToString(WorldContext), GFrameCounter, *What, *ScriptStack);
		for (UObject* Button : Buttons)
		{
			const UWidget* ButtonWidget = Cast<UWidget>(Button);
			UE_LOG(LogDesignerLoadTracer, Warning, TEXT("[#%d] Clicked button %s is in: %s\n    dispatcher bindings:%s"),
				EventId, *Button->GetName(), *DescribeWidgetOwners(ButtonWidget), *DescribeDispatcherBindings(Button));
		}
		if (bDumpNativeStack)
		{
			FDebug::DumpStackTraceToLog(*FString::Printf(TEXT("[#%d] Native stack:"), EventId), ELogVerbosity::Warning);
		}
	}
}

void FDesignerLoadTracerModule::StartupModule()
{
	// The editor only has stub implementations of FactoryGame, so there is nothing to hook there.
#if !WITH_EDITOR
	RegisterHooks();
#endif
}

void FDesignerLoadTracerModule::ShutdownModule()
{
}

void FDesignerLoadTracerModule::RegisterHooks()
{
	using namespace DesignerLoadTracer;

	// The designer's own load entry point (called by the designer UI, forwards to the server on clients).
	SUBSCRIBE_METHOD(AFGBuildableBlueprintDesigner::LoadBlueprintIntoDesigner, [](auto& Scope, AFGBuildableBlueprintDesigner* Designer, UFGBlueprintDescriptor* Descriptor, AFGPlayerController* Controller)
	{
		LogEvent(FString::Printf(TEXT("LoadBlueprintIntoDesigner: designer=%s descriptor=%s"), *DescribeDesigner(Designer), *DescribeDescriptor(Descriptor)), Designer, true);
	});

	// Blueprint thunk for the same function, in case the native call was inlined into it.
	SUBSCRIBE_METHOD(AFGBuildableBlueprintDesigner::execLoadBlueprintIntoDesigner, [](auto& Scope, UObject* Context, FFrame& Stack, void* Result)
	{
		LogEvent(FString::Printf(TEXT("execLoadBlueprintIntoDesigner (Blueprint call) on %s"), *DescribeDesigner(Cast<AFGBuildableBlueprintDesigner>(Context))), Context, false, true);
	});

	SUBSCRIBE_METHOD(AFGBuildableBlueprintDesigner::DismantleCurrentBuildables, [](auto& Scope, AFGBuildableBlueprintDesigner* Designer, AFGPlayerController* Controller)
	{
		LogEvent(FString::Printf(TEXT("DismantleCurrentBuildables: designer=%s"), *DescribeDesigner(Designer)), Designer, true);
	});

	// Client-side callers of the designer server RPCs.
	SUBSCRIBE_METHOD(UFGBlueprintRemoteCallObject::Server_LoadBlueprintInDesigner, [](auto& Scope, UFGBlueprintRemoteCallObject* RCO, AFGBuildableBlueprintDesigner* Designer, AFGPlayerController* Controller, const FString& BlueprintName)
	{
		LogEvent(FString::Printf(TEXT("Server_LoadBlueprintInDesigner RPC: designer=%s blueprint='%s'"), *DescribeDesigner(Designer), *BlueprintName), RCO, true);
	});

	SUBSCRIBE_METHOD(UFGBlueprintRemoteCallObject::Server_ClearBlueprintDesigner, [](auto& Scope, UFGBlueprintRemoteCallObject* RCO, AFGBuildableBlueprintDesigner* Designer, AFGPlayerController* Controller)
	{
		LogEvent(FString::Printf(TEXT("Server_ClearBlueprintDesigner RPC: designer=%s"), *DescribeDesigner(Designer)), RCO, true);
	});

	SUBSCRIBE_METHOD(UFGBlueprintRemoteCallObject::Server_SaveBlueprintInDesigner, [](auto& Scope, UFGBlueprintRemoteCallObject* RCO, AFGBuildableBlueprintDesigner* Designer, AFGPlayerController* Controller, FBlueprintRecord Record, FBlueprintCategoryRecord CategoryRecord, FBlueprintSubCategoryRecord SubCategoryRecord)
	{
		LogEvent(FString::Printf(TEXT("Server_SaveBlueprintInDesigner RPC: designer=%s saving as '%s'"), *DescribeDesigner(Designer), *Record.BlueprintName), RCO, false);
	});

	// Backstop: every RPC sent through a remote call object, in case the generated RPC wrappers above were inlined.
	SUBSCRIBE_METHOD_VIRTUAL(UFGRemoteCallObject::CallRemoteFunction, GetMutableDefault<UFGBlueprintRemoteCallObject>(), [](auto& Scope, UFGRemoteCallObject* RCO, UFunction* Function, void* Parameters, FOutParmRec* OutParms, FFrame* Stack)
	{
		if (Function && Function->GetName().Contains(TEXT("Designer")))
		{
			LogEvent(FString::Printf(TEXT("CallRemoteFunction: %s"), *Function->GetName()), RCO, true);
		}
	});

	// Blueprint loads that target a designer (designer loads) or the blueprint world (build gun hologram previews).
	SUBSCRIBE_METHOD(AFGBlueprintSubsystem::LoadStoredBlueprint, [](auto& Scope, AFGBlueprintSubsystem* Subsystem, UFGBlueprintDescriptor* Descriptor, const FTransform& Origin, TArray<AFGBuildable*>& OutSpawned, bool bUseBlueprintWorld, AFGBuildableBlueprintDesigner* Designer, APawn* Instigator, AFGBlueprintProxy* Proxy, const TFunction<void(AFGBuildable*, int32)>& PostSerialize, const TFunction<void()>& PreBeginPlay)
	{
		LogEvent(FString::Printf(TEXT("LoadStoredBlueprint: descriptor=%s useBlueprintWorld=%d designer=%s instigator=%s"),
			*DescribeDescriptor(Descriptor), bUseBlueprintWorld, *DescribeDesigner(Designer), *GetNameSafe(Instigator)), Subsystem, Designer != nullptr);
	});

	// What the build gun selected, so the log shows whether a designer load followed it.
	SUBSCRIBE_METHOD(UFGBuildGunStateBuild::SetActiveBlueprintDescriptor, [](auto& Scope, UFGBuildGunStateBuild* State, UFGBlueprintDescriptor* Descriptor)
	{
		LogEvent(FString::Printf(TEXT("BuildGun SetActiveBlueprintDescriptor: %s"), *DescribeDescriptor(Descriptor)), State, false, true);
	});

	// Where blueprint buttons come from: which menu's Blueprint code created each one.
	SUBSCRIBE_METHOD_AFTER(UWidgetBlueprintLibrary::Create, [](UUserWidget* const& Result, UObject* WorldContext, TSubclassOf<UUserWidget> WidgetType, APlayerController* OwningPlayer)
	{
		if (IsBlueprintButton(Result))
		{
			LogEvent(FString::Printf(TEXT("Button created: %s"), *Result->GetName()), WorldContext, false, false, 4);
		}
	});

	// Buttons being placed into panels. A button that already has a parent is being moved from one menu to another.
	SUBSCRIBE_METHOD_EXPLICIT(UPanelSlot*(UPanelWidget::*)(UWidget*), UPanelWidget::AddChild, [](auto& Scope, UPanelWidget* Panel, UWidget* Content)
	{
		if (!IsBlueprintButton(Content))
		{
			return;
		}
		const bool bIsMove = Content->GetParent() != nullptr;
		LogEvent(FString::Printf(TEXT("Button %s %s panel %s in: %s%s"),
			*Content->GetName(),
			bIsMove ? TEXT("MOVED into") : TEXT("added to"),
			*Panel->GetName(),
			*DescribeWidgetOwners(Panel),
			bIsMove ? *FString::Printf(TEXT("\n    previously in: %s\n    dispatcher bindings at move time:%s"), *DescribeWidgetOwners(Content), *DescribeDispatcherBindings(Content)) : TEXT("")),
			Panel, false, false, bIsMove ? 64 : 4);
	});

	// Designer state arriving from the server. A record change with no client-side load logged before it means the server changed it on its own.
	SUBSCRIBE_METHOD_AFTER(AFGBuildableBlueprintDesigner::execOnRep_RecordData, [](UObject* Context, FFrame& Stack, void* Result)
	{
		LogEvent(FString::Printf(TEXT("OnRep_RecordData (from server): %s"), *DescribeDesigner(Cast<AFGBuildableBlueprintDesigner>(Context))), Context, false);
	});

	SUBSCRIBE_METHOD_AFTER(AFGBuildableBlueprintDesigner::execOnRep_Buildables, [](UObject* Context, FFrame& Stack, void* Result)
	{
		LogEvent(FString::Printf(TEXT("OnRep_Buildables (from server): %s"), *DescribeDesigner(Cast<AFGBuildableBlueprintDesigner>(Context))), Context, false);
	});

	UE_LOG(LogDesignerLoadTracer, Warning, TEXT("Designer load tracer hooks installed"));
}

IMPLEMENT_MODULE(FDesignerLoadTracerModule, DesignerLoadTracer)
