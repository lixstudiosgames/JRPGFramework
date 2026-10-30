#pragma once

#include "CoreMinimal.h"
#include "Battle/JRPGEncounterTypes.h"
#include "Engine/DataTable.h"
#include "Party/CharacterData.h"
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
 * As partes do modelo de batalha de um herói, uma por seção de equipamento do jogo original.
 * O jogo não tem malha de item: cada seção troca objetos inteiros do esqueleto (a arma troca a
 * mão por outra mão com lâmina), e o herói é a soma das cinco (Docs/Guides/PS1_ASSETS.md,
 * "Equipment pieces"). A Noa é canhota, mas a parte é pelo papel, não pelo lado: Weapon é o braço
 * da arma de quem for.
 */
UENUM(BlueprintType)
enum class EJRPGBattlePiece : uint8
{
	Body,    // EJRPGEquipSlot::Armor
	Head,    // EJRPGEquipSlot::Helmet
	Weapon,  // EJRPGEquipSlot::Weapon
	RaSeru,  // o SeruLevel (1..9) — não é slot de equipamento
	Feet,    // EJRPGEquipSlot::Shoes

	MAX      UMETA(Hidden)
};

/** Uma parte do herói na luta: a malha (no esqueleto do BattleMesh) que ocupa aquela seção. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGBattlePieceMesh
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	EJRPGBattlePiece Piece = EJRPGBattlePiece::Body;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TSoftObjectPtr<USkeletalMesh> Mesh;
};

/**
 * FJRPGEquipVisualRow
 * Uma linha da DT_EquipVisuals: a malha de uma parte do herói com um equipamento (ou a padrão).
 * O row name é a chave da busca: <Personagem>_<Parte>_<Item>, com o Item sendo a chave da
 * DT_Items, "Lv<N>" para o Ra-Seru no nível N, ou "Default" para a parte sem equipamento — ex.:
 * Vahn_Weapon_survival_knife, Vahn_RaSeru_Lv3, Vahn_Body_Default. Gerada pelo
 * Extras/generate_equip_visuals_csv.py a partir das peças do Extras/ps1_equip_pieces.py.
 *
 * Herói com alguma linha Default na tabela entra na luta montado por partes; sem nenhuma, entra
 * com o BattleMesh inteiro (como antes).
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGEquipVisualRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	FName Character;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	EJRPGBattlePiece Piece = EJRPGBattlePiece::Body;

	/** Chave da DT_Items, Lv<N> (Ra-Seru) ou Default. Informativo: a busca é pelo row name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	FName Item;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TSoftObjectPtr<USkeletalMesh> Mesh;
};

/**
 * O modelo de batalha de um combatente: malha e animação. Ou um Anim Blueprint, ou o clip Idle
 * tocado direto.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGBattleVisual
{
	GENERATED_BODY()

	/**
	 * A malha que anima. Com Pieces, fica invisível e só conduz a pose: as partes a seguem
	 * (Leader Pose) — por isso todas usam o esqueleto dela.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TSoftObjectPtr<USkeletalMesh> Mesh;

	/** Opcional. Com ele, o IdleAnim é ignorado. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TSubclassOf<UAnimInstance> AnimClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TSoftObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle", meta = (ClampMin = "0.01"))
	float Scale = 1.0f;

	/** O herói montado por partes (o que ele tem equipado). Vazio = a Mesh inteira aparece. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TArray<FJRPGBattlePieceMesh> Pieces;

	/** Malhas presas a bones da Mesh (os acessórios). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TArray<FJRPGBattleAttachment> Attachments;
};
