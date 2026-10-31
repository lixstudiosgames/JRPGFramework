#include "Arts/JRPGArtTester.h"
#include "Arts/ArtsData.h"
#include "Party/CharacterData.h"
#include "Core/CoreSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"

namespace
{
	UDataTable* CharactersTable() { return UCoreSubsystem::ResolveDataTable(TEXT("DT_Characters")); }

	/** Os botões soltos que o Combo aceita. */
	struct FComboButton
	{
		const TCHAR* Key;
		EJRPGArtButton Value;
	};

	const FComboButton ComboButtons[] = {
		{ TEXT("Arms"), EJRPGArtButton::Arms },
		{ TEXT("Ra-Seru"), EJRPGArtButton::RaSeru },
		{ TEXT("High"), EJRPGArtButton::High },
		{ TEXT("Low"), EJRPGArtButton::Low },
	};
}

AJRPGArtTester::AJRPGArtTester()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetRelativeRotation(FRotator(0.0f, MeshYawOffset, 0.0f));

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Root);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextBottom);
	Label->SetWorldSize(14.0f);
	Label->SetTextRenderColor(FColor(255, 220, 120));
	Label->SetRelativeLocation(FVector(0.0f, 0.0f, LabelHeight));
}

// --- Details ---------------------------------------------------------------------------------

TArray<FString> AJRPGArtTester::GetHeroOptions() const
{
	TArray<FString> Out;
	if (const UDataTable* Table = CharactersTable())
	{
		Table->ForeachRow<FCharacterData>(TEXT("ArtTester"), [&Out](const FName& Key, const FCharacterData& Row)
		{
			if (!Row.BattleMesh.IsNull())
			{
				Out.Add(Key.ToString());
			}
		});
	}
	return Out;
}

TArray<FString> AJRPGArtTester::GetArtOptions() const
{
	TArray<FString> Out;
	for (const FName& Key : UJRPGArtSequenceLibrary::HeroArts(Hero, true))
	{
		Out.Add(Key.ToString());
	}
	return Out;
}

TArray<FString> AJRPGArtTester::GetComboOptions() const
{
	TArray<FString> Out;
	for (const FComboButton& Button : ComboButtons)
	{
		Out.Add(Button.Key);
	}
	for (const FName& Key : UJRPGArtSequenceLibrary::HeroArts(Hero, false))
	{
		Out.Add(Key.ToString());
	}
	return Out;
}

// --- Montagem --------------------------------------------------------------------------------

void AJRPGArtTester::ApplyHeroMesh()
{
	if (Hero == LoadedHero && Mesh->GetSkeletalMeshAsset())
	{
		return;
	}
	const UDataTable* Table = CharactersTable();
	const FCharacterData* Row = Table ? Table->FindRow<FCharacterData>(Hero, TEXT("ArtTester"), false) : nullptr;
	if (!Row || Row->BattleMesh.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("ArtTester: %s sem BattleMesh no DT_Characters"), *Hero.ToString());
		return;
	}
	Mesh->SetSkeletalMeshAsset(Row->BattleMesh.LoadSynchronous());
	Mesh->SetRelativeRotation(FRotator(0.0f, MeshYawOffset, 0.0f));
	LoadedHero = Hero;
}

void AJRPGArtTester::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Label->SetRelativeLocation(FVector(0.0f, 0.0f, LabelHeight));
	ApplyHeroMesh();

	// A Art tem que ser do herói escolhido
	const FJRPGArtData* Row = UJRPGArtSequenceLibrary::FindArt(Art);
	if (!Row || Row->Character != Hero)
	{
		const TArray<FName> Arts = UJRPGArtSequenceLibrary::HeroArts(Hero, false);
		Art = Arts.IsEmpty() ? NAME_None : Arts[0];
		Row = UJRPGArtSequenceLibrary::FindArt(Art);
	}
	if (!bRunning)
	{
		Steps.Reset();
		UJRPGArtSequenceLibrary::AppendArtSteps(Art, IsLeftHanded(), bPlayLeadingSwings, Steps);
		UJRPGArtSequenceLibrary::ApplyGameSpeeds(Steps);
		CurrentTitle = Row ? Row->DisplayName.ToString() : FString();
		NowPlaying.Reset();
		PlayIdle(false);
		UpdateLabel();
	}
}

// --- Tocar -----------------------------------------------------------------------------------

void AJRPGArtTester::BeginPlay()
{
	Super::BeginPlay();
	ApplyHeroMesh();
	if (bPlayOnBeginPlay)
	{
		Play();
	}
}

void AJRPGArtTester::Play()
{
	Queue.Reset();
	bComboMode = false;
	StartArt(Art);
}

