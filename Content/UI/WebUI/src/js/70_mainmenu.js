// 70_mainmenu.js - Main Menu (tela de título)
//
// A seção é transparente: a cena 3D do jogo fica visível por trás. As ações
// (New Game / Continue / Options / Quit) são delegadas ao C++ por
// bridge.onmainmenuaction(<ação>); os cards de apoio abrem o link no navegador
// do sistema por bridge.openurl(<url>).
//
// Continue e Options ESCONDEM o main menu antes de delegar (a tela de load e a
// de opções ocupam a tela inteira) e o estado JS continua 'main_menu' — quem
// reabre é JRPGMainMenu.reopenFromOptions(), chamado pela tela que fechou.
//
// A ponte é SEMPRE resolvida via getBridge() (00_core.js).

window.JRPGMainMenu = (function () {

    // --- Estado interno ---
    let selectedIndex = 0;
    let hasSaves = false;
    let isOpen = false;
    let isLeaving = false;

    // --- Referências de DOM ---
    const section = document.getElementById('screen-mainmenu');
    const layout = document.getElementById('mainmenu-layout');
    const items = document.querySelectorAll('#screen-mainmenu .mm-item');
    const animatedItems = document.querySelectorAll('#screen-mainmenu .animated-item');
    const supportLinks = document.querySelectorAll('#screen-mainmenu .mm-link');
    const continueItem = document.querySelector('#screen-mainmenu .mm-item[data-action="continue"]');

    // ============================================================
    // RENDERIZAÇÃO
    // ============================================================

    function isSelectable(index) {
        const item = items[index];
        return !!item && !item.classList.contains('mm-disabled');
    }

    function highlight(index, silent) {
        if (!items.length) return;

        if (!silent && index !== selectedIndex) playSound('Next');
        selectedIndex = index;

        items.forEach((item, idx) => {
            item.classList.toggle('selected', idx === selectedIndex);
        });
    }

    // Anda na lista pulando itens desabilitados (ex: Continue sem save)
    function move(step) {
        if (!items.length) return;

        let next = selectedIndex;
        for (let tries = 0; tries < items.length; tries++) {
            next = (next + step + items.length) % items.length;
            if (isSelectable(next)) {
                highlight(next);
                return;
            }
        }
    }

    function applySaveAvailability() {
        if (!continueItem) return;
        continueItem.classList.toggle('mm-disabled', !hasSaves);
    }

    // Mostra a seção com o flare do item selecionado JÁ no tamanho final.
    // Sem isso, o `width: 0 -> 62%` do flare roda ao sair de display:none e ele
    // pisca como um ponto circular (box-shadow de um elemento sem largura).
    function showSectionWithoutFlareFlash() {
        section.classList.add('flare-instant');
        section.classList.add('active');
        void section.offsetHeight; // aplica o estado final sem transição
        setTimeout(() => section.classList.remove('flare-instant'), 80);
    }

    // Entrada escalonada (mesmo padrão do triggerAnimateIn do menu de pausa)
    function triggerAnimateIn() {
        animatedItems.forEach(el => {
            el.classList.remove('animate-in');
            void el.offsetHeight; // força reflow para a transição rodar de novo
        });

        // O scrim do rodapé aparece primeiro (é o que dá legibilidade ao texto)
        const scrim = document.getElementById('mm-scrim-bottom');
        if (scrim) scrim.classList.add('animate-in');

        items.forEach((item, idx) => {
            setTimeout(() => item.classList.add('animate-in'), 60 + idx * 70);
        });

        const copyright = document.getElementById('mm-copyright');
        const support = document.getElementById('mm-support');
        setTimeout(() => { if (copyright) copyright.classList.add('animate-in'); }, 380);
        setTimeout(() => { if (support) support.classList.add('animate-in'); }, 460);
    }

    // ============================================================
    // AÇÕES
    // ============================================================

    function sendAction(action) {
        const bridge = getBridge();
        if (bridge && typeof bridge.onmainmenuaction === 'function') {
            bridge.onmainmenuaction(action);
            return true;
        }
        return false;
    }

    // Saída animada: os itens descem/somem e o layout inteiro faz fade.
    // É o que dá a sensação de transição (a tela seguinte entra depois disto).
    function triggerAnimateOut() {
        animatedItems.forEach(el => el.classList.remove('animate-in'));
        layout.style.opacity = '0';
    }

    // Continue / Options: esconde o main menu e delega — o estado JS continua
    // 'main_menu' até o C++ trocar (a sub-tela revalida com UIState.canOpen).
    function gotoSubscreen(action) {
        if (isLeaving) return;
        isLeaving = true;

        playSound('Select');
        triggerAnimateOut();

        setTimeout(() => {
            section.classList.remove('active');
            isLeaving = false;

            if (sendAction(action)) return;

            // Ponte offline (teste no Chrome): abre a sub-tela direto
            log(`Ponte offline: simulando '${action}' no modo de teste web.`);
            if (action === 'options' && window.JRPGOptions) {
                JRPGOptions.open('mainmenu');
            } else if (action === 'continue' && window.JRPGSaveLoad) {
                JRPGSaveLoad.open([], { mode: 'load', returnTo: 'mainmenu' });
            }
        }, 320);
    }

    // New Game / Quit: saem do main menu de vez (o C++ devolve o input ao jogo
    // antes do OpenLevel / QuitGame)
    function leaveMenu(action) {
        if (isLeaving) return;
        isLeaving = true;

        playSound('Select');
        triggerAnimateOut();

        setTimeout(() => {
            section.classList.remove('active');
            isOpen = false;
            isLeaving = false;
            UIState.set('hud_only');

            if (typeof kickUIRepaint === 'function') kickUIRepaint();

            if (!sendAction(action)) {
                log(`Ponte offline: '${action}' apenas registrado no modo de teste web.`);
            }
        }, 320);
    }

    function confirmSelection() {
        const item = items[selectedIndex];
        if (!item) return;

        const action = item.getAttribute('data-action');

        if (item.classList.contains('mm-disabled')) {
            playSound('Cancel');
            log(`Main menu: '${action}' indisponível (nenhum save encontrado).`);
            return;
        }

        if (action === 'continue' || action === 'options') {
            gotoSubscreen(action);
        } else {
            leaveMenu(action);
        }
    }

    function openExternalURL(url) {
        if (!url) return;
        playSound('Select');

        const bridge = getBridge();
        if (bridge && typeof bridge.openurl === 'function') {
            bridge.openurl(url);
        } else {
            log(`Ponte offline: abriria ${url} no navegador do sistema.`);
        }
    }

    // ============================================================
    // API PÚBLICA (chamada pelo C++ e pelo dispatcher de input)
    // ============================================================

    const api = {
        /** C++: abre a tela de título. payload = { hasSaves, version, build }. */
        open(payload) {
            if (!UIState.canOpen('mainmenu')) {
                log(`JRPGMainMenu.open ignorado: estado atual '${UIState.get()}' não permite abrir o main menu.`);
                return;
            }

            // Garante a escala correta mesmo se a janela mudou com a UI escondida
            if (typeof updateUIScaleFactor === 'function') updateUIScaleFactor();

            const data = payload || {};
            hasSaves = !!data.hasSaves;
            if (data.version || data.build) {
                window.setBuildInfo(data.version, data.build);
            }

            isOpen = true;
            isLeaving = false;

            showSectionWithoutFlareFlash();
            UIState.set('main_menu');
            applySaveAvailability();

            // Começa no primeiro item selecionável
            selectedIndex = 0;
            if (!isSelectable(0)) {
                move(1);
            } else {
                highlight(0, true);
            }

            playSound('Open');
            layout.style.opacity = '1';
            triggerAnimateIn();

            // Mesma proteção de frame usada nas outras telas
            if (typeof kickUIRepaint === 'function') kickUIRepaint();
        },

        /**
         * Reabre o main menu quando a tela de opções/load fecha. Reentra com o
         * mesmo stagger da abertura (os itens saíram animados) — sem som de
         * abertura e mantendo o item que estava selecionado.
         */
        reopenFromOptions() {
            if (typeof updateUIScaleFactor === 'function') updateUIScaleFactor();

            isOpen = true;
            isLeaving = false;
            showSectionWithoutFlareFlash();
            layout.style.opacity = '1';
            applySaveAvailability();
            highlight(selectedIndex, true);
            triggerAnimateIn();

            if (typeof kickUIRepaint === 'function') kickUIRepaint();
        },

        /** Entrada semântica (teclado e gamepad convergem aqui). */
        handleInput(action) {
            if (!isOpen || isLeaving) return;

            switch (action) {
                case 'up':
                    move(-1);
                    break;
                case 'down':
                    move(1);
                    break;
                case 'confirm':
                    confirmSelection();
                    break;
                case 'cancel':
                    // O main menu não fecha com Cancel/Escape (é a tela raiz)
                    break;
                default:
                    break;
            }
        },

        /** Limpa estado herdado (chamado pelo resetUIShell entre sessões PIE). */
        reset() {
            isOpen = false;
            isLeaving = false;
            hasSaves = false;
            selectedIndex = 0;
            layout.style.opacity = '0';
            section.classList.remove('active');
            section.classList.remove('flare-instant');
            animatedItems.forEach(el => el.classList.remove('animate-in'));
            items.forEach((item, idx) => item.classList.toggle('selected', idx === 0));
            if (continueItem) continueItem.classList.remove('mm-disabled');
        }
    };

    // --- Mouse ---

    // O hover é puramente visual (CSS): quem move a seleção é o clique.
    items.forEach((item, index) => {
        item.addEventListener('click', () => {
            if (!isOpen || isLeaving) return;
            if (isSelectable(index)) highlight(index, true);
            confirmSelection();
        });
    });

    supportLinks.forEach(link => {
        link.addEventListener('click', () => {
            if (!isOpen || isLeaving) return;
            openExternalURL(link.getAttribute('data-url'));
        });
    });

    // --- Teclado local (mesmas ações semânticas — testável em browser comum) ---

    window.addEventListener('keydown', (event) => {
        if (UIState.get() !== 'main_menu') return;

        switch (event.key) {
            case 'ArrowUp':
            case 'w':
            case 'W':
                handleUIInput('up');
                event.preventDefault();
                break;
            case 'ArrowDown':
            case 's':
            case 'S':
                handleUIInput('down');
                event.preventDefault();
                break;
            case 'Enter':
            case ' ':
                handleUIInput('confirm');
                event.preventDefault();
                break;
            case 'Escape':
                handleUIInput('cancel');
                event.preventDefault();
                break;
        }
    });

    return api;
})();

// CONTRATO COM O C++: atualiza versão/build mostrados no rodapé do main menu.
// Os valores default ficam escritos no HTML da seção.
window.setBuildInfo = function (version, build) {
    const versionEl = document.getElementById('mm-version');
    const buildEl = document.getElementById('mm-build');
    if (versionEl && version) versionEl.textContent = version;
    if (buildEl && build) buildEl.textContent = build;
};
