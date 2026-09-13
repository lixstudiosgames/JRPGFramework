// 50_saveload.js - Tela de Save/Load (estátua de save)
//
// Fluxo: o C++ chama JRPGSaveLoad.open(slots) e a tela JÁ ABRE no grid 5x3.
// Não existe mais a tela intermediária de escolha Save/Load: as duas viraram
// ABAS no topo, trocadas por Q/E (teclado) e LB/RB (controle) — as mesmas
// ações semânticas 'tab_prev'/'tab_next' da tela de Options.
//
// Três portas de entrada, todas caindo no grid:
//   JRPGSaveLoad.open(slots)      -> aba Save (padrão)
//   JRPGSaveLoad.openSave(slots)  -> aba Save
//   JRPGSaveLoad.openLoad(slots)  -> aba Load
// As duas últimas existem para o menu de pausa ter Save e Load separados e
// cada um cair direto na aba certa (o jogador ainda pode trocar com Q/E).
//
// opts (2º argumento, opcional):
//   mode:     'save' | 'load'  — aba inicial (openSave/openLoad já preenchem)
//   returnTo: 'mainmenu'       — ao cancelar volta ao título em vez do jogo
//             'menu'           — ao cancelar volta ao menu de pausa
//   lockTab:  true | false     — trava a aba (default: true se returnTo é
//                                'mainmenu', porque salvar no título não faz
//                                sentido; false no resto)
//
// A ponte é SEMPRE resolvida via getBridge() (00_core.js).