void AJRPGArtTester::PlayCombo()
{
	Queue.Reset();
	bComboMode = true;
	ApplyHeroMesh();
	Steps.Reset();
	for (const FName& Entry : Combo)
	{
		const FComboButton* Button = nullptr;
		for (const FComboButton& Candidate : ComboButtons)
		{
			if (Entry == FName(Candidate.Key))
			{
				Button = &Candidate;
			}
		}
		if (Button)
		{
			UJRPGArtSequenceLibrary::AppendButtonSwing(Hero, Button->Value, IsLeftHanded(), Steps);
		}
		else
		{
			UJRPGArtSequenceLibrary::AppendArtSteps(Entry, IsLeftHanded(), bPlayLeadingSwings, Steps);
		}
	}
	StartSteps(TEXT("Combo"));
}

void AJRPGArtTester::PlayAll()
{
	Queue = UJRPGArtSequenceLibrary::HeroArts(Hero, false);
	QueueIndex = 0;
	bComboMode = false;
	if (!Queue.IsEmpty())
	{
		Art = Queue[0];
		StartArt(Art);
	}
}

void AJRPGArtTester::NextArt()
{
	const TArray<FName> Arts = UJRPGArtSequenceLibrary::HeroArts(Hero, true);
	if (!Arts.IsEmpty())
	{
		Art = Arts[(Arts.IndexOfByKey(Art) + 1) % Arts.Num()];
		Play();
	}
}

void AJRPGArtTester::PreviousArt()
{
	const TArray<FName> Arts = UJRPGArtSequenceLibrary::HeroArts(Hero, true);
	if (!Arts.IsEmpty())
	{
		const int32 At = Arts.IndexOfByKey(Art);
		Art = Arts[(At <= 0 ? Arts.Num() : At) - 1];
		Play();
	}
}

void AJRPGArtTester::Stop()
{
	bRunning = false;
	Queue.Reset();
	WaitLeft = 0.0f;
	StepIndex = INDEX_NONE;
	NowPlaying.Reset();
	PlayIdle(true);
	UpdateLabel();
}

void AJRPGArtTester::ResetPosition()
{
	GoHome();
	PlayIdle(false);
}

void AJRPGArtTester::StartArt(FName ArtKey)
{
	ApplyHeroMesh();
	Steps.Reset();
	if (!UJRPGArtSequenceLibrary::AppendArtSteps(ArtKey, IsLeftHanded(), bPlayLeadingSwings, Steps))
	{
		Stop();
		return;
	}
	const FJRPGArtData* Row = UJRPGArtSequenceLibrary::FindArt(ArtKey);
	StartSteps(Row ? FString::Printf(TEXT("%s [%s]"), *Row->DisplayName.ToString(),
		*StaticEnum<EJRPGArtKind>()->GetDisplayNameTextByValue(static_cast<int64>(Row->Kind)).ToString()) : ArtKey.ToString());
}

void AJRPGArtTester::StartSteps(const FString& Title)
{
	// A casa é onde o ator estava no primeiro Play (Reset Position / bReturnHomeAfterArt voltam a ela)
	if (!bHasHome)
	{
		Home = GetActorLocation();
		bHasHome = true;
	}
	UJRPGArtSequenceLibrary::ApplyGameSpeeds(Steps);
	CurrentTitle = Title;
	bRunning = true;
	bWaitingNextArt = false;
	WaitLeft = 0.0f;
	UE_LOG(LogTemp, Log, TEXT("ArtTester: %s — %d passo(s)"), *Title, Steps.Num());
	StartStep(0);
}

FVector AJRPGArtTester::FollowBoneLocation() const
{
	return Mesh->GetBoneIndex(FollowBone) != INDEX_NONE
		? Mesh->GetBoneLocation(FollowBone, EBoneSpaces::WorldSpace)
		: Mesh->GetComponentLocation();
}

void AJRPGArtTester::StartStep(int32 Index)
{
	// Passo sem clip: pula (o aviso já saiu ao montar)
	while (Steps.IsValidIndex(Index) && !Steps[Index].Clip)
	{
		++Index;
	}
	if (!Steps.IsValidIndex(Index))
	{
		FinishArt();
		return;
	}
	StepIndex = Index;
	const FJRPGArtStep& Step = Steps[Index];
	UAnimSequence* Clip = Step.Clip;

	const float Rate = PlayRate * (bGameSpeed ? UJRPGArtSequenceLibrary::StepPlayRate(Step) : 1.0f);
	// O quadro 0 começa onde o corpo está agora (o fim do passo anterior, ou o Idle)
	PlayClipNow(Clip, false, Rate, true);

	StepElapsed = 0.0f;
	// Segurança caso o clip não avise o fim: bem acima da duração
	StepMaxTime = Clip->GetPlayLength() / FMath::Max(Rate * FMath::Max(Clip->RateScale, 0.05f), 0.01f) + 1.0f;
	NowPlaying = FString::Printf(TEXT("%d/%d %s  (x%d)"), Index + 1, Steps.Num(), *Step.Label, Step.GameSpeed);
	UpdateLabel();
}

