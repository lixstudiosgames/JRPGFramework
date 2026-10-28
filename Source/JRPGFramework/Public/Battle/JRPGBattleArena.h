#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/JRPGSplineArea.h"
#include "Battle/BattleTypes.h"
#include "Camera/CameraData.h"
#include "JRPGBattleArena.generated.h"

class USplineComponent;
class UBillboardComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class AJRPGBattlePawn;
class APlayerController;

/** De onde saem os lugares da formação automática. */
UENUM(BlueprintType)
enum class EJRPGArenaFormation : uint8
{
	/**
	 * Os lugares do jogo original (as tabelas de FUN_800513F0 no SCUS: até 3 heróis e 4
	 * inimigos), convertidos para cm com OriginalCmPerUnit. Acima dessas quantidades, o arco.
	 */
	Original,
	/** Um arco em volta do centro, para qualquer quantidade. */
	Arc
};

/** Se dá para lutar em cima de um lugar da formação. */
UENUM(BlueprintType)
enum class EJRPGArenaSeatStatus : uint8
{
	/** Chão achado, plano o bastante, nada em volta. Verde no editor. */
	Ok,
	/** Funciona, mas a altura foge muito da do centro da arena. Amarelo no editor. */
	Uneven,
	/** Sem chão, inclinado demais ou dentro de parede/objeto. Vermelho no editor. */
	Blocked
};

/** Um lugar da formação, já apoiado no chão. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGArenaSeat
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Battle Arena")
	EJRPGBattleSide Side = EJRPGBattleSide::Party;

	UPROPERTY(BlueprintReadOnly, Category = "Battle Arena")
	int32 Index = 0;

	/** Mundo: os pés no chão, virado para o lado inimigo. */
	UPROPERTY(BlueprintReadOnly, Category = "Battle Arena")
	FTransform Transform;

	UPROPERTY(BlueprintReadOnly, Category = "Battle Arena")
	EJRPGArenaSeatStatus Status = EJRPGArenaSeatStatus::Ok;

	/** Por que não está Ok (vazio quando está). */
	UPROPERTY(BlueprintReadOnly, Category = "Battle Arena")
	FString Problem;
};

/**
 * AJRPGBattleArena
 * O lugar do mapa onde uma luta é montada — e quem monta. Arraste para a cena: no editor ela já desenha onde
 * cada herói (seta azul) e cada inimigo (seta laranja) vai ficar, apoiados no chão, com a
 * cápsula na cor do chão embaixo — verde ok, amarelo desnivelado, vermelho sem chão /
 * inclinado / dentro de parede. O desenho acompanha o terreno ao vivo (esculpir o landscape
 * atualiza as cores).
 *
 * - Formação automática: heróis atrás do centro (-X), inimigos à frente (+X), cada lado virado
 *   para o outro. Original = os lugares do jogo de PS1 (1..3 heróis, 1..4 inimigos); acima
 *   disso, e com Formation = Arc, um arco que se ajusta à quantidade (5 heróis, 8 inimigos).
 * - Formação manual (bManualSeats): cada lugar vira uma alça no viewport; "Copy Formation To
 *   Manual Seats" começa a partir da automática. Índice sem lugar manual cai no automático.
 * - Área (AreaSpline): o pedaço do mapa que usa esta arena, desenhado à mão — qualquer
 *   formato, Alt+arrastar cria pontos. Várias arenas por mapa, cada uma com a sua área.
 *
 * Qual arena uma luta usa (FindArenaFor): a de maior Priority entre as que contêm o ponto do
 * toque (empate: a mais próxima); fora de todas, a mais próxima com bUseAsFallback; mapa sem
 * arena: nenhuma (a luta é montada onde foi o toque).
 *
 * Na luta (AssembleBattle, chamado pelo UJRPGBattleSceneSubsystem com a tela já preta): esconde e
 * congela a party do Field e o HideDuringBattle, desliga o controle do Field, cria um
 * AJRPGBattlePawn por combatente nos lugares (o modelo de batalha vem do DT_Characters /
 * DT_Enemies) e aponta a câmera — a BattleCamera, ou FrameGroup em todos. TeardownBattle devolve
 * tudo como estava. Mapa sem arena: o subsystem cria uma provisória no ponto do encontro.
 * Guia: Docs/Guides/BATTLE_STAGE.md.
 */
