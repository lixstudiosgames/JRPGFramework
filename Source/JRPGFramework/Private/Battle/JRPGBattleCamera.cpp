#include "Battle/JRPGBattleCamera.h"
#include "Battle/JRPGBattleArena.h"
#include "Battle/JRPGBattlePawn.h"
#include "Battle/JRPGBattleSceneSubsystem.h"
#include "Battle/JRPGBattleUISubsystem.h"
#include "Battle/BattleSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

namespace
{
	/** O passo do ease-out do original: 30 Hz (um passo de câmera = 2 quadros de 60). */
	constexpr float ChaseStepsPerSecond = 30.0f;
	/** A chave da linha de debug na tela. */
	const int32 DebugMessageKey = 0x4A424331; // "JBC1"
}

AJRPGBattleCamera::AJRPGBattleCamera(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	// Depois dos pawns andarem e animarem: a pose sai do quadro de agora (a câmera do jogador é atualizada depois de todo tick)
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	SetActorTickEnabled(true);

	if (UCameraComponent* CameraComp = GetCameraComponent())
	{
		// Sem barras pretas: a tela larga mostra mais dos lados (ApplyLens prende a vertical)
		CameraComp->bConstrainAspectRatio = false;
	}

	// A tabela de alturas do close do menu (0x801F4D2C)
	MemberLiftUnits.Add(TEXT("Vahn"), 1152.0f);
	MemberLiftUnits.Add(TEXT("Noa"), 960.0f);
	MemberLiftUnits.Add(TEXT("Gala"), 1408.0f);
}

void AJRPGBattleCamera::InitForArena(AJRPGBattleArena* InArena)
{
	Arena = InArena;
	ApplyLens();

	if (UWorld* World = GetWorld())
	{
		if (UJRPGBattleSceneSubsystem* Scene = World->GetSubsystem<UJRPGBattleSceneSubsystem>())
		{
			Scene->OnFormationShifted.AddUniqueDynamic(this, &AJRPGBattleCamera::HandleFormationShifted);
		}
	}

	// O primeiro quadro: o giro de abertura, e a pose de uma vez (a tela está preta; nada desliza do zero)
	Live = FJRPGBattleCameraPose();
	Live.Yaw = static_cast<float>(FRotator::NormalizeAxis(InArena ? InArena->GetActorRotation().Yaw + StartYaw : StartYaw));
	bHasPose = false;
	Shot = EJRPGBattleShot::Far;
	ShotSlot = ShotTargetSlot = INDEX_NONE;
	if (BuildShotPose(Shot, 0.0f, Target))
	{
		SnapToTarget();
	}
}

void AJRPGBattleCamera::SnapToTarget()
{
	Live = Target;
	bHasPose = true;
	ApplyLivePose();
}

void AJRPGBattleCamera::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Arena.IsValid())
	{
		return;
	}

	// Re-armado todo quadro, como o original: a pose alvo sai dos combatentes de agora
	Shot = ChooseShot(ShotSlot, ShotTargetSlot);
	FJRPGBattleCameraPose NewTarget;
	if (!BuildShotPose(Shot, DeltaSeconds, NewTarget))
	{
		// O personagem do enquadramento sumiu: o plano geral
		Shot = EJRPGBattleShot::Far;
		ShotSlot = ShotTargetSlot = INDEX_NONE;
		if (!BuildShotPose(Shot, DeltaSeconds, NewTarget))
		{
			return;   // ninguém para enquadrar: fica onde está
		}
	}
	Target = NewTarget;

	if (!bHasPose)
	{
		SnapToTarget();
	}
	else
	{
		Chase(DeltaSeconds, Shot == EJRPGBattleShot::Far ? FarChaseSteps : ChaseSteps);
		ApplyLivePose();
	}

	if (bPrintDebug && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(DebugMessageKey, 0.0f, FColor(150, 220, 255), FString::Printf(
			TEXT("BattleCamera %s (slot %d -> %d) | yaw %.1f pitch %.1f | TR (%.0f, %.0f, %.0f) cm | focus %s"),
			*StaticEnum<EJRPGBattleShot>()->GetNameStringByValue(static_cast<int64>(Shot)), ShotSlot, ShotTargetSlot,
			Live.Yaw, Live.Pitch, Live.Offset.X, Live.Offset.Y, Live.Offset.Z, *Live.Focus.ToCompactString()));
	}
}

