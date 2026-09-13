#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/SubclassOf.h"
#include "Containers/Ticker.h"
#include "WebUISubsystem.generated.h"

// REGRA: NUNCA incluir headers do SDK Ultralight aqui.
// O Ultralight define macros globais (GetBytesPerPixel, etc.) que corrompem
// os headers da Unreal Engine em cascata. Usamos void* opaco no header
// e fazemos o cast seguro somente nos arquivos .cpp.

class UWebContainerWidget;
class UWebUIBridge;
class UUserWidget;
class FUltralightRenderThread;
class FRunnableThread;

/**
 * Estado global da UI in-game. Espelhado no JS do shell (UIState):
 * o C++ usa o espelho para decisões síncronas de input/foco, o JS usa
 * o estado para decidir o que pode abrir a cada momento.
 */
UENUM(BlueprintType)
enum class EJRPGUIState : uint8
{
	HudOnly        UMETA(DisplayName = "HUD Only"),
	MenuOpen       UMETA(DisplayName = "Menu Open"),
	DialogueActive UMETA(DisplayName = "Dialogue Active"),
	OptionsOpen    UMETA(DisplayName = "Options Open"),
	// Novos estados SEMPRE no fim — os valores são serializados em Blueprints
	SaveLoadOpen   UMETA(DisplayName = "SaveLoad Open"),
	ShopOpen       UMETA(DisplayName = "Shop Open"),
	MainMenuOpen   UMETA(DisplayName = "Main Menu Open"),
	DevMenuOpen    UMETA(DisplayName = "Dev Menu Open"),
	PartyOpen      UMETA(DisplayName = "Party Open"),
	ItemsOpen      UMETA(DisplayName = "Items Open")
};

/**
 * Disparado sempre que o estado global da UI muda (abrir/fechar telas).
 * É o gatilho do auto-restore do CameraSubsystem (NewState == HudOnly).
 * NÃO dispara no teardown do fim da sessão (Deinitialize/PIE stop).
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnJRPGUIStateChanged, EJRPGUIState, OldState, EJRPGUIState, NewState);

/**
 * Disparado quando o jogador escolhe uma opção do main menu que o PROJETO
 * precisa resolver (o plugin não conhece os mapas do jogo):
 *   "newgame" — o CoreSubsystem JÁ recebeu StartNewGame() e o input já voltou
 *               ao jogo; o Blueprint só precisa fazer o OpenLevel do mapa inicial.
 *   "quit"    — disparado imediatamente antes do QuitGame (para salvar settings
 *               ou tocar um fade, se quiser).
 * "continue" e "options" são resolvidos dentro do próprio subsystem e NÃO
 * chegam aqui.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJRPGMainMenuAction, const FString&, Action);

/**
 * UWebUISubsystem — dono da WebUI (Ultralight) do jogo.
 *
 * Arquitetura SHELL (SPA): um único documento (shell.html) é carregado UMA VEZ
 * por processo e permanece vivo a sessão inteira. O widget fica sempre no
 * viewport (HitTestInvisible quando só HUD). Toda interação BP -> UI acontece
 * chamando funções JS já existentes no documento via ExecuteJS — nunca por
 * query param de URL nem por novo LoadURL.
 */
