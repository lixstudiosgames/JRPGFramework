#include "Core/CoreSubsystem.h"
#include "Inventory/InventorySubsystem.h"
#include "World/WorldStateSubsystem.h"
#include "Party/PartySubsystem.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Engine/DataTable.h"

// ============================================================
// INICIALIZAÇÃO — resolução automática das DataTables
//
// Mesmo padrão do Inventory e do Shop: procura primeiro na pasta Data do
// PROJETO e cai para a do plugin. Assim importar o CSV com o nome certo já
// basta — um GameInstanceSubsystem não tem painel de Details no editor, então
// não havia onde "arrastar" a tabela.
// ============================================================

namespace
{
	/**
	 * Teto de um atributo que não esteja no mapa StatCaps.
	 *
	 * Mora AQUI, no topo, porque UCoreSubsystem::GetStatCap está logo abaixo —
	 * constante de arquivo tem que ser declarada antes do primeiro uso.
	 */
	constexpr int32 DefaultStatCap = 999;

	/** Carrega /Game/Data/<Nome> e, se não achar, /JRPGFramework/Data/<Nome>. */
	UDataTable* ResolveDataTable(const TCHAR* Nome)
	{
		const FString DoProjeto = FString::Printf(TEXT("/Game/Data/%s"), Nome);
		if (UDataTable* Tabela = Cast<UDataTable>(
				StaticLoadObject(UDataTable::StaticClass(), nullptr, *DoProjeto)))
		{
			return Tabela;
		}

		const FString DoPlugin = FString::Printf(TEXT("/JRPGFramework/Data/%s"), Nome);
		return Cast<UDataTable>(
			StaticLoadObject(UDataTable::StaticClass(), nullptr, *DoPlugin));
	}
}

void UCoreSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!StatGrowthTable)
	{
		StatGrowthTable = ResolveDataTable(TEXT("DT_StatGrowth"));
	}
	if (!GrowthCurveTable)
	{
		GrowthCurveTable = ResolveDataTable(TEXT("DT_GrowthCurve"));
	}
	if (!DifficultyTable)
	{
		DifficultyTable = ResolveDataTable(TEXT("DT_Difficulty"));
	}

	// Sem as duas de crescimento NINGUÉM sobe de atributo, e isso seria
	// silencioso — por isso o aviso é explícito.
	if (!StatGrowthTable || !GrowthCurveTable)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("CoreSubsystem: progressão de atributos DESLIGADA — %s não encontrada. ")
			TEXT("Importe o CSV de Docs/Data como DataTable em /Game/Data/ com o mesmo nome, ")
			TEXT("ou chame SetStatGrowthTable/SetGrowthCurveTable."),
			!StatGrowthTable ? TEXT("DT_StatGrowth") : TEXT("DT_GrowthCurve"));
	}

	// Tetos do jogo original. O New Game+ mexe nestes por SetStatCap — menos na
	// AGL, que é travada pelo sistema de Arts (ver SetStatCap).
	if (StatCaps.IsEmpty())
	{
		StatCaps.Add(TEXT("HP"),  9999);
		StatCaps.Add(TEXT("MP"),   999);
		StatCaps.Add(TEXT("AGL"),  280);   // 0x118 — máximo de art blocks
		StatCaps.Add(TEXT("ATK"),  999);
		StatCaps.Add(TEXT("UDF"),  999);
		StatCaps.Add(TEXT("LDF"),  999);
		StatCaps.Add(TEXT("SPD"),  999);
		StatCaps.Add(TEXT("INT"),  999);
	}

	// A de dificuldade é opcional: o C++ tem os mesmos multiplicadores embutidos.
	if (!DifficultyTable)
	{
		UE_LOG(LogTemp, Log,
			TEXT("CoreSubsystem: DT_Difficulty não encontrada — usando os multiplicadores ")
			TEXT("embutidos no C++ (mesmos valores do CSV)."));
	}
}

