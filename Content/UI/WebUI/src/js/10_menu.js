// 10_menu.js - Controle do Menu de Pausa JRPG (Codex Áureo)
// No shell, o menu é a seção #screen-menu: abrir/fechar troca classes,
// nunca recarrega o documento. A ponte é acessada via getBridge() (00_core.js).

const menuItems = document.querySelectorAll('#screen-menu .menu-item');
// CONSULTA AO VIVO, nao um NodeList capturado: os cards de status sao
// gerados por JRPGSetParty() depois que este script rodou, e um NodeList
// estatico deixaria justamente eles de fora das animacoes de entrada e
// saida — o menu ficava travado meio aberto.
const menuCards = () => document.querySelectorAll('#screen-menu .animated-card');

// Timers da cascata de entrada. Precisam ser cancelaveis: fechar o menu no
// meio da cascata deixaria timers pendentes que re-adicionam as classes de
// entrada DEPOIS do fechamento, e os cards voltavam por cima da tela seguinte.
let staggerTimers = [];

function agendarEntrada(fn, ms) {
    staggerTimers.push(setTimeout(fn, ms));
}

function cancelarEntrada() {
    staggerTimers.forEach(clearTimeout);
    staggerTimers = [];
}

let selectedIndex = 0;
let isSelectionInitialized = false;
let isMenuClosing = false;

// Overlay de confirmação do Quit (voltar ao título perde o progresso não salvo)
const menuConfirmOverlay = document.getElementById('menu-confirm');
const menuConfirmOptions = document.querySelectorAll('#menu-confirm .confirm-option');
let isConfirmOpen = false;
let confirmChoice = 'no';

// Gravar só é permitido no Field. O C++ manda JRPGUI.setCanSave(...) ao abrir
// o menu (WorldStateSubsystem::IsInField). Default true para o modo de teste
// no browser, onde não há ponte para informar o contexto.
let canSave = true;

// --- MONITOR DE FPS DA RENDERIZAÇÃO DA WEBUI ---
// Ativo APENAS com o menu aberto: um rAF contínuo com o widget permanente
// forçaria a UL thread a renderizar a sessão inteira (damage a cada frame).
const fpsMonitor = (function () {
    let running = false;
    let lastTime = 0;
    let frameCount = 0;
    const fpsCounterEl = document.getElementById('fps-counter');

    function tickFPS() {
        if (!running) return;
        const now = performance.now();
        frameCount++;

        if (now >= lastTime + 1000) {
            const actualFPS = Math.round((frameCount * 1000) / (now - lastTime));
            if (fpsCounterEl) {
                fpsCounterEl.textContent = `UI FPS: ${actualFPS}`;
            }
            frameCount = 0;
            lastTime = now;
        }
        requestAnimationFrame(tickFPS);
    }

    return {
        start() {
            if (running) return;
            running = true;
            lastTime = performance.now();
            frameCount = 0;
            requestAnimationFrame(tickFPS);
        },
        stop() {
            running = false;
            if (fpsCounterEl) fpsCounterEl.textContent = 'FPS: --';
        }
    };
})();

// --- CONTROLE DE ANIMAÇÕES DE ENTRADA/SAÍDA ---

function triggerAnimateIn(silent) {
    log("Iniciando animações de entrada dos cards.");
    if (!silent) playSound('Open'); // Som de abertura

    // Revela o contêiner principal suavemente
    const layout = document.getElementById('menu-layout');
    if (layout) {
        layout.style.opacity = '1';
    }

    // Garante que iniciem ocultos antes da transição e força reflow
    menuCards().forEach(card => {
        card.classList.remove('animate-in');
        void card.offsetHeight; // Força reflow do layout na GPU
    });

    // Stagger de entrada otimizado e rápido com pequeno atraso inicial de render
    agendarEntrada(() => {
        const menuList = document.getElementById('card-menu-list');
        if (menuList) menuList.classList.add('animate-in');
    }, 50);

    agendarEntrada(() => {
        const goldTime = document.getElementById('card-gold-time');
        if (goldTime) goldTime.classList.add('animate-in');
    }, 90);

    // O card da party entra inteiro...
    agendarEntrada(() => {
        const cardParty = document.getElementById('card-party');
        if (cardParty) cardParty.classList.add('animate-in');
    }, 120);

    // ...e as linhas aparecem em cascata dentro dele.
    document.querySelectorAll('#party-rows .char-row').forEach((row, i) => {
        agendarEntrada(() => row.classList.add('row-in'), 260 + 70 * i);
    });
}