UCLASS(Blueprintable)
class JRPGFRAMEWORK_API AJRPGBattleArena : public AActor
{
	GENERATED_BODY()

public:
	AJRPGBattleArena();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Battle Arena")
	TObjectPtr<USceneComponent> Root;

	/** A área que usa esta arena (só a forma vista de cima conta). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Battle Arena")
	TObjectPtr<USplineComponent> AreaSpline;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> EditorIcon;
#endif

	/** Desligada, FindArenaFor ignora a arena. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena")
	bool bEnabled = true;

	/** Áreas sobrepostas: vale a de maior prioridade; empatando, a arena mais próxima. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena")
	int32 Priority = 0;

	/** Usar o AreaSpline. Desligado, a arena só é escolhida como a mais próxima. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Area")
	bool bUseArea = true;

	/** Altura máxima acima/abaixo do ator para contar como dentro da área (mapas com andares). 0 = sem limite. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Area", meta = (ClampMin = "0.0", EditCondition = "bUseArea"))
	float AreaHeightLimit = 0.0f;

	/** Pode ser escolhida como "a mais próxima" quando o toque foi fora de todas as áreas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena")
	bool bUseAsFallback = true;

	// --- Formação -----------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Formation")
	EJRPGArenaFormation Formation = EJRPGArenaFormation::Original;

	/**
	 * Original: a família alternada das linhas de inimigos (3 e 4 inimigos mudam; 1 e 2 são
	 * iguais). No jogo é a das lutas sem fuga / emboscadas (DAT_8007BD60 bit 7).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Formation", meta = (EditCondition = "Formation == EJRPGArenaFormation::Original", EditConditionHides))
	bool bAlternateEnemyRows = false;

	/**
	 * Original: cm por unidade do PS1. O mesmo fator do Extras/ps1_glb_to_fbx.py — os lugares e
	 * as malhas de batalha estão na mesma unidade no disco, então as distâncias ficam fiéis ao
	 * tamanho dos modelos (1 contra 1: 800 unidades para cada lado = 346 cm do centro).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Formation", meta = (ClampMin = "0.01", EditCondition = "Formation == EJRPGArenaFormation::Original", EditConditionHides))
	float OriginalCmPerUnit = 0.433005f;

	/** Arco: distância do centro até o arco dos heróis (atrás, -X). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Formation", meta = (ClampMin = "0.0"))
	float PartyRadius = 350.0f;

	/** Arco: distância do centro até o arco dos inimigos (à frente, +X). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Formation", meta = (ClampMin = "0.0"))
	float EnemyRadius = 475.0f;

	/** Arco: abertura do arco dos heróis, em graus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Formation", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float PartyArcDegrees = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Formation", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float EnemyArcDegrees = 120.0f;

	/** Usar PartySeats/EnemySeats (arrastados no viewport) em vez da formação automática. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Formation")
	bool bManualSeats = false;

	/** Lugares dos heróis, relativos à arena. Índice que falta cai na formação automática. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Formation", meta = (MakeEditWidget, EditCondition = "bManualSeats"))
	TArray<FTransform> PartySeats;

	/** Lugares dos inimigos, relativos à arena. Índice que falta cai na formação automática. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Formation", meta = (MakeEditWidget, EditCondition = "bManualSeats"))
	TArray<FTransform> EnemySeats;

	// --- Durante a luta -----------------------------------------------------------------------

	/** Atores escondidos enquanto a luta acontece aqui (a árvore na frente da câmera, um NPC). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Battle")
	TArray<TObjectPtr<AActor>> HideDuringBattle;

	/**
	 * Opcional: uma câmera posta à mão (CameraActor / CineCameraActor) para as lutas desta arena.
	 * Vazio, o palco enquadra todos os combatentes (UCameraSubsystem::FrameGroup).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Battle")
	TObjectPtr<AActor> BattleCamera;

	/** O enquadramento quando não há BattleCamera (gira em volta do primeiro do grupo). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Battle")
	ECameraFramingPreset CameraPreset = ECameraFramingPreset::BattleWide;

	/** Folga em volta dos combatentes (1 = encostando nas bordas da tela). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Battle", meta = (ClampMin = "1.0"))
	float CameraPadding = 1.15f;

	/** A câmera fica atrás da party, olhando para os inimigos. Desligado, o contrário. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Battle")
	bool bCameraBehindParty = true;

	/**
	 * Segundos de fade para o preto ao entrar e ao sair; a cena troca com a tela toda preta.
	 * 0 = corte seco. (A tela quebrando é a Fase 7.)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Battle", meta = (ClampMin = "0.0"))
	float TransitionTime = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Battle")
	FLinearColor TransitionColor = FLinearColor::Black;

	/** A classe de cada combatente na cena (um Blueprint filho dá efeitos, nome na cabeça...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Battle")
	TSubclassOf<AJRPGBattlePawn> PawnClass;

	// --- Chão ---------------------------------------------------------------------------------

	/** Até onde procurar o chão acima de cada lugar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Ground", meta = (ClampMin = "0.0"))
	float GroundSearchUp = 200.0f;

	/** Até onde procurar o chão abaixo de cada lugar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Ground", meta = (ClampMin = "0.0"))
	float GroundSearchDown = 1000.0f;

	/** Diferença de altura até o chão do centro a partir da qual o lugar fica amarelo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Ground", meta = (ClampMin = "0.0"))
	float MaxHeightDifference = 50.0f;

	/** Inclinação do chão a partir da qual o lugar fica vermelho. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Ground", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float MaxSlopeDegrees = 35.0f;

	/** O tamanho de um combatente: a cápsula do desenho e do teste de parede/objeto. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Ground", meta = (ClampMin = "10.0"))
	float SeatRadius = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Ground", meta = (ClampMin = "10.0"))
	float SeatHalfHeight = 90.0f;

	// --- Preview ------------------------------------------------------------------------------

	/** Quantos heróis desenhar no editor (a luta usa a quantidade real). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Preview", meta = (ClampMin = "0", ClampMax = "16"))
	int32 PreviewPartyCount = 3;

	/** Quantos inimigos desenhar no editor (o jogo original vai até 4). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Preview", meta = (ClampMin = "0", ClampMax = "16"))
	int32 PreviewEnemyCount = 5;

	/** Desenhar a formação só com a arena selecionada (muitas arenas no mesmo mapa). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Preview")
	bool bPreviewOnlyWhenSelected = false;

#if WITH_EDITORONLY_DATA
	/** Opcional: uma malha em cada lugar de herói (ex.: o Vahn de batalha). */
	UPROPERTY(EditAnywhere, Category = "Battle Arena|Preview")
	TObjectPtr<USkeletalMesh> PreviewPartyMesh;

