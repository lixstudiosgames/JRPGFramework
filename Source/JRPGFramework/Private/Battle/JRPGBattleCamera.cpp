#include "Battle/JRPGBattleCamera.h"
#include "Battle/JRPGBattleArena.h"
#include "Battle/JRPGBattlePawn.h"
#include "Battle/JRPGBattleSceneSubsystem.h"
#include "Battle/JRPGBattleUISubsystem.h"
#include "Battle/BattleSubsystem.h"
#include "Battle/HeroBattleData.h"
#include "Arts/ArtsData.h"
#include "Arts/JRPGArtSequence.h"
#include "Core/CoreSubsystem.h"
#include "Engine/DataTable.h"
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
	ActionActor = nullptr;
	ActionTarget = nullptr;
	ActionIndex = INDEX_NONE;
	bInAction = bActionMelee = bActionSpirit = bActionStruck = false;
	DoneTimeLeft = 0.0f;
	DeathRamp = 0.0f;
	ArtColumn = 0;
	ArtRamp = 0.0f;
	ArtAccum = 0.0;
	ArtLatch = 0;
	bArtTracksLoaded = false;
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

	// A opção Battle Camera do jogo (a tela de opções)
	if (bFollowGameOption)
	{
		if (const UGameInstance* GI = GetGameInstance())
		{
			if (const UBattleSubsystem* Battle = GI->GetSubsystem<UBattleSubsystem>())
			{
				CameraOption = Battle->GetBattleCameraOption();
			}
		}
	}

	// O prólogo de FUN_801D5854: as rampas das câmeras andam 8 por quadro de tela, a do Art até 0xC8, o acumulador sem teto
	ArtRamp = FMath::Min(ArtRamp + 480.0f * DeltaSeconds, 200.0f);
	ArtAccum += 480.0 * DeltaSeconds;

	// Re-armado todo quadro, como o original: a pose alvo sai dos combatentes de agora
	UpdateActionState(DeltaSeconds);
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
	float Steps = Shot == EJRPGBattleShot::Far ? FarChaseSteps : ChaseSteps;

	// A câmera própria da Art: por cima da tomada da ação (o tail de FUN_801D5854 chama o override depois do caso), com o ease-out
	// curto dela
	if (Shot == EJRPGBattleShot::Action || Shot == EJRPGBattleShot::TwoShot || Shot == EJRPGBattleShot::OnTarget)
	{
		FJRPGBattleCameraPose ArtPose;
		float ArtSteps = ChaseSteps;
		if (BuildArtPose(ArtPose, ArtSteps))
		{
			NewTarget = ArtPose;
			Steps = ArtSteps;
			Shot = EJRPGBattleShot::Art;
		}
	}
	Target = NewTarget;

	if (!bHasPose)
	{
		SnapToTarget();
		PrevShot = Shot;
	}
	else
	{
		// O arco longo do caso 7 só na entrada dos golpes (vindo da tomada de quem age): voltando da câmera de uma Art, ou trocando
		// entre 7 e 8, o caminho curto (senão a câmera dava a volta a cada corte de uma combo e trocava de lado o tempo todo)
		Chase(DeltaSeconds, Steps, Shot != PrevShot && PrevShot == EJRPGBattleShot::Action);
		ApplyLivePose();
		PrevShot = Shot;
	}

	if (bPrintDebug && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(DebugMessageKey, 0.0f, FColor(150, 220, 255), FString::Printf(
			TEXT("BattleCamera %s (slot %d -> %d, style %d, ladder %.0f) | yaw %.1f pitch %.1f | TR (%.0f, %.0f, %.0f) cm | focus %s"),
			*StaticEnum<EJRPGBattleShot>()->GetNameStringByValue(static_cast<int64>(Shot)), ShotSlot, ShotTargetSlot, Style, LadderUnits,
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

	// A ação (a máquina de estados FUN_801E295C): quem age na preparação e na caminhada (6), os dois nos golpes (7), o alvo no
	// knockdown / na queda e no fim (8)
	if (bActionShots)
	{
		const AJRPGBattlePawn* Actor = ActionActor.Get();
		const AJRPGBattlePawn* Victim = ActionTarget.Get();
		if (Actor && (bInAction || DoneTimeLeft > 0.0f))
		{
			OutSlot = Actor->GetCombatant().SlotIndex;
			OutTargetSlot = Victim ? Victim->GetCombatant().SlotIndex : INDEX_NONE;
			if (bInAction)
			{
				if (bActionSpirit || !Victim)
				{
					return EJRPGBattleShot::Action;
				}
				switch (Actor->GetPhase())
				{
				case EJRPGActionPhase::Strike:
				case EJRPGActionPhase::Chain:
					return EJRPGBattleShot::TwoShot;   // o laço dos golpes (0x1E): sempre o 7
				case EJRPGActionPhase::Recovery:
					// A espera do fim (0x1F / 0x20): o 7, ou o 8 com o alvo no knockdown / levantar ou caindo morto
					return (Victim->IsOnKnockdown() || Victim->IsDefeated()) ? EJRPGBattleShot::OnTarget : EJRPGBattleShot::TwoShot;
				default:
					// A preparação e a caminhada (0x14..0x19); um starter de Art no meio da combo continua no laço dos golpes
					return bActionStruck ? EJRPGBattleShot::TwoShot : EJRPGBattleShot::Action;
				}
			}
			// O done band (0x50 / 0x51): uma ação de golpe fica no alvo, um herói alvo caído também; o resto em quem agiu
			if (Victim && (bActionMelee || (Victim->GetCombatant().Side == EJRPGBattleSide::Party && Victim->IsDefeated())))
			{
				return EJRPGBattleShot::OnTarget;
			}
			return EJRPGBattleShot::Action;
		}
		OutSlot = OutTargetSlot = INDEX_NONE;
	}
	return EJRPGBattleShot::Far;
}

// ---------------------------------------------------------------------------------------------
// A câmera própria das Arts (FUN_801D71B8; o port do RE é legaia-engine-battle-vm::battle_attack_camera)
// ---------------------------------------------------------------------------------------------

namespace
{
	/** Um halfword do original: o resultado de somas em 16 bits com sinal. */
	int32 Half(int64 Value)
	{
		return static_cast<int16>(static_cast<uint16>(Value & 0xFFFF));
	}

	/** O empurrão pelo cursor do clip (0x801D735C): nada abaixo de 0x61; (cursor - 0x60) << 4, até 0x100. */
	bool AnimPush(int32 Cursor, int32& OutPush)
	{
		if (Cursor < 0x61)
		{
			return false;
		}
		OutPush = FMath::Min((Cursor - 0x60) << 4, 0x100);
		return true;
	}
}

void AJRPGBattleCamera::LoadArtTracks()
{
	bArtTracksLoaded = true;
	ArtTracks.Init(FIntPoint::ZeroValue, 20);
	const UDataTable* Table = UCoreSubsystem::ResolveDataTable(TEXT("DT_ArtCamera"));
	if (!Table)
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleCamera: DT_ArtCamera não encontrada (Extras/generate_art_camera_csv.py) — a câmera das Arts roda com as linhas em 0"));
		return;
	}
	for (int32 Row = 0; Row < ArtTracks.Num(); ++Row)
	{
		if (const FJRPGArtCameraTrackData* Data = Table->FindRow<FJRPGArtCameraTrackData>(*FString::Printf(TEXT("Row%02d"), Row), TEXT("BattleCamera"), false))
		{
			ArtTracks[Row] = FIntPoint(Data->Value0, Data->Value1);
		}
	}
}

int32 AJRPGBattleCamera::ArtTrack(int32 Row, int32 Column) const
{
	if (!ArtTracks.IsValidIndex(Row))
	{
		return 0;
	}
	return Column != 0 ? ArtTracks[Row].Y : ArtTracks[Row].X;
}

bool AJRPGBattleCamera::RunArtArm(int32 Character, int32 ArtId, int32 Select, int32 Cursor, int32 (&Rot)[3], int32 (&Dist)[3], int32& OutDuration)
{
	// As três tabelas de salto (0x801CEA88 / 0x801CEAD0 / 0x801CEB20): o braço de cada herói por clip
	enum class EArm : uint8 { None, A7308, A74A8, A7568, A7650, A76EC, A7870, A78F0, A797C, A79F8, A7B4C, A7D7C, A7EA0, A81FC };
	EArm Arm = EArm::None;
	switch (Character)
	{
	case 1:
		Arm = ArtId == 0x1A ? EArm::A7308 : ArtId == 0x1C ? EArm::A7650 : ArtId == 0x1D ? EArm::A7568
			: (ArtId == 0x1E || ArtId == 0x2A) ? EArm::A74A8 : EArm::None;
		break;
	case 2:
		Arm = ArtId == 0x1A ? EArm::A76EC : (ArtId == 0x1D || ArtId == 0x2C) ? EArm::A78F0 : (ArtId == 0x1E || ArtId == 0x2D) ? EArm::A797C
			: ArtId == 0x1F ? EArm::A79F8 : ArtId == 0x20 ? EArm::A7870 : EArm::None;
		break;
	case 3:
		Arm = ArtId == 0x1A ? EArm::A7B4C : ArtId == 0x1C ? EArm::A7D7C : ArtId == 0x1D ? EArm::A7EA0
			: (ArtId == 0x1E || ArtId == 0x2A) ? EArm::A81FC : EArm::None;
		break;
	default:
		break;
	}
	if (Arm == EArm::None)
	{
		return false;
	}

	const int32 Column = ArtColumn;
	auto T = [this, Column](int32 Row) { return ArtTrack(Row, Column); };
	auto RampNow = [this]() { return static_cast<int32>(ArtRamp); };
	auto AccNow = [this]() { return static_cast<uint32>(static_cast<uint64>(ArtAccum) & 0xFFFFFFFFu); };
	// A zerada na passagem de um limiar: com a trava ainda em Expect, a rampa (e o acumulador) zeram e a trava anda
	auto LatchReset = [this](int32 Expect, bool bClearAccum)
	{
		if (ArtLatch != Expect)
		{
			return;
		}
		ArtRamp = 0.0f;
		if (bClearAccum)
		{
			ArtAccum = 0.0;
		}
		++ArtLatch;
	};
	// O bloco do meio do golpe (0x801D7420 e irmãos) e o fim do braço 0x1A do Gala (0x801D7CF4): a rampa lida DEPOIS da zerada
	auto ThresholdArm = [&](int32 Expect, int32 YawRow, int32 DepthRow)
	{
		LatchReset(Expect, false);
		Rot[0] += 0xC0;
		Rot[1] += YawRow;
		Dist[1] -= 0x200;
		Dist[2] += DepthRow - (RampNow() << 1);
	};
	auto LateArm = [&](int32 Row)
	{
		LatchReset(1, false);
		Dist[1] -= 0x100;
		Dist[2] += 0x200;
		Rot[1] += Row;
		Dist[2] -= RampNow() << 1;
	};

	const int32 R = RampNow();
	const uint32 Acc = AccNow();
	const int32 AccLo = Half(Acc);
	auto AccShr = [Acc](int32 K) { return Half(Acc >> K); };
	auto AccShl = [Acc](int32 K) { return Half(static_cast<uint32>(Acc << K)); };
	const int32 F = Cursor;
	const int32 S = Select;
	int32 Push = 0;

	switch (Arm)
	{
	// --- Vahn ---------------------------------------------------------------------------------
	case EArm::A7308:   // o dash (0x1A)
		if (S == 5)
		{
			Dist[2] -= 0x100;
			Rot[0] += T(0);
			Rot[1] += T(1);
			if (AnimPush(F, Push))
			{
				Dist[1] -= Push;
				if (Column != 0)
				{
					Rot[1] += Push >> 1;
					Dist[2] -= Push >> 2;
					Dist[0] += Push;
				}
				else
				{
					Rot[1] -= Push;
					Dist[2] -= Push >> 1;
				}
			}
			OutDuration = 6;
		}
		else if (S < 2)
		{
			LateArm(T(4));
			OutDuration = 1;
		}
		else
		{
			ThresholdArm(0, T(2), T(3));
			OutDuration = 1;
		}
		break;
	case EArm::A74A8:   // Tornado Flame (0x1E / 0x2A)
		if (F < 0xE0)
		{
			Rot[0] -= R >> 2;
			Rot[1] += T(5) + AccLo;
			Dist[1] += R;
			Dist[2] += R >> 2;
			OutDuration = 6;
		}
		else
		{
			Rot[1] += 0x600;
			Dist[2] += 0x400;
			Dist[0] -= 0x200;
			OutDuration = 1;
		}
		break;
	case EArm::A7568:   // Fire Blow (0x1D)
		if (F < 0xF0)
		{
			Rot[0] -= R >> 1;
			Rot[1] += T(6) + AccLo;
			Dist[1] += R << 1;
			Dist[2] += AccShr(2);
			OutDuration = 6;
		}
		else
		{
			Rot[0] += 0x100;
			Dist[0] -= 0x400;
			Rot[1] += 0x200;
			Dist[2] += 0x400;
			Dist[1] += 0x200;
			OutDuration = 3;
		}
		break;
	case EArm::A7650:   // Burning Flare (0x1C)
		Rot[0] += R >> 2;
		Rot[1] += T(7) + AccLo;
		Dist[2] += AccShr(1);
		OutDuration = 6;
		break;
	// --- Noa ----------------------------------------------------------------------------------
	case EArm::A76EC:   // o dash (0x1A)
		if (S == 2)
		{
			Dist[2] -= 0x100;
			Dist[0] -= 0x100;
			Rot[0] += T(0);
			Rot[1] += T(13);
			OutDuration = 6;
		}
		else if (F < 0x90)
		{
			ThresholdArm(0, T(2), T(3));
			OutDuration = 1;
		}
		else
		{
			LateArm(T(14));
			OutDuration = 1;
		}
		break;
	case EArm::A7870:   // Frost Breath (0x20)
		Rot[0] += R >> 2;
		Rot[1] += T(8);
		Dist[1] += R;
		Dist[2] -= AccShr(1) - 0x400;
		OutDuration = 6;
		break;
	case EArm::A78F0:   // Hurricane Kick / Jurassic Blow 1 (0x1D / 0x2C): a linha e o acumulador somados e SUBTRAÍDOS do giro
		Rot[0] += R >> 1;
		Dist[0] += 0x200;
		Rot[1] -= T(9) + AccShr(1);
		Dist[1] += R >> 1;
		Dist[2] += AccShl(1);
		OutDuration = 6;
		break;
	case EArm::A797C:   // Hurricane Kick 3 / Jurassic Blow 2 (0x1E / 0x2D): a rampa OR 0xFF80 no pitch
		Rot[0] += Half(static_cast<uint16>(R >> 2) | 0xFF80);
		Dist[0] -= 0x300;
		Rot[1] += T(10) + AccShr(1);
		Dist[2] += AccShr(1);
		OutDuration = 1;
		break;
	case EArm::A79F8:   // Vulture Blade (0x1F)
		if (F < 0xE0)
		{
			Rot[0] += R >> 1;
			Rot[1] += T(11) + AccShr(1);
			Dist[0] += T(15);
			Dist[2] += R >> 1;
			OutDuration = 6;
		}
		else
		{
			Rot[0] += R >> 1;
			Rot[1] += T(12);
			Dist[0] += T(16);
			Dist[1] -= R >> 1;
			Dist[2] += R * 3;
			OutDuration = 1;
		}
		break;
	// --- Gala ---------------------------------------------------------------------------------
	case EArm::A7B4C:   // o dash (0x1A)
		if (F < 0x70)
		{
			Dist[2] -= T(19) + R;
			Rot[0] += T(0);
			Rot[1] += T(17);
			OutDuration = 6;
		}
		else if (F < 0xA0)
		{
			ThresholdArm(0, T(2), T(3));
			OutDuration = 1;
		}
		else
		{
			LateArm(T(4));
			OutDuration = 1;
		}
		break;
	case EArm::A7D7C:   // Explosive Fist (0x1C): quatro cortes, só literais
		if (F < 0x40)
		{
			Rot[0] += 0x80;
			Dist[0] -= 0x200;
			Rot[1] += 0x200;
		}
		else if (F < 0x70)
		{
			Rot[0] -= 0x100;
			Rot[1] -= 0x200;
			Dist[0] += 0x280;
			Dist[1] -= 0x200;
		}
		else if (F < 0xA0)
		{
			Rot[0] -= 0x100;
			Rot[1] += 0x200;
			Dist[0] -= 0x200;
			Dist[1] -= 0x200;
		}
		else
		{
			Rot[0] += 0x80;
			Rot[1] -= 0x200;
			Dist[0] += 0x300;
		}
		OutDuration = 1;
		break;
	case EArm::A7EA0:   // Lightning Storm (0x1D): o único com roll, e uma linha lida numa coluna fixa
		if (F < 0xB0)
		{
			if (Column != 0)
			{
				Rot[0] -= R >> 1;
				Rot[1] += T(18) + AccShl(1);
				Rot[2] -= R >> 1;
				Dist[0] += AccLo;
				Dist[1] -= AccShr(1);
				const int32 Third = Half(static_cast<uint32>(Acc / 3u));
				Dist[2] += Third - 0x100;
			}
			else
			{
				Rot[0] += R;
				Rot[1] += ArtTrack(18, 0) + AccLo;
			}
			OutDuration = 6;
		}
		else if (F < 0x110)
		{
			LatchReset(0, true);
			const uint32 Acc2 = AccNow();
			const int32 Lo = Half(Acc2);
			const int32 Shl1 = Half(static_cast<uint32>(Acc2 << 1));
			if (Column != 0)
			{
				Rot[0] += Lo - 0x60;
				Rot[1] -= Half(static_cast<uint32>(Acc2 * 3u)) - 0x400;
				Dist[0] += Shl1 - 0x300;
				Dist[1] += Shl1 - 0x200;
				Dist[2] -= Shl1 - 0x200;
			}
			else
			{
				Dist[0] -= 0x400;
				Rot[1] += 0x280;
				Dist[2] -= 0x100;
				Dist[1] -= 0x300;
				Rot[0] -= (RampNow() >> 2) + 0x80;
			}
			OutDuration = 1;
		}
		else
		{
			LatchReset(1, true);
			const uint32 Acc2 = AccNow();
			const int32 R2 = RampNow();
			const int32 Shl1 = Half(static_cast<uint32>(Acc2 << 1));
			Dist[0] -= Shl1 - 0x400;
			if (Column != 0)
			{
				Rot[0] -= R2;
				Rot[1] += R2 * 6 - 0x600;
				Dist[1] -= R2;
				Dist[2] -= R2;
			}
			else
			{
				Rot[0] -= 0x80;
				Dist[2] -= 0x100;
				Dist[1] -= 0x100;
				Rot[1] += Half(Acc2) - 0x200;
			}
			OutDuration = 1;
		}
		break;
	case EArm::A81FC:   // Thunder Punch (0x1E / 0x2A): passado 0xC0 não arma nada (a tomada da ação fica)
		if (F >= 0xC0)
		{
			return false;
		}
		Rot[1] += R * 5 - 0x600;
		Dist[0] += R << 2;
		Dist[2] += 0x40 - (R << 1);
		OutDuration = 1;
		break;
	default:
		return false;
	}

	// Os slots da pose são halfwords: as somas dão a volta em 16 bits, como no original
	for (int32 i = 0; i < 3; ++i)
	{
		Rot[i] = Half(Rot[i]);
		Dist[i] = Half(Dist[i]);
	}
	return true;
}

bool AJRPGBattleCamera::BuildArtPose(FJRPGBattleCameraPose& OutPose, float& OutSteps)
{
	// O portão (0x801D7138 e a entrada de FUN_801D71B8): a opção não é Far, um herói (Vahn / Noa / Gala) numa ação de golpe, o alvo
	// vivo (a re-mira da morte nunca é coberta)
	const AJRPGBattlePawn* Actor = ActionActor.Get();
	const AJRPGBattlePawn* Victim = ActionTarget.Get();
	if (!bArtShots || CameraOption == EJRPGBattleCameraOption::Far || !bInAction || !bActionMelee || !IsValid(Actor) || Actor->IsHidden()
		|| !IsValid(Victim) || Victim->IsDefeated() || Actor->GetCombatant().Side != EJRPGBattleSide::Party)
	{
		return false;
	}
	const FName Key = Actor->GetCombatant().Key;
	const int32 Character = Key == TEXT("Vahn") ? 1 : Key == TEXT("Noa") ? 2 : Key == TEXT("Gala") ? 3 : 0;
	if (Character == 0)
	{
		return false;
	}

	// O clip latched (+0x1DB): o dash do Super / Miracle é 0x1A, uma Art é o ActionConstant dela (0x1B..0x32)
	FJRPGActionStep Step;
	float ClipSeconds = 0.0f;
	float ClipLength = 0.0f;
	if (!Actor->GetPlayingStep(Step, ClipSeconds, ClipLength))
	{
		return false;
	}
	int32 ArtId = 0;
	int32 DiscRate = StarterDiscRate;
	if (Step.Kind == EJRPGActionStepKind::SpecialStarter)
	{
		ArtId = 0x1A;
	}
	else if (Step.Kind == EJRPGActionStepKind::Art)
	{
		if (const FJRPGArtData* Art = UJRPGArtSequenceLibrary::FindArt(Step.ArtKey))
		{
			ArtId = Art->ActionConstant;
		}
		if (const UDataTable* Hits = UCoreSubsystem::ResolveDataTable(TEXT("DT_ArtHits")))
		{
			if (const FJRPGArtHitData* Row = Hits->FindRow<FJRPGArtHitData>(Step.ArtKey, TEXT("BattleCamera"), false))
			{
				DiscRate = FMath::Max(Row->Rate, 1);
			}
		}
	}
	if (ArtId == 0)
	{
		return false;
	}
	if (!bArtTracksLoaded)
	{
		LoadArtTracks();
	}

	// O cursor do clip em 1/16 de keyframe do disco (o asset vem a 7,5 x rate keyframes por segundo); a contagem de loops do dash
	// sai do trecho de laço real do clip (UJRPGAnimNotify_LoopWindow: as voltas que faltam, o +0x21B do original, e o cursor já com as
	// voltas); num clip importado antes do trecho de laço, do progresso pela timeline
	float CursorSeconds = ClipSeconds;
	int32 Select = 0;
	int32 LoopsLeft = 0;
	bool bHasLoopWindow = false;
	if (Actor->GetPlayingClipCursor(CursorSeconds, LoopsLeft, bHasLoopWindow) && bHasLoopWindow)
	{
		Select = LoopsLeft;
	}
	else
	{
		CursorSeconds = ClipSeconds;
		const float Progress = ClipLength > 0.0f ? FMath::Clamp(ClipSeconds / ClipLength, 0.0f, 1.0f) : 0.0f;
		Select = FMath::CeilToInt((1.0f - Progress) * static_cast<float>(StarterLoopCount));
	}
	const int32 Cursor = FMath::FloorToInt(CursorSeconds * 7.5f * static_cast<float>(DiscRate) * 16.0f);

	// A pose semente (0x801D7230): pitch 0, o giro preso à frente, TR (0, 0x400, 0x400) (0x600 de altura no Gala), olhando o herói
	int32 Rot[3] = { 0, 0, 0 };
	int32 Dist[3] = { 0, Character == 3 ? 0x600 : 0x400, 0x400 };
	int32 Duration = 6;
	if (!RunArtArm(Character, ArtId, Select, Cursor, Rot, Dist, Duration))
	{
		return false;
	}

	OutPose.Focus = FeetOf(Actor);
	OutPose.Yaw = FacingRelativeYaw(Actor, static_cast<float>(Rot[1]));
	// O pitch em +-89 graus (a soma do original pode passar da vertical; uma câmera de cabeça para baixo aqui não serve)
	const int32 PitchUnits = ((Rot[0] % 4096) + 4096 + 2048) % 4096 - 2048;
	OutPose.Pitch = FMath::Clamp(UnitsToDegrees(static_cast<float>(PitchUnits)), -89.0f, 89.0f);
	OutPose.Offset = FVector(CameraUnitsToCm(static_cast<float>(Dist[0])), CameraUnitsToCm(static_cast<float>(Dist[1])),
		FMath::Max(CameraUnitsToCm(Prescale(static_cast<float>(Dist[2]))) * DistanceScale, 20.0f));
	OutPose.bOneWayYaw = false;
	// A duração do original em quadros de tela; um passo do ease-out são dois (1 = corte)
	OutSteps = static_cast<float>(Duration) * 0.5f;
	return true;
}

void AJRPGBattleCamera::UpdateActionState(float DeltaSeconds)
{
	AJRPGBattlePawn* Acting = nullptr;
	if (const AJRPGBattleArena* CurrentArena = Arena.Get())
	{
		for (AJRPGBattlePawn* Pawn : CurrentArena->GetBattlePawns())
		{
			if (IsValid(Pawn) && Pawn->GetActiveActionIndex() != INDEX_NONE)
			{
				Acting = Pawn;
				break;
			}
		}
	}

	if (Acting)
	{
		AJRPGBattlePawn* Victim = Acting->GetActionTarget();
		if (Victim == Acting)
		{
			Victim = nullptr;
		}
		const int32 Index = Acting->GetActiveActionIndex();
		if (!bInAction || ActionActor.Get() != Acting || ActionIndex != Index)
		{
			// O começo da ação (o estado 0x0C): a escada de frente (0x800), 0x200 numa ação de golpe, um dos quatro ângulos, e a
			// distância pelo tamanho do monstro (o Spirit: 0x800, e só o ângulo de baixo ou não)
			ActionActor = Acting;
			ActionIndex = Index;
			ActionTarget = Victim;
			bInAction = true;
			bActionStruck = false;
			DoneTimeLeft = 0.0f;
			const FJRPGActionTimeline* Timeline = Acting->GetActiveTimeline();
			bActionSpirit = Timeline && Timeline->Steps.ContainsByPredicate([](const FJRPGActionStep& Step)
			{
				return Step.Kind == EJRPGActionStepKind::Spirit;
			});
			bActionMelee = Victim && !bActionSpirit;
			LadderUnits = bActionMelee ? AttackYawUnits : SeedYawUnits;
			Style = !bRandomStyle ? 0 : (bActionSpirit ? FMath::RandRange(0, 1) * 2 : FMath::RandRange(0, 3));
			// A câmera das Arts: a coluna da tabela é um cara-ou-coroa da ação (o Gala sempre a 0); rampa, acumulador e trava zeram
			ArtColumn = Acting->GetCombatant().Key == TEXT("Gala") ? 0 : FMath::RandRange(0, 1);
			ArtRamp = 0.0f;
			ArtAccum = 0.0;
			ArtLatch = 0;
			if (bActionSpirit)
			{
				ActionDepthUnits = SpiritDepthUnits;
			}
			else
			{
				// FUN_801F0348: a classe do alvo quando um herói ataca; a do próprio monstro quando ele ataca
				int32 SizeClass = 0;
				if (Acting->GetCombatant().Side == EJRPGBattleSide::Enemy)
				{
					SizeClass = Acting->GetSizeClass();
				}
				else if (Victim && Victim->GetCombatant().Side == EJRPGBattleSide::Enemy)
				{
					SizeClass = Victim->GetSizeClass();
				}
				ActionDepthUnits = FMath::Clamp(static_cast<float>(SizeClass << 7), ActionMinDepthUnits, FMath::Max(ActionMinDepthUnits, ActionMaxDepthUnits));
			}
		}
		else if (Victim)
		{
			ActionTarget = Victim;
		}

		const EJRPGActionPhase Phase = Acting->GetPhase();
		if (!bActionStruck && (Phase == EJRPGActionPhase::Strike || Phase == EJRPGActionPhase::Chain))
		{
			bActionStruck = true;
			// O primeiro clip de golpe de um herói (FUN_8004E13C): três quartos de um dos dois lados, sem o ângulo sorteado
			if (Acting->GetCombatant().Side == EJRPGBattleSide::Party && !bActionSpirit)
			{
				LadderUnits = static_cast<float>(FMath::RandRange(0, 1) * 2048) + SwingYawUnits;
				Style = 0;
			}
		}
	}
	else if (bInAction)
	{
		// Acabou: o done band segura a tomada (o timer 0x3C), depois o plano geral volta
		bInAction = false;
		DoneTimeLeft = DoneHoldSeconds;
	}
	else if (DoneTimeLeft > 0.0f)
	{
		DoneTimeLeft = FMath::Max(0.0f, DoneTimeLeft - DeltaSeconds);
	}

	// O giro lento de toda ação (o prólogo da máquina de estados; a opção Far o pula)
	if (CameraOption != EJRPGBattleCameraOption::Far && (bInAction || DoneTimeLeft > 0.0f))
	{
		LadderUnits = FMath::Fmod(LadderUnits + DriftUnitsPerSecond * DeltaSeconds, 4096.0f);
	}
}

float AJRPGBattleCamera::ActionYaw(const AJRPGBattlePawn* Actor, float ExtraUnits) const
{
	// O bit 0 do ângulo sorteado filma do outro lado
	return FacingRelativeYaw(Actor, LadderUnits + ((Style & 1) ? 2048.0f : 0.0f) + ExtraUnits);
}

void AJRPGBattleCamera::ApplyActionBase(FJRPGBattleCameraPose& OutPose) const
{
	// O bit 1 do ângulo sorteado: de baixo (a inclinação sobe 0x80 e o olho desce para 0x400)
	const bool bLow = (Style & 2) != 0;
	OutPose.Pitch = UnitsToDegrees(bLow ? StylePitchUnits : 0.0f);
	OutPose.Offset = FVector(0.0, CameraUnitsToCm(bLow ? StyleLiftUnits : ActionLiftUnits),
		CameraUnitsToCm(Prescale(ActionDepthUnits)) * DistanceScale);
}

bool AJRPGBattleCamera::BuildActionPose(FJRPGBattleCameraPose& OutPose) const
{
	const AJRPGBattlePawn* Actor = ActionActor.Get();
	if (!IsValid(Actor) || Actor->IsHidden())
	{
		return false;
	}
	// O caso 6: a posição viva de quem age (ela anda na caminhada e a câmera vai junto), o foco no chão
	OutPose.Focus = FeetOf(Actor);
	OutPose.Yaw = ActionYaw(Actor, 0.0f);
	ApplyActionBase(OutPose);
	return true;
}

bool AJRPGBattleCamera::BuildTwoShotPose(FJRPGBattleCameraPose& OutPose) const
{
	const AJRPGBattlePawn* Actor = ActionActor.Get();
	const AJRPGBattlePawn* Victim = ActionTarget.Get();
	if (!IsValid(Actor) || Actor->IsHidden())
	{
		return false;
	}
	if (!IsValid(Victim) || Victim->IsHidden())
	{
		return BuildActionPose(OutPose);
	}
	// O caso 7: o meio dos dois corpos (os body pairs), no chão; o giro do 6 menos 0x700, sempre no mesmo sentido
	OutPose.Focus = (Actor->GetBodyLocation() + Victim->GetBodyLocation()) * 0.5;
	OutPose.Focus.Z = (FeetOf(Actor).Z + FeetOf(Victim).Z) * 0.5;
	OutPose.Yaw = ActionYaw(Actor, -TwoShotYawUnits);
	OutPose.bOneWayYaw = true;
	ApplyActionBase(OutPose);
	if (CameraOption == EJRPGBattleCameraOption::Close)
	{
		// O pull-in da opção Close: nivelado, o olho 0x40 mais alto, 3/5 da distância
		OutPose.Pitch = 0.0f;
		OutPose.Offset.Y += CameraUnitsToCm(64.0f);
		OutPose.Offset.Z *= 0.6;
	}
	return true;
}

bool AJRPGBattleCamera::BuildOnTargetPose(float DeltaSeconds, FJRPGBattleCameraPose& OutPose)
{
	const AJRPGBattlePawn* Actor = ActionActor.Get();
	const AJRPGBattlePawn* Victim = ActionTarget.Get();
	if (!IsValid(Actor))
	{
		return false;
	}
	const bool bHasVictim = IsValid(Victim) && !Victim->IsHidden();
	// O caso 8: o corpo do alvo, no chão; sem ele (sumiu), quem agiu
	if (bHasVictim)
	{
		OutPose.Focus = Victim->GetBodyLocation();
		OutPose.Focus.Z = FeetOf(Victim).Z;
	}
	else if (!Actor->IsHidden())
	{
		OutPose.Focus = FeetOf(Actor);
	}
	else
	{
		return false;
	}

	const bool bClose = CameraOption == EJRPGBattleCameraOption::Close;
	if (bClose && bHasVictim && Victim->IsDefeated())
	{
		// A queda (opção Close): a escada zera (a tomada da morte não herda o giro dos golpes)
		LadderUnits = 0.0f;
	}
	ApplyActionBase(OutPose);
	if (CameraOption == EJRPGBattleCameraOption::Far)
	{
		OutPose.Yaw = Live.Yaw;   // a opção Far não monta o giro do 8: fica onde está
	}
	else
	{
		OutPose.Yaw = ActionYaw(Actor, -TwoShotYawUnits);
		OutPose.bOneWayYaw = true;
	}
	if (!bClose || !bHasVictim)
	{
		return true;
	}

	if (Victim->IsDefeated())
	{
		if (Victim->IsFallingDead())
		{
			// O corpo ainda cai: o ramp (8 por quadro de tela, até 0xC8) desce o olho, nivela e chega perto
			DeathRamp = FMath::Min(DeathRamp + 480.0f * DeltaSeconds, 200.0f);
			OutPose.Offset.Z = CameraUnitsToCm(Prescale(FMath::Max(ActionDepthUnits - 4.0f * DeathRamp, 0.0f))) * DistanceScale;
			OutPose.Offset.Y = CameraUnitsToCm(768.0f - DeathRamp);
			OutPose.Pitch = UnitsToDegrees(384.0f - 1.5f * DeathRamp);
		}
		else
		{
			// No chão: a pose plana
			DeathRamp = 0.0f;
			OutPose.Offset.Z = CameraUnitsToCm(Prescale(ActionDepthUnits)) * DistanceScale;
			OutPose.Offset.Y = CameraUnitsToCm(768.0f);
			OutPose.Pitch = UnitsToDegrees(320.0f);
		}
		return true;
	}

	// De pé (0x801D6BFC): a re-mira pela altura do alvo (y = a altura do modelo, em unidades)
	const AJRPGBattleArena* CurrentArena = Arena.Get();
	const float CmPerUnit = CurrentArena ? FMath::Max(CurrentArena->OriginalCmPerUnit, KINDA_SMALL_NUMBER) : 0.433005f;
	const float Height = Victim->GetDisplayHeightCm() / CmPerUnit;
	const bool bTilted = (Style & 2) != 0;
	float Lift = 0.0f;
	float Floor = 0.0f;
	float DepthRaw = 0.0f;
	bool bHold = false;
	if (Victim->GetCombatant().Side == EJRPGBattleSide::Party)
	{
		DepthRaw = 1536.0f;
		Floor = 640.0f;
		if (Victim->IsAnimating())
		{
			Lift = Height * (bTilted ? 3.5f : 4.0f);
		}
		else
		{
			const float* MemberLift = MemberLiftUnits.Find(Victim->GetCombatant().Key);
			Lift = (MemberLift ? *MemberLift : MemberDefaultLiftUnits) - (bTilted ? 192.0f : 320.0f);
		}
	}
	else
	{
		DepthRaw = ActionDepthUnits * 0.7f;
		Floor = 768.0f;
		if (Victim->IsAnimating())
		{
			Lift = Height * (bTilted ? 3.0f : 3.5f);
		}
		else
		{
			bHold = true;   // o monstro parado: a altura e a inclinação de agora ficam
		}
	}
	OutPose.Offset.Z = CameraUnitsToCm(Prescale(DepthRaw)) * DistanceScale;
	if (bHold)
	{
		OutPose.Offset.Y = Live.Offset.Y;
		OutPose.Pitch = Live.Pitch;
	}
	else
	{
		// Abaixo do piso, o olho fica no piso e um quarto do que faltava vira inclinação (a câmera olha para baixo em vez de afundar)
		float PitchUnits = bTilted ? StylePitchUnits : 0.0f;
		if (Lift < Floor)
		{
			PitchUnits += (Floor - Lift) * 0.25f;
			Lift = Floor;
		}
		OutPose.Offset.Y = CameraUnitsToCm(Lift);
		OutPose.Pitch = UnitsToDegrees(PitchUnits);
	}
	return true;
}

bool AJRPGBattleCamera::BuildShotPose(EJRPGBattleShot InShot, float DeltaSeconds, FJRPGBattleCameraPose& OutPose)
{
	switch (InShot)
	{
	case EJRPGBattleShot::Member:
		return BuildMemberPose(OutPose);
	case EJRPGBattleShot::TargetEnemy:
		return BuildTargetEnemyPose(OutPose);
	case EJRPGBattleShot::Action:
		return BuildActionPose(OutPose);
	case EJRPGBattleShot::TwoShot:
		return BuildTwoShotPose(OutPose);
	case EJRPGBattleShot::OnTarget:
		return BuildOnTargetPose(DeltaSeconds, OutPose);
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

void AJRPGBattleCamera::Chase(float DeltaSeconds, float Steps, bool bShotChanged)
{
	if (Steps <= 1.0f)
	{
		Live = Target;
		bYawLongWay = false;
		return;
	}
	// Cada passo de 30 Hz cobre 1/Steps do que falta: em DeltaSeconds, sobra (1 - 1/Steps)^(passos)
	const float Keep = FMath::Pow(1.0f - 1.0f / Steps, DeltaSeconds * ChaseStepsPerSecond);
	const float Alpha = 1.0f - Keep;
	Live.Focus = FMath::Lerp(Live.Focus, Target.Focus, Alpha);
	Live.Offset = FMath::Lerp(Live.Offset, Target.Offset, Alpha);
	Live.Pitch = FMath::Lerp(Live.Pitch, Target.Pitch, Alpha);
	// O giro pelo caminho mais curto; nos casos 7 e 8, só no sentido em que o giro do original cresce (o arco longo, se for ele)
	float YawDelta = FMath::FindDeltaAngleDegrees(Live.Yaw, Target.Yaw);
	// O giro do original sobe; o olhar da câmera anda ao contrário dele (FacingRelativeYaw)
	const float Forward = bMirrorYaw ? 1.0f : -1.0f;
	if (!Target.bOneWayYaw)
	{
		bYawLongWay = false;
	}
	else if (bShotChanged)
	{
		// Escolhido na entrada do caso: o arco longo se o curto for para trás (mais que a folga); um alvo que vira depois não faz a
		// câmera dar a volta inteira
		bYawLongWay = YawDelta * Forward < 0.0f && FMath::Abs(YawDelta) > OneWaySlackDegrees;
	}
	if (bYawLongWay)
	{
		if (YawDelta * Forward < 0.0f)
		{
			YawDelta += Forward * 360.0f;
		}
		else
		{
			bYawLongWay = false;   // passou da meia volta: daqui o caminho curto é o mesmo sentido
		}
	}
	Live.Yaw = static_cast<float>(FRotator::NormalizeAxis(Live.Yaw + YawDelta * Alpha));
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
