#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Battle/JRPGEncounterTypes.h"
#include "Field/JRPGFieldEnemy.h"
#include "Core/JRPGSplineArea.h"
#include "JRPGEncounterZone.generated.h"

class USphereComponent;
class UBoxComponent;
class USplineComponent;
class UBillboardComponent;
class AJRPGBattleArena;
class USkeletalMeshComponent;

/** Forma da área de uma zona de encontro. */
UENUM(BlueprintType)
enum class EJRPGEncounterZoneShape : uint8
{
	/** Um raio em volta da zona (círculo no chão). */
	Sphere,
	/** Uma caixa (pode girar com o ator). */
	Box,
	/**
	 * Um contorno desenhado à mão (spline fechado, visto de cima): qualquer formato, inclusive
	 * côncavo. Alt+arrastar um ponto cria outro.
	 */
	Spline
};

/**
 * AJRPGEncounterZone
 * Área do mapa onde inimigos do Field nascem, passeiam e reaparecem.
 *
 * - Mantém MaxAlive inimigos (EnemyClass, o BP_FieldEnemy do projeto) em pontos do navmesh
 *   DENTRO da forma; eles passeiam por ela, e ao desistir de uma perseguição voltam para ela.
 * - Enemy: qual inimigo do DT_Enemies anda por ela — o modelo e as animações vêm de lá, e cada
 *   toque é uma luta contra um dele. Grupo misto: Custom Encounter (avançado). Visuals (avançado):
 *   troca o visual dos inimigos desta zona.
 * - Spawn: MinAlive..MaxAlive inimigos (sorteado ao começar), a pelo menos MinSpacing um do outro e, com
 *   MaxSpacing, a no máximo isso do mais perto; nunca dentro de objetos (bAvoidObstacles).
 * - Inimigo que encostou some e volta depois de RespawnDelayMin..Max, num ponto novo — o
 *   jogador não limpa a área para sempre.
 * - Depois de uma luta, os inimigos da zona passam PostBattleNoChaseMin..Max sem perseguir.
 * - BattleArena: a arena das lutas desta zona (escolhida na cena); vazia, a do ponto do toque.
 * - RequiredFlagID libera a zona; DisableFlagID a desliga. A zona confere as flags a cada segundo:
 *   desligando, os inimigos somem; religando, voltam.
 *
 * Guia: Docs/Guides/FIELD_ENEMIES.md.
 */
UCLASS(Blueprintable)
class JRPGFRAMEWORK_API AJRPGEncounterZone : public AActor
{
	GENERATED_BODY()

public:
	AJRPGEncounterZone();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Encounter Zone")
	TObjectPtr<USceneComponent> Root;

	/** Só desenho no editor (sem colisão). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Encounter Zone")
	TObjectPtr<USphereComponent> AreaSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Encounter Zone")
	TObjectPtr<UBoxComponent> AreaBox;

	/** Shape = Spline: o contorno da zona. Só a forma vista de cima conta. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Encounter Zone")
	TObjectPtr<USplineComponent> AreaSpline;

#if WITH_EDITORONLY_DATA
	/** Só no editor: o ícone vermelho no centro da zona. */
	UPROPERTY()
	TObjectPtr<UBillboardComponent> EditorIcon;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone|Area")
	EJRPGEncounterZoneShape Shape = EJRPGEncounterZoneShape::Sphere;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone|Area", meta = (ClampMin = "50.0", EditCondition = "Shape == EJRPGEncounterZoneShape::Sphere", EditConditionHides))
	float Radius = 1000.0f;

