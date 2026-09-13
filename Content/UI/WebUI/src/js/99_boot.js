// 99_boot.js - Boot do Shell (último script a rodar)

// CONTRATO COM O C++: SUltralightBrowser::OnKeyDown injeta exatamente
// "triggerAnimateOutAndClose();" quando Escape é pressionado com a UI focada.
// No shell, essa função vira um dispatcher contextual por estado.
window.triggerAnimateOutAndClose = function () {
    switch (UIState.get()) {
        case 'menu_open':
            JRPGUI.closeMenu();
            break;
        case 'saveload_open':
            // Escape contextual: confirm -> grid -> escolha -> fecha a tela
            JRPGSaveLoad.handleInput('cancel');
            break;
        case 'shop_open':
            // Escape contextual: quantidade -> lista -> escolha -> fecha
            JRPGShop.handleInput('cancel');
            break;
        case 'options_open':
            // Escape contextual: lista -> abas -> fecha (voltando à origem:
            // menu de pausa, main menu ou jogo)
            JRPGOptions.handleInput('cancel');
            break;
        case 'dev_open':
            JRPGDev.close();
            break;
        case 'party_open':
            // Volta para o menu de pausa, como Options/Save/Load.
            JRPGParty.close();
            break;
        case 'items_open':
            // Escape contextual: fecha o confirm de descarte antes da tela.
            JRPGItems.handleInput('cancel');
            break;
        case 'main_menu':
            // Tela raiz: Escape não fecha o main menu
            break;
        case 'dialogue_active':
            // Diálogo não fecha com Escape (decisão futura)
            break;
        default:
            // hud_only: nada a fechar. Failsafe: se uma tela ficou visível
            // com o estado dessincronizado, força o fechamento.
            if (window.JRPGSaveLoad) JRPGSaveLoad.forceCloseIfVisible();
            if (window.JRPGOptions) JRPGOptions.forceCloseIfVisible();
            if (window.JRPGDev) JRPGDev.forceCloseIfVisible();
            if (window.JRPGParty) JRPGParty.forceCloseIfVisible();
            if (window.JRPGItems) JRPGItems.forceCloseIfVisible();
            break;
    }
};

// Inicialização: estado limpo + avisa o C++ que o shell está pronto.
// O bridge já existe aqui (BindNativeFunctions roda em OnWindowObjectReady,
// antes dos scripts da página executarem).
(function bootShell() {
    resetUIShell();

    const bridge = getBridge();
    if (bridge && typeof bridge.onuiready === "function") {
        bridge.onuiready();
        log("Shell carregado e pronto (onuiready enviado ao C++).");
    } else {
        log("Shell carregado em modo de teste web (ponte offline).");
    }
})();
