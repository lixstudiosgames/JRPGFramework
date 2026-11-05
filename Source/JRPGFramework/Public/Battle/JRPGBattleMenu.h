#pragma once

#include "CoreMinimal.h"
#include "Arts/ArtsData.h"
#include "Battle/BattleTypes.h"

/**
 * O menu de comandos da luta (a UI de comando, placeholder), SEM UObject, SEM subsistema, SEM WebUI: é uma máquina de
 * estados pura. Entra a foto da rodada (FJRPGBattleMenuContext) e as teclas semânticas (cima / baixo / esquerda /
 * direita / confirmar / cancelar, as mesmas do teclado, do gamepad e do clique do mouse); sai o que desenhar (a view,
 * como um objeto literal JS) e, quando o jogador termina, um PEDIDO (confirmar a rodada, fugir, ligar o auto). Quem
 * conversa com a luta e com a tela é o UJRPGBattleUISubsystem; aqui ficam só as regras do fluxo, para os testes
 * andarem o menu inteiro sem mapa (Private/Tests/JRPGBattleUITests.cpp).
 *
 * O FLUXO (Docs/Guides/BATTLE.md › Battle UI), todo ele um d-pad de quatro lados em volta de um ícone central:
 *
 *   Start ── Esquerda: Begin ──▶ Command(herói 0) ── Esquerda: Attack ──▶ Attack ── confirmar ──▶ Target ── confirmar ──▶
 *     │                              ▲ cancelar volta ao herói anterior                                       Command(herói 1) …
 *     │                                                                                                              │
 *     ├─ Cima: Tactics ──▶ Tactics (um comportamento por herói) ── confirmar ──▶ AUTO ligado, a rodada roda sozinha   ▼
 *     └─ Direita: Escape ──▶ Escape (Confirm / Cancel)                                           Final: Esquerda Begin (manda tudo
 *                                                                                                  na ordem) / Direita Reselect
 *
 * As escolhas ficam num BUFFER (FJRPGBattleCommandPlan): o núcleo aceita um comando por slot, mas o Reselect pede tudo
 * coletado antes, então nada vai ao UBattleSubsystem até o Begin da tela Final. O comando de cada herói sai do mesmo
 * contrato do console (FJRPGBattleCommand) e é mandado COM O JOGADOR que o enviou (regra 22): o núcleo confere o dono.
 */

/** Como um herói luta sozinho no auto-battle (a tela Tactics). Standard é o 0: um herói sem escolha é Standard. */
enum class EJRPGBattleTactic : uint8
{
	/** O que o JRPG.Battle.Auto faz: um golpe de Arms no inimigo vivo de menor slot. */
	Standard,
	/** Gasta o gauge na cadeia mais longa que cabe (uma Art conhecida, completada com o botão mais barato). */
	Aggressive,
	/** Seguro: o Spirit (Guard), quando o núcleo o tiver. Hoje cai no Standard. */
	Defensive
};

/** Os textos fixos do menu num lugar só: o rótulo do botão de cima do Start ("Tactics" ou "Quick Battle") muda aqui. */
namespace JRPGBattleMenuText
{
	inline constexpr const TCHAR* BeginLabel = TEXT("Begin");
	/** A decisão do nome está aberta ("Quick Battle" ou "Tactics"): é só trocar este texto. */
	inline constexpr const TCHAR* TacticsLabel = TEXT("Tactics");
	inline constexpr const TCHAR* EscapeLabel = TEXT("Escape");
	inline constexpr const TCHAR* ReselectLabel = TEXT("Reselect");
	inline constexpr const TCHAR* ConfirmLabel = TEXT("Confirm");
	inline constexpr const TCHAR* CancelLabel = TEXT("Cancel");
	inline constexpr const TCHAR* AttackLabel = TEXT("Attack");
	inline constexpr const TCHAR* ItemLabel = TEXT("Item");
	inline constexpr const TCHAR* SpiritLabel = TEXT("Spirit");
	inline constexpr const TCHAR* TargetLabel = TEXT("Target");
	inline constexpr const TCHAR* RaSeruFallbackLabel = TEXT("Ra-Seru");
}

