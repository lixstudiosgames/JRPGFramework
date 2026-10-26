#include "Field/JRPGFieldEnemy.h"
#include "Field/JRPGEncounterZone.h"
#include "Field/JRPGFieldEnemyController.h"
#include "Field/JRPGFieldEnemyAnimInstance.h"
#include "Battle/BattleSubsystem.h"
#include "World/JRPGMistDisturberComponent.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/AudioComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "Sound/SoundConcurrency.h"
#include "TimerManager.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Abaixo disto ele está parado (Idle); acima, andando. */
	constexpr float MovingSpeed = 10.0f;

	/** Andar reto (sem navmesh): chegou quando estiver a esta distância. */
	constexpr float DirectArriveDistance = 60.0f;

	/** Um passeio que não chega em tanto tempo é abandonado (caminho travado). */
	constexpr float WanderTimeout = 8.0f;
	constexpr float ReturnTimeout = 12.0f;
}

AJRPGFieldEnemy::AJRPGFieldEnemy()
{
	PrimaryActorTick.bCanEverTick = true;

	AIControllerClass = AJRPGFieldEnemyController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	GetCapsuleComponent()->InitCapsuleSize(40.0f, 90.0f);

	// Gira para onde anda, como o jogador
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = true;
	Move->RotationRate = FRotator(0.0f, 360.0f, 0.0f);
	Move->MaxWalkSpeed = WalkSpeed;
	Move->MaxAcceleration = 800.0f;
	Move->BrakingDecelerationWalking = 1500.0f;

	GetMesh()->SetRelativeRotation(FRotator(0.0f, MeshYawOffset, 0.0f));
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ContactSphere = CreateDefaultSubobject<USphereComponent>(TEXT("ContactSphere"));
	ContactSphere->SetupAttachment(RootComponent);
	ContactSphere->InitSphereRadius(60.0f);
	ContactSphere->SetCollisionProfileName(TEXT("NoCollision"));
	ContactSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ContactSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	ContactSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	ContactSphere->SetGenerateOverlapEvents(true);

	MistDisturber = CreateDefaultSubobject<UJRPGMistDisturberComponent>(TEXT("MistDisturber"));

	// Ouve-se só perto: cheio até 1,5 m, some por volta de 13 m
	SoundAttenuation.bAttenuate = true;
	SoundAttenuation.bSpatialize = true;
	SoundAttenuation.AttenuationShape = EAttenuationShape::Sphere;
	SoundAttenuation.AttenuationShapeExtents = FVector(150.0f, 0.0f, 0.0f);
	SoundAttenuation.FalloffDistance = 1200.0f;
	SoundAttenuation.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;

#if WITH_EDITORONLY_DATA
	DetectPreview = CreateEditorOnlyDefaultSubobject<USphereComponent>(TEXT("DetectPreview"));
	if (DetectPreview)
	{
		DetectPreview->SetupAttachment(RootComponent);
		DetectPreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		DetectPreview->SetGenerateOverlapEvents(false);
		DetectPreview->SetHiddenInGame(true);
		DetectPreview->ShapeColor = FColor(255, 80, 40);
		DetectPreview->InitSphereRadius(DetectRadius);
	}

	EditorIcon = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("EditorIcon"));
	if (EditorIcon)
	{
		static ConstructorHelpers::FObjectFinderOptional<UTexture2D> Icon(TEXT("/JRPGFramework/Editor/T_FieldEnemyIcon.T_FieldEnemyIcon"));
		EditorIcon->Sprite = Icon.Get();
		EditorIcon->SetupAttachment(RootComponent);
		EditorIcon->SetRelativeLocation(FVector(0.0f, 0.0f, 140.0f));
		EditorIcon->SetHiddenInGame(true);
		EditorIcon->bIsScreenSizeScaled = true;
	}
#endif
}