void UCoreSubsystem::SetMaxLevel(int32 NewMax)
{
	const int32 Antigo = MaxLevel;
	MaxLevel = FMath::Clamp(NewMax, 1, AbsoluteMaxLevel);

	if (NewMax > AbsoluteMaxLevel)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("CoreSubsystem: SetMaxLevel(%d) limitado a %d — acima disso o XP acumulado ")
			TEXT("estoura o int32 (no L598 passa de 2.147.483.647)."),
			NewMax, AbsoluteMaxLevel);
	}

	if (MaxLevel != Antigo)
	{
		UE_LOG(LogTemp, Log, TEXT("CoreSubsystem: teto de level %d -> %d."), Antigo, MaxLevel);
	}
}

int32 UCoreSubsystem::GetStatCap(FName Stat) const
{
	if (const int32* Found = StatCaps.Find(Stat))
	{
		return *Found;
	}
	return DefaultStatCap;
}

void UCoreSubsystem::SetStatCap(FName Stat, int32 NewCap)
{
	if (NewCap < 1)
	{
		return;
	}

	// A AGL é a exceção que não se negocia: 280 é o máximo de art blocks que o
	// sistema de Arts comporta. Subir geraria mais blocks do que a barra de
	// comando mostra e quebraria a UI de batalha.
	if (Stat == TEXT("AGL") && NewCap != 280)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("CoreSubsystem: SetStatCap(AGL, %d) IGNORADO. O teto de AGL é o máximo de ")
			TEXT("art blocks do sistema de Arts — mexer nele quebra a barra de comando."),
			NewCap);
		return;
	}

	StatCaps.Add(Stat, NewCap);
}

void UCoreSubsystem::SetStatGrowthTable(UDataTable* NewTable)
{
	StatGrowthTable = NewTable;
}

void UCoreSubsystem::SetGrowthCurveTable(UDataTable* NewTable)
{
	GrowthCurveTable = NewTable;
}

void UCoreSubsystem::SetDifficultyTable(UDataTable* NewTable)
{
	DifficultyTable = NewTable;
}

// ============================================================
// GOLD
// ============================================================

void UCoreSubsystem::AddGold(int32 Amount)
{
	if (Amount <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("CoreSubsystem: AddGold ignorado — quantia inválida (%d)."), Amount);
		return;
	}

	Gold += Amount;
	OnGoldChanged.Broadcast(Gold);
}

bool UCoreSubsystem::RemoveGold(int32 Amount)
{
	if (Amount <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("CoreSubsystem: RemoveGold ignorado — quantia inválida (%d)."), Amount);
		return false;
	}

	if (Gold < Amount)
	{
		return false;
	}

	Gold -= Amount;
	OnGoldChanged.Broadcast(Gold);
	return true;
}

// ============================================================
// PLAYTIME
// ============================================================

void UCoreSubsystem::StartPlaytimeTracking()
{
	// Re-ancorar é seguro: o tempo decorrido até agora é consolidado no acumulado
	if (bPlaytimeTracking)
	{
		AccumulatedPlaytimeSeconds = GetPlaytimeSeconds();
	}

	bPlaytimeTracking = true;
	TrackingStartPlatformTime = FPlatformTime::Seconds();
}

void UCoreSubsystem::StopPlaytimeTracking()
{
	if (!bPlaytimeTracking)
	{
		return;
	}

	// Consolida ANTES de desligar a flag: GetPlaytimeSeconds() já retornaria
	// só o acumulado com bPlaytimeTracking = false, perdendo o trecho atual.
	AccumulatedPlaytimeSeconds = GetPlaytimeSeconds();
	bPlaytimeTracking = false;
}

double UCoreSubsystem::GetPlaytimeSeconds() const
{
	if (!bPlaytimeTracking)
	{
		return AccumulatedPlaytimeSeconds;
	}

	return AccumulatedPlaytimeSeconds + (FPlatformTime::Seconds() - TrackingStartPlatformTime);
}

FString UCoreSubsystem::GetPlaytimeFormatted() const
{
	return FormatPlaytimeSeconds(GetPlaytimeSeconds());
}

FString UCoreSubsystem::FormatPlaytimeSeconds(double Seconds)
{
	const int64 Total = FMath::Max<int64>(0, static_cast<int64>(Seconds));
	const int64 Hours = Total / 3600;
	const int64 Minutes = (Total % 3600) / 60;
	const int64 Secs = Total % 60;
	return FString::Printf(TEXT("%02lld:%02lld:%02lld"), Hours, Minutes, Secs);
}