// ============================================================
// CARDS DE STATUS — gerados a partir da formação
//
// Antes eram três <div> fixos no HTML. Agora o C++ manda a formação em
// JRPGUI.setParty() no OpenMenu e os cards nascem dela: se só o Vahn está
// ativo, aparece um card só.
//
// O id de cada card continua "card-status-<key minúscula>" para a função
// updateCharacterStatus() continuar achando o card pelo mesmo caminho.
// ============================================================

function hpClassFor(pct) {
    if (pct >= 50) return { bar: 'hp-high', txt: 'hp-high-text' };
    if (pct >= 25) return { bar: 'hp-med', txt: 'hp-med-text' };
    return { bar: 'hp-low', txt: 'hp-low-text' };
}

function statusCardHTML(m) {
    const hpPct = (m.maxhp > 0) ? Math.max(0, Math.min(100, (m.hp / m.maxhp) * 100)) : 0;
    const mpPct = (m.maxmp > 0) ? Math.max(0, Math.min(100, (m.mp / m.maxmp) * 100)) : 0;
    const apPct = Math.max(0, Math.min(100, m.ap || 0));
    const hpCls = hpClassFor(hpPct);

    // LINHA, não card: quem é card agora é o #card-party que envolve todas.
    // O id continua card-status-<key> para o updateCharacterStatus() achar.
    return `<div id="card-status-${String(m.id).toLowerCase()}" class="char-row">
        <div class="char-layout">
            <div class="char-avatar-container">
                <img src="images/${m.portrait}.png" alt="" class="char-avatar"
                     onerror="this.style.visibility='hidden'">
            </div>
            <div class="char-info">
                <div class="char-header">
                    <span class="char-name">${m.name}</span>
                    <span class="char-level-container"><span class="char-level-label">LV</span><span class="char-level">${m.lv}</span></span>
                </div>

                <div class="char-bar-group">
                    <div class="char-bar-row">
                        <span class="bar-label hp-text">HP</span>
                        <div class="bar-track">
                            <div class="bar-fill hp ${hpCls.bar}" style="width: ${hpPct}%;"></div>
                        </div>
                        <span class="bar-values"><span class="cur ${hpCls.txt}">${m.hp}</span><span class="sep">/</span><span class="max">${m.maxhp}</span></span>
                    </div>

                    <div class="char-bar-row">
                        <span class="bar-label mp-text">MP</span>
                        <div class="bar-track">
                            <div class="bar-fill mp mp-high" style="width: ${mpPct}%;"></div>
                        </div>
                        <span class="bar-values"><span class="cur">${m.mp}</span><span class="sep">/</span><span class="max">${m.maxmp}</span></span>
                    </div>

                    <div class="char-bar-row">
                        <span class="bar-label ap-text">AP</span>
                        <div class="bar-track ap-track">
                            <div class="bar-fill ap" style="width: ${apPct}%;"></div>
                        </div>
                        <span class="bar-values ap-values-group"><span class="cur-ap">${m.ap}</span><span class="sep-ap">/</span><span class="max-ap">${m.maxap || 100}</span></span>
                    </div>
                </div>
            </div>
        </div>
    </div>`;
}

/**
 * Redesenha as linhas de status com a formação atual.
 * members: [{id,name,portrait,lv,hp,maxhp,mp,maxmp,ap,maxap}]
 *
 * Escreve DENTRO do #card-party — o card em si vem do HTML e nunca é
 * recriado, senão ele perderia a classe .animate-in no meio da animação.
 */