	/** Opcional: uma malha em cada lugar de inimigo (ex.: o Gimard). */
	UPROPERTY(EditAnywhere, Category = "Battle Arena|Preview")
	TObjectPtr<USkeletalMesh> PreviewEnemyMesh;

	/** Giro das malhas de preview. As do PS1 (Extras/ps1_glb_to_fbx.py) olham para -Y: +90. */
	UPROPERTY(EditAnywhere, Category = "Battle Arena|Preview")
	float PreviewMeshYaw = 90.0f;
#endif

	/** Mensagens da escolha da arena e a formação desenhada no PIE quando um encontro a escolhe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Arena|Debug")
	bool bPrintDebug = false;

	// --- Consulta -----------------------------------------------------------------------------

	/**
	 * A arena de uma luta que começou em Point: a de maior Priority entre as que contêm o ponto
	 * (empate: a mais próxima); senão a mais próxima com bUseAsFallback; senão nula.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle Arena", meta = (WorldContext = "WorldContextObject"))
	static AJRPGBattleArena* FindArenaFor(const UObject* WorldContextObject, const FVector& Point);

	/** O ponto está dentro da área desta arena? */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle Arena")
	bool ContainsPoint(const FVector& Point) const;

	/** Um lugar, ainda sem apoiar no chão (mundo). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle Arena")
	FTransform GetSeatTransform(EJRPGBattleSide Side, int32 Index, int32 Count) const;

	/** Um lugar apoiado no chão, com o status dele. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle Arena")
	FJRPGArenaSeat ResolveSeat(EJRPGBattleSide Side, int32 Index, int32 Count) const;

	/** Todos os lugares de uma luta: primeiro os heróis, depois os inimigos. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle Arena")
	TArray<FJRPGArenaSeat> GetFormation(int32 PartyCount, int32 EnemyCount) const;

	/** Desenha a formação no mundo por Duration segundos (debug no PIE). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle Arena")
	void DrawDebugFormation(int32 PartyCount, int32 EnemyCount, float Duration = 5.0f) const;

	// --- A luta montada aqui ------------------------------------------------------------------

	/**
	 * No toque, antes do fade: tira o controle do jogador e para a party do Field na hora (o
	 * personagem e os seguidores), para ninguém continuar correndo enquanto a tela escurece.
	 * TeardownBattle devolve o controle.
	 */
	void StopFieldForBattle();

