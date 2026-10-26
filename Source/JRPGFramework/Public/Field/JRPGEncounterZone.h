#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Battle/JRPGEncounterTypes.h"
#include "Field/JRPGFieldEnemy.h"
#include "JRPGEncounterZone.generated.h"

class USphereComponent;
class UBoxComponent;
class UBillboardComponent;

/** Forma da área de uma zona de encontro. */
UENUM(BlueprintType)
enum class EJRPGEncounterZoneShape : uint8
{
	/** Um raio em volta da zona (círculo no chão). */
	Sphere,
	/** Uma caixa (pode girar com o ator). */
	Box
};

/**
 * AJRPGEncounterZone
 * Área do mapa onde inimigos do Field nascem, passeiam e reaparecem.
 *
 * - Mantém MaxAlive inimigos (EnemyClass, o BP_FieldEnemy do projeto) em pontos do navmesh
 *   DENTRO da forma; eles passeiam por ela, e ao desistir de uma perseguição voltam para ela.
 * - Encounter: quem luta quando qualquer um deles encosta no jogador. Visuals: troca o visual
 *   dos inimigos desta zona (vazio = o modelo do inimigo principal do encounter, no DT_Enemies).
 * - Inimigo que encostou some e volta depois de RespawnDelayMin..Max, num ponto novo — o
 *   jogador não limpa a área para sempre.
 * - RequiredFlagID: a zona só cria inimigos com esse event flag no valor pedido.
 *
 * Guia: Docs/Guides/FIELD_ENEMIES.md.
 */
UCLASS(Blueprintable)
class JRPGFRAMEWORK_API AJRPGEncounterZone : public AActor
{
	GENERATED_BODY()

public:
	AJRPGEncounterZone();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JRPG|Encounter Zone")
	TObjectPtr<USceneComponent> Root;

	/** Só desenho no editor (sem colisão). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JRPG|Encounter Zone")
	TObjectPtr<USphereComponent> AreaSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JRPG|Encounter Zone")
	TObjectPtr<UBoxComponent> AreaBox;

#if WITH_EDITORONLY_DATA
	/** Só no editor: o ícone vermelho no centro da zona. */
	UPROPERTY()
	TObjectPtr<UBillboardComponent> EditorIcon;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Encounter Zone")
	EJRPGEncounterZoneShape Shape = EJRPGEncounterZoneShape::Sphere;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Encounter Zone", meta = (ClampMin = "50.0", EditCondition = "Shape == EJRPGEncounterZoneShape::Sphere", EditConditionHides))
	float Radius = 1000.0f;

	/** Esfera: até onde procurar o chão acima e abaixo do centro (a zona pode estar no ar). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Encounter Zone", meta = (ClampMin = "50.0", EditCondition = "Shape == EJRPGEncounterZoneShape::Sphere", EditConditionHides))
	float FloorSearchDistance = 2000.0f;

	/** Metade do tamanho da caixa; o Z é até onde procurar o chão acima e abaixo. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Encounter Zone", meta = (EditCondition = "Shape == EJRPGEncounterZoneShape::Box", EditConditionHides))
	FVector BoxExtent = FVector(1000.0f, 1000.0f, 300.0f);

	/** A classe dos inimigos (o BP_FieldEnemy do projeto, com o FallbackVisual e os ajustes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Encounter Zone")
	TSubclassOf<AJRPGFieldEnemy> EnemyClass;

	/** Quem luta quando um inimigo desta zona encosta no jogador. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Encounter Zone")
	FJRPGEncounterSetup Encounter;

	/** Opcional: o visual dos inimigos desta zona. Vazio = o modelo do encounter (DT_Enemies). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Encounter Zone")
	TArray<FJRPGFieldEnemyVisual> Visuals;

	/** Quantos andam pela zona ao mesmo tempo. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Encounter Zone", meta = (ClampMin = "0", ClampMax = "32"))
	int32 MaxAlive = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Encounter Zone", meta = (ClampMin = "0.0"))
	float RespawnDelayMin = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Encounter Zone", meta = (ClampMin = "0.0"))
	float RespawnDelayMax = 60.0f;

	/** Event flag que libera a zona (WorldStateSubsystem). None = sempre ativa. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Encounter Zone")
	FName RequiredFlagID;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Encounter Zone")
	bool bRequiredFlagValue = true;

	/** Mensagens na tela e no log da zona E dos inimigos dela. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Encounter Zone|Debug")
	bool bPrintDebug = false;

	/** Um ponto do chão (navmesh, se houver) dentro da forma. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Encounter Zone")
	bool GetRandomFloorPoint(FVector& OutFloor) const;

	/** 0 dentro da forma; fora, a distância (no plano) até a borda. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Encounter Zone")
	float DistanceOutside(const FVector& Point) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Encounter Zone")
	TArray<AJRPGFieldEnemy*> GetEnemies() const;

	/** Chamado pelo inimigo que sumiu: agenda a volta dele num ponto novo. */
	void NotifyEnemyHidden(AJRPGFieldEnemy* Enemy);

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool IsUnlocked() const;
	void SpawnEnemy();
	void Respawn(TWeakObjectPtr<AJRPGFieldEnemy> Enemy);
	void Print(const FString& Message, const FColor& Color = FColor::Silver) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AJRPGFieldEnemy>> Enemies;
};
