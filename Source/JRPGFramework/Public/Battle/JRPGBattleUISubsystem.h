#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Battle/BattleTypes.h"
#include "Battle/JRPGBattleMenu.h"
#include "JRPGBattleUISubsystem.generated.h"

class AJRPGBattleArena;
class AJRPGBattlePawn;
class UBattleSubsystem;
class UWebUISubsystem;

/**
 * UJRPGBattleUISubsystem
 * A UI de comando da luta (placeholder): liga a máquina de estados do menu (FJRPGBattleMenu, sem UObject) à luta
 * (UBattleSubsystem) e à tela HTML (UWebUISubsystem). Existe sozinho em todo mapa jogado, como o
 * UJRPGBattleSceneSubsystem; não há nada para pôr na cena.
 *
 * Quando aparece: a cena da luta está de pé (OnSceneAssembled) E a rodada espera o comando de um herói deste jogador
 * (OnSlotChanged em Commands, com ActiveSlot >= 0). Some quando a rodada toca (a execução começou) e no fim da luta; a
 * próxima fase de comandos a traz de volta na tela Start.
 *
 * O que ela faz com a tela: o JS só desenha a view (JRPGBattle.render) e repassa cada aperto (bridge.onbattleinput); quem
 * decide é o FJRPGBattleMenu. As escolhas ficam no buffer do menu e só vão ao UBattleSubsystem::SubmitCommand quando o
 * jogador dá o Begin da tela Final, uma a uma, em ordem, COM O JOGADOR LOCAL (regra 22: o núcleo confere o dono do slot).
 *
 * Auto-battle: o menu guarda uma tática por herói pela luta toda. Com o auto ligado, cada fase de comandos mostra a tela Start
 * com a contagem de AutoStartDelay segundos; passada ela, a rodada sai pelas táticas. O Cancel na contagem desliga o auto.
 *
 * O prompt de debug na tela (e os comandos JRPG.Battle.*) continuam funcionando: o prompt só é escondido enquanto a UI está
 * de pé (UJRPGBattleSceneSubsystem::SetDebugPromptSuppressed). Docs/Guides/BATTLE.md › Battle UI.
 */
UCLASS()
class JRPGFRAMEWORK_API UJRPGBattleUISubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** O menu está na tela. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle UI")
	bool IsMenuUp() const { return bMenuUp; }

	/** O menu (só leitura): a tela, o herói comandado e o cursor de alvo. A câmera da luta enquadra por ele. */
	const FJRPGBattleMenu& GetMenu() const { return Menu; }

	/** Desliga a UI inteira: a luta volta a só ter o prompt de debug e os comandos de console. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle UI")
	bool bUIEnabled = true;

	/** O jogador local: o dono dos heróis que o menu comanda e quem manda os comandos (jogo local = 0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle UI")
	int32 LocalPlayerIndex = 0;

	/** Segundos da contagem do auto na tela Start antes de a rodada sair sozinha (dá tempo de cancelar). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle UI", meta = (ClampMin = "0.0"))
	float AutoStartDelay = 1.2f;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	UFUNCTION()
	void HandleBattleStarted(const FJRPGBattleSetup& Setup, const TArray<FJRPGCombatant>& Combatants);

	UFUNCTION()
	void HandleBattleEnded(EJRPGBattleResult Result);

	UFUNCTION()
	void HandleSlotChanged(int32 Slot, EJRPGBattleStage Stage);

	UFUNCTION()
	void HandleCommandRefused(int32 Slot, const FString& Why);

	UFUNCTION()
	void HandleSceneAssembled(AJRPGBattleArena* Arena, const TArray<AJRPGBattlePawn*>& Pawns);

	UFUNCTION()
	void HandleFieldShown();

	/** Um aperto vindo do JS (bridge.onbattleinput): 'up' 'down' 'left' 'right' 'confirm' 'cancel' ou 'pick' com A e B. */
	void HandleUIInput(const FString& Action, int32 A, int32 B);

	/** Mostra o menu se é a hora (cena de pé, rodada esperando um herói deste jogador). */
	void TryShowMenu();
	void HideMenu();

	/** A foto da rodada: os heróis, os inimigos com A / B, as regras de que o menu precisa. */
	FJRPGBattleMenuContext BuildContext(const UBattleSubsystem& Battle) const;

	/** O que o menu pediu depois de um aperto. */
	void ApplyResult(const FJRPGBattleMenuResult& Result);

	/** Desenha a view de agora (com o som, se há). */
	void Render(const TCHAR* Sfx = TEXT(""));

	/** O Begin: manda o buffer ao núcleo, um comando por vez, em ordem. */
	void CommitPlan();

	/** O auto: monta a rodada pelas táticas e a manda. */
	void RunAutoRound();

	/** A fuga: TODO(Fase 7) o sorteio da fuga; hoje termina a luta como Escaped. */
	void DoEscape();

	void StartAutoTimer();
	void OnAutoTimer();

	UBattleSubsystem* GetBattle() const;
	UWebUISubsystem* GetWebUI() const;

	/** A máquina de estados do menu (vive a luta inteira: as táticas e o auto persistem entre as rodadas). */
	FJRPGBattleMenu Menu;

	bool bFightOn = false;
	bool bSceneUp = false;
	bool bMenuUp = false;
	/** Dentro do CommitPlan: as mudanças de slot que o próprio envio provoca não abrem nem fecham nada. */
	bool bCommitting = false;

	/** O motivo da última recusa do núcleo (HandleCommandRefused), para mostrar na tela. */
	FString LastRefusal;

	FTimerHandle AutoTimer;
};