// ============================================================
// MAPA ATUAL
// ============================================================

void UCoreSubsystem::SetCurrentMapDisplayName(FText DisplayName)
{
	CurrentMapDisplayName = DisplayName.ToString();
}

FString UCoreSubsystem::GetCurrentMapDisplayName() const
{
	return CurrentMapDisplayName.IsEmpty() ? GetCurrentMapName() : CurrentMapDisplayName;
}

FString UCoreSubsystem::GetCurrentMapName() const
{
	// bRemoveStreamingPrefix=true tira o "UEDPIE_0_" do PIE — o nome curto
	// resultante é o mesmo aceito pelo OpenLevel no build empacotado
	return UGameplayStatics::GetCurrentLevelName(GetGameInstance(), /*bRemoveStreamingPrefix=*/true);
}

// ============================================================
// POSIÇÃO DO PLAYER
// ============================================================

bool UCoreSubsystem::GetPlayerSaveTransform(FVector& OutLocation, FRotator& OutRotation) const
{
	UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}

	APlayerController* PC = GI->GetFirstLocalPlayerController(World);
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return false;
	}

	OutLocation = Pawn->GetActorLocation();
	// Rotação de controle, não do ator: preserva a direção da câmera no load
	OutRotation = PC->GetControlRotation();
	return true;
}

// ============================================================
// NEW GAME / LOAD
// ============================================================

void UCoreSubsystem::StartNewGame()
{
	UGameInstance* GI = GetGameInstance();
	if (UInventorySubsystem* Inventory = GI->GetSubsystem<UInventorySubsystem>())
	{
		Inventory->ResetInventory();
	}
	if (UWorldStateSubsystem* WorldState = GI->GetSubsystem<UWorldStateSubsystem>())
	{
		WorldState->ResetWorldState();
	}
	if (UPartySubsystem* Party = GI->GetSubsystem<UPartySubsystem>())
	{
		// O jogo comeca so com o Vahn. Os outros entram quando a historia
		// chamar RecruitCharacter.
		Party->ResetParty();
		Party->RecruitCharacter(TEXT("Vahn"));
	}

	Gold = 0;
	OnGoldChanged.Broadcast(Gold);

	AccumulatedPlaytimeSeconds = 0.0;
	bPlaytimeTracking = false;
	StartPlaytimeTracking();

	CurrentMapDisplayName.Empty();
	bIsNewGame = true;

	UE_LOG(LogTemp, Log, TEXT("CoreSubsystem: Novo jogo iniciado (inventário/mundo/party resetados, ")
		TEXT("Vahn recrutado, gold=0, playtime=0, bIsNewGame=true)."));
}

void UCoreSubsystem::RestoreFromSave(int32 InGold, double InPlaytimeSeconds, const FString& InMapDisplayName)
{
	Gold = FMath::Max(0, InGold);
	OnGoldChanged.Broadcast(Gold);

	AccumulatedPlaytimeSeconds = FMath::Max(0.0, InPlaytimeSeconds);
	bPlaytimeTracking = false;
	StartPlaytimeTracking();

	CurrentMapDisplayName = InMapDisplayName;

	// A partida atual passou a vir de um save — não é mais um jogo novo
	bIsNewGame = false;
}

// ============================================================
// PROGRESSÃO (curva de XP)
//
// Fórmula do jogo original (US), derivada de FUN_801E9504. Reproduzida aqui em
// vez de virar tabela: é fechada, exata e não precisa de asset. A DataTable
// DT_LevelCurve existe só para consulta/tuning no LegaiaStudio.
// Detalhes e validação em Docs/Referencia/progressao.md.
// ============================================================

namespace
{
	/** Soma dos deltas até `Level`: delta(n) = n*n/4 + 1 (tabela DAT_80076AF4). */
	int64 XPDeltaSum(int32 Level)
	{
		int64 Sum = 0;
		for (int64 n = 1; n <= Level; ++n)
		{
			Sum += (n * n) / 4 + 1;
		}
		return Sum;
	}
}