void AJRPGFieldEnemy::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	GetMesh()->SetRelativeRotation(FRotator(0.0f, MeshYawOffset, 0.0f));

	// Prévia no editor: o modelo que ele vai mostrar (o primeiro da lista / do encounter), para
	// dar para ver no mapa onde e o que ele é. No jogo o BeginPlay sorteia e aplica de verdade.
	const UWorld* World = GetWorld();
	if (World && !World->IsGameWorld())
	{
		FJRPGFieldEnemyVisual Visual;
		FString Source;
		if (ResolveVisual(Visual, Source, false, true))
		{
			ApplyVisual(Visual, Source, false);
		}
	}

#if WITH_EDITORONLY_DATA
	if (DetectPreview)
	{
		DetectPreview->SetSphereRadius(DetectRadius);
	}
	if (EditorIcon)
	{
		EditorIcon->SetRelativeLocation(FVector(0.0f, 0.0f, GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() + 50.0f));
	}
#endif
}

void AJRPGFieldEnemy::InitFromZone(AJRPGEncounterZone* InZone)
{
	Zone = InZone;
}

const FJRPGEncounterSetup& AJRPGFieldEnemy::GetEncounterSetup() const
{
	return Zone ? Zone->Encounter : Encounter;
}

void AJRPGFieldEnemy::BeginPlay()
{
	Super::BeginPlay();

	ContactSphere->OnComponentBeginOverlap.AddDynamic(this, &AJRPGFieldEnemy::HandleContactOverlap);

	ApplyRandomVisual();
	HomeLocation = GetActorLocation();

	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UWebUISubsystem* WebUI = GI->GetSubsystem<UWebUISubsystem>())
		{
			WebUI->OnUIStateChanged.AddDynamic(this, &AJRPGFieldEnemy::HandleUIStateChanged);
			SetFrozen(WebUI->GetUIState() != EJRPGUIState::HudOnly);
		}
	}

	EnterIdle(FMath::FRandRange(WaitTimeMin, WaitTimeMax));
	GetWorldTimerManager().SetTimer(ThinkTimer, this, &AJRPGFieldEnemy::Think, DecisionInterval, true,
		FMath::FRandRange(0.0f, DecisionInterval)); // cada um num instante, não todos no mesmo frame

	Print(FString::Printf(TEXT("apareceu (%s)"), *VisualName), FColor::Cyan);
}

void AJRPGFieldEnemy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearAllTimersForObject(this);
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UWebUISubsystem* WebUI = GI->GetSubsystem<UWebUISubsystem>())
		{
			WebUI->OnUIStateChanged.RemoveDynamic(this, &AJRPGFieldEnemy::HandleUIStateChanged);
		}
	}
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------------------------
// Visual
// ---------------------------------------------------------------------------------------------