	/**
	 * Monta a cena: esconde o Field, desliga o controle, cria os combatentes nos lugares e aponta
	 * a câmera. Chamado pelo UJRPGBattleSceneSubsystem com a tela já preta.
	 */
	void AssembleBattle(const FJRPGBattleSetup& Setup, const TArray<FJRPGCombatant>& Combatants);

	/** Desmonta a cena e devolve o Field como estava (visibilidade, controle, câmera). */
	void TeardownBattle();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle Arena")
	bool IsBattleAssembled() const { return bBattleAssembled; }

	/** Os combatentes na cena, na ordem dos slots (heróis, depois inimigos). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle Arena")
	TArray<AJRPGBattlePawn*> GetBattlePawns() const;

	/** O pawn de um combatente (pelo SlotIndex do FJRPGCombatant). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle Arena")
	AJRPGBattlePawn* GetPawnForSlot(int32 SlotIndex) const;

	// --- Botões no editor ---------------------------------------------------------------------

	/** Lista no Message Log (Map Check) os lugares amarelos/vermelhos da formação de preview. */
	UFUNCTION(CallInEditor, Category = "Battle Arena")
	void ValidateGround();

	/** Copia a formação automática (apoiada no chão) para PartySeats/EnemySeats e liga bManualSeats. */
	UFUNCTION(CallInEditor, Category = "Battle Arena|Formation")
	void CopyFormationToManualSeats();

	/** Desce os pontos do contorno da área até o chão (só o desenho; a área é a mesma). */
	UFUNCTION(CallInEditor, Category = "Battle Arena|Area")
	void SnapAreaToGround();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override;

#if WITH_EDITOR
	virtual void CheckForErrors() override;
#endif

protected:
	virtual void BeginPlay() override;

private:
	FTransform GetLocalSeat(EJRPGBattleSide Side, int32 Index, int32 Count) const;
	/** O lugar do jogo original, relativo à arena; false se a quantidade passa da tabela. */
	bool GetOriginalSeat(EJRPGBattleSide Side, int32 Index, int32 Count, FTransform& OutLocal) const;
	FJRPGArenaSeat ResolveSeatAgainst(EJRPGBattleSide Side, int32 Index, int32 Count, const TOptional<double>& ReferenceZ) const;
	bool TraceGround(const FVector& At, FHitResult& OutHit) const;
	/** A altura do chão no centro da arena (vazio se não há chão embaixo dela). */
	TOptional<double> GetReferenceFloorZ() const;
	/** Os lugares não-Ok da formação de preview, uma linha cada. */
	TArray<FString> CollectProblems() const;
	void DrawFormation(const TArray<FJRPGArenaSeat>& Seats, bool bPersistent, float Duration) const;
	void Print(const FString& Message, const FColor& Color = FColor::Silver) const;
	/** Problema na montagem: sempre no log e na tela, com ou sem bPrintDebug. */
	void Warn(const FString& Message) const;

	// Montagem da luta
	void HideFieldActors();
	void HideActor(AActor* Actor, bool bFreeze);
	void RestoreHiddenActors();
	void DisableFieldInput();
	void RestoreFieldInput();
	void FrameCamera();
	bool ResolveVisual(const FJRPGCombatant& Combatant, FJRPGBattleVisual& OutVisual, FString& OutWhy) const;
	APlayerController* GetPlayerController() const;

	/** Um ator que a arena escondeu na luta, e como ele estava. */
	struct FHiddenActor
	{
		TWeakObjectPtr<AActor> Actor;
		bool bWasHidden = false;
		bool bHadCollision = true;
		float TimeDilation = 1.0f;
	};

#if WITH_EDITOR
	void RebuildPreviewMeshes();
#endif

	FJRPGSplineArea SplineArea;

	UPROPERTY(Transient, DuplicateTransient)
	TArray<TObjectPtr<USkeletalMeshComponent>> PreviewMeshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AJRPGBattlePawn>> BattlePawns;

	TArray<FHiddenActor> HiddenActors;
	TWeakObjectPtr<APlayerController> InputController;
	/** O controle do Field já foi tirado (StopFieldForBattle ou AssembleBattle). */
	bool bFieldInputOff = false;
	bool bRestoreFieldInput = false;
	bool bRestoreMoveInput = false;
	bool bBattleAssembled = false;
};