window.JRPGSaveLoad = (function () {
    const SLOT_COUNT = 15;
    const GRID_COLS = 5;

    // Índice da aba <-> modo. A ordem casa com a ordem no HTML.
    const TABS = ['save', 'load'];

    // Arte fixa de cada quadradinho do grid: images/1.webp .. images/15.webp,
    // na mesma ordem do grid (slot 0 = 1.webp). É a identidade visual do slot,
    // não um screenshot do save — por isso não vem do payload do C++.
    function slotArt(i) {
        return `images/${i + 1}.webp`;
    }

    // Retratos placeholder da party até o PartySubsystem existir.
    // TODO: quando o save tiver party real, usar images/<CharacterID>.png.
    const PLACEHOLDER_PARTY = ['vahn', 'noa', 'gala'];

    // Onda de entrada dos slots: cada linha entra depois da anterior e, dentro
    // da linha, cada coluna atrasa um pouco mais (efeito de subida em cascata).
    const RISE_ROW_DELAY = 60;   // ms por linha
    const RISE_COL_DELAY = 35;   // ms por coluna

    // --- Estado interno ---
    let slotsData = [];              // payload vindo do C++ (15 entradas)
    let view = null;                 // 'grid' | 'confirm' | null (fechado)
    let tabIndex = 0;                // 0 = Save, 1 = Load
    let selectedSlot = 0;            // 0..14
    let confirmChoice = 'no';        // 'yes' | 'no'
    let pendingSlot = -1;            // slot aguardando confirmação
    let pendingAction = null;        // 'save' (overwrite) | 'load'
    let isClosing = false;
    let lockTab = false;             // true = aba fixa (Continue do main menu)
    let returnTo = null;             // null = jogo (closemenu) | 'mainmenu'

    // --- Referências de DOM ---
    const section = document.getElementById('screen-saveload');
    const layout = document.getElementById('saveload-layout');
    const gridView = document.getElementById('saveload-grid-view');
    const gridEl = document.getElementById('saveload-grid');
    const tabBar = document.getElementById('saveload-tabbar');
    const detailCard = document.getElementById('saveload-detail');
    const confirmOverlay = document.getElementById('saveload-confirm');
    const tabItems = document.querySelectorAll('#saveload-tabs .jrpg-tab');
    const arrowPrev = document.querySelector('#saveload-tabbar .arrow-left');
    const arrowNext = document.querySelector('#saveload-tabbar .arrow-right');
    const confirmOptions = document.querySelectorAll('#saveload-confirm .confirm-option');
    const confirmText = document.getElementById('saveload-confirm-text');

    function getSlot(i) {
        return slotsData[i] || { i: i, used: false };
    }

    function currentMode() {
        return TABS[tabIndex];
    }

    // ============================================================
    // RENDERIZAÇÃO
    // ============================================================

    // animate = true só ao ENTRAR na tela. O rebuild depois de um save passa
    // false, senão o grid inteiro reanimaria a cada gravação.
    function buildGrid(animate) {
        gridEl.innerHTML = '';
        for (let i = 0; i < SLOT_COUNT; i++) {
            const slot = getSlot(i);
            const el = document.createElement('div');
            el.className = 'save-slot'
                + (slot.used ? '' : ' slot-empty')
                + (animate ? ' slot-rise' : '');
            el.setAttribute('data-slot', String(i));

            if (animate) {
                const row = Math.floor(i / GRID_COLS);
                const col = i % GRID_COLS;
                el.style.animationDelay = (row * RISE_ROW_DELAY + col * RISE_COL_DELAY) + 'ms';
            }

            el.innerHTML =
                `<img class="slot-thumb" src="${slotArt(i)}" alt="">` +
                `<div class="slot-veil"></div>` +
                `<div class="slot-empty-tag">NO DATA</div>`;

            // Hover só destaca (CSS); o clique é que move e confirma.
            el.addEventListener('click', () => {
                if (view === 'grid') {
                    selectSlot(i);
                    confirmSlot();
                }
            });

            gridEl.appendChild(el);
        }
        highlightSlot();
    }

    function highlightSlot() {
        const slots = gridEl.querySelectorAll('.save-slot');
        slots.forEach((el, idx) => {
            el.classList.toggle('selected', idx === selectedSlot);
        });
        updateDetail();
    }

    function highlightTab() {
        tabItems.forEach((item, idx) => {
            item.classList.toggle('selected', idx === tabIndex);
        });
        tabBar.classList.toggle('tabs-locked', lockTab);
    }

    function updateDetail() {
        const slot = getSlot(selectedSlot);
        if (!slot.used) {
            detailCard.classList.add('detail-empty');
            return;
        }
        detailCard.classList.remove('detail-empty');

        document.getElementById('detail-map').textContent = slot.map || '???';
        document.getElementById('detail-date').textContent = slot.date || '--';
        document.getElementById('detail-time').textContent = slot.time || '--:--:--';
        document.getElementById('detail-gold').textContent = (typeof slot.gold === 'number') ? slot.gold : '--';

        // Party: dados reais quando existirem; senão placeholders "--"
        const partyEl = document.getElementById('detail-party');
        partyEl.innerHTML = '';
        const party = (slot.party && slot.party.length > 0)
            ? slot.party
            : PLACEHOLDER_PARTY.map(id => ({ id: id, lv: null }));

        party.forEach(member => {
            const hasStats = (member.lv !== null && member.lv !== undefined);
            const lv = hasStats ? member.lv : '--';
            const hp = hasStats ? `${member.hp}/${member.maxhp}` : '--/--';
            const mp = hasStats ? `${member.mp}/${member.maxmp}` : '--/--';
            const ap = hasStats ? `${member.ap}/${member.maxap}` : '--/--';

            const div = document.createElement('div');
            div.className = 'detail-member';
            div.innerHTML =
                `<div class="detail-member-portrait"><img src="images/${member.id}.png" alt=""></div>` +
                `<div class="detail-member-stats">` +
                `<span class="detail-member-lv">LV ${lv}</span>` +
                `<span class="detail-member-hp">HP ${hp}</span>` +
                `<span class="detail-member-mp">MP ${mp}</span>` +
                `<span class="detail-member-ap">AP ${ap}</span>` +
                `</div>`;
            partyEl.appendChild(div);
        });
    }

    function showView(newView) {
        view = newView;
        // O grid continua visível por baixo do overlay de confirmação
        gridView.classList.toggle('view-active', newView === 'grid' || newView === 'confirm');
        confirmOverlay.classList.toggle('view-active', newView === 'confirm');
    }

    function highlightConfirmChoice() {
        confirmOptions.forEach(opt => {
            opt.classList.toggle('selected', opt.getAttribute('data-choice') === confirmChoice);
        });
    }

    // Feedback visual da setinha ao trocar de aba (elas não são clicáveis —
    // só indicam que Q/E e LB/RB trocam de aba). Mesmo truque de Options.
    function kickArrow(el) {
        if (!el) return;
        el.classList.remove('arrow-kick');
        void el.offsetHeight;
        el.classList.add('arrow-kick');
    }

    // ============================================================
    // AÇÕES
    // ============================================================

    // O grid é o MESMO nas duas abas (15 slots): trocar de aba só muda o que
    // o confirm faz (gravar x carregar), então não remonta nem reanima nada.
    function setTab(index, silent) {
        if (lockTab) {
            if (!silent) playSound('Cancel');
            return;
        }

        const previous = tabIndex;
        if (index < 0) index = TABS.length - 1;
        if (index >= TABS.length) index = 0;
        if (index === tabIndex) return;

        tabIndex = index;
        highlightTab();

        if (!silent) {
            playSound('Select');
            // Trocou pra trás (inclui o wrap do primeiro para o último)?
            const wentBack = (index === TABS.length - 1 && previous === 0) ||
                             (index < previous && !(index === 0 && previous === TABS.length - 1));
            kickArrow(wentBack ? arrowPrev : arrowNext);
        }
    }

    function selectSlot(i) {
        if (i === selectedSlot) return;
        playSound('Next');
        selectedSlot = i;
        highlightSlot();
    }

    // Abre o overlay Yes/No para 'save' (sobrescrever) ou 'load' (carregar).
    function askConfirm(action, slotIndex) {
        playSound('Select');
        pendingAction = action;
        pendingSlot = slotIndex;
        confirmChoice = 'no';
        confirmText.textContent = (action === 'load')
            ? 'Load this record?'
            : 'Overwrite this record?';
        highlightConfirmChoice();

        // flare-instant: o flare do Yes/No selecionado nasce no tamanho final
        // (sem isso pisca como um ponto ao sair de display:none). Mesma
        // proteção do Options.
        section.classList.add('flare-instant');
        showView('confirm');
        void confirmOverlay.offsetHeight;
        setTimeout(() => section.classList.remove('flare-instant'), 80);
    }

    function confirmSlot() {
        const slot = getSlot(selectedSlot);

        if (currentMode() === 'save') {
            if (slot.used) {
                // Slot ocupado: confirma antes de sobrescrever (default No)
                askConfirm('save', selectedSlot);
            } else {
                requestSave(selectedSlot);
            }
            return;
        }

        // aba Load: carregar descarta o progresso atual, então também confirma
        if (!slot.used) {
            playSound('Cancel');
            return;
        }
        askConfirm('load', selectedSlot);
    }

    function requestLoad(slotIndex) {
        const bridge = getBridge();
        log(`Load do slot ${slotIndex} — fechando a tela e delegando ao C++.`);

        // Esconde a seção IMEDIATAMENTE e delega ao C++: onloadslot restaura o
        // input do jogo e faz o OpenLevel (NÃO chamar bridge.closemenu aqui).
        hideSection();
        if (bridge && typeof bridge.onloadslot === 'function') {
            bridge.onloadslot(slotIndex);
        } else {
            log('Ponte offline: load simulado no modo de teste web.');
        }
    }

    function requestSave(slotIndex) {
        const bridge = getBridge();
        log(`Save no slot ${slotIndex} solicitado.`);
        if (bridge && typeof bridge.onsaveslot === 'function') {
            bridge.onsaveslot(slotIndex);
        } else {
            log('Ponte offline: save simulado no modo de teste web.');
            slotsData[slotIndex] = {
                i: slotIndex, used: true, map: 'Test Map', time: '00:12:34',
                date: '2026-01-01 12:00', gold: 999, party: []
            };
            api.updateSlots(slotsData);
            api.onSaveResult(slotIndex, true);
        }
    }

    function resolveConfirm(choice) {
        const action = pendingAction;
        const slotIndex = pendingSlot;
        pendingAction = null;
        pendingSlot = -1;

        if (choice !== 'yes' || slotIndex < 0) {
            playSound('Cancel');
            showView('grid');
            return;
        }

        showView('grid');
        if (action === 'load') {
            requestLoad(slotIndex);
        } else {
            requestSave(slotIndex);
        }
    }

    function hideSection() {
        layout.style.opacity = '0';
        section.classList.remove('active');
        showView(null);
        view = null;
        UIState.set('hud_only');

        // Garante a apresentação do frame final (página vazia)
        if (typeof kickUIRepaint === 'function') kickUIRepaint();
    }

    // O header e o card de detalhes são DOM permanente: sem reiniciar a
    // classe, a animação só rodaria na primeira abertura. Ordem obrigatória:
    // showView ANTES (offsetHeight de um elemento display:none é 0 e não
    // força o reflow que reinicia a animação).
    function restartEnterAnimation() {
        gridView.classList.remove('sl-enter');
        void gridView.offsetHeight;
        gridView.classList.add('sl-enter');
    }

    // ============================================================
    // API PÚBLICA (chamada pelo C++ e pelo dispatcher de input)
    // ============================================================

    const api = {
        // C++: abre a tela JÁ NO GRID com os metadados dos 15 slots.
        // opts (opcional): { mode, returnTo, lockTab } — ver cabeçalho.
        open(slots, opts) {
            if (!UIState.canOpen('saveload')) {
                log(`JRPGSaveLoad.open ignorado: estado atual '${UIState.get()}' não permite abrir.`);
                return;
            }

            // Garante a escala correta mesmo se a janela mudou com a UI escondida
            if (typeof updateUIScaleFactor === 'function') updateUIScaleFactor();

            const options = opts || {};
            slotsData = slots || [];
            isClosing = false;
            selectedSlot = 0;
            pendingSlot = -1;
            pendingAction = null;
            returnTo = options.returnTo || null;

            const wanted = TABS.indexOf(options.mode);
            tabIndex = (wanted >= 0) ? wanted : 0;

            // Trava só quando explicitamente pedido ou no Continue do título
            // (salvar do main menu não faz sentido: não há partida em curso).
            lockTab = (typeof options.lockTab === 'boolean')
                ? options.lockTab
                : (returnTo === 'mainmenu');

            // flare-instant: o flare da aba selecionada nasce no tamanho final.
            // Sem isso o `width: 0 -> 82%` roda ao sair de display:none e o
            // flare pisca como um ponto antes de virar faixa.
            section.classList.add('flare-instant');
            section.classList.add('active');
            void section.offsetHeight;
            setTimeout(() => section.classList.remove('flare-instant'), 80);

            UIState.set('saveload_open');

            highlightTab();
            buildGrid(true);
            showView('grid');
            restartEnterAnimation();

            playSound('Open');
            layout.style.opacity = '1';

            // Garante a apresentação dos primeiros frames (mesma proteção do
            // fechamento — abrir de página quase-vazia pode perder o frame)
            if (typeof kickUIRepaint === 'function') kickUIRepaint();
        },

        // Atalhos para o menu de pausa ter Save e Load separados, cada um
        // caindo direto na aba certa (Q/E continuam trocando).
        openSave(slots, opts) {
            api.open(slots, withMode(opts, 'save'));
        },

        openLoad(slots, opts) {
            api.open(slots, withMode(opts, 'load'));
        },

        // C++: re-renderiza o grid após um save (slot novo aparece ocupado)
        updateSlots(slots) {
            slotsData = slots || slotsData;
            if (view === 'grid' || view === 'confirm') {
                buildGrid(false);
            }
        },

        // C++: resultado do save (feedback sonoro + flash no slot)
        onSaveResult(slotIndex, ok) {
            if (ok) {
                playSound('Item');
                const el = gridEl.querySelector(`.save-slot[data-slot="${slotIndex}"]`);
                if (el) {
                    el.classList.remove('slot-saved-flash');
                    void el.offsetHeight; // reinicia a animação
                    el.classList.add('slot-saved-flash');
                }
            } else {
                playSound('Cancel');
                log(`Save no slot ${slotIndex} FALHOU (ver log do Unreal).`);
            }
        },

        // Fecha a tela devolvendo o input ao jogo (caminho padrão via closemenu).
        // Mesmo padrão do menu: fade-out e só então esconde a seção e chama
        // bridge.closemenu() dentro de um setTimeout.
        close() {
            if (view === null || isClosing) return;
            isClosing = true;

            playSound('Close');
            layout.style.opacity = '0';

            setTimeout(() => {
                section.classList.remove('active');
                section.classList.remove('flare-instant');
                showView(null);
                isClosing = false;

                // Continue do main menu: volta pro título em vez de devolver o
                // input ao jogo (o C++ mantém o foco na UI)
                if (returnTo === 'mainmenu' && window.JRPGMainMenu) {
                    UIState.set('main_menu');

                    const uiBridge = getBridge();
                    if (uiBridge && typeof uiBridge.onuistatechanged === 'function') {
                        uiBridge.onuistatechanged('main_menu');
                    }
                    JRPGMainMenu.reopenFromOptions();
                    return;
                }

                // Save/Load abertos pelo menu de pausa: os cards do menu voltam
                // no lugar de devolver o input ao jogo (mesmo caminho de Options)
                if (returnTo === 'menu' && window.JRPGUI) {
                    UIState.set('menu_open');

                    const menuBridge = getBridge();
                    if (menuBridge && typeof menuBridge.onuistatechanged === 'function') {
                        menuBridge.onuistatechanged('menu_open');
                    }
                    JRPGUI.reopenFromOptions();
                    return;
                }

                UIState.set('hud_only');

                // Garante a apresentação do frame final (página vazia)
                if (typeof kickUIRepaint === 'function') kickUIRepaint();

                const bridge = getBridge();
                if (bridge && typeof bridge.closemenu === 'function') {
                    bridge.closemenu();
                } else {
                    log('Ponte offline: tela de save fechada no modo de teste web.');
                }
            }, 200);
        },

        // Failsafe (chamado pelo dispatcher do Escape): se a seção ficou
        // visível com o estado dessincronizado, força o fechamento.
        forceCloseIfVisible() {
            if (!section.classList.contains('active')) return;
            log('JRPGSaveLoad: failsafe — tela visível com estado dessincronizado, fechando.');
            if (view === null) view = 'grid';
            isClosing = false;
            UIState.set('saveload_open');
            api.close();
        },

        // Entrada semântica (teclado e gamepad convergem aqui).
        // As ABAS trocam só por 'tab_prev'/'tab_next' (Q/E no teclado, LB/RB no
        // controle) — as setinhas ◀ ▶ são apenas o indicativo visual. Assim
        // left/right continuam navegando o grid.
        handleInput(action) {
            if (view === null || isClosing) return;

            switch (view) {
                case 'grid':
                    if (action === 'tab_prev') {
                        setTab(tabIndex - 1);
                    } else if (action === 'tab_next') {
                        setTab(tabIndex + 1);
                    } else if (action === 'left') {
                        selectSlot((selectedSlot + SLOT_COUNT - 1) % SLOT_COUNT);
                    } else if (action === 'right') {
                        selectSlot((selectedSlot + 1) % SLOT_COUNT);
                    } else if (action === 'up') {
                        selectSlot((selectedSlot + SLOT_COUNT - GRID_COLS) % SLOT_COUNT);
                    } else if (action === 'down') {
                        selectSlot((selectedSlot + GRID_COLS) % SLOT_COUNT);
                    } else if (action === 'confirm') {
                        confirmSlot();
                    } else if (action === 'cancel') {
                        // Não há mais tela de escolha para voltar: cancela fecha
                        api.close();
                    }
                    break;

                case 'confirm':
                    // Yes/No ficam lado a lado: só left/right trocam a escolha
                    if (action === 'left' || action === 'right') {
                        playSound('Next');
                        confirmChoice = confirmChoice === 'yes' ? 'no' : 'yes';
                        highlightConfirmChoice();
                    } else if (action === 'confirm') {
                        resolveConfirm(confirmChoice);
                    } else if (action === 'cancel') {
                        resolveConfirm('no');
                    }
                    break;
            }
        },

        // Limpa estado herdado (chamado pelo resetUIShell entre sessões PIE)
        reset() {
            slotsData = [];
            view = null;
            tabIndex = 0;
            selectedSlot = 0;
            confirmChoice = 'no';
            pendingSlot = -1;
            pendingAction = null;
            isClosing = false;
            lockTab = false;
            returnTo = null;
            layout.style.opacity = '0';
            section.classList.remove('active');
            section.classList.remove('flare-instant');
            gridView.classList.remove('view-active');
            gridView.classList.remove('sl-enter');
            confirmOverlay.classList.remove('view-active');
            highlightTab();
        }
    };

    // Cópia rasa das opções com o modo forçado — não muta o objeto do chamador.
    function withMode(opts, m) {
        const o = opts || {};
        const out = { mode: m };
        if (o.returnTo) out.returnTo = o.returnTo;
        if (typeof o.lockTab === 'boolean') out.lockTab = o.lockTab;
        return out;
    }

    // --- Mouse nas abas (as setinhas ◀ ▶ são só visuais, sem clique) ---

    tabItems.forEach((item, idx) => {
        item.addEventListener('click', () => {
            if (view !== 'grid') return;
            setTab(idx);
        });
    });

    // --- Mouse nas opções da confirmação ---

    confirmOptions.forEach(opt => {
        opt.addEventListener('click', () => {
            if (view !== 'confirm') return;
            resolveConfirm(opt.getAttribute('data-choice'));
        });
    });

    // --- Teclado local (mesmas ações semânticas — testável em browser comum) ---

    window.addEventListener('keydown', (event) => {
        if (UIState.get() !== 'saveload_open') return;

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
            // Q/E = trocar de aba (equivalentes ao LB/RB do controle)
            case 'q':
            case 'Q':
                handleUIInput('tab_prev');
                event.preventDefault();
                break;
            case 'e':
            case 'E':
                handleUIInput('tab_next');
                event.preventDefault();
                break;
        }
    });

    return api;
})();