bool AJRPGFieldEnemy::ResolveVisual(FJRPGFieldEnemyVisual& Out, FString& OutSource, bool bRandom, bool bQuiet) const
{
	const TArray<FJRPGFieldEnemyVisual>& List = (Zone && Zone->Visuals.Num() > 0) ? Zone->Visuals : Visuals;
	if (List.Num() > 0)
	{
		Out = List[bRandom ? FMath::RandRange(0, List.Num() - 1) : 0];
		OutSource = TEXT("visual da lista");
		return !Out.Mesh.IsNull();
	}

	if (bVisualFromEncounter)
	{
		// O principal primeiro; senão, um dos outros que tenha modelo, pelo peso
		const FJRPGEncounterSetup& Setup = GetEncounterSetup();
		TArray<const FJRPGEncounterEnemy*> WithModel;
		TArray<FName> WithoutModel;
		const FJRPGEncounterEnemy* Main = nullptr;
		for (const FJRPGEncounterEnemy& Line : Setup.Enemies)
		{
			FJRPGEnemyData Data;
			if (!UJRPGEncounterLibrary::GetEnemyData(Line.EnemyKey, Data))
			{
				continue;
			}
			if (Data.Mesh.IsNull())
			{
				WithoutModel.Add(Line.EnemyKey);
				continue;
			}
			WithModel.Add(&Line);
			if (Line.bAlwaysJoins && !Main)
			{
				Main = &Line;
			}
		}
		if (!Main && WithModel.Num() > 0 && !bRandom)
		{
			Main = WithModel[0];
		}
		if (!Main && WithModel.Num() > 0)
		{
			int32 Total = 0;
			for (const FJRPGEncounterEnemy* Line : WithModel)
			{
				Total += FMath::Max(Line->Weight, 1);
			}
			int32 Pick = FMath::RandRange(1, Total);
			for (const FJRPGEncounterEnemy* Line : WithModel)
			{
				Pick -= FMath::Max(Line->Weight, 1);
				if (Pick <= 0)
				{
					Main = Line;
					break;
				}
			}
		}
		if (Main)
		{
			FJRPGEnemyData Data;
			UJRPGEncounterLibrary::GetEnemyData(Main->EnemyKey, Data);
			Out = FJRPGFieldEnemyVisual();
			Out.Mesh = Data.Mesh;
			Out.IdleAnim = Data.Anims.Idle;
			Out.WalkAnim = Data.Anims.Walk;
			OutSource = Main->EnemyKey.ToString();
			return true;
		}
		if (WithoutModel.Num() > 0 && !bQuiet)
		{
			Print(FString::Printf(TEXT("%s ainda sem modelo no DT_Enemies — usando o FallbackVisual"),
				*WithoutModel[0].ToString()), FColor::Orange);
		}
	}

	Out = FallbackVisual;
	OutSource = TEXT("fallback");
	return !Out.Mesh.IsNull();
}

void AJRPGFieldEnemy::ApplyRandomVisual()
{
	FJRPGFieldEnemyVisual Visual;
	FString Source;
	if (!ResolveVisual(Visual, Source, true, false))
	{
		VisualName = TEXT("sem modelo");
		Print(TEXT("nenhum visual com malha (Visuals, DT_Enemies e FallbackVisual vazios)"), FColor::Red);
		return;
	}
	ApplyVisual(Visual, Source, true);
}

void AJRPGFieldEnemy::ApplyVisual(const FJRPGFieldEnemyVisual& Visual, const FString& Source, bool bKeepFeetOnFloor)
{
	USkeletalMesh* LoadedMesh = Visual.Mesh.LoadSynchronous();
	USkeletalMeshComponent* MeshComp = GetMesh();
	MeshComp->SetSkeletalMeshAsset(LoadedMesh);
	MeshComp->SetRelativeScale3D(FVector(Visual.Scale));
	MeshComp->SetRelativeRotation(FRotator(0.0f, MeshYawOffset, 0.0f));
	VisualName = FString::Printf(TEXT("%s: %s"), *Source, LoadedMesh ? *LoadedMesh->GetName() : TEXT("?"));

	IdleClip = Visual.IdleAnim.LoadSynchronous();
	WalkClip = Visual.WalkAnim.LoadSynchronous();
	RunClip = Visual.RunAnim.LoadSynchronous();
	PlayingClip = nullptr;
	bUsesAnimClass = Visual.AnimClass != nullptr;
	if (bUsesAnimClass)
	{
		MeshComp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		MeshComp->SetAnimInstanceClass(Visual.AnimClass);
	}
	else
	{
		// Sem Anim Blueprint: a instância nativa com crossfade entre os clips
		MeshComp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		MeshComp->SetAnimInstanceClass(UJRPGFieldEnemyAnimInstance::StaticClass());
		UpdateAnimation();
	}

	// Cápsula do tamanho da malha, com os pés no mesmo chão de antes
	if (bFitCapsuleToMesh && LoadedMesh)
	{
		UCapsuleComponent* Capsule = GetCapsuleComponent();
		const float OldHalf = Capsule->GetScaledCapsuleHalfHeight();
		const FBoxSphereBounds Bounds = LoadedMesh->GetBounds();
		const float Scale = Visual.Scale;
		const float MinZ = (Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale;
		const float Height = Bounds.BoxExtent.Z * 2.0f * Scale;
		const float Radius = FMath::Clamp(FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y) * Scale * 0.6f, 20.0f, 150.0f);
		const float Half = FMath::Max(Height * 0.5f, Radius);

		Capsule->SetCapsuleSize(Radius, Half);
		MeshComp->SetRelativeLocation(FVector(0.0f, 0.0f, -Half - MinZ));
		ContactSphere->SetSphereRadius(Radius + 25.0f);
		if (bKeepFeetOnFloor)
		{
			AddActorWorldOffset(FVector(0.0f, 0.0f, Half - OldHalf));
		}
	}
}