	/** Esfera/Spline: até onde procurar o chão acima e abaixo do ator (a zona pode estar no ar). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone|Area", meta = (ClampMin = "50.0", EditCondition = "Shape != EJRPGEncounterZoneShape::Box", EditConditionHides))
	float FloorSearchDistance = 2000.0f;

	/** Metade do tamanho da caixa; o Z é até onde procurar o chão acima e abaixo. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone|Area", meta = (EditCondition = "Shape == EJRPGEncounterZoneShape::Box", EditConditionHides))
	FVector BoxExtent = FVector(1000.0f, 1000.0f, 300.0f);

	/**
	 * Qual inimigo anda pela zona (row do DT_Enemies). O modelo e as animações vêm de lá, e cada
	 * toque é uma luta contra um dele.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone", meta = (DisplayName = "Enemy", GetOptions = "JRPGFramework.JRPGEncounterLibrary.GetEnemyKeyOptions"))
	FName EnemyKey;

	/** Faixa do level dele na luta (sorteado a cada encontro). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone", meta = (ClampMin = "1"))
	int32 LevelMin = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone", meta = (ClampMin = "1"))
	int32 LevelMax = 1;

	/** Grupo misto: vários inimigos, pesos, quantidades. Preenchido, substitui o Enemy acima. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone", AdvancedDisplay, meta = (DisplayName = "Custom Encounter"))
	FJRPGEncounterSetup Encounter;

	/** A classe dos inimigos (o BP_FieldEnemy do projeto, com o FallbackVisual e os ajustes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone", AdvancedDisplay)
	TSubclassOf<AJRPGFieldEnemy> EnemyClass;

	/** Opcional: o visual dos inimigos desta zona. Vazio = o modelo do Enemy (DT_Enemies). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone", AdvancedDisplay)
	TArray<FJRPGFieldEnemyVisual> Visuals;

	/**
	 * A arena das lutas desta zona (o toque dos inimigos dela e, com os encontros aleatórios, as lutas deles): escolha uma
	 * AJRPGBattleArena da cena. Vazia ou desligada (bEnabled), vale a do ponto do toque (AJRPGBattleArena::FindArenaFor).
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Encounter Zone|Battle")
	TSoftObjectPtr<AJRPGBattleArena> BattleArena;

	// --- Spawn ---

	/** Quantos andam pela zona ao mesmo tempo: sorteado entre MinAlive e MaxAlive quando a zona começa. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone|Spawn", meta = (ClampMin = "0", ClampMax = "32"))
	int32 MinAlive = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone|Spawn", meta = (ClampMin = "0", ClampMax = "32"))
	int32 MaxAlive = 3;

	/** Distância mínima (cm, no plano) de um inimigo novo até os outros da zona. 0 = sem mínimo. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone|Spawn", meta = (ClampMin = "0.0"))
	float MinSpacing = 400.0f;

	/**
	 * Distância máxima (cm, no plano) de um inimigo novo até o mais perto dos outros: o grupo não se espalha pela zona
	 * inteira. 0 = sem máximo. Sem outro de pé, não conta.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone|Spawn", meta = (ClampMin = "0.0"))
	float MaxSpacing = 0.0f;

	/**
	 * Não nascer dentro de objetos: a cápsula do inimigo no ponto sorteado não pode encostar em nada estático ou dinâmico
	 * (rochas, paredes, caixas). Os pontos com problema são sorteados de novo.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone|Spawn")
	bool bAvoidObstacles = true;

	/** Folga a mais em volta da cápsula no teste de objetos (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone|Spawn", meta = (ClampMin = "0.0", EditCondition = "bAvoidObstacles"))
	float ObstacleClearance = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone|Respawn", meta = (ClampMin = "0.0"))
	float RespawnDelayMin = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone|Respawn", meta = (ClampMin = "0.0"))
	float RespawnDelayMax = 70.0f;

	/**
	 * Depois de uma luta (a tela abrindo de novo), cada inimigo da zona passa um tempo sorteado nesta faixa sem perseguir o
	 * jogador. Vale por cima do PostBattleNoChaseMin / Max da classe do inimigo.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone|Respawn", meta = (ClampMin = "0.0"))
	float PostBattleNoChaseMin = 7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone|Respawn", meta = (ClampMin = "0.0"))
	float PostBattleNoChaseMax = 15.0f;

	/** Event flag que libera a zona (WorldStateSubsystem). None = sempre ativa. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone|Unlock")
	FName RequiredFlagID;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone|Unlock")
	bool bRequiredFlagValue = true;

	/**
	 * Event flag que DESLIGA a zona quando está ligada (a área limpa na história). Vale junto com o RequiredFlagID: a zona
	 * abre num ponto da história e fecha em outro. Desligada, os inimigos dela somem (e os encontros aleatórios param).
	 * None = nunca desliga por flag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone|Unlock")
	FName DisableFlagID;

	// --- Preview (só no editor) ---

	/**
	 * Mostra no editor onde os inimigos nasceriam: MaxAlive pontos sorteados com as regras do jogo (dentro da forma, no chão,
	 * MinSpacing / MaxSpacing entre eles, fora de objetos). Verde: os MinAlive que sempre nascem; azul: os que podem nascer
	 * (até o MaxAlive); amarelo: sem lugar perfeito (zona pequena para o espaçamento). O círculo em volta de cada um tem
	 * MinSpacing / 2 de raio: dois círculos que não se encostam estão a MinSpacing um do outro. O sorteio muda a cada
	 * edição da zona (e no Reroll Preview); no jogo cada luta sorteia de novo.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone|Preview")
	bool bPreviewSpawns = true;

	/** O modelo do inimigo em cada ponto (Visuals, senão o do DT_Enemies de cada inimigo do encontro, um por vez). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone|Preview", meta = (EditCondition = "bPreviewSpawns"))
	bool bPreviewMeshes = true;

	/** Desenhar o preview só com a zona selecionada (muitas zonas no mesmo mapa). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Encounter Zone|Preview", meta = (EditCondition = "bPreviewSpawns"))
	bool bPreviewOnlyWhenSelected = false;

	/** Sorteia o preview de novo. */
	UFUNCTION(CallInEditor, Category = "Encounter Zone|Preview")
	void RerollPreview();