EJRPGBattleShot AJRPGBattleCamera::ChooseShot(int32& OutSlot, int32& OutTargetSlot) const
{
	OutSlot = OutTargetSlot = INDEX_NONE;
	const UWorld* World = GetWorld();
	if (!World)
	{
		return EJRPGBattleShot::Far;
	}

	// A fase de comandos com a UI de pé: a tela do menu escolhe (o FUN_801D388C do original arma o caso 0 no anel e na entrada das
	// Arts, o 1 no cursor de alvo, e o 9 no prompt da rodada e na confirmação)
	const UGameInstance* GI = World->GetGameInstance();
	const UBattleSubsystem* Battle = GI ? GI->GetSubsystem<UBattleSubsystem>() : nullptr;
	const UJRPGBattleUISubsystem* UI = World->GetSubsystem<UJRPGBattleUISubsystem>();
	if (bMenuShots && UI && UI->IsMenuUp() && (!Battle || Battle->GetState().Stage == EJRPGBattleStage::Commands))
	{
		const FJRPGBattleMenu& Menu = UI->GetMenu();
		switch (Menu.GetScreen())
		{
		case EJRPGBattleMenuScreen::Command:
		case EJRPGBattleMenuScreen::Attack:
			OutSlot = Menu.GetFocusHeroSlot();
			if (OutSlot != INDEX_NONE)
			{
				return EJRPGBattleShot::Member;
			}
			break;
		case EJRPGBattleMenuScreen::Target:
			OutSlot = Menu.GetFocusHeroSlot();
			OutTargetSlot = Menu.GetTargetCursorSlot();
			if (OutSlot != INDEX_NONE && OutTargetSlot != INDEX_NONE)
			{
				return EJRPGBattleShot::TargetEnemy;
			}
			break;
		default:
			break;
		}
		OutSlot = OutTargetSlot = INDEX_NONE;
	}
	return EJRPGBattleShot::Far;
}

bool AJRPGBattleCamera::BuildShotPose(EJRPGBattleShot InShot, float DeltaSeconds, FJRPGBattleCameraPose& OutPose)
{
	switch (InShot)
	{
	case EJRPGBattleShot::Member:
		return BuildMemberPose(OutPose);
	case EJRPGBattleShot::TargetEnemy:
		return BuildTargetEnemyPose(OutPose);
	case EJRPGBattleShot::Far:
	default:
		return BuildFarPose(DeltaSeconds, OutPose);
	}
}

bool AJRPGBattleCamera::BuildMemberPose(FJRPGBattleCameraPose& OutPose) const
{
	const AJRPGBattlePawn* Hero = PawnForSlot(ShotSlot);
	if (!Hero)
	{
		return false;
	}
	// O foco é a posição do herói (o par vivo +0x34/+0x36/+0x38, no chão em pé); o giro é preso à frente dele
	const float* Lift = MemberLiftUnits.Find(Hero->GetCombatant().Key);
	OutPose.Focus = FeetOf(Hero);
	OutPose.Yaw = FacingRelativeYaw(Hero, MemberYawUnits);
	OutPose.Pitch = UnitsToDegrees(MemberPitchUnits);
	OutPose.Offset = FVector(CameraUnitsToCm(MemberSideUnits), CameraUnitsToCm(Lift ? *Lift : MemberDefaultLiftUnits),
		CameraUnitsToCm(Prescale(MemberDepthUnits)) * DistanceScale);
	return true;
}