void AJRPGFieldEnemy::UpdateAnimation()
{
	if (bUsesAnimClass)
	{
		return;
	}

	const float Speed = GetVelocity().Size2D();
	UAnimSequence* Want = IdleClip;
	float Rate = 1.0f;
	if (Speed > MovingSpeed)
	{
		if (State == EJRPGFieldEnemyState::Chase && RunClip)
		{
			Want = RunClip;
		}
		else if (WalkClip)
		{
			Want = WalkClip;
			// Sem clip de corrida: o andar mais rápido, proporcional à velocidade
			Rate = FMath::Clamp(Speed / FMath::Max(WalkSpeed, 1.0f), 0.5f, 2.5f);
		}
	}
	if (!Want)
	{
		return;
	}

	USkeletalMeshComponent* MeshComp = GetMesh();
	if (UJRPGFieldEnemyAnimInstance* Anim = Cast<UJRPGFieldEnemyAnimInstance>(MeshComp->GetAnimInstance()))
	{
		Anim->PlayClip(Want, Rate, AnimBlendTime);
		PlayingClip = Want;
		return;
	}
	// Sem a instância (ainda não criada): troca seca, como o modo single node
	if (Want != PlayingClip)
	{
		MeshComp->PlayAnimation(Want, true);
		PlayingClip = Want;
	}
	MeshComp->SetPlayRate(Rate);
}

void AJRPGFieldEnemy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (State == EJRPGFieldEnemyState::Hidden || bFrozen)
	{
		return;
	}

	// Sem navmesh: anda reto até o alvo
	if (bDirectMove)
	{
		const FVector Target = DirectPawn.IsValid() ? DirectPawn->GetActorLocation() : DirectTarget;
		FVector ToTarget = Target - GetActorLocation();
		ToTarget.Z = 0.0f;
		if (ToTarget.Size() > DirectArriveDistance)
		{
			AddMovementInput(ToTarget.GetSafeNormal());
		}
		else if (!DirectPawn.IsValid())
		{
			bDirectMove = false;
		}
	}

	UpdateAnimation();
}

// ---------------------------------------------------------------------------------------------
// Decisão
// ---------------------------------------------------------------------------------------------

APawn* AJRPGFieldEnemy::GetPlayerPawn() const
{
	// O pawn que o jogador controla agora: troca junto com o R3, e nunca é um seguidor
	return UGameplayStatics::GetPlayerPawn(this, 0);
}

float AJRPGFieldEnemy::DistanceFromHome(const FVector& Point) const
{
	if (Zone)
	{
		return Zone->DistanceOutside(Point);
	}
	return FVector::Dist2D(Point, HomeLocation);
}

bool AJRPGFieldEnemy::CanDetect(const APawn* Player) const
{
	if (!Player)
	{
		return false;
	}
	const UWorld* World = GetWorld();
	if (World->GetTimeSeconds() - LastReturnTime < ReacquireDelay)
	{
		return false;
	}
	if (FVector::Dist2D(Player->GetActorLocation(), GetActorLocation()) > DetectRadius)
	{
		return false;
	}
	// Um jogador fora do alcance do leash seria perseguido e largado no mesmo instante
	if (DistanceFromHome(Player->GetActorLocation()) > LeashDistance)
	{
		return false;
	}
	if (bRequireLineOfSight)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(JRPGFieldEnemySight), false, this);
		Params.AddIgnoredActor(Player);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, GetPawnViewLocation(), Player->GetActorLocation(), ECC_Visibility, Params))
		{
			return false;
		}
	}
	return true;
}