/** As teclas semânticas do menu (teclado, gamepad e clique chegam todos como uma delas). */
enum class EJRPGBattleMenuKey : uint8
{
	Up,
	Down,
	Left,
	Right,
	Confirm,
	Cancel
};

/** As telas do menu. */
enum class EJRPGBattleMenuScreen : uint8
{
	Start,
	Escape,
	Tactics,
	/** O menu principal de um herói: Attack / Item / Ra-Seru / Spirit. */
	Command,
	/** A entrada dos botões (a janela de entrada das Arts). */
	Attack,
	Target,
	Final
};

/** O que a tela pede a quem a controla quando o jogador termina um passo. */
enum class EJRPGBattleMenuRequest : uint8
{
	None,
	/** Mandar o plano inteiro, em ordem, ao UBattleSubsystem (o Begin da tela Final). */
	Commit,
	/** Acabar a luta como fuga. */
	Escape,
	/** As Tactics foram confirmadas: o auto está ligado, monta e manda a rodada agora. */
	AutoRound,
	/** O jogador cancelou a contagem do auto: volta ao manual. */
	AutoOff
};

/** Uma Art da lista de consulta da tela de comando (o painel do Triângulo), já na ordem de exibição. */
struct JRPGFRAMEWORK_API FJRPGBattleMenuArt
{
	FString Name;
	EJRPGArtKind Kind = EJRPGArtKind::Regular;
	/** O AP que ela dá (Regular, APGain) ou tira (Hyper / Super / Miracle, APCost, bCost). */
	int32 AP = 0;
	bool bCost = false;
	/** Os botões do comando dela (a view os mostra como setas, pela mão do herói). */
	TArray<EJRPGArtButton> Buttons;
};

/** Um herói como o menu o vê: a foto da rodada, só dados. */
struct JRPGFRAMEWORK_API FJRPGBattleMenuHero
{
	int32 Slot = INDEX_NONE;
	FName Key;
	FString Name;
	/** Meta / Terra / Ozma (DT_Characters.RaSeru): o rótulo do botão da direita do menu principal. */
	FString RaSeruName;
	int32 Level = 1;
	int32 HP = 0;
	int32 MaxHP = 0;
	int32 MP = 0;
	int32 MaxMP = 0;
	int32 AP = 0;
	int32 MaxAP = 100;
	/** O gauge desta rodada (FJRPGCombatant::CommandGauge); 0 ou menos = sem limite de gauge (só os 16 botões). */
	int32 Gauge = 0;
	/** O custo de cada botão no gauge, indexado por EJRPGArtButton (FJRPGCombatant::ButtonCost). */
	TArray<int32> ButtonCost;
	bool bLeftHanded = false;
	bool bAlive = true;
	/** Este jogador comanda o herói e ele ainda não comandou nesta rodada (o núcleo vai pedir o comando dele). */
	bool bNeedsCommand = false;
	/** Os botões das Arts Regulares que ele conhece (a Aggressive escolhe entre elas). */
	TArray<TArray<EJRPGArtButton>> KnownArtCommands;
	/**
	 * Todas as Arts que ele conhece, para a lista de consulta: as Hyper no topo, depois a Miracle, as Super e as Regulares; em
	 * cada grupo, a de mais AP primeiro (o painel do original).
	 */
	TArray<FJRPGBattleMenuArt> ArtList;

	/** O custo de um botão; 30 quando a lista não diz (o 0x1E do original). */
	int32 CostOf(EJRPGArtButton Button) const;
};

/** Um inimigo como o menu o vê. */
struct JRPGFRAMEWORK_API FJRPGBattleMenuEnemy
{
	int32 Slot = INDEX_NONE;
	FName Key;
	/** Com o sufixo A / B quando há mais de um da mesma espécie ("Frog A"). */
	FString Name;
	bool bAlive = true;
};

