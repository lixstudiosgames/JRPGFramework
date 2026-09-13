#include "World/WorldStateSubsystem.h"
#include "Math/UnrealMathUtility.h"

UWorldStateSubsystem::UWorldStateSubsystem()
{
}

void UWorldStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UWorldStateSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void UWorldStateSubsystem::SetEventFlag(FName FlagID, bool bValue, FString Metadata)
{
	if (FlagID.IsNone())
	{
		return;
	}

	FEventFlag& Flag = EventFlags.FindOrAdd(FlagID);
	Flag.FlagID = FlagID;
	Flag.bValue = bValue;
	
	// Apenas sobrescreve o metadado se um valor não-vazio for fornecido
	if (!Metadata.IsEmpty())
	{
		Flag.Metadata = Metadata;
	}
}

bool UWorldStateSubsystem::GetEventFlagValue(FName FlagID) const
{
	if (FlagID.IsNone())
	{
		return false;
	}

	if (const FEventFlag* Flag = EventFlags.Find(FlagID))
	{
		return Flag->bValue;
	}

	return false;
}

bool UWorldStateSubsystem::HasEventFlag(FName FlagID) const
{
	if (FlagID.IsNone())
	{
		return false;
	}

	return EventFlags.Contains(FlagID);
}

TArray<FEventFlag> UWorldStateSubsystem::GetAllEventFlags() const
{
	TArray<FEventFlag> OutFlags;
	EventFlags.GenerateValueArray(OutFlags);
	return OutFlags;
}

void UWorldStateSubsystem::SetChestOpened(FName ChestID, bool bOpened)
{
	if (ChestID.IsNone())
	{
		return;
	}

	if (bOpened)
	{
		OpenedChests.Add(ChestID);
	}
	else
	{
		OpenedChests.Remove(ChestID);
	}
}

bool UWorldStateSubsystem::IsChestOpened(FName ChestID) const
{
	if (ChestID.IsNone())
	{
		return false;
	}

	return OpenedChests.Contains(ChestID);
}

TArray<FName> UWorldStateSubsystem::GetOpenedChests() const
{
	return OpenedChests.Array();
}

void UWorldStateSubsystem::SetRevivalTreeState(FName TreeID, ERevivalTreeStage Stage)
{
	if (TreeID.IsNone())
	{
		return;
	}

	FRevivalTreeState& Tree = RevivalTreeStates.FindOrAdd(TreeID);
	Tree.TreeID = TreeID;
	Tree.Stage = Stage;
}

bool UWorldStateSubsystem::GetRevivalTreeState(FName TreeID, FRevivalTreeState& OutState) const
{
	if (TreeID.IsNone())
	{
		return false;
	}

	if (const FRevivalTreeState* Tree = RevivalTreeStates.Find(TreeID))
	{
		OutState = *Tree;
		return true;
	}

	return false;
}

TArray<FRevivalTreeState> UWorldStateSubsystem::GetAllRevivalTreeStates() const
{
	TArray<FRevivalTreeState> OutTrees;
	RevivalTreeStates.GenerateValueArray(OutTrees);
	return OutTrees;
}

void UWorldStateSubsystem::SetIsInField(bool bValue)
{
	if (bIsInField == bValue)
	{
		return;
	}

	bIsInField = bValue;
	UE_LOG(LogTemp, Log, TEXT("WorldStateSubsystem: bIsInField = %s (Save no menu de pausa %s)."),
		bIsInField ? TEXT("true") : TEXT("false"),
		bIsInField ? TEXT("liberado") : TEXT("travado"));
}

void UWorldStateSubsystem::ResetWorldState()
{
	EventFlags.Empty();
	OpenedChests.Empty();
	RevivalTreeStates.Empty();
	bIsInField = false;
}

void UWorldStateSubsystem::LoadWorldState(const TArray<FEventFlag>& InEventFlags,
                                          const TArray<FName>& InOpenedChests,
                                          const TArray<FRevivalTreeState>& InRevivalTrees)
{
	ResetWorldState();

	for (const FEventFlag& Flag : InEventFlags)
	{
		if (!Flag.FlagID.IsNone())
		{
			EventFlags.Add(Flag.FlagID, Flag);
		}
	}

	OpenedChests.Append(InOpenedChests);
	OpenedChests.Remove(NAME_None);

	for (const FRevivalTreeState& Tree : InRevivalTrees)
	{
		if (!Tree.TreeID.IsNone())
		{
			RevivalTreeStates.Add(Tree.TreeID, Tree);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("WorldStateSubsystem: Estado do mundo carregado (%d flags, %d baús, %d trees)."),
		EventFlags.Num(), OpenedChests.Num(), RevivalTreeStates.Num());
}