void AJRPGFieldEnemy::Think()
{
	if (State == EJRPGFieldEnemyState::Hidden || bFrozen)
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	APawn* Player = GetPlayerPawn();

	switch (State)
	{
	case EJRPGFieldEnemyState::Idle:
		if (CanDetect(Player))
		{
			StartChase(Player);
		}
		else if (bWander && Now >= WaitUntil)
		{
			StartWander();
		}
		break;

	case EJRPGFieldEnemyState::Wander:
		if (CanDetect(Player))
		{
			StartChase(Player);
		}
		else if (IsMoveFinished() || Now >= StateDeadline)
		{
			EnterIdle(FMath::FRandRange(WaitTimeMin, WaitTimeMax));
		}
		break;

	case EJRPGFieldEnemyState::Chase:
	{
		if (!Player)
		{
			StartReturn(TEXT("sem jogador"));
			break;
		}
		if (DistanceFromHome(GetActorLocation()) > LeashDistance)
		{
			StartReturn(TEXT("longe demais de casa"));
			break;
		}
		const float Dist = FVector::Dist2D(Player->GetActorLocation(), GetActorLocation());
		if (Dist > LoseRadius)
		{
			if (LostTime == 0.0f)
			{
				Print(TEXT("perdeu o jogador de vista"), FColor::Yellow);
			}
			LostTime += DecisionInterval;
			if (LostTime >= GiveUpTime)
			{
				StartReturn(TEXT("desistiu"));
				break;
			}
		}
		else
		{
			LostTime = 0.0f;
		}
		// O MoveToActor acompanha o alvo sozinho; se ele parou (chegou, caminho falhou), de novo
		if (IsMoveFinished())
		{
			MoveToPawn(Player);
		}
		break;
	}

	case EJRPGFieldEnemyState::Return:
		// Voltando, ainda enxerga: passou de novo na frente dele, ele vem (depois do
		// ReacquireDelay contado do início da volta, e só se o jogador estiver no alcance do leash)
		if (CanDetect(Player))
		{
			StartChase(Player);
		}
		else if (IsMoveFinished() || Now >= StateDeadline)
		{
			EnterIdle(FMath::FRandRange(WaitTimeMin, WaitTimeMax));
		}
		break;

	default:
		break;
	}
}

void AJRPGFieldEnemy::EnterIdle(float WaitSeconds)
{
	State = EJRPGFieldEnemyState::Idle;
	WaitUntil = GetWorld()->GetTimeSeconds() + WaitSeconds;
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	StopMoving();
}