/** A foto de uma rodada de comandos: quem luta, com o que, e as regras que o menu precisa saber. */
struct JRPGFRAMEWORK_API FJRPGBattleMenuContext
{
	int32 Round = 1;
	/** O jogador local: quem manda os comandos (o núcleo confere que ele é o dono do slot). */
	int32 PlayerIndex = 0;
	/** Falso numa luta roteirizada (a regra do original: não se foge dela). */
	bool bCanEscape = true;
	/** Todos os heróis da luta, por slot (os que este jogador não comanda e os caídos também: a faixa de baixo mostra todos). */
	TArray<FJRPGBattleMenuHero> Heroes;
	/** Todos os inimigos, vivos ou não (o nome com o sufixo A / B é fixo na luta). */
	TArray<FJRPGBattleMenuEnemy> Enemies;

	const FJRPGBattleMenuHero* FindHero(int32 Slot) const;
	const FJRPGBattleMenuEnemy* FindEnemy(int32 Slot) const;
	/** O inimigo vivo de menor slot (o alvo do auto); INDEX_NONE se não há. */
	int32 LowestLivingEnemySlot() const;
	/** Os índices em Enemies dos que estão vivos, na ordem. */
	TArray<int32> LivingEnemyIndices() const;
	/** Os slots dos heróis que este jogador ainda precisa comandar, em ordem de slot (a ordem da party). */
	TArray<int32> HeroSlotsNeedingCommand() const;
};

/**
 * O buffer de comandos: um por herói, na ordem da party, mais o cursor de quem está sendo comandado. Nada daqui chega ao
 * núcleo antes do Commit; o Reselect o zera. Só dados e a ordem — não conhece o menu nem a luta.
 */
struct JRPGFRAMEWORK_API FJRPGBattleCommandPlan
{
	/** Os slots a comandar, em ordem crescente (a ordem da party: Vahn, Noa, Gala). */
	TArray<int32> HeroSlots;
	/** Um por HeroSlots; Kind None = ainda não escolhido. */
	TArray<FJRPGBattleCommand> Entries;
	/** Quem está sendo comandado (índice em HeroSlots); Num() quando todos já foram. */
	int32 Cursor = 0;

	/** Recomeça com estes heróis (em ordem), tudo por escolher, o cursor no primeiro. */
	void Reset(const TArray<int32>& Slots);

	int32 Num() const { return HeroSlots.Num(); }
	bool IsEmpty() const { return HeroSlots.IsEmpty(); }

	/** O slot de quem está sendo comandado; INDEX_NONE se já passou do último. */
	int32 CurrentSlot() const { return HeroSlots.IsValidIndex(Cursor) ? HeroSlots[Cursor] : INDEX_NONE; }

	/** Guarda o comando de quem está no cursor (o Slot do comando tem que ser o dele) e anda para o próximo. */
	bool Set(const FJRPGBattleCommand& Command);

	/** Volta ao herói anterior e apaga a escolha dele (vai refazê-la). Falso no primeiro: não há anterior. */
	bool StepBack();

	/** O Reselect: apaga todas as escolhas e volta ao primeiro herói. */
	void Reselect();

	/** Todos os heróis têm comando. */
	bool IsComplete() const;

	/**
	 * Manda os comandos, UM A UM, na ordem do plano, a Submit (o UBattleSubsystem::SubmitCommand do jogador que mandou).
	 * Para no primeiro que falta ou que Submit recusa (OutRefusedIndex = o índice dele). Devolve quantos foram aceitos.
	 */
	int32 Commit(TFunctionRef<bool(const FJRPGBattleCommand&)> Submit, int32* OutRefusedIndex = nullptr) const;
};

/** A tática -> o comando (puro: o auto-battle e os testes usam as mesmas contas). */
namespace JRPGBattleTactics
{
	/** O nome na tela ("Aggressive", "Standard", "Defensive"). */
	JRPGFRAMEWORK_API const TCHAR* Name(EJRPGBattleTactic Tactic);

