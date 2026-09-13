#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Inventory/ItemData.h"
#include "Party/JRPGPartyTypes.h"
#include "PartySubsystem.generated.h"

class UDataTable;

/** Disparado quando o roster ou a formação muda (recrutou, ativou, desativou). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPartyChangedSignature);

/** Disparado quando um personagem sobe de level. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMemberLevelUpSignature, FName, Character, int32, NewLevel);

/**
 * UPartySubsystem
 * Dono do estado dos personagens do jogador.
 *
 * DUAS COISAS DIFERENTES, e a distinção é o desenho todo:
 *
 *   ROSTER    todo mundo que já foi recrutado. Cresce e nunca encolhe. É o que
 *             o save guarda e o que o New Game+ vai carregar.
 *   FORMAÇÃO  o subconjunto que luta (ActiveOrder). O jogador mexe à vontade.
 *
 * Tirar alguém da formação NÃO mexe em nada do registro dele. É o que permite o
 * Vahn sumir por horas e voltar exatamente como estava.
 *
 * A DataTable de personagens é resolvida sozinha no Initialize
 * (/Game/Data/DT_Characters, com fallback para a pasta do plugin).
 */
UCLASS(BlueprintType, Blueprintable)
class JRPGFRAMEWORK_API UPartySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UPartySubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// ============================================================
	// RECRUTAMENTO — "a partir daqui o personagem é do jogador"
	// ============================================================

	/**
	 * Traz o personagem para o grupo, criando o registro dele a partir dos
	 * atributos iniciais de DT_Characters.
	 *
	 * IDEMPOTENTE: se ele já foi recrutado, NÃO reseta nada — só marca como
	 * disponível de novo. É o que protege o progresso de quem sai e volta.
	 *
	 * Devolve false se a key não existe na DataTable.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Party")
	bool RecruitCharacter(FName Character);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Party")
	bool IsRecruited(FName Character) const;

	/**
	 * Liga/desliga a presença do personagem na história. Deixar indisponível
	 * tira ele da formação, mas preserva o registro inteiro (caso da Terra
	 * depois do Mount Rikuroa).
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Party")
	void SetAvailable(FName Character, bool bAvailable);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Party")
	bool IsAvailable(FName Character) const;

	// ============================================================
	// FORMAÇÃO — quem luta
	// ============================================================

	/**
	 * Põe/tira o personagem da formação.
	 *
	 * Recusa se: não está recrutado, não está disponível, ou ativaria mais que
	 * MaxActiveMembers. Desativar o último ativo também é recusado — a
	 * formação nunca fica vazia.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Party")
	bool SetActive(FName Character, bool bActive);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Party")
	bool IsActive(FName Character) const;

	/** Troca a formação inteira de uma vez, na ordem dada. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Party")
	bool SetActiveParty(const TArray<FName>& Characters);

	/** Keys da formação, na ordem. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Party")
	TArray<FName> GetActiveParty() const { return ActiveOrder; }

	/** Registros da formação, na ordem — o que a UI desenha. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Party")
	TArray<FJRPGPartyMember> GetActiveMembers() const;

	/** Todo mundo já recrutado, inclusive quem está fora da formação. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Party")
	TArray<FJRPGPartyMember> GetRoster() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Party")
	bool GetMember(FName Character, FJRPGPartyMember& OutMember) const;

	/**
	 * Acesso direto ao registro, para o UStatusSubsystem escrever equipamento e
	 * condicoes. nullptr se o personagem nao esta no roster.
	 *
	 * NAO e UFUNCTION de proposito: ponteiro cru nao atravessa Blueprint, e a
	 * escrita direta e privilegio do Status. De Blueprint, use os metodos dele.
	 */
	FJRPGPartyMember* FindMemberForWrite(FName Character);

	/** Avisa que alguem mexeu no registro por FindMemberForWrite. */
	void NotifyMemberChanged();

	/**
	 * Quantos podem lutar ao mesmo tempo. 3 é o do original; o setter existe
	 * porque a party não é fixa — dá para abrir mais vagas depois.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Party")
	int32 MaxActiveMembers = 3;

	UFUNCTION(BlueprintCallable, Category = "JRPG|Party")
	void SetMaxActiveMembers(int32 NewMax);

	// ============================================================
	// EXPERIÊNCIA E LEVEL
	// ============================================================

	/**
	 * Distribui o XP de uma batalha, do jeito do original (FUN_8004E568):
	 *
	 *     total   = soma do XP dos inimigos × 3/4
	 *     cada um = total / (quantos estão VIVOS na formação)
	 *
	 * Ou seja: quem está fora da formação não ganha nada, e quem morreu não
	 * entra no divisor nem recebe. Sobrando um vivo, ele leva o total inteiro —
	 * é por isso que jogar sozinho sobe de level bem mais rápido.
	 *
	 * O multiplicador de XP da dificuldade é aplicado ao total.
	 * Devolve o XP que cada um recebeu.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Party|XP")
	int32 AwardBattleXP(int32 EnemyXPSum);

	/** Dá XP a um personagem específico, subindo os levels que couberem. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Party|XP")
	void GrantXP(FName Character, int32 Amount);

	/** XP que falta para o próximo level (0 se já está no teto). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Party|XP")
	int32 GetXPToNextLevel(FName Character) const;

	/**
	 * Sobe N levels, aplicando o crescimento de atributos a cada um.
	 *
	 * Não é "setar o número": cada level passa por RollStatGain, com jitter,
	 * exatamente como se o personagem tivesse subido lutando. O XP acumulado
	 * é sincronizado com o limiar do level alcançado.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Party|XP")
	int32 LevelUpMember(FName Character, int32 Levels = 1);

	/**
	 * Leva o personagem até um level específico.
	 *
	 * Para BAIXAR, refaz do zero: volta aos atributos iniciais de
	 * DT_Characters e simula a subida de novo. Não dá para "desrolar" o jitter
	 * dos levels já ganhos, então refazer é a única forma honesta.
	 *
	 * Serve para o jogo também: um personagem que entra no meio da história
	 * não precisa entrar no level 1.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Party|XP")
	bool SetMemberLevel(FName Character, int32 TargetLevel);

	/** Escreve um atributo direto (respeitando o teto). HP/MP mexem no MÁXIMO. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Party")
	bool SetMemberStat(FName Character, FName Stat, int32 Value);

	/**
	 * Tira o personagem do roster de vez, apagando o registro.
	 *
	 * NÃO é o que a história usa — para alguém que só sai do grupo existe o
	 * SetAvailable, que preserva tudo. Isto aqui é para teste e para o New
	 * Game+ eventualmente descartar alguém.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Party")
	bool RemoveFromRoster(FName Character);

	// ============================================================
	// HP / MP / AP
	// ============================================================

	/** Tira HP. Chegando a zero o personagem morre (para de receber XP). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Party|Vitals")
	void ApplyDamage(FName Character, int32 Amount);

	/** Cura HP. Não ressuscita: em quem está morto, use ReviveMember. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Party|Vitals")
	void HealHP(FName Character, int32 Amount);

	UFUNCTION(BlueprintCallable, Category = "JRPG|Party|Vitals")
	void RestoreMP(FName Character, int32 Amount);

	UFUNCTION(BlueprintCallable, Category = "JRPG|Party|Vitals")
	void SetAP(FName Character, int32 NewAP);

	/** Volta com HP definido (mínimo 1). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Party|Vitals")
	void ReviveMember(FName Character, int32 WithHP);

	/** HP e MP cheios em todo o roster (pousada, Genesis Tree). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Party|Vitals")
	void FullRestoreAll();

	// ============================================================
	// ITENS
	// ============================================================

	/** Aplica o efeito de um consumível. Chamado pelo InventorySubsystem. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Party")
	void ApplyConsumableEffect(FName CharacterName, const FItemData& ItemData);

	// ============================================================
	// SAVE / NEW GAME
	// ============================================================

	/** Zera tudo. Chamado pelo StartNewGame antes de recrutar o Vahn. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Party")
	void ResetParty();

	/** Restaura o roster de um save. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Party")
	void LoadPartyState(const TArray<FJRPGPartyMember>& SavedRoster);

	/** O roster para gravar no save (ordem estável: a formação primeiro). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Party")
	TArray<FJRPGPartyMember> GetPartyStateForSave() const;

	// ============================================================
	// EVENTOS
	// ============================================================

	UPROPERTY(BlueprintAssignable, Category = "JRPG|Party")
	FOnPartyChangedSignature OnPartyChanged;

	UPROPERTY(BlueprintAssignable, Category = "JRPG|Party")
	FOnMemberLevelUpSignature OnMemberLevelUp;

	/** DataTable de FCharacterData. Resolvida sozinha no Initialize. */
	UPROPERTY(BlueprintReadWrite, Category = "JRPG|Party")
	TObjectPtr<UDataTable> CharacterTable;

	UFUNCTION(BlueprintCallable, Category = "JRPG|Party")
	void SetCharacterTable(UDataTable* NewTable);

private:
	/** Registro por key. */
	UPROPERTY()
	TMap<FName, FJRPGPartyMember> Roster;

	/** Formação, na ordem. Sempre um subconjunto do Roster. */
	UPROPERTY()
	TArray<FName> ActiveOrder;

	/** Aplica um level-up: sobe o level e sorteia o ganho dos 8 atributos. */
	void ApplyLevelUp(FJRPGPartyMember& Member);

	/** Devolve o membro aos atributos de level 1 de DT_Characters. */
	bool ResetMemberToBase(FJRPGPartyMember& Member);

	/** Reordena ActiveOrder a partir das flags, mantendo a ordem do roster. */
	void RebuildActiveOrder();
};