bool AJRPGFieldEnemy::PickWanderPoint(FVector& OutFloor) const
{
	if (Zone)
	{
		return Zone->GetRandomFloorPoint(OutFloor);
	}

	const FVector Feet = HomeLocation - FVector(0.0f, 0.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	if (const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation Point;
		if (Nav->GetRandomReachablePointInRadius(Feet, WanderRadius, Point))
		{
			OutFloor = Point.Location;
			return true;
		}
	}
	const FVector2D Offset = FMath::RandPointInCircle(WanderRadius);
	OutFloor = Feet + FVector(Offset.X, Offset.Y, 0.0f);
	return true;
}

void AJRPGFieldEnemy::StartWander()
{
	FVector Floor;
	if (!PickWanderPoint(Floor))
	{
		EnterIdle(FMath::FRandRange(WaitTimeMin, WaitTimeMax));
		return;
	}
	State = EJRPGFieldEnemyState::Wander;
	StateDeadline = GetWorld()->GetTimeSeconds() + WanderTimeout;
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	MoveToPoint(Floor);
}

void AJRPGFieldEnemy::StartChase(APawn* Player)
{
	State = EJRPGFieldEnemyState::Chase;
	LostTime = 0.0f;
	GetCharacterMovement()->MaxWalkSpeed = FMath::Max(ChaseSpeed, WalkSpeed);
	MoveToPawn(Player);
	Print(TEXT("detectou o jogador — perseguindo"), FColor::Orange);
}

void AJRPGFieldEnemy::StartReturn(const TCHAR* Why)
{
	State = EJRPGFieldEnemyState::Return;
	LastReturnTime = GetWorld()->GetTimeSeconds();
	StateDeadline = LastReturnTime + ReturnTimeout;
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;

	FVector Floor = HomeLocation - FVector(0.0f, 0.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	if (Zone && !Zone->GetRandomFloorPoint(Floor))
	{
		Floor = Zone->GetActorLocation();
	}
	MoveToPoint(Floor);
	Print(FString::Printf(TEXT("%s — voltando"), Why), FColor::Yellow);
}

// ---------------------------------------------------------------------------------------------
// Movimento
// ---------------------------------------------------------------------------------------------

void AJRPGFieldEnemy::MoveToPoint(const FVector& Target)
{
	bDirectMove = false;
	DirectPawn.Reset();
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		const EPathFollowingRequestResult::Type Result = AI->MoveToLocation(Target, 40.0f, false, true, true);
		if (Result != EPathFollowingRequestResult::Failed)
		{
			return;
		}
	}
	bDirectMove = true;
	DirectTarget = Target;
}

void AJRPGFieldEnemy::MoveToPawn(APawn* Target)
{
	bDirectMove = false;
	DirectPawn.Reset();
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		UPathFollowingComponent* PathFollowing = AI->GetPathFollowingComponent();
		if (PathFollowing && !PathFollowing->PostProcessMove.IsBound())
		{
			PathFollowing->PostProcessMove.BindUObject(this, &AJRPGFieldEnemy::HandlePostProcessMove);
		}
		const EPathFollowingRequestResult::Type Result = AI->MoveToActor(Target, 10.0f, true, true, false);
		if (Result != EPathFollowingRequestResult::Failed)
		{
			return;
		}
	}
	bDirectMove = true;
	DirectPawn = Target;
}

void AJRPGFieldEnemy::HandlePostProcessMove(UPathFollowingComponent* PathFollowing, FVector& MoveInput)
{
	// O path following freia perto do fim do caminho, e perseguindo o fim do caminho é o
	// jogador: ele nunca encosta (as cápsulas batem antes), então ficava freando atrás de quem
	// anda reto. Perseguindo, sempre força total; passear e voltar continuam freando suave
	if (State == EJRPGFieldEnemyState::Chase && !MoveInput.IsNearlyZero())
	{
		MoveInput = MoveInput.GetSafeNormal();
	}
}

void AJRPGFieldEnemy::StopMoving()
{
	bDirectMove = false;
	DirectPawn.Reset();
	if (AController* C = GetController())
	{
		C->StopMovement();
	}
}

bool AJRPGFieldEnemy::IsMoveFinished() const
{
	if (bDirectMove)
	{
		return false;
	}
	if (const AAIController* AI = Cast<AAIController>(GetController()))
	{
		return AI->GetMoveStatus() == EPathFollowingStatus::Idle;
	}
	return true;
}

// ---------------------------------------------------------------------------------------------
// Toque, sumir, voltar
// ---------------------------------------------------------------------------------------------

void AJRPGFieldEnemy::HandleContactOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (State == EJRPGFieldEnemyState::Hidden || bFrozen)
	{
		return;
	}
	// Só o pawn que o jogador controla: seguidor da party encostando não conta
	if (OtherActor && OtherActor == GetPlayerPawn())
	{
		TriggerEncounter();
	}
}

void AJRPGFieldEnemy::OnContact_Implementation(const FJRPGFieldEncounter& EncounterInfo)
{
}