window.JRPGSetParty = function (members) {
    const box = document.getElementById('party-rows');
    if (!box) return;

    const lista = Array.isArray(members) ? members : [];
    box.innerHTML = lista.map(statusCardHTML).join('');
    log(`Menu: ${lista.length} linha(s) de status montada(s).`);
};

// --- POSICIONAMENTO DO CURSOR JRPG ---

function isItemEnabled(index) {
    const item = menuItems[index];
    return !!item && !item.classList.contains('disabled');
}

function updateSelection(index) {
    if (index < 0) index = menuItems.length - 1;
    if (index >= menuItems.length) index = 0;

    // Toca som de clique de cursor apenas na mudança ativa de item
    if (isSelectionInitialized && index !== selectedIndex) {
        playSound('Next');
    }
    isSelectionInitialized = true;

    selectedIndex = index;

    menuItems.forEach((item, idx) => {
        if (idx === selectedIndex) {
            item.classList.add('selected');
        } else {
            item.classList.remove('selected');
        }
    });
}

// Anda na lista pulando os itens desabilitados (Save fora do Field). Percorre
// no máximo uma volta: se TODOS estiverem desabilitados, o cursor fica onde
// está em vez de entrar em loop infinito.
function moveSelection(delta) {
    let index = selectedIndex;
    for (let step = 0; step < menuItems.length; step++) {
        index = (index + delta + menuItems.length) % menuItems.length;
        if (isItemEnabled(index)) {
            updateSelection(index);
            return;
        }
    }
}

// Aplica o estado de "pode gravar" na lista. Se o cursor estava justamente no
// Save quando ele foi travado, empurra a seleção para o próximo item válido.
function applyCanSave() {
    menuItems.forEach((item, idx) => {
        if (item.getAttribute('data-option') !== 'save') return;
        item.classList.toggle('disabled', !canSave);
        if (!canSave && idx === selectedIndex) {
            moveSelection(1);
        }
    });
}

// --- OVERLAY DE CONFIRMAÇÃO DO QUIT ---

function highlightConfirmChoice() {
    menuConfirmOptions.forEach(opt => {
        opt.classList.toggle('selected', opt.getAttribute('data-choice') === confirmChoice);
    });
}

function openQuitConfirm() {
    playSound('Select');
    isConfirmOpen = true;
    confirmChoice = 'no';
    highlightConfirmChoice();
    if (!menuConfirmOverlay) return;

    // flare-instant: o flare do Yes/No selecionado nasce no tamanho final.
    // Sem isso o `width: 0 -> 82%` roda ao sair de display:none e o flare
    // pisca como um ponto antes de virar faixa (mesma proteção do Options).
    const screen = document.getElementById('screen-menu');
    if (screen) {
        screen.classList.add('flare-instant');
        menuConfirmOverlay.classList.add('view-active');
        void menuConfirmOverlay.offsetHeight;
        setTimeout(() => screen.classList.remove('flare-instant'), 80);
        return;
    }
    menuConfirmOverlay.classList.add('view-active');
}

function resolveQuitConfirm(choice) {
    isConfirmOpen = false;
    if (menuConfirmOverlay) menuConfirmOverlay.classList.remove('view-active');

    if (choice !== 'yes') {
        playSound('Cancel');
        return;
    }

    // Some com os cards SEM devolver o input ao jogo (hideForOptions): o C++
    // é quem restaura o foco e faz o OpenLevel do main menu, nessa ordem.
    JRPGUI.hideForOptions(() => {
        UIState.set('hud_only');
        const bridge = getBridge();
        if (bridge && typeof bridge.onmenuoptionselected === 'function') {
            bridge.onmenuoptionselected('quit');
        } else {
            log('Ponte offline: quit para o main menu simulado no modo de teste web.');
        }
    });
}