	/**
	 * O comando que um herói dá com esta tática. Standard: um golpe de Arms no alvo. Aggressive: a cadeia mais longa que cabe
	 * no gauge (ver LongestAffordableChain). Defensive: o Spirit quando o núcleo o aceitar; hoje é o Standard.
	 * TODO(Fase 7): a Defensive vira Guard quando o Spirit existir no núcleo (EJRPGBattleCommandKind::Guard é recusado hoje).
	 */
	JRPGFRAMEWORK_API FJRPGBattleCommand ChooseCommand(EJRPGBattleTactic Tactic, const FJRPGBattleMenuHero& Hero, int32 TargetSlot, int32 PlayerIndex);

	/**
	 * Os botões da Aggressive: a Art Regular conhecida mais longa que cabe no gauge (empate: a que gasta mais) e, com o que
	 * sobra do gauge, o botão mais barato quantas vezes couber (até 16 botões). Vazia se o gauge não paga nem um botão
	 * ou é 0 (sem gauge: o auto não inventa uma cadeia sem limite).
	 * TODO: as Hyper / Super / Miracle (gastam AP) entram quando o auto souber conferir o AP da cadeia.
	 */
	JRPGFRAMEWORK_API TArray<EJRPGArtButton> LongestAffordableChain(const FJRPGBattleMenuHero& Hero);

	/**
	 * Os botões como comando: um Arms sozinho é o Attack do núcleo (o golpe de Arms); qualquer outra coisa é Arts com os
	 * bytes (UJRPGArtsRules::EncodeArtBytes).
	 */
	JRPGFRAMEWORK_API FJRPGBattleCommand MakeChainCommand(const FJRPGBattleMenuHero& Hero, const TArray<EJRPGArtButton>& Buttons, int32 TargetSlot, int32 PlayerIndex);
}

namespace JRPGBattleMenu
{
	/** Os nomes dos inimigos com o sufixo A / B quando a mesma espécie aparece mais de uma vez ("Frog A", "Frog B"). */
	JRPGFRAMEWORK_API TArray<FString> MakeEnemyLabels(const TArray<FName>& Keys, const TArray<FString>& Names);
}

/** O que um aperto de tecla produziu. */
struct FJRPGBattleMenuResult
{
	/** A tela mudou (redesenhar). */
	bool bChanged = false;
	EJRPGBattleMenuRequest Request = EJRPGBattleMenuRequest::None;
	/** O som do aperto: "Next" (andou), "Select" (confirmou), "Cancel" (voltou ou recusou); vazio = nenhum. */
	const TCHAR* Sfx = TEXT("");
};

/**
 * FJRPGBattleMenu
 * A máquina de estados do menu. Vive a luta inteira: BeginRound a cada fase de comandos (volta à tela Start com o buffer
 * novo) e ResetFight no começo de uma luta; as Tactics e o auto PERSISTEM entre as rodadas.
 */
class JRPGFRAMEWORK_API FJRPGBattleMenu
{
public:
	/** O Item, o Ra-Seru e o Spirit do menu principal estão apagados (um X por cima) nesta fase. */
	static constexpr bool bItemEnabled = false;
	static constexpr bool bRaSeruEnabled = false;
	static constexpr bool bSpiritEnabled = false;

	/** Uma luta nova: todos Standard, auto desligado. */
	void ResetFight();

	/** Uma fase de comandos nova (ou o menu refeito depois de uma recusa): a tela Start com o buffer vazio. As Tactics ficam. */
	void BeginRound(const FJRPGBattleMenuContext& InContext);

	/** Uma tecla semântica na tela de agora. */
	FJRPGBattleMenuResult Press(EJRPGBattleMenuKey Key);

	/**
	 * Um clique que escolhe por posição (nunca move o cursor só por passar o mouse): na tela Target, A = o inimigo (índice na
	 * lista dos vivos); na Tactics, A = a linha (o herói; a última é o botão Confirm) e B = a opção. Os índices vêm da UI: são
	 * conferidos aqui (regra 22).
	 */
	FJRPGBattleMenuResult Pick(int32 A, int32 B);

	/**
	 * O auto: monta no buffer o comando de cada herói pela tática dele, contra o inimigo vivo de menor slot. Falso (e o buffer
	 * como estava) se não há alvo ou se algum herói não tem comando possível.
	 */
	bool PlanAuto();