void AJRPGFieldEnemy::TriggerEncounter()
{
	if (State == EJRPGFieldEnemyState::Hidden)
	{
		return;
	}

	const TArray<FJRPGEncounterCombatant> Enemies = UJRPGEncounterLibrary::RollEncounter(GetEncounterSetup());
	if (Enemies.Num() == 0)
	{
		Print(TEXT("encostou, mas o Encounter está vazio — nada a fazer"), FColor::Red);
		return;
	}

	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UBattleSubsystem* Battle = GI->GetSubsystem<UBattleSubsystem>())
		{
			if (!Battle->TryStartEncounter(Enemies))
			{
				return; // outro encontro acabou de começar
			}
		}
	}

	FJRPGFieldEncounter Info;
	Info.Enemies = Enemies;
	Info.FieldEnemy = this;
	Info.Zone = Zone;
	Info.ContactLocation = GetActorLocation();

	TArray<FString> Names;
	for (const FJRPGEncounterCombatant& C : Enemies)
	{
		FJRPGEnemyData Data;
		const bool bFound = UJRPGEncounterLibrary::GetEnemyData(C.EnemyKey, Data);
		Names.Add(FString::Printf(TEXT("%s Lv%d%s"),
			bFound ? *Data.DisplayName.ToString() : *C.EnemyKey.ToString(), C.Level,
			bFound ? *FString::Printf(TEXT(" (HP %d)"), Data.BaseHP) : TEXT(" (fora do DT_Enemies!)")));
	}
	Print(FString::Printf(TEXT("ENCONTRO! %s"), *FString::Join(Names, TEXT(", "))), FColor::Red);

	OnEncounterTriggered.Broadcast(Info);
	OnContact(Info);
	HideEnemy();
}

void AJRPGFieldEnemy::HideEnemy()
{
	if (State == EJRPGFieldEnemyState::Hidden)
	{
		return;
	}
	StopMoving();
	State = EJRPGFieldEnemyState::Hidden;
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	GetCharacterMovement()->StopMovementImmediately();
	// Escondida, a malha do ACharacter continua animando (AlwaysTickPoseAndRefreshBones) e os
	// notifies continuariam tocando o passo no lugar do toque
	GetMesh()->bPauseAnims = true;
	StopClipSounds();
	ScheduleRespawn();
}

void AJRPGFieldEnemy::PlayClipSound(USoundBase* Sound, float VolumeMultiplier, float PitchMultiplier, bool bFollow, FName AttachName)
{
	if (!Sound || !bPlayClipSounds || State == EJRPGFieldEnemyState::Hidden || IsHidden())
	{
		return;
	}
	if (!SoundAttenuationAsset)
	{
		SoundAttenuationAsset = NewObject<USoundAttenuation>(this);
		SoundAttenuationAsset->Attenuation = SoundAttenuation;
	}
	if (!SoundConcurrency)
	{
		// Um som por vez por inimigo: o passo que ainda toca não ganha outro por cima
		SoundConcurrency = NewObject<USoundConcurrency>(this);
		SoundConcurrency->Concurrency.MaxCount = 1;
		SoundConcurrency->Concurrency.bLimitToOwner = true;
		SoundConcurrency->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::PreventNew;
	}

	const float Volume = VolumeMultiplier * SoundVolume;
	UAudioComponent* Audio = nullptr;
	if (bFollow || !AttachName.IsNone())
	{
		Audio = UGameplayStatics::SpawnSoundAttached(Sound, GetMesh(), AttachName, FVector::ZeroVector, EAttachLocation::SnapToTarget,
			true, Volume, PitchMultiplier, 0.0f, SoundAttenuationAsset, SoundConcurrency, true);
	}
	else
	{
		Audio = UGameplayStatics::SpawnSoundAtLocation(this, Sound, GetMesh()->GetComponentLocation(), FRotator::ZeroRotator,
			Volume, PitchMultiplier, 0.0f, SoundAttenuationAsset, SoundConcurrency, true);
	}
	ClipSounds.RemoveAll([](const TWeakObjectPtr<UAudioComponent>& Ptr) { return !Ptr.IsValid(); });
	if (Audio)
	{
		ClipSounds.Add(Audio);
	}
}