// Registra clique nas opções
function selectOption(optionName) {
    log(`Opção selecionada: ${optionName}`);

    // Save travado fora do Field: recusa aqui também (o clique do mouse não
    // passa pela navegação que pula os itens desabilitados).
    if (optionName === 'save' && !canSave) {
        playSound('Cancel');
        log('Save recusado: fora do Field (setCanSave(false)).');
        return;
    }

    // Quit: confirma antes de largar a partida. O overlay é que chama a ponte.
    if (optionName === 'quit') {
        openQuitConfirm();
        return;
    }

    // Options, Save e Load: o menu apenas SOME (o estado continua 'menu_open')
    // e o C++ abre a tela por cima. Ao fechar, a tela chama
    // JRPGUI.reopenFromOptions() e o menu volta como estava.
    if (optionName === 'options' || optionName === 'save' || optionName === 'load'
        || optionName === 'party' || optionName === 'items') {
        JRPGUI.hideForOptions(() => {
            const bridge = getBridge();
            if (bridge && typeof bridge.onmenuoptionselected === "function") {
                bridge.onmenuoptionselected(optionName);
                return;
            }

            // Ponte offline (teste no Chrome): abre a tela direto
            log("Ponte offline: abrindo a tela localmente.");
            if (optionName === 'items' && window.JRPGItems) {
                JRPGItems.open({ gold: 0, items: [] });
            } else if (optionName === 'party' && window.JRPGParty) {
                JRPGParty.open({ max: 3, members: [] });
            } else if (optionName === 'options' && window.JRPGOptions) {
                JRPGOptions.open('menu');
            } else if (window.JRPGSaveLoad) {
                const opts = { returnTo: 'menu' };
                if (optionName === 'save') {
                    JRPGSaveLoad.openSave([], opts);
                } else {
                    JRPGSaveLoad.openLoad([], opts);
                }
            }
        });
        return;
    }

    const bridge = getBridge();
    if (bridge && typeof bridge.onmenuoptionselected === "function") {
        bridge.onmenuoptionselected(optionName);
    }
}

// --- API PÚBLICA DO MENU (chamada pelo C++ e pelo dispatcher de input) ---