bool AJRPGBattleCamera::BuildTargetEnemyPose(FJRPGBattleCameraPose& OutPose) const
{
	const AJRPGBattlePawn* Hero = PawnForSlot(ShotSlot);
	const AJRPGBattlePawn* Enemy = PawnForSlot(ShotTargetSlot);
	if (!Hero || !Enemy)
	{
		return false;
	}
	OutPose.Focus = FeetOf(Hero);
	// O giro olha do foco de agora da câmera para o alvo (o original mede do alvo até o foco e soma meia volta): por trás do herói
	FVector Look = Enemy->GetBodyLocation() - (bHasPose ? Live.Focus : OutPose.Focus);
	Look.Z = 0.0;
	if (Look.SizeSquared() < 1.0)
	{
		Look = Enemy->GetBodyLocation() - OutPose.Focus;
		Look.Z = 0.0;
	}
	OutPose.Yaw = Look.SizeSquared() >= 1.0 ? static_cast<float>(Look.Rotation().Yaw) : static_cast<float>(Hero->GetActorRotation().Yaw);
	OutPose.Pitch = UnitsToDegrees(TargetPitchUnits);
	OutPose.Offset = FVector(0.0, CameraUnitsToCm(TargetLiftUnits), CameraUnitsToCm(Prescale(TargetDepthUnits)) * DistanceScale);
	return true;
}

float AJRPGBattleCamera::FacingRelativeYaw(const AJRPGBattlePawn* Pawn, float Units) const
{
	// O original: giro = ângulo - frente. O olhar da câmera é a frente do personagem virada de -ângulo: 0 = olhando para onde ele
	// olha (por trás), 0x800 = olhando para ele (de frente)
	const float Forward = Pawn ? static_cast<float>(Pawn->GetActorRotation().Yaw) : 0.0f;
	const float Sign = bMirrorYaw ? -1.0f : 1.0f;
	return static_cast<float>(FRotator::NormalizeAxis(Forward - Sign * UnitsToDegrees(Units)));
}

AJRPGBattlePawn* AJRPGBattleCamera::PawnForSlot(int32 Slot) const
{
	const AJRPGBattleArena* CurrentArena = Arena.Get();
	AJRPGBattlePawn* Pawn = (CurrentArena && Slot != INDEX_NONE) ? CurrentArena->GetPawnForSlot(Slot) : nullptr;
	return (IsValid(Pawn) && !Pawn->IsHidden()) ? Pawn : nullptr;
}

bool AJRPGBattleCamera::BuildFarPose(float DeltaSeconds, FJRPGBattleCameraPose& OutPose)
{
	const AJRPGBattleArena* CurrentArena = Arena.Get();
	const TArray<AJRPGBattlePawn*> Pawns = GetVisiblePawns();
	if (!CurrentArena || Pawns.Num() == 0)
	{
		return false;
	}

	// A caixa da formação no plano da arena (o caso 9 do original reconstrói a caixa e o centro a cada passo: o atacante que anda
	// até o alvo encolhe a largura até o piso 0x800, e ela volta quando ele volta)
	const FTransform ArenaXf(FRotator(0.0, CurrentArena->GetActorRotation().Yaw, 0.0), CurrentArena->GetActorLocation());
	FBox2D Box(ForceInit);
	double FloorZ = 0.0;
	for (const AJRPGBattlePawn* Pawn : Pawns)
	{
		const FVector Feet = FeetOf(Pawn);
		const FVector Local = ArenaXf.InverseTransformPosition(Feet);
		Box += FVector2D(Local.X, Local.Y);
		FloorZ += Feet.Z;
	}
	FloorZ /= Pawns.Num();

	const float CmPerUnit = CurrentArena->OriginalCmPerUnit;
	const FVector2D Size = Box.GetSize();
	const float SpanUnits = static_cast<float>(FMath::Max(Size.X, Size.Y)) / FMath::Max(CmPerUnit, KINDA_SMALL_NUMBER);
	const float DepthUnits = Prescale(FMath::Max(SpanUnits * FarSpanFactor, FarMinSpanUnits));

	const FVector2D Centre = Box.GetCenter();
	OutPose.Focus = ArenaXf.TransformPosition(FVector(Centre.X, Centre.Y, 0.0));
	OutPose.Focus.Z = FloorZ;
	OutPose.Pitch = UnitsToDegrees(FarPitchUnits);
	OutPose.Offset = FVector(0.0, CameraUnitsToCm(FarLiftUnits), CameraUnitsToCm(DepthUnits) * DistanceScale);

	// O giro passa direto (o caso 9 não o toca): é o relógio da órbita, que anda sozinho
	if (bOrbit && bHasPose)
	{
		Live.Yaw = static_cast<float>(FRotator::NormalizeAxis(Live.Yaw + OrbitDegreesPerSecond * DeltaSeconds));
	}
	OutPose.Yaw = Live.Yaw;
	return true;
}