void AJRPGFieldEnemy::StopClipSounds()
{
	for (const TWeakObjectPtr<UAudioComponent>& Ptr : ClipSounds)
	{
		if (UAudioComponent* Audio = Ptr.Get())
		{
			Audio->Stop();
		}
	}
	ClipSounds.Reset();

	// Visual com Anim Blueprint: o AnimNotify_PlaySound da engine prende o som à malha
	TArray<UAudioComponent*> Attached;
	GetComponents<UAudioComponent>(Attached);
	for (UAudioComponent* Audio : Attached)
	{
		Audio->Stop();
	}
}

void AJRPGFieldEnemy::ScheduleRespawn()
{
	if (Zone)
	{
		Zone->NotifyEnemyHidden(this);
		Print(TEXT("sumiu — a zona agenda a volta"), FColor::Silver);
		return;
	}
	if (!bRespawn)
	{
		Print(TEXT("sumiu (sem respawn)"), FColor::Silver);
		return;
	}
	const float Delay = FMath::FRandRange(RespawnDelayMin, FMath::Max(RespawnDelayMin, RespawnDelayMax));
	const FVector Floor = HomeLocation - FVector(0.0f, 0.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	GetWorldTimerManager().SetTimer(RespawnTimer, FTimerDelegate::CreateUObject(this, &AJRPGFieldEnemy::RespawnAtFloor, Floor), Delay, false);
	Print(FString::Printf(TEXT("sumiu — volta em %.0fs"), Delay), FColor::Silver);
}

void AJRPGFieldEnemy::RespawnAtFloor(FVector FloorLocation)
{
	GetWorldTimerManager().ClearTimer(RespawnTimer);
	GetMesh()->bPauseAnims = false;
	ApplyRandomVisual();
	const float Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	SetActorLocation(FloorLocation + FVector(0.0f, 0.0f, Half), false, nullptr, ETeleportType::TeleportPhysics);
	if (!Zone)
	{
		HomeLocation = GetActorLocation();
	}
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	LastReturnTime = GetWorld()->GetTimeSeconds(); // não pula em cima do jogador ao reaparecer
	EnterIdle(FMath::FRandRange(WaitTimeMin, WaitTimeMax));
	Print(FString::Printf(TEXT("reapareceu (%s)"), *VisualName), FColor::Cyan);
}

// ---------------------------------------------------------------------------------------------
// Menu aberto = mundo parado
// ---------------------------------------------------------------------------------------------

void AJRPGFieldEnemy::HandleUIStateChanged(EJRPGUIState OldState, EJRPGUIState NewState)
{
	SetFrozen(NewState != EJRPGUIState::HudOnly);
}

void AJRPGFieldEnemy::SetFrozen(bool bFreeze)
{
	if (bFrozen == bFreeze)
	{
		return;
	}
	bFrozen = bFreeze;
	// Dilatação 0 para movimento e animação; o Think também não decide nada enquanto isso
	CustomTimeDilation = bFreeze ? 0.0f : 1.0f;
}

// ---------------------------------------------------------------------------------------------

FString AJRPGFieldEnemy::Describe() const
{
	const FJRPGEncounterSetup& Setup = GetEncounterSetup();
	const FString Key = Setup.Enemies.Num() > 0 ? Setup.Enemies[0].EnemyKey.ToString() : TEXT("sem encounter");
	return FString::Printf(TEXT("%s [%s]"), *GetActorNameOrLabel(), *Key);
}

void AJRPGFieldEnemy::Print(const FString& Message, const FColor& Color) const
{
	if (!bPrintDebug)
	{
		return;
	}
	const FString Line = FString::Printf(TEXT("FieldEnemy %s: %s"), *Describe(), *Message);
	UE_LOG(LogTemp, Log, TEXT("%s"), *Line);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, Color, Line);
	}
}