void AJRPGArtTester::PlayClipNow(UAnimSequence* Clip, bool bLooping, float Rate, bool bAlign)
{
	const bool bDoAlign = bAlign && bFollowAnimation && Mesh->GetSkeletalMeshAsset();
	const FVector Before = bDoAlign ? FollowBoneLocation() : FVector::ZeroVector;

	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->PlayAnimation(Clip, bLooping);
	Mesh->SetPlayRate(Rate);
	if (!Mesh->IsRegistered() || !Mesh->GetAnimInstance())
	{
		return;
	}
	Mesh->TickAnimation(0.0f, false);
	Mesh->RefreshBoneTransforms();

	if (bDoAlign)
	{
		// Só para a frente do herói: o jogo leva o ator ao longo da face no fim do clip (+0x0E), nunca
		// de lado — um clip que termina deslocado de lado (Hurricane Kick) volta ao centro no próximo.
		const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
		const FVector Delta = Forward * FVector::DotProduct(Before - FollowBoneLocation(), Forward);
		AddActorWorldOffset(Delta);
	}
}

void AJRPGArtTester::FinishArt()
{
	StepIndex = INDEX_NONE;
	NowPlaying.Reset();
	if (bReturnHomeAfterArt)
	{
		GoHome();
		PlayIdle(false);
	}
	else
	{
		PlayIdle(true);     // o Idle continua de onde a Art terminou
	}
	if (!Queue.IsEmpty())
	{
		if (++QueueIndex >= Queue.Num())
		{
			if (!bLoop)
			{
				Stop();
				return;
			}
			QueueIndex = 0;
		}
		Art = Queue[QueueIndex];
	}
	else if (!bLoop)
	{
		bRunning = false;
		UpdateLabel();
		return;
	}
	// Próxima Art (Play All) ou a mesma de novo (Loop), depois da pausa
	WaitLeft = FMath::Max(PauseBetweenArts, 0.01f);
	bWaitingNextArt = true;
	UpdateLabel();
}

void AJRPGArtTester::GoHome()
{
	if (bHasHome)
	{
		SetActorLocation(Home);
	}
}

void AJRPGArtTester::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRunning)
	{
		return;
	}
	if (WaitLeft > 0.0f)
	{
		WaitLeft -= DeltaSeconds;
		if (WaitLeft <= 0.0f)
		{
			WaitLeft = 0.0f;
			if (bWaitingNextArt)
			{
				bWaitingNextArt = false;
				if (bComboMode)
				{
					PlayCombo();
				}
				else
				{
					StartArt(Art);
				}
			}
			else
			{
				StartStep(StepIndex + 1);
			}
		}
		return;
	}
	if (StepIndex == INDEX_NONE)
	{
		return;
	}
	StepElapsed += DeltaSeconds;
	// Com outro passo na fila, o jogo corta o clip no Chain Cut (o golpe emenda sem voltar à guarda)
	const bool bHasNext = Steps.IsValidIndex(StepIndex + 1);
	const float CutTime = Steps[StepIndex].CutTime;
	const bool bCut = bChainCut && bHasNext && CutTime >= 0.0f && Mesh->GetPosition() >= CutTime;
	if (bCut || !Mesh->IsPlaying() || StepElapsed >= StepMaxTime)
	{
		if (PauseBetweenSteps > 0.0f && bHasNext)
		{
			WaitLeft = PauseBetweenSteps;
		}
		else
		{
			StartStep(StepIndex + 1);
		}
	}
}

void AJRPGArtTester::PlayIdle(bool bAlign)
{
	UAnimSequence* Idle = nullptr;
	const UDataTable* Table = CharactersTable();
	if (const FCharacterData* Row = Table ? Table->FindRow<FCharacterData>(Hero, TEXT("ArtTester"), false) : nullptr)
	{
		Idle = Row->BattleAnims.Idle.LoadSynchronous();
	}
	if (!Idle)
	{
		Idle = UJRPGArtSequenceLibrary::LoadHeroClip(Hero, TEXT("Idle"));
	}
	if (Idle)
	{
		PlayClipNow(Idle, true, 1.0f, bAlign);
	}
}

void AJRPGArtTester::UpdateLabel()
{
	FString Text = CurrentTitle;
	if (!NowPlaying.IsEmpty())
	{
		Text += TEXT("\n") + NowPlaying;
	}
	Label->SetText(FText::FromString(Text));
}