UCLASS(BlueprintType, Blueprintable)
class JRPGFRAMEWORK_API UWebUISubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UWebUISubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ============================================================
	// API NOVA (Shell) — uma função por ação do jogo
	// ============================================================

	/**
	 * Cria o widget permanente e carrega o shell.html (uma única vez por
	 * processo — idempotente). Chamar no BeginPlay do mapa/GameMode.
	 * As demais funções chamam isto como rede de segurança se necessário.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void InitializeUIShell();

	/**
	 * Mostra o popup de item recebido (baú). Overlay passivo: NÃO rouba o
	 * input do jogo. Os dados vêm direto do Blueprint (Data Table Row + qty).
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void ShowItemPopup(FText ItemName, int32 ItemQty = 1);

	/** Abre o menu de pause/status. Foca a UI (cursor + teclado/gamepad). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OpenMenu();

	/** Fecha o menu com animação; o input volta ao jogo ao fim da animação. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void CloseMenu();

	/**
	 * Abre um diálogo (PLACEHOLDER — o lado JS ainda só loga; o nó Blueprint
	 * já existe com a assinatura final para o futuro sistema de diálogo).
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OpenDialogue(FText SpeakerName, FText DialogueText, const TArray<FText>& Options);

	/**
	 * Abre a tela de opções (Display/Audio/Game/Controller/Keybindings).
	 * Pode ser chamada do jogo (HudOnly), de dentro do menu de pausa e do main
	 * menu — ao fechar, a UI volta sozinha para a tela de origem.
	 *
	 * NOTA: por enquanto os controles são SÓ VISUAIS (mudar resolução/volume na
	 * tela ainda não aplica nada no jogo).
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OpenOptions();

	/**
	 * Abre o main menu (tela de título). Chamar no BeginPlay do mapa de menu,
	 * depois do InitializeUIShell. A seção é transparente — a cena 3D do mapa
	 * aparece por trás.
	 *
	 * Ligue OnMainMenuAction no Blueprint para fazer o OpenLevel do mapa inicial
	 * quando o jogador escolher "New Game".
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OpenMainMenu();

	/** Ação do main menu que o projeto precisa resolver ("newgame" / "quit"). */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|WebUI")
	FOnJRPGMainMenuAction OnMainMenuAction;

	/** Versão mostrada no rodapé do main menu (ex: "v0.1.0"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|WebUI")
	FString GameVersion;

	/** Build mostrado no rodapé do main menu (ex: "Build 2026.08"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|WebUI")
	FString GameBuild;

	/**
	 * Abre a tela de Save/Load (estátua de save): grid de 15 slots com as abas
	 * Save/Load no topo (Q/E no teclado, LB/RB no controle). Cai direto no
	 * grid, na aba Save. Foca a UI e envia os metadados dos slots para o JS.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OpenSaveLoad();

	/**
	 * Mesma tela, já na aba Save — para o menu de pausa ter Save e Load como
	 * entradas separadas. O jogador ainda pode trocar de aba com Q/E ou LB/RB.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OpenSaveScreen();

	/** Mesma tela, já na aba Load (par de OpenSaveScreen). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OpenLoadScreen();

	/**
	 * Abre Save/Load a partir do menu de pausa: o menu continua "por baixo"
	 * (estado MenuOpen -> SaveLoadOpen) e cancelar devolve o jogador ao menu
	 * em vez do jogo. Chamado pelo bridge quando o JS manda 'save'/'load'.
	 */
	void OpenSaveLoadFromMenu(bool bLoadTab);

	/**
	 * True se gravar é permitido agora — hoje equivale a estar no Field
	 * (UWorldStateSubsystem::IsInField). O menu de pausa usa isto para apagar
	 * a opção Save, e OpenSaveLoadFromMenu recusa quando é false.
	 *
	 * A estátua de save (OpenSaveLoad) NÃO passa por este gate: ela só existe
	 * dentro do Field, e travá-la quebraria o fluxo antes de o jogo começar a
	 * chamar SetIsInField.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|WebUI")
	bool CanSaveNow() const;

	/**
	 * Larga a partida e volta para a tela de título (MainMenuLevelName).
	 * Congela o playtime e limpa o flag de Field; o resto do estado fica como
	 * está, porque New Game reseta e Continue restaura por cima.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void QuitToMainMenu();

	/**
	 * Mostra/esconde o ponteiro enquanto a UI está aberta. O JS chama isto
	 * (bridge.setcursorvisible) ao alternar entre mouse e teclado/gamepad: o
	 * cursor é desenhado pelo Slate por cima da textura do Ultralight, então
	 * `cursor: none` no CSS não daria conta.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void SetUICursorVisible(bool bVisible);

	/**
	 * Aplica a dificuldade escolhida na tela de opções. O índice vem da lista
	 * do JS e casa com a ordem de EJRPGDifficulty.
	 */
	void SetDifficultyFromUI(int32 DifficultyIndex);

	/** Mapa carregado por QuitToMainMenu (sem extensão). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|WebUI")
	FName MainMenuLevelName = TEXT("MainMenu");

	/**
	 * Abre a loja indicada (row name da DT_Shops). Recusa se a loja não existir
	 * ou estiver indisponível (condição de flag) — use ShopSubsystem->IsShopAvailable
	 * no BP do NPC para escolher a variante certa (ex: before/after mist).
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OpenShop(FName ShopID);

	/**
	 * Tela de formação da party. Abre pelo item "party" do menu de pausa e
	 * volta para ele ao fechar, igual a Options/Save/Load.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OpenPartyScreen();

	/** Tela de inventário. Abre pelo item "items" do menu de pausa. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void OpenItemsScreen();

	/** O jogador jogou um item fora. Quem valida é o InventorySubsystem. */
	void HandleItemDiscard(FName ItemID, int32 Quantity);

	/**
	 * O jogador pediu para pôr/tirar alguém da formação. Quem valida é o
	 * PartySubsystem — a UI não decide sozinha; ela recebe o estado de volta.
	 */
	void HandlePartyToggle(FName Character, bool bActivate);

	/**
	 * Troca o estado global da UI. Chamar sempre que o contexto do jogo mudar
	 * (ex: diálogo começou/terminou) — controla o que pode abrir a cada momento.
	 * Não mexe em input/foco por si só.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void SetUIState(EJRPGUIState NewState);

	/** Estado atual da UI (espelho C++ do UIState do JS). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	EJRPGUIState GetUIState() const { return CurrentState; }

	/** Disparado sempre que o estado da UI muda (ex: loja abriu, tela fechou). */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|WebUI")
	FOnJRPGUIStateChanged OnUIStateChanged;

	/**
	 * DEV: força recarregar o shell.html (ex: após editar src/ e rodar
	 * build_webui.py) sem reiniciar o editor.
	 */
	// ============================================================
	// DEV MENU — ferramenta de teste, some no Shipping
	// ============================================================

	/**
	 * Abre o menu de desenvolvimento. Chame de um Input Action do seu personagem
	 * (uma tecla livre, F1 por exemplo).
	 *
	 * É uma tela de TESTE: mexe em gold, itens, flags e dificuldade direto, sem
	 * validação de jogo. Em build Shipping esta função não faz nada — o corpo
	 * inteiro é compilado fora, então não precisa lembrar de tirar a chamada do
	 * Blueprint antes do cooking.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI|Dev")
	void OpenDevMenu();

	/**
	 * Executa uma linha de comando do dev menu ("gold.add 500") e devolve a
	 * resposta para a tela. Chamado pela ponte; exposto para você poder disparar
	 * comandos de Blueprint ou de um console command também.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI|Dev")
	void RunDevCommand(const FString& Command);

	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void ReloadShell();

	// ============================================================
	// Infra
	// ============================================================

	/** Executa JavaScript arbitrário no shell (uso avançado/debug). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void ExecuteJS(const FString& JSCode);

	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	UWebUIBridge* GetBridge() const { return Bridge; }

	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	UWebContainerWidget* GetContainerWidget() const { return WebContainerWidget; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|WebUI")
	TSubclassOf<UUserWidget> WebContainerWidgetClass;

	/**
	 * Teto de FPS aplicado enquanto a UI está aberta (0 = desabilitado).
	 *
	 * A View do Ultralight roda a 60Hz numa thread própria. Com o jogo a centenas
	 * de fps, a Render Thread disputa o KeyedMutex da shared texture muito mais
	 * rápido do que há frames de UI para consumir — puro desperdício, e foi o
	 * regime em que a UI travava. Casar as duas cadências enquanto o menu está
	 * aberto elimina a contenção; o valor anterior de t.MaxFPS é restaurado ao sair.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|WebUI")
	float UIFrameRateCap = 60.f;

	/** Retorna a thread dedicada de renderização do Ultralight */
	FUltralightRenderThread* GetRenderThread() const;

	// --- Chamados pelo UWebUIBridge (já na Game Thread via AsyncTask) ---

	/** O JS avisou que o shell terminou de carregar (bridge.onuiready). */
	void HandleShellReady();

	/** O JS terminou de fechar uma tela e pede o input de volta ao jogo. */
	void HandleCloseRequestFromJS();

	/** O JS pediu para salvar no slot indicado (bridge.onsaveslot). */
	void HandleSaveSlotFromJS(int32 SlotIndex);

	/** O JS pediu para carregar o slot indicado (bridge.onloadslot). */
	void HandleLoadSlotFromJS(int32 SlotIndex);

	/** Reenvia os metadados dos 15 slots para o grid (pós-save). */
	void RefreshSaveLoadSlots();

	/** O JS confirmou compra na loja aberta (bridge.onshopbuy). */
	void HandleShopBuyFromJS(const FString& ItemID, int32 Quantity);

	/** O JS confirmou venda na loja aberta (bridge.onshopsell). */
	void HandleShopSellFromJS(const FString& ItemID, int32 Quantity);

	/** O JS escolheu uma opção do main menu (bridge.onmainmenuaction). */
	void HandleMainMenuActionFromJS(const FString& Action);

	/**
	 * O JS trocou de tela por conta própria (bridge.onuistatechanged) — ex:
	 * opções fechando e voltando para o menu de pausa / main menu. Só sincroniza
	 * o espelho C++ do estado; NÃO mexe em input nem em foco, porque a UI
	 * continua no controle nessas transições.
	 */
	void HandleUIStateChangedFromJS(const FString& State);

