// 05_ui_state.js - Máquina de estados global do Shell
// Estados: 'hud_only' | 'menu_open' | 'dialogue_active' | 'options_open' |
//          'saveload_open' | 'shop_open' | 'main_menu' | 'dev_open' |
//          'party_open' | 'items_open'
// O popup de item NÃO é estado: é um overlay passivo que pode aparecer sempre.

window.UIState = (function () {
    let current = 'hud_only';

    // O que pode abrir a partir de cada estado atual
    const openRules = {
        menu:     ['hud_only'],
        dialogue: ['hud_only'],
        // Opções abrem do jogo, de dentro do menu de pausa e do main menu —
        // e voltam para a tela de origem ao fechar (ver 40_options.js).
        options:  ['hud_only', 'menu_open', 'main_menu'],
        // estátua de save (hud_only), Save/Load do menu de pausa (menu_open) e
        // Continue do main menu (main_menu)
        saveload: ['hud_only', 'menu_open', 'main_menu'],
        shop:     ['hud_only'],              // loja (NPC lojista)
        mainmenu: ['hud_only'],              // tela de título (mapa de main menu)
        dev:      ['hud_only'],              // DEV MENU — ferramenta de teste
        // Formação da party: só do menu de pausa, e volta para ele.
        party:    ['menu_open'],
        // Inventário: idem.
        items:    ['menu_open']
    };

    return {
        get() {
            return current;
        },
        set(state) {
            if (current === state) return;
            log(`UIState: ${current} -> ${state}`);
            current = state;
        },
        canOpen(target) {
            const allowed = openRules[target];
            return !!allowed && allowed.indexOf(current) !== -1;
        }
    };
})();

// Reset total do shell — chamado no boot e pelo C++ no início de cada sessão
// PIE (o documento sobrevive entre sessões, então estado JS herdado precisa
// ser limpo). Deve ser idempotente.
window.resetUIShell = function () {
    document.querySelectorAll('.ui-section').forEach(section => {
        section.classList.remove('active');
    });
    if (window.JRPGPopup) JRPGPopup.reset();
    if (window.JRPGUI) JRPGUI.reset();
    if (window.JRPGOptions) JRPGOptions.reset();
    if (window.JRPGSaveLoad) JRPGSaveLoad.reset();
    if (window.JRPGShop) JRPGShop.reset();
    if (window.JRPGMainMenu) JRPGMainMenu.reset();
    if (window.JRPGDev) JRPGDev.reset();
    if (window.JRPGParty) JRPGParty.reset();
    if (window.JRPGItems) JRPGItems.reset();
    if (window.InputMode) InputMode.reset();
    UIState.set('hud_only');
    log("Shell resetado (hud_only).");
};

// Entrada semântica unificada (teclado E gamepad chegam aqui).
// O C++ traduz botões de gamepad para estas ações; o teclado local também.
// Ações: 'up' | 'down' | 'left' | 'right' | 'confirm' | 'cancel'
window.handleUIInput = function (action) {
    // Qualquer ação semântica vem de teclado ou gamepad: esconde o ponteiro
    if (window.InputMode) InputMode.useKeyboard();

    switch (UIState.get()) {
        case 'menu_open':
            if (window.JRPGUI) JRPGUI.handleInput(action);
            break;
        case 'dialogue_active':
            // Futuro: navegação das opções de diálogo
            break;
        case 'options_open':
            if (window.JRPGOptions) JRPGOptions.handleInput(action);
            break;
        case 'main_menu':
            if (window.JRPGMainMenu) JRPGMainMenu.handleInput(action);
            break;
        case 'saveload_open':
            if (window.JRPGSaveLoad) JRPGSaveLoad.handleInput(action);
            break;
        case 'shop_open':
            if (window.JRPGShop) JRPGShop.handleInput(action);
            break;
        case 'dev_open':
            if (window.JRPGDev) JRPGDev.handleInput(action);
            break;
        case 'party_open':
            if (window.JRPGParty) JRPGParty.handleInput(action);
            break;
        case 'items_open':
            if (window.JRPGItems) JRPGItems.handleInput(action);
            break;
        default:
            break;
    }
};

// ============================================================
// MODO DE INPUT (mouse x teclado/gamepad)
//
// Regra da UI: o mouse SÓ destaca no hover e seleciona no clique — passar o
// ponteiro por cima nunca move o cursor de navegação. E quando o jogador usa
// teclado ou controle, o ponteiro some até ele mexer no mouse de novo.
//
// O cursor visível é do PlayerController (bShowMouseCursor), desenhado pelo
// Slate POR CIMA da textura do Ultralight: `cursor: none` no CSS não daria
// conta sozinho. Por isso a ponte setcursorvisible.
//
// No lado CSS, a classe .kbd-mode no <html> desliga cursor e pointer-events
// de uma vez — assim o item sob o ponteiro parado não fica destacado enquanto
// se navega pelo teclado. O mousemove continua chegando porque
// `pointer-events: none` só tira o elemento do hit-test; o evento segue para o
// document e o listener de window recebe.
// ============================================================

window.InputMode = (function () {
    let usingMouse = true;

    function apply(mouse) {
        if (usingMouse === mouse) return;
        usingMouse = mouse;
        document.documentElement.classList.toggle('kbd-mode', !mouse);

        const bridge = getBridge();
        if (bridge && typeof bridge.setcursorvisible === 'function') {
            bridge.setcursorvisible(mouse);
        }
    }

    window.addEventListener('mousemove', () => apply(true));
    window.addEventListener('mousedown', () => apply(true));
    window.addEventListener('wheel', () => apply(true));

    return {
        useKeyboard() { apply(false); },
        useMouse() { apply(true); },
        isMouse() { return usingMouse; },
        /** Volta ao padrão (mouse) — chamado pelo resetUIShell. */
        reset() {
            usingMouse = true;
            document.documentElement.classList.remove('kbd-mode');
        }
    };
})();
