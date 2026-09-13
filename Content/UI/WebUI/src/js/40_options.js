// 40_options.js - Tela de Opções (Display / Audio / Game / Controller / Keybindings)
//
// ESCOPO ATUAL: **somente visual**. Mudar um valor aqui NÃO aplica nada no jogo
// ainda (resolução, vsync, qualidade, volumes, idioma). A ligação com o
// AudioSubsystem (Set*Volume) e com o UGameUserSettings entra na próxima rodada —
// os pontos de ligação estão marcados com "TODO(apply)".
//
// Abre a partir de 3 origens e SEMPRE volta para a origem certa:
//   'mainmenu' -> volta ao main menu     | 'menu' -> volta ao menu de pausa
//   'hud'      -> devolve o input ao jogo (bridge.closemenu)
//
// A ponte é SEMPRE resolvida via getBridge() (00_core.js).

window.JRPGOptions = (function () {

    // ============================================================
    // DEFINIÇÃO DAS ABAS (rows geradas por JS, como o grid do save)
    // types: 'cycler' | 'slider' | 'keybind' | 'static'
    // ============================================================

    const TABS = [
        {
            id: 'display',
            label: 'Display',
            desc: 'Window, resolution and graphics quality settings.',
            groups: [
                {
                    title: 'Display',
                    rows: [
                        {
                            id: 'windowmode', type: 'cycler', label: 'Window Mode', index: 0,
                            values: ['Fullscreen', 'Borderless', 'Windowed'],
                            desc: 'Chooses how the game is presented on your monitor. Fullscreen gives the best performance; Borderless makes it easier to switch to other windows.'
                        },
                        {
                            id: 'resolution', type: 'cycler', label: 'Resolution', index: 2,
                            values: ['1280 × 720', '1600 × 900', '1920 × 1080', '2560 × 1440', '3840 × 2160'],
                            desc: 'Sets the rendering resolution. Lower values give a noticeable performance boost on weaker hardware.'
                        },
                        {
                            id: 'vsync', type: 'cycler', label: 'VSync', index: 0,
                            values: ['On', 'Off'],
                            desc: 'Synchronizes the frame rate with your monitor refresh rate to remove screen tearing, at the cost of a little input latency.'
                        },
                        {
                            id: 'fpslimit', type: 'cycler', label: 'FPS Limit', index: 1,
                            values: ['30', '60', '120', 'Unlimited'],
                            desc: 'Caps the maximum frames per second. A lower cap reduces heat and battery drain on laptops.'
                        }
                    ]
                },
                {
                    title: 'Graphics',
                    rows: [
                        {
                            id: 'quality', type: 'cycler', label: 'Overall Quality', index: 2,
                            values: ['Low', 'Medium', 'High', 'Epic'],
                            desc: 'Applies a preset to every graphics setting below at once. Pick a preset first, then fine tune what you need.'
                        },
                        {
                            id: 'textures', type: 'cycler', label: 'Textures', index: 2,
                            values: ['Low', 'Medium', 'High', 'Epic'],
                            desc: 'Resolution of surface textures. Higher settings need more video memory but cost little frame rate.'
                        },
                        {
                            id: 'shadows', type: 'cycler', label: 'Shadows', index: 2,
                            values: ['Low', 'Medium', 'High', 'Epic'],
                            desc: 'Quality and draw distance of dynamic shadows. One of the heaviest settings on most machines.'
                        },
                        {
                            id: 'lighting', type: 'cycler', label: 'Lighting', index: 2,
                            values: ['Low', 'Medium', 'High', 'Epic'],
                            desc: 'Quality of global illumination and reflections used to light the world.'
                        },
                        {
                            id: 'effects', type: 'cycler', label: 'Effects', index: 2,
                            values: ['Low', 'Medium', 'High', 'Epic'],
                            desc: 'Quality of particles, magic effects and battle impacts.'
                        },
                        {
                            id: 'postprocess', type: 'cycler', label: 'Post-Processing', index: 2,
                            values: ['Low', 'Medium', 'High', 'Epic'],
                            desc: 'Bloom, depth of field, motion blur and color grading applied to the final image.'
                        },
                        {
                            id: 'viewdistance', type: 'cycler', label: 'View Distance', index: 2,
                            values: ['Near', 'Medium', 'Far', 'Epic'],
                            desc: 'How far away objects and foliage keep being drawn before fading out.'
                        },
                        {
                            id: 'antialiasing', type: 'cycler', label: 'Anti-Aliasing', index: 2,
                            values: ['Off', 'FXAA', 'TAA', 'TSR'],
                            desc: 'Smooths jagged edges. TSR gives the cleanest image; FXAA is the cheapest option.'
                        }
                    ]
                }
            ]
        },
        {
            id: 'audio',
            label: 'Audio',
            desc: 'Volume levels and audio output settings.',
            groups: [
                {
                    title: 'Volumes',
                    rows: [
                        {
                            id: 'vol_master', type: 'slider', label: 'Master Volume', value: 100,
                            desc: 'Overall volume of the game. Every other channel is scaled by this value.'
                        },
                        {
                            id: 'vol_music', type: 'slider', label: 'Music', value: 80,
                            desc: 'Volume of the background music.'
                        },
                        {
                            id: 'vol_sfx', type: 'slider', label: 'Sound Effects', value: 100,
                            desc: 'Volume of battle hits, spells and world sound effects.'
                        },
                        {
                            id: 'vol_ambient', type: 'slider', label: 'Ambient', value: 80,
                            desc: 'Volume of ambient loops such as wind, rain and crowds.'
                        },
                        {
                            id: 'vol_ui', type: 'slider', label: 'Interface', value: 100,
                            desc: 'Volume of the menu cursor, confirm and cancel sounds.'
                        }
                    ]
                },
                {
                    title: 'Other',
                    rows: [
                        {
                            id: 'audiodevice', type: 'cycler', label: 'Audio Device', index: 0,
                            values: ['System Default'],
                            desc: 'Output device used by the game. Only the system default is available for now.'
                        }
                    ]
                }
            ]
        },
        {
            id: 'game',
            label: 'Game',
            desc: 'Language and general gameplay settings.',
            groups: [
                {
                    title: 'Game Settings',
                    rows: [
                        {
                            id: 'difficulty', type: 'cycler', label: 'Difficulty', index: 0,
                            values: ['Normal', 'Hard', 'Juggernaut'],
                            desc: 'Normal segue o balanceamento do jogo original. Hard e Juggernaut deixam os INIMIGOS mais fortes (1.5x e 2.5x em HP, ataque e defesa) e dão um pouco mais de XP e gold. Seus personagens crescem igual em qualquer dificuldade.'
                        },
                        {
                            id: 'language', type: 'cycler', label: 'Language', index: 0,
                            values: ['English', 'Português (BR)'],
                            desc: 'Language used for menus, dialogue and item names.'
                        }
                    ]
                }
            ]
        },
        {
            id: 'controller',
            label: 'Controller',
            desc: 'A diagram of the gamepad layout will be shown here, with the action bound to each button.',
            groups: [
                {
                    title: 'Gamepad',
                    rows: [
                        { type: 'static', text: 'Controller diagram coming soon.' }
                    ]
                }
            ]
        },
        {
            id: 'keybindings',
            label: 'Keybindings',
            desc: 'Keys currently used by the game. Rebinding will be available in a future update.',
            groups: [
                {
                    title: 'Movement',
                    rows: [
                        { id: 'kb_up', type: 'keybind', label: 'Move Up', keys: ['W', '↑'], desc: 'Moves the party north. Rebinding will be available in a future update.' },
                        { id: 'kb_down', type: 'keybind', label: 'Move Down', keys: ['S', '↓'], desc: 'Moves the party south. Rebinding will be available in a future update.' },
                        { id: 'kb_left', type: 'keybind', label: 'Move Left', keys: ['A', '←'], desc: 'Moves the party west. Rebinding will be available in a future update.' },
                        { id: 'kb_right', type: 'keybind', label: 'Move Right', keys: ['D', '→'], desc: 'Moves the party east. Rebinding will be available in a future update.' }
                    ]
                },
                {
                    title: 'Actions',
                    rows: [
                        { id: 'kb_confirm', type: 'keybind', label: 'Confirm', keys: ['Enter', 'A'], desc: 'Confirms the highlighted option and talks to NPCs. Rebinding will be available in a future update.' },
                        { id: 'kb_cancel', type: 'keybind', label: 'Cancel', keys: ['Esc', 'B'], desc: 'Goes back one step and closes menus. Rebinding will be available in a future update.' },
                        { id: 'kb_menu', type: 'keybind', label: 'Open Menu', keys: ['Tab', 'Start'], desc: 'Opens the party menu. Rebinding will be available in a future update.' },
                        { id: 'kb_run', type: 'keybind', label: 'Run', keys: ['Shift', 'B'], desc: 'Hold to move faster on the field. Rebinding will be available in a future update.' }
                    ]
                }
            ]
        }
    ];

    const SLIDER_STEP = 5;

    // --- Estado interno ---
    let tabIndex = 0;
    let rowIndex = 0;         // índice dentro de selectableRows
    let selectableRows = [];  // [{ def, el }] só das linhas navegáveis da aba atual
    let origin = 'hud';       // 'hud' | 'menu' | 'mainmenu'
    let isOpen = false;
    let isClosing = false;

    // --- Referências de DOM ---
    const section = document.getElementById('screen-options');
    const layout = document.getElementById('options-layout');
    const tabItems = document.querySelectorAll('#options-tabs .opt-tab');
    const arrowPrev = document.querySelector('#options-tabbar .arrow-left');
    const arrowNext = document.querySelector('#options-tabbar .arrow-right');
    const rowsEl = document.getElementById('options-rows');
    const descCard = document.getElementById('options-desc');
    const descTitle = document.getElementById('options-desc-title');
    const descText = document.getElementById('options-desc-text');

    // Blocos que entram/saem escalonados na transição entre telas
    const animBlocks = [
        document.getElementById('options-bg'),
        document.getElementById('options-header'),
        document.getElementById('options-body'),
    ];

    // ============================================================
    // RENDERIZAÇÃO
    // ============================================================

    function buildRows() {
        rowsEl.innerHTML = '';
        selectableRows = [];

        const tab = TABS[tabIndex];

        tab.groups.forEach((group, gIdx) => {
            const title = document.createElement('div');
            title.className = 'opt-group-title' + (gIdx > 0 ? ' spaced' : '');
            title.textContent = group.title;
            rowsEl.appendChild(title);

            group.rows.forEach(def => {
                const row = document.createElement('div');

                if (def.type === 'static') {
                    row.className = 'opt-row opt-static';
                    row.textContent = def.text;
                    rowsEl.appendChild(row);
                    return;
                }

                row.className = 'opt-row';

                const label = document.createElement('div');
                label.className = 'opt-row-label';
                label.textContent = def.label;
                row.appendChild(label);
                row.appendChild(buildControl(def));

                const myIndex = selectableRows.length;
                row.addEventListener('click', () => selectRow(myIndex));
                row.addEventListener('click', () => selectRow(myIndex));

                rowsEl.appendChild(row);
                selectableRows.push({ def: def, el: row });
            });
        });

        rowsEl.scrollTop = 0;
        rowIndex = 0;
        highlightRow();
    }

    function buildControl(def) {
        if (def.type === 'slider') {
            const wrap = document.createElement('div');
            wrap.className = 'opt-slider';
            wrap.innerHTML =
                '<div class="opt-slider-track">' +
                '<div class="opt-slider-fill"></div>' +
                '<div class="opt-slider-thumb"></div>' +
                '</div>' +
                '<div class="opt-slider-value"></div>';

            const track = wrap.querySelector('.opt-slider-track');
            track.addEventListener('click', (event) => {
                const rect = track.getBoundingClientRect();
                const pct = (event.clientX - rect.left) / rect.width;
                setSliderValue(def, Math.round((pct * 100) / SLIDER_STEP) * SLIDER_STEP);
            });

            paintSlider(wrap, def.value);
            return wrap;
        }

        if (def.type === 'keybind') {
            const wrap = document.createElement('div');
            wrap.className = 'opt-keybind';
            def.keys.forEach(key => {
                const badge = document.createElement('span');
                badge.className = 'key-badge';
                badge.textContent = key;
                wrap.appendChild(badge);
            });
            return wrap;
        }

        // 'cycler' (padrão)
        const wrap = document.createElement('div');
        wrap.className = 'opt-cycler';
        wrap.innerHTML =
            '<span class="opt-arrow arrow-prev">◀</span>' +
            '<span class="opt-value"></span>' +
            '<span class="opt-arrow arrow-next">▶</span>';

        wrap.querySelector('.arrow-prev').addEventListener('click', (event) => {
            event.stopPropagation();
            cycleValue(def, -1);
        });
        wrap.querySelector('.arrow-next').addEventListener('click', (event) => {
            event.stopPropagation();
            cycleValue(def, 1);
        });

        wrap.querySelector('.opt-value').textContent = def.values[def.index];
        return wrap;
    }

    function paintSlider(wrap, value) {
        const fill = wrap.querySelector('.opt-slider-fill');
        const thumb = wrap.querySelector('.opt-slider-thumb');
        const valueEl = wrap.querySelector('.opt-slider-value');
        if (fill) fill.style.width = `${value}%`;
        if (thumb) thumb.style.left = `${value}%`;
        if (valueEl) valueEl.textContent = String(value);
    }

    function findRowEntry(def) {
        for (let i = 0; i < selectableRows.length; i++) {
            if (selectableRows[i].def === def) return selectableRows[i];
        }
        return null;
    }

    /**
     * Procura a definição de uma linha pelo id em TODAS as abas — não só na
     * aberta. É como o C++ acerta um valor (ex: a dificuldade) antes de a tela
     * ser montada.
     */
    function findRowDef(id) {
        for (const tab of TABS) {
            for (const group of tab.groups) {
                for (const row of group.rows) {
                    if (row.id === id) return row;
                }
            }
        }
        return null;
    }

    function highlightRow() {
        selectableRows.forEach((entry, idx) => {
            entry.el.classList.toggle('selected', idx === rowIndex);
        });

        // Aba sem linhas navegáveis (Controller): a descrição mostra a própria aba
        if (selectableRows[rowIndex]) {
            const entry = selectableRows[rowIndex];
            updateDesc(entry.def.label, entry.def.desc);
            ensureVisible(entry.el);
        } else {
            const tab = TABS[tabIndex];
            updateDesc(tab.label, tab.desc);
        }
    }

    function highlightTab() {
        tabItems.forEach((item, idx) => {
            item.classList.toggle('selected', idx === tabIndex);
        });
    }

    function updateDesc(title, text) {
        if (descTitle.textContent === title && descText.textContent === text) return;
        descTitle.textContent = title;
        descText.textContent = text;

        // Reinicia a animação de entrada do painel
        descCard.classList.remove('desc-swap');
        void descCard.offsetHeight;
        descCard.classList.add('desc-swap');
    }

    // Mantém a linha selecionada visível na lista rolável (sem scrollIntoView,
    // que não é confiável no Ultralight)
    function ensureVisible(el) {
        if (!el) return;
        const top = el.offsetTop;
        const bottom = top + el.offsetHeight;

        if (top < rowsEl.scrollTop) {
            rowsEl.scrollTop = Math.max(0, top - 8);
        } else if (bottom > rowsEl.scrollTop + rowsEl.clientHeight) {
            rowsEl.scrollTop = bottom - rowsEl.clientHeight + 8;
        }
    }

    // ============================================================
    // AÇÕES
    // ============================================================

    // Transição de entrada: fundo primeiro, depois header/corpo/atalhos.
    // O reflow é OBRIGATÓRIO — trocar display (via .active) e opacidade no mesmo
    // frame não dispara transição nenhuma.
    function animateIn() {
        animBlocks.forEach(el => { if (el) el.classList.remove('anim-in'); });
        void section.offsetHeight;

        // O fundo entra em 0.18s; o conteúdo só começa depois dele estar quase
        // opaco, senão os cards semitransparentes aparecem sobre a cena 3D.
        const delays = [0, 120, 170, 220];
        animBlocks.forEach((el, idx) => {
            if (!el) return;
            setTimeout(() => el.classList.add('anim-in'), delays[idx]);
        });
    }

    // Saída: tudo volta ao estado inicial junto (mais curto que a entrada)
    function animateOut() {
        animBlocks.forEach(el => { if (el) el.classList.remove('anim-in'); });
    }

    // Feedback visual da setinha correspondente ao trocar de aba (elas não são
    // clicáveis — só indicam que Q/E e LB/RB trocam de aba)
    function kickArrow(el) {
        if (!el) return;
        el.classList.remove('arrow-kick');
        void el.offsetHeight;
        el.classList.add('arrow-kick');
    }

    function setTab(index, silent) {
        const previous = tabIndex;

        if (index < 0) index = TABS.length - 1;
        if (index >= TABS.length) index = 0;
        if (index === tabIndex && selectableRows.length > 0) {
            highlightTab();
            return;
        }

        tabIndex = index;
        if (!silent) {
            playSound('Select');
            // Trocou pra trás (inclui o wrap do primeiro para o último)?
            const wentBack = (index === TABS.length - 1 && previous === 0) ||
                             (index < previous && !(index === 0 && previous === TABS.length - 1));
            kickArrow(wentBack ? arrowPrev : arrowNext);
        }

        highlightTab();
        buildRows();
    }

    function selectRow(index) {
        if (index < 0 || index >= selectableRows.length) return;
        if (index === rowIndex) return;

        playSound('Next');
        rowIndex = index;
        highlightRow(); // já cuida do ensureVisible da linha nova
    }

    function pulseValue(entry) {
        const valueEl = entry.el.querySelector('.opt-value');
        if (!valueEl) return;
        valueEl.classList.remove('value-changed');
        void valueEl.offsetHeight;
        valueEl.classList.add('value-changed');
    }

    function cycleValue(def, dir) {
        if (def.values.length <= 1) {
            playSound('Cancel');
            return;
        }

        def.index = (def.index + dir + def.values.length) % def.values.length;

        const entry = findRowEntry(def);
        if (entry) {
            const valueEl = entry.el.querySelector('.opt-value');
            if (valueEl) valueEl.textContent = def.values[def.index];
            pulseValue(entry);
        }

        playSound('Next');

        // A dificuldade é a primeira opção que aplica de verdade: o índice
        // casa com EJRPGDifficulty (0 Normal, 1 Hard, 2 Juggernaut).
        if (def.id === 'difficulty') {
            const bridge = getBridge();
            if (bridge && typeof bridge.onsetdifficulty === 'function') {
                bridge.onsetdifficulty(def.index);
                log(`Options: dificuldade = ${def.values[def.index]}.`);
            } else {
                log(`Options: dificuldade = ${def.values[def.index]} (ponte offline).`);
            }
            return;
        }

        // TODO(apply): aplicar no jogo (UGameUserSettings / idioma) quando a
        // rodada de "opções funcionais" chegar.
        log(`Options: ${def.id} = ${def.values[def.index]} (visual apenas).`);
    }

    function setSliderValue(def, value) {
        const clamped = Math.max(0, Math.min(100, value));
        if (clamped === def.value) return;
        def.value = clamped;

        const entry = findRowEntry(def);
        if (entry) paintSlider(entry.el, def.value);

        playSound('Next');
        // TODO(apply): AudioSubsystem->Set*Volume(def.value / 100) via ponte.
        log(`Options: ${def.id} = ${def.value} (visual apenas).`);
    }

    function changeSelected(dir) {
        const entry = selectableRows[rowIndex];
        if (!entry) return;

        if (entry.def.type === 'cycler') {
            cycleValue(entry.def, dir);
        } else if (entry.def.type === 'slider') {
            setSliderValue(entry.def, entry.def.value + dir * SLIDER_STEP);
        } else {
            // keybind: rebind ainda não implementado (spec pede só o visual)
            playSound('Cancel');
        }
    }

    function notifyState(state) {
        const bridge = getBridge();
        if (bridge && typeof bridge.onuistatechanged === 'function') {
            bridge.onuistatechanged(state);
        }
    }

    // Esconde a seção e devolve o controle para a tela de origem
    function finishClose() {
        section.classList.remove('active');
        isOpen = false;
        isClosing = false;

        if (origin === 'menu' && window.JRPGUI) {
            UIState.set('menu_open');
            notifyState('menu_open');
            JRPGUI.reopenFromOptions();
            return;
        }

        if (origin === 'mainmenu' && window.JRPGMainMenu) {
            UIState.set('main_menu');
            notifyState('main_menu');
            JRPGMainMenu.reopenFromOptions();
            return;
        }

        // origin 'hud': caminho normal — o C++ devolve o input ao jogo
        UIState.set('hud_only');
        if (typeof kickUIRepaint === 'function') kickUIRepaint();

        const bridge = getBridge();
        if (bridge && typeof bridge.closemenu === 'function') {
            bridge.closemenu();
        } else {
            log('Ponte offline: opções fechadas no modo de teste web.');
        }
    }

    // ============================================================
    // API PÚBLICA (chamada pelo C++ e pelo dispatcher de input)
    // ============================================================

    const api = {
        /** C++: abre a tela. origin = 'hud' | 'menu' | 'mainmenu'. */
        open(fromOrigin) {
            if (!UIState.canOpen('options')) {
                log(`JRPGOptions.open ignorado: estado atual '${UIState.get()}' não permite abrir as opções.`);
                return;
            }

            // Garante a escala correta mesmo se a janela mudou com a UI escondida
            if (typeof updateUIScaleFactor === 'function') updateUIScaleFactor();

            origin = fromOrigin || 'hud';
            isClosing = false;
            isOpen = true;
            tabIndex = 0;
            rowIndex = 0;

            // flare-instant: o flare da aba selecionada nasce no tamanho final.
            // Sem isso o `width: 0 -> 82%` roda ao sair de display:none e o
            // flare pisca como um ponto circular antes de virar faixa.
            section.classList.add('flare-instant');
            section.classList.add('active');
            void section.offsetHeight;
            setTimeout(() => section.classList.remove('flare-instant'), 80);

            UIState.set('options_open');

            highlightTab();
            buildRows();

            playSound('Open');
            layout.style.opacity = '1';
            animateIn();

            // Mesma proteção de frame usada nas outras telas
            if (typeof kickUIRepaint === 'function') kickUIRepaint();
        },

        /** Fecha a tela voltando para a origem (menu de pausa, main menu ou jogo). */
        close() {
            if (!isOpen || isClosing) return;
            isClosing = true;

            playSound('Close');
            animateOut();
            layout.style.opacity = '0';
            // 300ms cobre a saída do conteúdo (0.16s) + a do fundo (0.28s)
            setTimeout(finishClose, 300);
        },

        /** Failsafe do dispatcher do Escape (seção visível com estado dessincronizado). */
        forceCloseIfVisible() {
            if (!section.classList.contains('active')) return;
            log('JRPGOptions: failsafe — tela visível com estado dessincronizado, fechando.');
            isOpen = true;
            isClosing = false;
            UIState.set('options_open');
            api.close();
        },

        /**
         * Entrada semântica (teclado e gamepad convergem aqui).
         * As ABAS trocam só por 'tab_prev'/'tab_next' (Q/E no teclado, LB/RB no
         * controle) — as setinhas ◀ ▶ da barra são apenas o indicativo visual.
         * Assim left/right ficam livres para mudar o valor da linha selecionada.
         */
        handleInput(action) {
            if (!isOpen || isClosing) return;

            switch (action) {
                case 'tab_prev':
                    setTab(tabIndex - 1);
                    break;
                case 'tab_next':
                    setTab(tabIndex + 1);
                    break;
                case 'up':
                    selectRow(rowIndex - 1);
                    break;
                case 'down':
                    selectRow(rowIndex + 1);
                    break;
                case 'left':
                    changeSelected(-1);
                    break;
                case 'right':
                case 'confirm':
                    changeSelected(1);
                    break;
                case 'cancel':
                    api.close();
                    break;
                default:
                    break;
            }
        },

        /**
         * C++: sincroniza a dificuldade mostrada com a do CoreSubsystem.
         * Chamado no OpenOptions, antes de a tela aparecer.
         */
        setDifficulty(index) {
            const def = findRowDef('difficulty');
            if (!def) return;
            def.index = Math.max(0, Math.min(def.values.length - 1, index | 0));

            const entry = findRowEntry(def);
            if (entry) {
                const valueEl = entry.el.querySelector('.opt-value');
                if (valueEl) valueEl.textContent = def.values[def.index];
            }
        },

        /** Limpa estado herdado (chamado pelo resetUIShell entre sessões PIE). */
        reset() {
            isOpen = false;
            isClosing = false;
            origin = 'hud';
            tabIndex = 0;
            rowIndex = 0;
            layout.style.opacity = '0';
            section.classList.remove('active');
            section.classList.remove('flare-instant');
            animateOut();
        }
    };

    // --- Mouse nas abas (as setinhas ◀ ▶ são só visuais, sem clique) ---

    // O hover é puramente visual (CSS): quem troca de aba é o clique.
    tabItems.forEach((item, idx) => {
        item.addEventListener('click', () => {
            if (!isOpen || isClosing) return;
            setTab(idx);
        });
    });

    // --- Teclado local (mesmas ações semânticas — testável em browser comum) ---

    window.addEventListener('keydown', (event) => {
        if (UIState.get() !== 'options_open') return;

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

// CONTRATO COM O C++ (nome mantido desde o placeholder): OpenOptions() chama
// openOptionsScreen('<origem>').
window.openOptionsScreen = function (origin) {
    JRPGOptions.open(origin || 'hud');
};
