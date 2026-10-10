#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "JRPGInteractionComponent.generated.h"

/**
 * UJRPGInteractionComponent
 * Escolhe com quem o personagem interage e chama o Interact dele.
 *
 * Candidatos: os atores sobrepostos ao dono (a Box de cada interagível) que implementam
 * IJRPGInteractable — ou uma das LegacyInterfaces, a ponte para interfaces de Blueprint
 * antigas. Entre eles vale o mais perto E na frente do personagem: com dois NPCs ou um baú ao
 * lado de uma porta, nunca um aleatório.
 *
 * O alvo é acompanhado enquanto o personagem anda (OnTargetChanged), para a UI mostrar um
 * prompt antes de apertar. TryInteract interage com ele — o controller chama no APERTO do
 * botão, nunca enquanto ele continua segurado.
 *
 * Vem no AJRPGFieldCharacter. Guia: Docs/Guides/PLAYER.md.
 */
UCLASS(ClassGroup = (JRPG), meta = (BlueprintSpawnableComponent))
class JRPGFRAMEWORK_API UJRPGInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UJRPGInteractionComponent();

	/**
	 * Interfaces de Blueprint antigas aceitas também, com uma função Interact SEM parâmetros.
	 * Ponte de migração: os atores que ainda usam a interface velha continuam funcionando
	 * enquanto passam para JRPG Interactable. Esvazie quando não sobrar nenhum.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Interaction")
	TArray<TSoftClassPtr<UInterface>> LegacyInterfaces;

	/**
	 * Quanto pesa estar atrás do personagem, em uu: um alvo atrás conta como se estivesse esta
	 * distância mais longe. 0 = só a distância decide.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Interaction", meta = (ClampMin = "0.0"))
	float FacingWeight = 200.0f;

	/** Interage com o alvo atual (recalcula na hora). Devolve com quem interagiu, ou nullptr. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Interaction")
	AActor* TryInteract();

	/** Com quem interagiria agora (nullptr = ninguém no alcance). */
	UFUNCTION(BlueprintPure, Category = "JRPG|Interaction")
	AActor* GetCurrentTarget() const { return CurrentTarget.Get(); }

	/** True se o ator é interagível (interface nova ou uma das LegacyInterfaces). */
	UFUNCTION(BlueprintPure, Category = "JRPG|Interaction")
	bool IsInteractable(const AActor* Actor) const;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInteractTargetChanged, AActor*, NewTarget);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInteracted, AActor*, Target);

	/** O alvo mudou (entrou/saiu do alcance, virou para outro): mostrar/esconder o prompt. */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|Interaction")
	FOnInteractTargetChanged OnTargetChanged;

	/** Interagiu com Target (depois de chamar o Interact dele). */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|Interaction")
	FOnInteracted OnInteracted;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** O melhor candidato agora (mais perto e na frente). */
	AActor* FindBestTarget() const;

	/** Recalcula o alvo e avisa se mudou. */
	void UpdateTarget();

	TWeakObjectPtr<AActor> CurrentTarget;
};
