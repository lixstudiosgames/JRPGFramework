#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "WebUIBridge.generated.h"

// REGRA: NUNCA incluir headers do SDK (Ultralight, JavaScriptCore) neste arquivo.
// Os callbacks JSC são declarados como funções file-scope estáticas no .cpp,
// não como membros da classe, para evitar vazamento de tipos do SDK.

class UWebUISubsystem;
class USoundBase;

UCLASS(BlueprintType, Blueprintable)
class JRPGFRAMEWORK_API UWebUIBridge : public UObject
{
	GENERATED_BODY()

public:
	UWebUIBridge();

	/** Inicializa a ponte vinculando-a ao WebUISubsystem */
	void Initialize(UWebUISubsystem* InSubsystem);

	/** Limpa o ponteiro global ativo — chamar antes de destruir o Bridge entre PIE sessions */
	static void ClearActiveBridge();

	virtual void BeginDestroy() override;

	/**
	 * Expõe a ponte no escopo global JS da View indicada.
	 * RawView é um ponteiro opaco para ultralight::View* — cast feito no .cpp.
	 */
	void BindNativeFunctions(void* RawView);

	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	FString Echo(const FString& Message);

	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OnUIReady();

	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OnMenuOptionSelected(const FString& OptionName);

	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void CloseMenu();

	/**
	 * O JS pediu para mostrar/esconder o ponteiro (bridge.setcursorvisible).
	 * O cursor visível é do PlayerController, não do HTML — `cursor: none` no
	 * CSS não o esconderia, porque o Slate o desenha por cima da textura.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void SetCursorVisible(bool bVisible);

	/**
	 * O JS trocou a dificuldade na tela de opções (bridge.onsetdifficulty).
	 * O índice casa com EJRPGDifficulty: 0 Normal, 1 Hard, 2 Juggernaut.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OnSetDifficulty(int32 DifficultyIndex);

	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void PlaySFX(const FString& SoundName);

	/** O JS confirmou salvar no slot indicado (bridge.onsaveslot). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OnSaveSlotSelected(int32 SlotIndex);

	/** O JS confirmou carregar o slot indicado (bridge.onloadslot). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OnLoadSlotSelected(int32 SlotIndex);

	/** O JS confirmou compra na loja (bridge.onshopbuy). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OnShopBuy(const FString& ItemID, int32 Quantity);

	/** O JS confirmou venda na loja (bridge.onshopsell). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OnShopSell(const FString& ItemID, int32 Quantity);

	/** O JS escolheu uma opção do main menu (bridge.onmainmenuaction). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OnMainMenuAction(const FString& Action);

	/**
	 * O JS trocou de tela sozinho (bridge.onuistatechanged) — ex: as opções
	 * fecharam e devolveram o controle ao menu de pausa/main menu. Só sincroniza
	 * o espelho C++ do estado.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OnUIStateChanged(const FString& State);

	/**
	 * Abre uma URL no navegador do SISTEMA (bridge.openurl) — usado pelos cards
	 * de apoio do main menu (YouTube/Patreon/itch.io). Só aceita http(s).
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OpenURL(const FString& URL);

	/**
	 * DEV MENU: uma linha de comando vinda do JS ("gold.add 500").
	 *
	 * De propósito é UMA função genérica em vez de uma por botão: a tela de dev
	 * cresce sem nunca mais mexer na ponte. O parse e o despacho ficam em
	 * UWebUISubsystem::RunDevCommand, e a resposta volta pelo JRPGDev.result().
	 *
	 * Vira no-op em build Shipping.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI|Dev")
	void OnDevCommand(const FString& Command);

	/** A tela de Party pediu para ativar/desativar um personagem. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OnPartyToggle(const FString& Character, bool bActivate);

	/** A tela de Itens pediu para jogar um item fora. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OnItemDiscard(const FString& ItemID, int32 Quantity);

private:
	UPROPERTY()
	UWebUISubsystem* WebUISubsystem;

	UPROPERTY()
	TMap<FString, USoundBase*> SoundCache;
};