window.JRPGUI = {
    openMenu() {
        if (!UIState.canOpen('menu')) {
            log(`openMenu ignorado: estado atual '${UIState.get()}' não permite abrir o menu.`);
            return;
        }

        // Garante a escala correta mesmo se a janela mudou com a UI escondida
        if (typeof updateUIScaleFactor === 'function') updateUIScaleFactor();

        const screen = document.getElementById('screen-menu');
        if (screen) screen.classList.add('active');
        UIState.set('menu_open');
        isMenuClosing = false;

        fpsMonitor.start();
        triggerAnimateIn();

        // Define a seleção inicial
        isSelectionInitialized = false;
        setTimeout(() => {
            updateSelection(0);
        }, 100);
    },

    closeMenu() {
        if (UIState.get() !== 'menu_open' || isMenuClosing) {
            return;
        }
        isMenuClosing = true;

        log("Iniciando animações de saída.");
        playSound('Close'); // Som de fechamento

        // Todos os cards saem (heróis para a direita, menu/gold para a esquerda)
        cancelarEntrada();
        menuCards().forEach(card => card.classList.remove('animate-in'));
        document.querySelectorAll('#party-rows .char-row')
            .forEach(row => row.classList.remove('row-in'));

        // Aguarda o término da animação antes de esconder a seção e devolver
        // o input ao jogo (350ms cobrem os 300ms de transição)
        setTimeout(() => {
            const layout = document.getElementById('menu-layout');
            if (layout) layout.style.opacity = '0';

            const screen = document.getElementById('screen-menu');
            if (screen) screen.classList.remove('active');

            fpsMonitor.stop();
            playtimeTicker.stop();
            UIState.set('hud_only');
            isMenuClosing = false;

            // Garante a apresentação do frame final (página vazia)
            if (typeof kickUIRepaint === 'function') kickUIRepaint();

            const bridge = getBridge();
            if (bridge && typeof bridge.closemenu === "function") {
                // Avisa o C++ para restaurar o input mode do jogo (o documento
                // permanece carregado — nada é descarregado aqui)
                bridge.closemenu();
            } else {
                log("Ponte offline: menu fechado no modo de teste web.");
            }
        }, 350);
    },

    // Esconde o menu SEM fechar (para a tela de opções ocupar a tela).
    // O estado continua 'menu_open' até o C++ trocar para 'options_open'.
    // Os cards saem animados e o callback só roda no fim, para a tela de
    // opções entrar depois da saída (transição encadeada, sem "pulo").
    hideForOptions(onDone) {
        if (isMenuClosing) return;
        // Trava o menu durante a transição: um Escape nesses 300ms cairia no
        // closeMenu e o menu fecharia por baixo da tela de opções.
        isMenuClosing = true;
        playSound('Select');

        // Mesma saída do closeMenu: os cards voam para fora
        cancelarEntrada();
        menuCards().forEach(card => card.classList.remove('animate-in'));
        document.querySelectorAll('#party-rows .char-row')
            .forEach(row => row.classList.remove('row-in'));

        setTimeout(() => {
            const layout = document.getElementById('menu-layout');
            if (layout) layout.style.opacity = '0';

            const screen = document.getElementById('screen-menu');
            if (screen) screen.classList.remove('active');

            fpsMonitor.stop();
            playtimeTicker.stop();

            if (typeof onDone === 'function') onDone();
        }, 300);
    },

    // Volta a mostrar o menu quando a tela de opções fecha: os cards reentram
    // com o mesmo stagger da abertura, mas sem tocar o som de abertura de novo
    reopenFromOptions() {
        if (typeof updateUIScaleFactor === 'function') updateUIScaleFactor();

        const screen = document.getElementById('screen-menu');
        if (screen) screen.classList.add('active');

        isMenuClosing = false;
        fpsMonitor.start();
        playtimeTicker.resume();
        triggerAnimateIn(true);

        if (typeof kickUIRepaint === 'function') kickUIRepaint();
    },

    // C++: libera ou trava a opção Save conforme WorldStateSubsystem::IsInField.
    // Chamado logo antes de JRPGUI.openMenu(), mas é seguro a qualquer momento.
    setCanSave(value) {
        canSave = !!value;
        applyCanSave();
    },

    // Entrada semântica com o menu aberto (teclado e gamepad convergem aqui)
    handleInput(action) {
        if (isMenuClosing) return;

        // O overlay de confirmação do Quit captura o input inteiro.
        // Yes/No ficam lado a lado: só left/right trocam a escolha.
        if (isConfirmOpen) {
            switch (action) {
                case 'left':
                case 'right':
                    playSound('Next');
                    confirmChoice = confirmChoice === 'yes' ? 'no' : 'yes';
                    highlightConfirmChoice();
                    break;
                case 'confirm':
                    resolveQuitConfirm(confirmChoice);
                    break;
                case 'cancel':
                    resolveQuitConfirm('no');
                    break;
                default:
                    break;
            }
            return;
        }

        switch (action) {
            case 'up':
                moveSelection(-1);
                break;
            case 'down':
                moveSelection(1);
                break;
            case 'confirm': {
                if (!isItemEnabled(selectedIndex)) {
                    playSound('Cancel');
                    break;
                }
                const activeOption = menuItems[selectedIndex].getAttribute('data-option');
                selectOption(activeOption);
                break;
            }
            case 'cancel':
                JRPGUI.closeMenu();
                break;
            default:
                break;
        }
    },

    // Limpa qualquer estado visual herdado (chamado pelo resetUIShell)
    reset() {
        cancelarEntrada();
        menuCards().forEach(card => card.classList.remove('animate-in'));
        document.querySelectorAll('#party-rows .char-row')
            .forEach(row => row.classList.remove('row-in'));
        const layout = document.getElementById('menu-layout');
        if (layout) layout.style.opacity = '0';
        fpsMonitor.stop();
        playtimeTicker.stop();
        isMenuClosing = false;
        isSelectionInitialized = false;
        selectedIndex = 0;

        isConfirmOpen = false;
        confirmChoice = 'no';
        if (menuConfirmOverlay) menuConfirmOverlay.classList.remove('view-active');

        // canSave volta ao default: o C++ manda o valor real no próximo openMenu
        canSave = true;
        applyCanSave();
    }
};

