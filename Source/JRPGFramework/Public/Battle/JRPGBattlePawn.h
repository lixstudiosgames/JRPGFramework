#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Battle/BattleTypes.h"
#include "JRPGBattlePawn.generated.h"

class UCapsuleComponent;
class USkeletalMeshComponent;
class UAnimSequence;

/**
 * AJRPGBattlePawn
 * Um combatente na cena da luta: o modelo de batalha (não o do Field) parado no seu lugar da
 * arena. Criado e destruído pela AJRPGBattleArena da luta, um por combatente. O modelo vem do
 * DT_Characters (BattleMesh/BattleAnims) para os heróis e do DT_Enemies (Mesh/Anims) para os
 * inimigos.
 *
 * O ator fica com os pés no lugar da formação; a cápsula se ajusta à malha (serve para a câmera
 * e, na Fase 5, para mirar). Sem Anim Blueprint, o clip Idle toca em loop e a troca de clip
 * (PlayClip) mistura com o anterior — a mesma animação nativa dos inimigos do Field, que serve
 * para qualquer esqueleto do PS1.
 */
UCLASS(Blueprintable)
class JRPGFRAMEWORK_API AJRPGBattlePawn : public APawn
{
	GENERATED_BODY()

public:
	AJRPGBattlePawn();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Battle Pawn")
	TObjectPtr<UCapsuleComponent> Capsule;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Battle Pawn")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	/**
	 * Giro da malha para ela olhar para a frente do ator (+X). Os modelos convertidos do PS1
	 * (Extras/ps1_glb_to_fbx.py) olham para -Y do Unreal: +90 vale para todos eles.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn")
	float MeshYawOffset = 90.0f;

	/** Segundos de mistura quando o clip troca (sem Anim Blueprint). 0 = corte seco. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Pawn", meta = (ClampMin = "0.0"))
	float AnimBlendTime = 0.2f;

	/** Quem ele é na luta. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle")
	const FJRPGCombatant& GetCombatant() const { return Combatant; }

	/**
	 * Põe o modelo e o combatente, com os pés em FeetTransform (o lugar da arena). Chamado pelo
	 * palco logo depois do spawn.
	 */
	void InitCombatant(const FJRPGCombatant& InCombatant, const FJRPGBattleVisual& Visual, const FTransform& FeetTransform);

	/** Toca um clip, misturando com o atual por AnimBlendTime (sem Anim Blueprint). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle")
	void PlayClip(UAnimSequence* Clip, bool bLoop = true, float PlayRate = 1.0f);

	/** Volta ao clip Idle do visual. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle")
	void PlayIdle();

	/** O modelo está posto (Blueprint: efeitos de entrada, nome em cima da cabeça...). */
	UFUNCTION(BlueprintImplementableEvent, Category = "JRPG|Battle")
	void OnCombatantInitialized();

private:
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> IdleClip;

	UPROPERTY(Transient)
	FJRPGCombatant Combatant;

	bool bUsesAnimClass = false;
};
