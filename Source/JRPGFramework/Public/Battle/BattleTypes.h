#pragma once

#include "CoreMinimal.h"
#include "Battle/JRPGEncounterTypes.h"
#include "BattleTypes.generated.h"

class USkeletalMesh;
class UAnimSequence;
class UAnimInstance;

/** De que lado da luta. */
UENUM(BlueprintType)
enum class EJRPGBattleSide : uint8
{
	Party,
	Enemy
};

/** O que é um combatente. Os Seru entram como membros de apoio (Fase 8). */
UENUM(BlueprintType)
enum class EJRPGCombatantType : uint8
{
	Character,
	Seru,
	Enemy
};

/** Como a luta acabou. */
UENUM(BlueprintType)
enum class EJRPGBattleResult : uint8
{
	Victory,
	Defeat,
	Escaped,
	/** Encerrada de fora (comando de debug, troca de mapa): sem recompensa nem game over. */
	Aborted
};

/**
 * Um combatente da luta. Na Fase 4 é só quem é e onde fica; a Fase 5 põe os stats, o gauge e
 * o estado da ação aqui (Docs/BATTLE_ROADMAP.md, "BattleTypes.h").
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGCombatant
{
	GENERATED_BODY()

	/** Índice na lista da luta (heróis primeiro, depois inimigos). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 SlotIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	EJRPGBattleSide Side = EJRPGBattleSide::Party;

	/** Índice dentro do lado: o lugar da formação (0 = o líder / o primeiro inimigo). */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 SideIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	EJRPGCombatantType Type = EJRPGCombatantType::Character;

	/** O membro da party (DT_Characters) ou a row do DT_Enemies. */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	FName Key;

	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Battle")
	int32 Level = 1;
};

/** O pedido de uma luta: quem entra e onde ela acontece. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGBattleSetup
{
	GENERATED_BODY()

	/** Os heróis, na ordem da formação. Vazio = a party ativa (UPartySubsystem). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TArray<FName> Party;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TArray<FJRPGEncounterCombatant> Enemies;

	/**
	 * Onde foi o encontro. O palco monta a luta na arena dele (AJRPGBattleArena::FindArenaFor);
	 * sem arena no mapa, monta aqui mesmo.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	FVector Location = FVector::ZeroVector;

	/** Sem arena: para onde a party olha (o yaw de quem encostou até o inimigo). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	float FacingYaw = 0.0f;
};

/**
 * O modelo de batalha de um combatente: malha e animação. Ou um Anim Blueprint, ou o clip Idle
 * tocado direto.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGBattleVisual
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TSoftObjectPtr<USkeletalMesh> Mesh;

	/** Opcional. Com ele, o IdleAnim é ignorado. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TSubclassOf<UAnimInstance> AnimClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TSoftObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle", meta = (ClampMin = "0.01"))
	float Scale = 1.0f;
};