float AJRPGBattleCamera::CameraUnitsToCm(float Units) const
{
	const AJRPGBattleArena* CurrentArena = Arena.Get();
	const float CmPerUnit = CurrentArena ? CurrentArena->OriginalCmPerUnit : 0.433005f;
	// As translações da câmera estão na escala 4x da luta (a matriz base de batalha escala tudo por 4)
	return Units * 0.25f * CmPerUnit;
}

TArray<AJRPGBattlePawn*> AJRPGBattleCamera::GetVisiblePawns() const
{
	TArray<AJRPGBattlePawn*> Out;
	if (const AJRPGBattleArena* CurrentArena = Arena.Get())
	{
		for (AJRPGBattlePawn* Pawn : CurrentArena->GetBattlePawns())
		{
			if (IsValid(Pawn) && !Pawn->IsHidden())
			{
				Out.Add(Pawn);
			}
		}
	}
	return Out;
}

FVector AJRPGBattleCamera::FeetOf(const AJRPGBattlePawn* Pawn)
{
	const float Half = Pawn->Capsule ? Pawn->Capsule->GetScaledCapsuleHalfHeight() : 0.0f;
	return Pawn->GetActorLocation() - FVector(0.0, 0.0, Half);
}

void AJRPGBattleCamera::HandleFormationShifted(FVector Translation, int32 Frames)
{
	// De uma vez (Frames 0): o foco anda o mesmo tanto no mesmo quadro, e a cena parece parada. Deslizando, a perseguição acompanha
	if (Frames <= 0 && bHasPose)
	{
		Live.Focus += Translation;
		Target.Focus += Translation;
		ApplyLivePose();
	}
}

void AJRPGBattleCamera::Chase(float DeltaSeconds, float Steps)
{
	if (Steps <= 1.0f)
	{
		Live = Target;
		return;
	}
	// Cada passo de 30 Hz cobre 1/Steps do que falta: em DeltaSeconds, sobra (1 - 1/Steps)^(passos)
	const float Keep = FMath::Pow(1.0f - 1.0f / Steps, DeltaSeconds * ChaseStepsPerSecond);
	const float Alpha = 1.0f - Keep;
	Live.Focus = FMath::Lerp(Live.Focus, Target.Focus, Alpha);
	Live.Offset = FMath::Lerp(Live.Offset, Target.Offset, Alpha);
	Live.Pitch = FMath::Lerp(Live.Pitch, Target.Pitch, Alpha);
	// O giro pelo caminho mais curto
	Live.Yaw = static_cast<float>(FRotator::NormalizeAxis(Live.Yaw + FMath::FindDeltaAngleDegrees(Live.Yaw, Target.Yaw) * Alpha));
}

void AJRPGBattleCamera::ApplyLivePose()
{
	const FRotator Rotation(-Live.Pitch, Live.Yaw, 0.0);
	const FRotationMatrix Axes(Rotation);
	const FVector Forward = Axes.GetUnitAxis(EAxis::X);
	const FVector Right = Axes.GetUnitAxis(EAxis::Y);
	const FVector Up = Axes.GetUnitAxis(EAxis::Z);
	// No espaço da vista o foco está em (TR.x, TR.y para baixo, TR.z à frente): o olho fica atrás, acima e do outro lado
	const FVector Eye = Live.Focus - Forward * Live.Offset.Z + Up * Live.Offset.Y - Right * Live.Offset.X;
	SetActorLocationAndRotation(Eye, Rotation);
}

void AJRPGBattleCamera::ApplyLens()
{
	UCameraComponent* CameraComp = GetCameraComponent();
	if (!CameraComp || !bOriginalLens)
	{
		return;
	}
	// O FOV do componente é horizontal na AspectRatio dele; com a vertical presa, a engine tira a vertical daqui (uns 50 graus)
	CameraComp->bConstrainAspectRatio = false;
	CameraComp->SetFieldOfView(FieldOfView43);
	CameraComp->SetAspectRatio(4.0f / 3.0f);
	CameraComp->bOverrideAspectRatioAxisConstraint = true;
	CameraComp->SetAspectRatioAxisConstraint(EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV);
}