	/** Mensagens na tela e no log da zona E dos inimigos dela. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Encounter Zone|Debug")
	bool bPrintDebug = false;

	/** O encontro dos inimigos desta zona: o Custom Encounter, ou o Enemy sozinho. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Encounter Zone")
	FJRPGEncounterSetup GetEncounterSetup() const;

	/** Um ponto do chão (navmesh, se houver) dentro da forma. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Encounter Zone")
	bool GetRandomFloorPoint(FVector& OutFloor) const;

	/**
	 * Um ponto do chão a pelo menos MinDistance (no plano) de Avoid: o reaparecer não cai em cima do jogador. Numa zona
	 * pequena demais devolve o mais longe de alguns sorteios. False se a zona não achou chão.
	 */
	bool GetRandomFloorPointAway(const FVector& Avoid, float MinDistance, FVector& OutFloor) const;

	/** 0 dentro da forma; fora, a distância (no plano) até a borda. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Encounter Zone")
	float DistanceOutside(const FVector& Point) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Encounter Zone")
	TArray<AJRPGFieldEnemy*> GetEnemies() const;

	/** Chamado pelo inimigo que sumiu: agenda a volta dele num ponto novo. */
	void NotifyEnemyHidden(AJRPGFieldEnemy* Enemy);

	/**
	 * Um ponto do chão para um inimigo da zona nascer ou voltar: dentro da forma, longe de AvoidActor (MinDistance, no plano),
	 * a MinSpacing..MaxSpacing dos outros inimigos da zona à vista e, com bAvoidObstacles, sem a cápsula dentro de nada.
	 * Self: quem vai nascer (a cápsula dele; nulo = a do EnemyClass) e quem não conta como "outro". Sem ponto perfeito, o que
	 * menos passa das distâncias; nunca um dentro de objetos. False se nenhum serve.
	 */
	bool PickSpawnPoint(const AJRPGFieldEnemy* Self, const AActor* AvoidActor, float MinDistance, FVector& OutFloor) const;

	/** A BattleArena escolhida, se está no mundo e ligada; nulo = escolher pelo ponto. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Encounter Zone")
	AJRPGBattleArena* GetBattleArena() const;

	/** As flags deixam a zona funcionar (RequiredFlagID no valor pedido e DisableFlagID desligada). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Encounter Zone")
	bool IsZoneActive() const;

	/** Spline: desce os pontos do contorno até o chão (só o desenho; a área é a mesma). */
	UFUNCTION(CallInEditor, Category = "Encounter Zone|Area")
	void SnapAreaToGround();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** O Shape de verdade: Spline com menos de 3 pontos vira Sphere (com aviso). */
	EJRPGEncounterZoneShape GetEffectiveShape() const;
	/** A cada segundo: a zona ligou ou desligou pelas flags. */
	void PollFlags();
	/** Cria os que faltam até DesiredAlive (os sumidos esperando o respawn contam). */
	void FillToDesired();
	void SpawnEnemy();
	void Respawn(TWeakObjectPtr<AJRPGFieldEnemy> Enemy);
	/** A cápsula do inimigo (a dele, ou a do CDO do EnemyClass). */
	void GetSpawnCapsule(const AJRPGFieldEnemy* Self, float& OutRadius, float& OutHalfHeight) const;
	/**
	 * O miolo do PickSpawnPoint: o espaçamento conta de Others. OutPenalty = o quanto o ponto escolhido passa das distâncias
	 * (0 = perfeito).
	 */
	bool PickPointAmong(const TArray<FVector>& Others, const AJRPGFieldEnemy* Self, const AActor* AvoidActor, float MinDistance,
		FVector& OutFloor, float& OutPenalty) const;
#if WITH_EDITOR
	/** Sorteia os pontos do preview e refaz as malhas. */
	void RebuildPreview();
	void ClearPreviewMeshes();
#endif
	void Print(const FString& Message, const FColor& Color = FColor::Silver) const;

	/** Já avisou que o navmesh não cobre a zona (avisa uma vez por play). */
	mutable bool bWarnedNoNavmesh = false;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AJRPGFieldEnemy>> Enemies;

	/** Quantos esta zona mantém (MinAlive..MaxAlive, sorteado no BeginPlay). */
	int32 DesiredAlive = 0;

	/** O estado das flags na última conferida (PollFlags). */
	bool bZoneActive = false;

	FTimerHandle FlagPollTimer;
	/** O respawn agendado de cada inimigo sumido (a zona desligando cancela; religando, volta na hora). */
	TMap<TWeakObjectPtr<AJRPGFieldEnemy>, FTimerHandle> RespawnTimers;

#if WITH_EDITORONLY_DATA
	/** O preview: os pontos sorteados, os sem lugar perfeito, quantos não acharam lugar, e as malhas (transitórias). */
	TArray<FVector> PreviewPoints;
	TArray<bool> PreviewImperfect;
	int32 PreviewMissing = 0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USkeletalMeshComponent>> PreviewMeshes;
#endif

	/** O AreaSpline amostrado (OnConstruction no editor, BeginPlay no jogo). */
	FJRPGSplineArea SplineArea;
};