private:
	// Garante widget permanente no viewport + shell.html carregado (idempotente).
	// Retorna false se ainda não há World/PlayerController utilizável.
	bool EnsureShellWidget();

	// Executa JS agora se o shell está pronto; senão enfileira até o onuiready.
	void DispatchJS(const FString& JSCode);

	// Menu aberto: widget visível/interativo + cursor + FInputModeUIOnly + foco no browser.
	void ApplyUIFocus();

	// HUD/popup: widget HitTestInvisible + FInputModeGameOnly + foco no viewport do jogo.
	void ApplyGameFocus();

	// Remove o widget do viewport (apenas no Deinitialize — fim da sessão PIE).
	void TeardownWidget();

	// Aplica/desfaz o teto de FPS da UI (t.MaxFPS). Idempotentes: só a primeira
	// entrada guarda o valor original, para transições menu → opções → menu não
	// gravarem o próprio cap como se fosse o valor do jogador.
	void ClampFrameRateForUI();
	void RestoreFrameRate();

	// Valor de t.MaxFPS anterior ao clamp, válido enquanto bFrameRateClamped.
	float SavedMaxFPS = 0.f;
	bool bFrameRateClamped = false;

	// Atribui CurrentState e faz broadcast de OnUIStateChanged SÓ se mudou.
	// O teardown do fim da sessão NÃO passa por aqui de propósito.
	void SetStateInternal(EJRPGUIState NewState);

	// Corpo comum de OpenSaveLoad/OpenSaveScreen/OpenLoadScreen: valida estado,
	// foca a UI e chama JRPGSaveLoad.<JSFunction>(<slots><ExtraOpts>).
	// ExtraOpts entra CRU depois do array de slots, já com a vírgula
	// (ex: ",{returnTo:'menu'}") — passe TEXT("") para nenhum.
	void OpenSaveLoadWith(const TCHAR* JSFunction, const TCHAR* ExtraOpts);

	// Monta o array literal JS com os metadados dos 15 slots de save.
	FString BuildSaveSlotsPayloadJS() const;

	// Monta o objeto literal JS da loja aberta (CurrentShopID): estoque com
	// gates aplicados, inventário vendável e gold atual.
	FString BuildShopPayloadJS() const;

	/** Roster inteiro (formação + reserva) para a tela de Party. */
	FString BuildPartyPayloadJS() const;

	/** Só a formação, no formato enxuto dos cards de status do menu. */
	FString BuildMenuPartyJS() const;

	/** Inventário resolvido contra a DT_Items, para a tela de Itens. */
	FString BuildItemsPayloadJS() const;

	// OpenLevel (ex: Load de save) destrói os widgets do viewport e o
	// PlayerController dono — remonta o shell no mundo novo.
	void HandlePostLoadMap(UWorld* NewWorld);

	static const TCHAR* UIStateToJS(EJRPGUIState State);

	FDelegateHandle PostLoadMapHandle;

	// Loja atualmente aberta (row name da DT_Shops); None quando fechada
	FName CurrentShopID;

	UPROPERTY()
	UWebContainerWidget* WebContainerWidget;

	UPROPERTY()
	UWebUIBridge* Bridge;

	// Shell pronto para receber ExecuteJS (documento carregado + boot rodou)
	bool bShellReady;

	// Espelho C++ do estado do shell — decisões síncronas de input/foco
	EJRPGUIState CurrentState;

	// Comandos JS acumulados antes do shell ficar pronto (flush no onuiready)
	TArray<FString> PendingShellJS;

	void* DLLHandleCore;
	void* DLLHandleUl;
	void* DLLHandleWeb;
	void* DLLHandleApp;
};