	/** O auto estava ligado quando a fase de comandos abriu: a tela Start mostra a contagem e o Cancel o desliga. */
	bool IsAutoPending() const { return bAutoPending; }
	bool IsAutoOn() const { return bAuto; }

	/** A contagem acabou ou o auto rodou: nada mais pendente. */
	void ClearAutoPending() { bAutoPending = false; }

	/** Desliga o auto (o manual de volta). */
	void TurnAutoOff();

	EJRPGBattleMenuScreen GetScreen() const { return Screen; }
	const FJRPGBattleMenuContext& GetContext() const { return Ctx; }
	const FJRPGBattleCommandPlan& GetPlan() const { return Plan; }
	EJRPGBattleTactic GetTactic(FName HeroKey) const { return Tactics.FindRef(HeroKey); }

	/** O botões digitados na tela Attack (para os testes e a view). */
	const TArray<EJRPGArtButton>& GetButtons() const { return Buttons; }
	int32 GetGaugeLeft() const { return GaugeLeft; }

	/** O texto da última recusa (gauge cheio, nada digitado, sem como fugir...): mostrado na tela, apagado ao andar. */
	FString Refusal;

	/**
	 * A tela como um objeto literal JS (a view de JRPGBattle.render): a tela, as migalhas, o painel do grupo, o d-pad, a
	 * janela de entrada, os alvos, as Tactics, o resumo. Os textos passam por ToJSStringLiteral (regra 10). Sfx = o som a
	 * tocar com ela.
	 */
	FString BuildViewJS(const FString& Sfx) const;

private:
	FJRPGBattleMenuResult PressStart(EJRPGBattleMenuKey Key);
	FJRPGBattleMenuResult PressEscape(EJRPGBattleMenuKey Key);
	FJRPGBattleMenuResult PressTactics(EJRPGBattleMenuKey Key);
	FJRPGBattleMenuResult PressCommand(EJRPGBattleMenuKey Key);
	FJRPGBattleMenuResult PressAttack(EJRPGBattleMenuKey Key);
	FJRPGBattleMenuResult PressTarget(EJRPGBattleMenuKey Key);
	FJRPGBattleMenuResult PressFinal(EJRPGBattleMenuKey Key);

	/** Confirma a tela Tactics: guarda as escolhas e liga o auto. */
	FJRPGBattleMenuResult ConfirmTactics();
	/** Confirma o alvo: o comando do herói entra no buffer. */
	FJRPGBattleMenuResult ConfirmTarget();

	void EnterTactics();

	/**
	 * Abre a janela de entrada do herói com os botões da última cadeia que ele mandou nesta luta (o que couber no gauge e nos 16
	 * botões desta rodada): repetir a Art é só confirmar. O Cancel tira um botão de cada vez, como sempre.
	 */
	void PrefillButtons(const FJRPGBattleMenuHero& Hero);

	/** O herói que está sendo comandado (o do cursor do buffer); nulo se já passou do último. */
	const FJRPGBattleMenuHero* FocusHero() const;

	FJRPGBattleMenuContext Ctx;
	FJRPGBattleCommandPlan Plan;
	EJRPGBattleMenuScreen Screen = EJRPGBattleMenuScreen::Start;

	/** Tela Attack: os botões digitados e o que sobra do gauge. */
	TArray<EJRPGArtButton> Buttons;
	int32 GaugeLeft = 0;

	/** Tela Target: o cursor, índice na lista dos inimigos VIVOS. */
	int32 TargetCursor = 0;

	/** Tela Tactics: a linha (a última é o Confirm) e a opção de cada herói (índice na ordem de exibição). */
	int32 TacticRow = 0;
	TArray<int32> TacticPicks;

	/** O que vale a luta inteira. */
	TMap<FName, EJRPGBattleTactic> Tactics;
	/** A última cadeia de botões que cada herói mandou (preenche a janela de entrada na vez seguinte). */
	TMap<FName, TArray<EJRPGArtButton>> LastButtons;
	bool bAuto = false;
	bool bAutoPending = false;
};