int32 UCoreSubsystem::GetXPForLevel(int32 Level) const
{
	if (Level <= 1)
	{
		return 0;
	}
	Level = FMath::Min(Level, GetMaxLevel());

	// O limiar do level L é calculado com a soma até L-1 (o índice do delta é
	// o level de ORIGEM, não o de destino).
	const int32 From = Level - 1;
	const int64 Sum = XPDeltaSum(From);

	// Abaixo de 17 o jogo usa a divisão exata; daí em diante, o fator inteiro.
	const int64 Threshold = (From < 0x11)
		? (Sum * 9999999) / 0x140FE
		: (Sum * 0x79);

	return static_cast<int32>(FMath::Min<int64>(Threshold, MAX_int32));
}

int32 UCoreSubsystem::GetXPForLevelForSlot(int32 Level, int32 PartySlot) const
{
	const int64 Base = GetXPForLevel(Level);
	if (Base <= 0 || PartySlot <= 0 || PartySlot > 2)
	{
		// Slot 0 (Vahn), slot 3 (Terra) e qualquer outro usam a curva base.
		return static_cast<int32>(Base);
	}

	// No original o divisor vem de uma tabela do disco (o head do sin LUT
	// amostrado a cada 0x28). `125 * level` é a aproximação que reproduz os
	// limiares de new game: Noa 102 e Gala 140 contra os 121 do Vahn.
	const int64 Divisor = FMath::Max<int64>(1, 125LL * FMath::Max(1, Level - 1));
	const int64 Correction = (Base * 0x14) / Divisor;

	const int64 Result = (PartySlot == 1) ? (Base - Correction) : (Base + Correction);
	return static_cast<int32>(FMath::Max<int64>(1, Result));
}

int32 UCoreSubsystem::GetLevelForXP(int32 CumulativeXP) const
{
	if (CumulativeXP <= 0)
	{
		return 1;
	}
	for (int32 Level = 2; Level <= GetMaxLevel(); ++Level)
	{
		if (CumulativeXP < GetXPForLevel(Level))
		{
			return Level - 1;
		}
	}
	return GetMaxLevel();
}

// ============================================================
// DIFICULDADE
//
// Só mexe nos inimigos: o personagem do jogador cresce sempre com a curva do
// original, então level, stats e XP continuam comparáveis entre dificuldades.
// ============================================================

void UCoreSubsystem::SetDifficulty(EJRPGDifficulty NewDifficulty)
{
	if (Difficulty == NewDifficulty)
	{
		return;
	}
	Difficulty = NewDifficulty;

	UE_LOG(LogTemp, Log, TEXT("CoreSubsystem: dificuldade -> %s."),
		*UEnum::GetValueAsString(Difficulty));

	OnDifficultyChanged.Broadcast(Difficulty);
}

FDifficultyScaling UCoreSubsystem::GetDifficultyScaling() const
{
	// A DataTable é opcional: sem ela o sistema funciona com os defaults abaixo,
	// para não depender de asset criado no editor.
	//
	// ATENÇÃO: estes defaults têm que bater com Docs/Data/DT_Difficulty.csv, que
	// sai de Extras/generate_progression_csv.py (build_difficulty). Mexeu num,
	// mexa no outro — senão um projeto sem a tabela importada joga com um
	// balanceamento diferente do que está documentado.
	if (DifficultyTable)
	{
		const FString RowName = UEnum::GetValueAsName(Difficulty).ToString();
		const FName Row(*RowName.RightChop(RowName.Find(TEXT("::")) + 2));
		if (const FDifficultyScaling* Found =
				DifficultyTable->FindRow<FDifficultyScaling>(Row, TEXT("GetDifficultyScaling"), false))
		{
			return *Found;
		}
	}

	FDifficultyScaling Out;
	switch (Difficulty)
	{
	case EJRPGDifficulty::Hard:
		Out.DisplayName = NSLOCTEXT("JRPG", "DiffHard", "Hard");
		Out.Description = NSLOCTEXT("JRPG", "DiffHardDesc", "Inimigos aguentam bem mais e batem mais forte.");
		Out.EnemyHP = 1.8f;
		Out.EnemyATK = 1.2f;
		Out.EnemyDEF = 1.25f;
		Out.XPReward = 1.25f;
		Out.GoldReward = 1.25f;
		break;

	case EJRPGDifficulty::Juggernaut:
		Out.DisplayName = NSLOCTEXT("JRPG", "DiffJuggernaut", "Juggernaut");
		Out.Description = NSLOCTEXT("JRPG", "DiffJuggernautDesc", "Cada encontro é uma ameaça real.");
		Out.EnemyHP = 3.0f;
		Out.EnemyATK = 1.45f;
		Out.EnemyDEF = 1.4f;
		Out.XPReward = 1.5f;
		Out.GoldReward = 1.5f;
		break;

	case EJRPGDifficulty::Normal:
	default:
		// Normal = jogo original: todos os multiplicadores em 1.0 (defaults).
		Out.DisplayName = NSLOCTEXT("JRPG", "DiffNormal", "Normal");
		Out.Description = NSLOCTEXT("JRPG", "DiffNormalDesc", "O balanceamento do jogo original.");
		break;
	}
	return Out;
}