// --- INTERAÇÕES DO MOUSE ---

// O hover é puramente visual (CSS): quem move a seleção é o clique.
menuItems.forEach((item, index) => {
    item.addEventListener('click', () => {
        if (isConfirmOpen || !isItemEnabled(index)) return;
        updateSelection(index);
        selectOption(item.getAttribute('data-option'));
    });
});

menuConfirmOptions.forEach(opt => {
    opt.addEventListener('click', () => {
        if (!isConfirmOpen) return;
        resolveQuitConfirm(opt.getAttribute('data-choice'));
    });
});

// --- TECLADO JRPG (W/S, Setas, Enter, ESC) ---
// Traduz teclas para as mesmas ações semânticas do gamepad (handleUIInput),
// mantendo um único caminho de lógica — e testável em browser comum.

window.addEventListener('keydown', (event) => {
    if (UIState.get() !== 'menu_open') return;

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
        // Left/right não navegam a lista, mas trocam o Yes/No do overlay de Quit
        case 'ArrowLeft':
        case 'a':
        case 'A':
            handleUIInput('left');
            event.preventDefault();
            break;
        case 'ArrowRight':
        case 'd':
        case 'D':
            handleUIInput('right');
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

// --- GOLD & TIME (chamado pelo C++ com os dados do CoreSubsystem) ---
// O C++ envia o playtime em SEGUNDOS; o JS formata e mantém os segundos
// subindo ao vivo enquanto o menu está aberto. Usa requestAnimationFrame
// (mesmo mecanismo comprovado do fpsMonitor) — setInterval NÃO dispara de
// forma confiável dentro do Ultralight in-game.
const playtimeTicker = (function () {
    let running = false;
    let baseSeconds = 0;
    let anchorMs = 0;
    let lastShown = '';

    function format(total) {
        total = Math.max(0, Math.floor(total));
        const h = String(Math.floor(total / 3600)).padStart(2, '0');
        const m = String(Math.floor((total % 3600) / 60)).padStart(2, '0');
        const s = String(total % 60).padStart(2, '0');
        return `${h}:${m}:${s}`;
    }

    function tick() {
        if (!running) return;

        const el = document.getElementById('time-value');
        if (el) {
            const text = format(baseSeconds + (performance.now() - anchorMs) / 1000);
            // Só toca o DOM quando o segundo virar (evita damage por frame)
            if (text !== lastShown) {
                lastShown = text;
                el.textContent = text;
            }
        }
        requestAnimationFrame(tick);
    }

    return {
        start(seconds) {
            baseSeconds = Number(seconds) || 0;
            anchorMs = performance.now();
            lastShown = format(baseSeconds);

            const el = document.getElementById('time-value');
            if (el) el.textContent = lastShown;

            if (!running) {
                running = true;
                requestAnimationFrame(tick);
            }
        },
        // Retoma a contagem de onde parou (menu escondido pela tela de opções).
        // O total é sempre recalculado de baseSeconds + tempo desde a âncora,
        // então o relógio não "perde" o tempo em que ficou parado.
        resume() {
            if (running || anchorMs === 0) return;
            running = true;
            requestAnimationFrame(tick);
        },

        stop() {
            running = false;
        }
    };
})();

window.updateGoldTime = function (gold, playtimeSeconds) {
    const goldEl = document.getElementById('gold-value');
    if (goldEl) goldEl.textContent = String(gold);

    playtimeTicker.start(playtimeSeconds);
};

// --- FUNÇÃO DE INTEGRAÇÃO COM C++ (AUTOMÁTICA & DINÂMICA) ---
// Esta função pode ser chamada do Unreal para atualizar em tempo real a vida, mana, ap e nível do herói.
window.updateCharacterStatus = function (charId, level, hpCur, hpMax, mpCur, mpMax, apCur, apMax) {
    const card = document.getElementById(`card-status-${charId}`);
    if (!card) return;

    log(`Atualizando status de ${charId} -> LV: ${level}, HP: ${hpCur}/${hpMax}, MP: ${mpCur}/${mpMax}, AP: ${apCur}/${apMax}`);

    // 1. Atualizar Nível
    const lvlEl = card.querySelector('.char-level');
    if (lvlEl) lvlEl.textContent = level;

    // 2. Atualizar HP (Vida) e aplicar cores inteligentes
    const hpBar = card.querySelector('.bar-fill.hp');
    const hpCurEl = card.querySelector('.hp-text').parentNode.querySelector('.cur');
    const hpMaxEl = card.querySelector('.hp-text').parentNode.querySelector('.max');

    if (hpCurEl) hpCurEl.textContent = hpCur;
    if (hpMaxEl) hpMaxEl.textContent = hpMax;

    const hpPct = Math.max(0, Math.min(100, (hpCur / hpMax) * 100));
    if (hpBar) {
        hpBar.style.width = `${hpPct}%`;

        // Remove classes antigas
        hpBar.classList.remove('hp-high', 'hp-med', 'hp-low');
        if (hpCurEl) hpCurEl.classList.remove('hp-high-text', 'hp-med-text', 'hp-warn-text', 'hp-low-text');

        // Aplica cores com base no percentual
        if (hpPct >= 50) {
            hpBar.classList.add('hp-high');
            if (hpCurEl) hpCurEl.classList.add('hp-high-text');
        } else if (hpPct >= 20) {
            hpBar.classList.add('hp-med');
            if (hpCurEl) hpCurEl.classList.add('hp-med-text');
        } else if (hpPct >= 5) {
            hpBar.classList.add('hp-low');
            if (hpCurEl) hpCurEl.classList.add('hp-warn-text'); // Laranja
        } else {
            hpBar.classList.add('hp-low');
            if (hpCurEl) hpCurEl.classList.add('hp-low-text');  // Vermelho crítico (< 5%)
        }
    }

    // 3. Atualizar MP (Magia)
    const mpBar = card.querySelector('.bar-fill.mp');
    const mpCurEl = card.querySelector('.mp-text').parentNode.querySelector('.cur');
    const mpMaxEl = card.querySelector('.mp-text').parentNode.querySelector('.max');

    if (mpCurEl) mpCurEl.textContent = mpCur;
    if (mpMaxEl) mpMaxEl.textContent = mpMax;

    const mpPct = Math.max(0, Math.min(100, (mpCur / mpMax) * 100));
    if (mpBar) {
        mpBar.style.width = `${mpPct}%`;

        // Remove e aplica cores
        mpBar.classList.remove('mp-high', 'mp-med', 'mp-low');
        if (mpPct >= 50) {
            mpBar.classList.add('mp-high');
        } else if (mpPct >= 20) {
            mpBar.classList.add('mp-med');
        } else {
            mpBar.classList.add('mp-low');
        }
    }

    // 4. Atualizar AP (Pontos de Ação)
    const apBar = card.querySelector('.bar-fill.ap');
    const apCurEl = card.querySelector('.cur-ap');
    const apMaxEl = card.querySelector('.max-ap');

    if (apCurEl) apCurEl.textContent = apCur;
    if (apMaxEl) apMaxEl.textContent = apMax;

    const apPct = Math.max(0, Math.min(100, (apCur / apMax) * 100));
    if (apBar) {
        apBar.style.width = `${apPct}%`;
    }
};
