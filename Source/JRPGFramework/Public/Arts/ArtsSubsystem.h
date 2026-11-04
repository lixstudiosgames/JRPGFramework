#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Arts/JRPGArtsRules.h"
#include "ArtsSubsystem.generated.h"

class UPartySubsystem;
struct FJRPGPartyMember;

/** Uma Art entrou na lista de um personagem (no uso, por livro, NPC ou gatilho). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnArtLearnedSignature, FName, Character, FName, ArtKey);

/**
 * UArtsSubsystem
 * As Arts que cada personagem sabe e o caminho das Arts fora da luta.
 *
 *  - A LISTA é o FJRPGPartyMember::LearnedArts (vai no save) e só este subsistema escreve nela
 *    (LearnArt), como o Status com o Equipment.
 *  - As REGRAS (reconhecer, Super, AP) são do UJRPGArtsRules, funções puras sobre o DT_Arts e uma
 *    foto do herói. Aqui só se monta a foto a partir do roster (MakeContext) e se grava o que foi
 *    aprendido. A luta (Fase 5) tira a foto do combatente e chama as regras direto.
 *
 * Como cada tipo é aprendido: Regular fazendo (ou ensinada), Hyper só ensinada (os livros, um NPC),
 * Super quando a cadeia dela já é conhecida, Miracle pelo Ra-Seru — o Unlock da linha troca isso.
 * Guia: Docs/Guides/ARTS.md.
 */
UCLASS(BlueprintType, Blueprintable)
class JRPGFRAMEWORK_API UArtsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UArtsSubsystem();

	// ============================================================
	// BOTÕES
	// ============================================================

	/** O botão de Art de uma direção do controle para o herói (a Noa é canhota: Arms é a direita). */
	UFUNCTION(BlueprintPure, Category = "JRPG|Arts")
	static EJRPGArtButton ButtonForPad(FName Character, EJRPGPadButton Pad);

	/** O DT_Characters marca o herói como canhoto (bLeftHanded). */
	UFUNCTION(BlueprintPure, Category = "JRPG|Arts")
	static bool IsLeftHanded(FName Character);

	/** O nome de uma Art para mostrar (o DisplayName do DT_Arts; a key se a linha não existe; vazio em None). */
	UFUNCTION(BlueprintPure, Category = "JRPG|Arts")
	static FText GetArtDisplayName(FName ArtKey);

	// ============================================================
	// O QUE O PERSONAGEM SABE
	// ============================================================

	/** As Arts que ele sabe, na ordem em que aprendeu. */
	UFUNCTION(BlueprintPure, Category = "JRPG|Arts")
	TArray<FName> GetKnownArts(FName Character) const;

	/** Para diálogo e gatilhos: "ensinar o Hyper Elbow ao Vahn" só quando ele ainda não sabe. */
	UFUNCTION(BlueprintPure, Category = "JRPG|Arts")
	bool HasLearnedArt(FName Character, FName ArtKey) const;

	/**
	 * Põe a Art na lista do personagem — no uso, por livro, NPC, gatilho ou cena; funciona fora da
	 * luta. Devolve true só se ela é NOVA (o chamador mostra o aviso uma vez). Falso se a Art não é
	 * dele, é uma Part, não existe, ou ele não está no roster.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Arts")
	bool LearnArt(FName Character, FName ArtKey);

	/** Tira a Art da lista (teste / debug; o jogo não esquece Arts). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Arts")
	bool ForgetArt(FName Character, FName ArtKey);

	/** O caminho dos livros (UInventorySubsystem::UseItem): chama LearnArt. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Arts")
	void TeachArtToCharacter(const FString& CharacterName, FName ArtKey);

	// ============================================================
	// RECONHECER E FECHAR A CADEIA (fora da luta)
	// ============================================================

	/** A foto que as regras usam, tirada do roster agora: Arts sabidas, AP, Ra-Seru, chance de aprender. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Arts")
	FJRPGArtsContext MakeContext(FName Character) const;

	/**
	 * As Arts que os botões fazem para o herói, sobrepostas como no original; as que ele ainda não
	 * sabe voltam com bNewlyLearned (o LearnArt / CommitArtsChain as acrescenta). Não grava nada.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Arts")
	TArray<FJRPGRecognizedArt> RecognizeArtSequence(FName Character, const TArray<EJRPGArtButton>& Input) const;

	/** O turno inteiro que os botões dariam agora (Arts, Super, AP correndo, o que cai). Não grava nada. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Arts")
	FJRPGArtChain PreviewArtsChain(FName Character, const TArray<EJRPGArtButton>& Input) const;

	/** Para o menu: cada Hyper / Super / Miracle, se sairia digitada depois do que já foi (acinzentar). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Arts")
	TArray<FJRPGArtAvailability> GetSpecialsAvailability(FName Character, const TArray<EJRPGArtButton>& TypedSoFar) const;

	/**
	 * Fecha a janela: reconhece de novo a partir dos bytes (o payload não é confiável), aprende as
	 * Regular novas e devolve o turno. O AP NÃO anda aqui — ele se move quando a ação chega em cada
	 * Art (Fase 5). bValid falso (e a cadeia vazia) se os bytes não são botões.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Arts")
	FJRPGArtChain CommitArtsChain(FName Character, const TArray<uint8>& ArtBytes, bool& bValid);

	/** Chance, em %, de uma Regular desconhecida ser aprendida ao ser feita (100 = sempre, o original). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Arts")
	int32 LearnOnUseChance = 100;

	UPROPERTY(BlueprintAssignable, Category = "JRPG|Arts")
	FOnArtLearnedSignature OnArtLearned;

	/** A linha do DT_Arts de uma Art do personagem (nullptr se não existe ou é de outro). */
	static const FJRPGArtData* FindHeroArt(FName Character, FName ArtKey);

	/**
	 * A regra do LearnArt direto no registro (sem roster, sem aviso): a Art tem que ser do Member.Key
	 * e não uma Part. True se ela é nova e entrou na lista. O LearnArt chama isto.
	 */
	static bool AddLearnedArt(FJRPGPartyMember& Member, FName ArtKey);

private:
	UPartySubsystem* GetParty() const;

	/** Sorteio do aprender-no-uso fora da luta (a luta usa o Rng dela). */
	FRandomStream LearnRng;
};