namespace
{
	/** Aplica um multiplicador com piso 1 — inimigo nunca fica com 0 de stat. */
	int32 ApplyScale(int32 BaseValue, float Multiplier)
	{
		if (BaseValue <= 0)
		{
			return BaseValue;
		}
		return FMath::Max(1, FMath::RoundToInt(BaseValue * Multiplier));
	}
}

int32 UCoreSubsystem::ScaleEnemyHP(int32 BaseValue) const
{
	return ApplyScale(BaseValue, GetDifficultyScaling().EnemyHP);
}

int32 UCoreSubsystem::ScaleEnemyATK(int32 BaseValue) const
{
	return ApplyScale(BaseValue, GetDifficultyScaling().EnemyATK);
}

int32 UCoreSubsystem::ScaleEnemyDEF(int32 BaseValue) const
{
	return ApplyScale(BaseValue, GetDifficultyScaling().EnemyDEF);
}

int32 UCoreSubsystem::ScaleXPReward(int32 BaseValue) const
{
	return ApplyScale(BaseValue, GetDifficultyScaling().XPReward);
}

int32 UCoreSubsystem::ScaleGoldReward(int32 BaseValue) const
{
	return ApplyScale(BaseValue, GetDifficultyScaling().GoldReward);
}

// ============================================================
// CRESCIMENTO DE ATRIBUTOS
//
// Fórmula do original (FUN_801E9504), com os parâmetros extraídos do disco
// por Extras/extract_growth_from_disc.py. Ver Docs/Referencia/progressao.md.
// ============================================================

namespace
{
	/** Divisor da fórmula. Cada curva soma exatamente isto ao longo dos 98 níveis. */
	constexpr int32 GrowthCurveSum = 0x24C0;   // 9408

	/**
	 * Última linha de DT_GrowthCurve. Acima do level 99 o crescimento repete
	 * esta linha: as três curvas do original já são constantes em 64 desde o
	 * level 50, então extrapolar não cria degrau — é a mesma taxa que já vinha
	 * rodando na segunda metade do jogo.
	 */
	constexpr int32 GrowthCurveLastRow = 98;
}

int32 UCoreSubsystem::GetStatGainCore(FName Character, FName Stat, int32 FromLevel) const
{
	if (!StatGrowthTable || !GrowthCurveTable)
	{
		return 0;
	}
	// O ganho de L->L+1 lê a curva na linha L; não há ganho a partir do teto.
	if (FromLevel < 1 || FromLevel >= GetMaxLevel())
	{
		return 0;
	}

	const FName GrowthRowName(*FString::Printf(TEXT("%s_%s"), *Character.ToString(), *Stat.ToString()));
	const FStatGrowthRow* Growth =
		StatGrowthTable->FindRow<FStatGrowthRow>(GrowthRowName, TEXT("GetStatGainCore"), false);
	if (!Growth)
	{
		return 0;
	}

	// Acima do level 99 não há linha na tabela: repete a última (98). As três
	// curvas do original já são constantes em 64 desde o level 50, então a
	// extrapolação continua exatamente no mesmo ritmo — não há degrau.
	const int32 CurveLevel = FMath::Min(FromLevel, GrowthCurveLastRow);
	const FName CurveRowName(*FString::FromInt(CurveLevel));
	const FGrowthCurveRow* Curve =
		GrowthCurveTable->FindRow<FGrowthCurveRow>(CurveRowName, TEXT("GetStatGainCore"), false);
	if (!Curve)
	{
		return 0;
	}

	// Gate de dificuldade: abaixo do mínimo o atributo não cresce. É como a
	// Terra fica no Normal — parada no GrowthStart, igual ao original.
	if (static_cast<uint8>(Difficulty) < static_cast<uint8>(Growth->MinDifficulty))
	{
		return 0;
	}

	int32 CurveByte = Curve->Row0;
	if (Growth->CurveRow == 1)      { CurveByte = Curve->Row1; }
	else if (Growth->CurveRow == 2) { CurveByte = Curve->Row2; }

	const int64 Span = FMath::Max(0, Growth->MaxValue - Growth->GrowthStart);
	const int32 Gain = static_cast<int32>((Span * CurveByte) / GrowthCurveSum);

	// O original tem piso de 1: nenhum atributo fica parado num level-up.
	return FMath::Max(1, Gain);
}

int32 UCoreSubsystem::RollStatGain(FName Character, FName Stat, int32 FromLevel) const
{
	const int32 Core = GetStatGainCore(Character, Stat, FromLevel);
	if (Core <= 0)
	{
		return 0;
	}

	int32 Jitter = 0;
	if (StatGrowthTable)
	{
		const FName RowName(*FString::Printf(TEXT("%s_%s"), *Character.ToString(), *Stat.ToString()));
		if (const FStatGrowthRow* Growth =
				StatGrowthTable->FindRow<FStatGrowthRow>(RowName, TEXT("RollStatGain"), false))
		{
			Jitter = FMath::Max(0, Growth->Jitter);
		}
	}
	if (Jitter == 0)
	{
		return Core;
	}

	// rand() % (2*jitter + 1) - jitter: sorteio centrado em [-jitter, +jitter]
	const int32 Roll = FMath::RandRange(0, 2 * Jitter) - Jitter;
	return FMath::Max(1, Core + Roll);
}

bool UCoreSubsystem::DoesCharacterGrow(FName Character) const
{
	if (!StatGrowthTable)
	{
		return false;
	}

	// Basta um atributo liberado: quem cresce, cresce em tudo.
	static const TCHAR* Stats[] = { TEXT("HP"), TEXT("MP"), TEXT("AGL"), TEXT("ATK"),
	                                TEXT("UDF"), TEXT("LDF"), TEXT("SPD"), TEXT("INT") };
	for (const TCHAR* Stat : Stats)
	{
		const FName RowName(*FString::Printf(TEXT("%s_%s"), *Character.ToString(), Stat));
		if (const FStatGrowthRow* Growth =
				StatGrowthTable->FindRow<FStatGrowthRow>(RowName, TEXT("DoesCharacterGrow"), false))
		{
			if (static_cast<uint8>(Difficulty) >= static_cast<uint8>(Growth->MinDifficulty))
			{
				return true;
			}
		}
	}
	return false;
}

int32 UCoreSubsystem::GetStatAtLevel(FName Character, FName Stat, int32 Level) const
{
	if (!StatGrowthTable)
	{
		return 0;
	}

	const FName RowName(*FString::Printf(TEXT("%s_%s"), *Character.ToString(), *Stat.ToString()));
	const FStatGrowthRow* Growth =
		StatGrowthTable->FindRow<FStatGrowthRow>(RowName, TEXT("GetStatAtLevel"), false);
	if (!Growth)
	{
		return 0;
	}

	Level = FMath::Clamp(Level, 1, GetMaxLevel());

	// Acumula level a level, como o original (record[stat] += gain). A divisão
	// trunca a cada passo, então o total no L99 fica alguns pontos abaixo do
	// MaxValue — comportamento do jogo, não bug. Ver progressao.md.
	int32 Value = Growth->GrowthStart;
	for (int32 From = 1; From < Level; ++From)
	{
		Value += GetStatGainCore(Character, Stat, From);
	}

	return FMath::Min(Value, GetStatCap(Stat));
}
