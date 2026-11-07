// 96_battle.js — MENU DE COMANDOS DA LUTA (placeholder)
//
// Só DESENHA e REPASSA. Quem decide cada passo do fluxo é o C++ (FJRPGBattleMenu, Battle/JRPGBattleMenu.h): a cada aperto o
// C++ manda a view inteira e esta tela a redesenha. Nada de regra aqui (regra 39): o que cabe no gauge, qual botão é qual
// direção para a mão do herói, quem é o alvo — tudo já chega pronto na view. Os apertos (teclado, gamepad, mouse) saem por
// UMA função da ponte: bridge.onbattleinput(action, a, b).
//
// C++ -> JS
//   JRPGBattle.open()         abre a seção (estado 'battle_open')
//   JRPGBattle.render(view)   desenha a view; view = {
//       screen: 'start'|'escape'|'tactics'|'command'|'attack'|'target'|'final',
//       round, sfx,                           (sfx: o som a tocar com esta tela, '' = nenhum)
//       crumbs: ['Begin','Vahn','Attack'],    (as migalhas, canto de cima à esquerda)
//       party: [{name,lv,hp,maxhp,mp,maxmp,ap,maxap,alive}], focus: índice do herói comandado ou -1,
//       dpad: {left,up,right,down},           (cada um {label,enabled,cost} ou null; ver 95_battle_dpad.js)
//       refusal: '',                          (o motivo de uma recusa do núcleo ou do menu)
//       art: {chips:[{label,cost}], gauge, left, slots, slotsLeft} | null,    (a janela de entrada)
//       targets: {list:[{name}], cursor} | null,
//       tactics: {rows:[{name,pick}], options:[...], row, confirmRow, confirm} | null,
//       summary: [{who,what,target}],         (a tela Final)
//       artlist: [{name,kind,ap,cost,dirs}] | null,   (só na tela Attack: o painel do Triângulo, já na ordem)
//       autobattle: {on, pending} }
//   JRPGBattle.close()        esconde a seção (o C++ já devolveu o input ao jogo)
// JS -> C++
//   bridge.onbattleinput('up'|'down'|'left'|'right'|'confirm'|'cancel')
//   bridge.onbattleinput('pick', a, b)   (um clique por posição: alvo = a; tactics: a = linha, b = opção)

window.JRPGBattle = (function () {

    const section = () => document.getElementById('screen-battle');
    const el = (id) => document.getElementById(id);

    let isOpen = false;
    let view = null;

    // A lista de consulta das Arts: aberta ou não, e a página (cinco por página, como o painel do original). Estado só de tela:
    // os dados (as Arts, o AP, as setas pela mão do herói) chegam prontos em view.artlist
    const ARTS_PER_PAGE = 5;
    let listOpen = false;
    let listPage = 0;

    function esc(text) {
        return String(text === undefined || text === null ? '' : text)
            .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    }

    /** UM ponto de saída: o aperto vai ao C++, que confere tudo (regra 22). */
    function send(action, a, b) {
        const bridge = getBridge();
        if (bridge && typeof bridge.onbattleinput === 'function') {
            bridge.onbattleinput(action, a | 0, b | 0);
        } else {
            log(`Battle: aperto '${action}' ${a | 0},${b | 0} (ponte offline)`);
        }
    }

    // ------------------------------------------------------------------
    // peças
    // ------------------------------------------------------------------

    function drawCrumbs(v) {
        const box = el('bt-crumbs');
        if (!box) return;
        const list = v.crumbs || [];
        box.innerHTML = list.map(c => `<span class="bt-crumb">${esc(c)}</span>`).join('');
    }

    function drawAuto(v) {
        const box = el('bt-auto');
        if (!box) return;
        const a = v.autobattle || {};
        if (a.pending) {
            // A contagem do auto: a rodada sai sozinha; o Cancel (ou este botão) volta ao manual
            box.innerHTML = '<span class="bt-auto-text">AUTO-BATTLE starting</span>' +
                            '<span class="bt-btn bt-auto-cancel">Cancel</span>';
            const btn = box.querySelector('.bt-auto-cancel');
            if (btn) {
                btn.onmouseenter = () => { if (window.InputMode) InputMode.useMouse(); };
                btn.onclick = () => send('cancel');
            }
            box.classList.add('active');
        } else if (a.on && v.screen === 'start') {
            box.innerHTML = '<span class="bt-auto-text">AUTO-BATTLE on</span>';
            box.classList.add('active');
        } else {
            box.innerHTML = '';
            box.classList.remove('active');
        }
    }

    function heroCard(h) {
        return `<div class="bt-card${h.alive ? '' : ' down'}">
            <div class="bt-card-top"><span class="bt-name">${esc(h.name)}</span>
                <span class="bt-lv"><i class="bt-lv-tag">LV</i>${h.lv}</span></div>
            <div class="bt-stat"><i class="bt-tag hp">HP</i><span>${h.hp} / ${h.maxhp}</span></div>
            <div class="bt-stat"><i class="bt-tag mp">MP</i><span>${h.mp} / ${h.maxmp}</span></div>
        </div>`;
    }

    function drawParty(v) {
        const box = el('bt-party');
        if (!box) return;
        const party = v.party || [];
        const focus = (v.focus !== undefined && v.focus >= 0) ? party[v.focus] : null;

        if (focus) {
            // O herói que está sendo comandado: a faixa com HP/MP e, em cima, a barra de AP
            const pct = focus.maxap > 0 ? Math.max(0, Math.min(100, Math.round(100 * focus.ap / focus.maxap))) : 0;
            box.className = 'focus';
            box.innerHTML = `
                <div class="bt-ap"><i class="bt-tag ap">AP</i>
                    <div class="bt-ap-track"><div class="bt-ap-fill" style="width:${pct}%"></div></div>
                    <span class="bt-ap-num">${focus.ap}</span></div>
                <div class="bt-focus-bar">
                    <span class="bt-name">${esc(focus.name)}</span>
                    <span class="bt-stat"><i class="bt-tag hp">HP</i><span>${focus.hp} / ${focus.maxhp}</span></span>
                    <span class="bt-stat"><i class="bt-tag mp">MP</i><span>${focus.mp} / ${focus.maxmp}</span></span>
                </div>`;
        } else {
            box.className = 'all';
            box.innerHTML = party.map(heroCard).join('');
        }
    }

    function drawArt(v) {
        const box = el('bt-art');
        if (!box) return;
        const art = v.art;
        if (!art) {
            box.innerHTML = '';
            box.classList.remove('active');
            return;
        }
        // Os botões já digitados, e os espaços que ainda cabem (tracejados)
        const chips = (art.chips || []).map(c =>
            `<span class="bt-chip"><span>${esc(c.label)}</span><i>${c.cost}</i></span>`).join('');
        const empty = Array.from({ length: Math.max(0, art.slotsLeft || 0) }, () => '<span class="bt-chip empty"></span>').join('');
        const limited = art.gauge > 0;
        const pct = limited ? Math.max(0, Math.min(100, Math.round(100 * art.left / art.gauge))) : 100;
        box.innerHTML = `
            <div class="bt-art-slots">${chips}${empty}</div>
            <div class="bt-art-gauge">
                <span class="bt-art-label">GAUGE</span>
                <div class="bt-art-track"><div class="bt-art-fill" style="width:${pct}%"></div></div>
                <span class="bt-art-num">${limited ? `${art.left} / ${art.gauge}` : 'no limit'}</span>
            </div>`;
        box.classList.add('active');
    }

    function drawTargets(v) {
        const box = el('bt-targets');
        const list = el('bt-targets-list');
        if (!box || !list) return;
        const t = v.targets;
        if (!t) {
            list.innerHTML = '';
            return;
        }
        list.innerHTML = (t.list || []).map((e, i) =>
            `<div class="bt-target${i === t.cursor ? ' selected' : ''}" data-i="${i}">${esc(e.name)}</div>`).join('');
        list.querySelectorAll('.bt-target').forEach(node => {
            node.onmouseenter = () => { if (window.InputMode) InputMode.useMouse(); };
            node.onclick = () => send('pick', parseInt(node.dataset.i, 10), 0);
        });
    }

    function drawTactics(v) {
        const rowsBox = el('bt-tactics-rows');
        const confirm = el('bt-tactics-confirm');
        if (!rowsBox || !confirm) return;
        const t = v.tactics;
        if (!t) {
            rowsBox.innerHTML = '';
            return;
        }
        rowsBox.innerHTML = (t.rows || []).map((r, i) => {
            const opts = (t.options || []).map((o, k) =>
                `<span class="bt-opt${k === r.pick ? ' selected' : ''}" data-row="${i}" data-opt="${k}">${esc(o)}</span>`).join('');
            return `<div class="bt-tactic-row${i === t.row ? ' current' : ''}">
                <span class="bt-tactic-name">${esc(r.name)}</span><span class="bt-tactic-opts">${opts}</span></div>`;
        }).join('');
        confirm.textContent = t.confirm || 'Confirm';
        confirm.classList.toggle('current', t.row === t.confirmRow);

        rowsBox.querySelectorAll('.bt-opt').forEach(node => {
            node.onmouseenter = () => { if (window.InputMode) InputMode.useMouse(); };
            node.onclick = () => send('pick', parseInt(node.dataset.row, 10), parseInt(node.dataset.opt, 10));
        });
        confirm.onmouseenter = () => { if (window.InputMode) InputMode.useMouse(); };
        confirm.onclick = () => send('pick', t.confirmRow, 0);
    }

    function drawFinal(v) {
        const rows = el('bt-final-rows');
        if (!rows) return;
        rows.innerHTML = (v.summary || []).map(s =>
            `<div class="bt-final-row"><span class="bt-final-who">${esc(s.who)}</span>` +
            `<span class="bt-final-what">${esc(s.what)}</span><span class="bt-final-target">${esc(s.target)}</span></div>`).join('');
    }

    const ARROW_DIRS = { left: 1, up: 1, right: 1, down: 1 };

    /**
     * Uma seta do painel (os símbolos #ar-<dir>-p / -y de 95_battle.html). As cores do original: roxa, a ÚLTIMA de cada Art
     * amarela, e todas amarelas numa Hyper.
     */
    function arrowSvg(dir, yellow) {
        const d = ARROW_DIRS[dir] ? dir : 'up';
        return `<svg class="bt-al-arrow" viewBox="0 0 20 20"><use href="#ar-${d}-${yellow ? 'y' : 'p'}" xlink:href="#ar-${d}-${yellow ? 'y' : 'p'}"/></svg>`;
    }

    function drawArtList(v) {
        const box = el('bt-artlist');
        if (!box) return;
        const list = v && v.artlist;
        if (!list) {
            box.innerHTML = '';
            box.classList.remove('active');
            return;
        }
        box.classList.add('active');
        if (!listOpen) {
            box.innerHTML = '<div class="bt-al-tab"><span class="key-badge">&#9651; / E</span> Arts</div>';
        } else {
            const pages = Math.max(1, Math.ceil(list.length / ARTS_PER_PAGE));
            listPage = Math.max(0, Math.min(listPage, pages - 1));
            const rows = list.slice(listPage * ARTS_PER_PAGE, (listPage + 1) * ARTS_PER_PAGE).map(a => {
                // O original: a Art que tira AP leva o "-"; a que dá fica só com o número
                const ap = a.cost ? `-${a.ap}` : `${a.ap}`;
                const kind = ['miracle', 'hyper', 'super'].includes(a.kind) ? a.kind : 'regular';
                const seq = a.dirs || [];
                const dirs = seq.map((d, i) => arrowSvg(d, kind === 'hyper' || i === seq.length - 1)).join('');
                return `<div class="bt-al-row ${kind}">
                    <div class="bt-al-line"><span class="bt-al-name">${esc(a.name)}</span>
                        <span class="bt-al-ap${a.cost ? ' cost' : ''}">${ap}</span></div>
                    <div class="bt-al-dirs">${dirs}</div>
                </div>`;
            }).join('');
            box.innerHTML = `<div class="bt-al-panel">
                ${rows || '<div class="bt-al-empty">No Arts learned yet.</div>'}
                <div class="bt-al-foot">
                    <span><span class="key-badge">L1 / Q</span> <span class="key-badge">R1 / R</span> Page ${listPage + 1}/${pages}</span>
                    <span><span class="key-badge">&#9651; / E</span> Close</span>
                </div>
            </div>`;
        }
        const clickable = box.querySelector('.bt-al-tab, .bt-al-foot');
        if (clickable) {
            clickable.onmouseenter = () => { if (window.InputMode) InputMode.useMouse(); };
            clickable.onclick = () => toggleArtList();
        }
    }

    function toggleArtList() {
        if (!view || !view.artlist) return;
        listOpen = !listOpen;
        listPage = 0;
        drawArtList(view);
        if (typeof kickUIRepaint === 'function') kickUIRepaint();
    }

    function pageArtList(delta) {
        if (!listOpen || !view || !view.artlist) return;
        const pages = Math.max(1, Math.ceil(view.artlist.length / ARTS_PER_PAGE));
        // Sem dar a volta: o L1 sobe e o R1 desce, e os dois param na primeira / na última página
        const next = Math.max(0, Math.min(pages - 1, listPage + delta));
        if (next === listPage) return;
        listPage = next;
        drawArtList(view);
        if (typeof kickUIRepaint === 'function') kickUIRepaint();
    }

    function drawRefusal(v) {
        const box = el('bt-refusal');
        if (!box) return;
        box.textContent = v.refusal || '';
        box.classList.toggle('active', !!v.refusal);
    }

    // ------------------------------------------------------------------
    // a tela
    // ------------------------------------------------------------------

    function draw(v) {
        const stage = el('bt-stage');
        if (stage) stage.setAttribute('data-screen', v.screen || 'start');

        drawCrumbs(v);
        drawAuto(v);
        drawParty(v);
        drawArt(v);
        drawTargets(v);
        drawTactics(v);
        drawFinal(v);
        drawArtList(v);
        drawRefusal(v);

        // O d-pad é a peça isolada (95_battle_dpad.js): a posição (centro ou à direita) é do CSS, pela tela
        const dpad = el('bt-dpad');
        if (dpad) {
            dpad.setAttribute('data-pos', (v.screen === 'command' || v.screen === 'attack') ? 'right' : 'center');
        }
        if (window.JRPGBattleDpad) JRPGBattleDpad.render(dpad, v.dpad);
    }

    function hideSection() {
        const sec = section();
        if (sec) sec.classList.remove('active');
    }

    // ------------------------------------------------------------------
    // API pública
    // ------------------------------------------------------------------

    const api = {
        open() {
            if (!UIState.canOpen('battle')) {
                log(`JRPGBattle.open ignorado: estado atual '${UIState.get()}' não permite.`);
                return;
            }
            if (typeof updateUIScaleFactor === 'function') updateUIScaleFactor();
            isOpen = true;
            view = null;
            const sec = section();
            if (sec) sec.classList.add('active');
            UIState.set('battle_open');
            // O C++ mostra o ponteiro ao focar a UI a cada rodada; quem vem jogando de teclado / controle não quer vê-lo de volta
            if (window.InputMode && !InputMode.isMouse()) {
                const bridge = getBridge();
                if (bridge && typeof bridge.setcursorvisible === 'function') bridge.setcursorvisible(false);
            }
            if (typeof kickUIRepaint === 'function') kickUIRepaint();
        },

        render(v) {
            if (!isOpen || !v) return;
            view = v;
            draw(v);
            if (v.sfx) playSound(v.sfx);
            if (typeof kickUIRepaint === 'function') kickUIRepaint();
        },

        close() {
            if (!isOpen) return;
            isOpen = false;
            view = null;
            hideSection();
            // O C++ fechou por conta própria (a rodada toca): nada de bridge.closemenu aqui, o input já voltou ao jogo
            if (UIState.get() === 'battle_open') UIState.set('hud_only');
            if (typeof kickUIRepaint === 'function') kickUIRepaint();
        },

        forceCloseIfVisible() {
            hideSection();
            isOpen = false;
            view = null;
        },

        /** teclado e gamepad chegam aqui (handleUIInput); o mouse vai direto ao send dos próprios elementos. */
        handleInput(action) {
            if (!isOpen) return;
            switch (action) {
                case 'up': case 'down': case 'left': case 'right': case 'confirm': case 'cancel':
                    send(action);
                    break;
                // A lista de consulta das Arts é só tela: abrir, fechar e paginar não falam com o C++
                case 'list':     toggleArtList(); break;
                case 'tab_prev': pageArtList(-1); break;
                case 'tab_next': pageArtList(1);  break;
                default:
                    break;
            }
        },

        reset() {
            hideSection();
            isOpen = false;
            view = null;
            listOpen = false;
            listPage = 0;
            ['bt-crumbs', 'bt-auto', 'bt-art', 'bt-party', 'bt-targets-list', 'bt-tactics-rows', 'bt-final-rows', 'bt-refusal',
             'bt-artlist'].forEach(id => { const n = el(id); if (n) n.innerHTML = ''; });
            if (window.JRPGBattleDpad) JRPGBattleDpad.clear(el('bt-dpad'));
        },

        /**
         * Só para ver a tela no Chrome (a ponte fica offline): JRPGBattle.preview('start' | 'command' | 'attack' | 'target' |
         * 'final' | 'tactics' | 'escape'). Os dados são de mentira; os apertos só vão para o log.
         */
        preview(screen) {
            const party = [
                { name: 'Vahn', lv: 6, hp: 347, maxhp: 397, mp: 60, maxmp: 60, ap: 100, maxap: 100, alive: true },
                { name: 'Noa', lv: 7, hp: 308, maxhp: 384, mp: 47, maxmp: 47, ap: 40, maxap: 100, alive: true },
                { name: 'Gala', lv: 6, hp: 358, maxhp: 445, mp: 80, maxmp: 80, ap: 0, maxap: 100, alive: true }
            ];
            const pad = (l, u, r, d) => ({ left: l, up: u, right: r, down: d });
            const b = (label, enabled, cost) => ({ label, enabled, cost: cost === undefined ? -1 : cost });
            const base = {
                round: 1, sfx: '', crumbs: [], party, focus: -1, refusal: '', art: null, targets: null, tactics: null,
                summary: [], autobattle: { on: false, pending: false }, artlist: null,
                dpad: pad(null, null, null, null)
            };
            const sampleArts = [
                { name: 'Miracle Art', kind: 'miracle', ap: 100, cost: true, dirs: ['right', 'down', 'left', 'up', 'left', 'down', 'right', 'up'] },
                { name: 'Tornado Flame', kind: 'hyper', ap: 30, cost: true, dirs: ['right', 'right', 'left'] },
                { name: "Vahn's Craze", kind: 'super', ap: 60, cost: true, dirs: ['right', 'down', 'left', 'up', 'left', 'down', 'right'] },
                { name: 'Slash Kick', kind: 'regular', ap: 12, cost: false, dirs: ['down', 'up', 'right'] },
                { name: 'Hyper Elbow', kind: 'regular', ap: 9, cost: false, dirs: ['left', 'right', 'left'] },
                { name: 'Somersault', kind: 'regular', ap: 8, cost: false, dirs: ['up', 'down', 'up'] },
                { name: 'Power Punch', kind: 'regular', ap: 6, cost: false, dirs: ['left', 'left', 'up'] }
            ];
            const screens = {
                start: { screen: 'start', dpad: pad(b('Begin', true), b('Tactics', true), b('Escape', true), null) },
                escape: { screen: 'escape', crumbs: ['Escape'], dpad: pad(b('Confirm', true), null, b('Cancel', true), null) },
                command: {
                    screen: 'command', crumbs: ['Begin', 'Vahn'], focus: 0,
                    dpad: pad(b('Attack', true), b('Item', false), b('Meta', false), b('Spirit', true))
                },
                attack: {
                    screen: 'attack', crumbs: ['Begin', 'Vahn', 'Attack'], focus: 0, refusal: 'Not enough gauge for that button.',
                    artlist: sampleArts,
                    dpad: pad(b('Arms', true, 30), b('High', true, 54), b('Ra-Seru', true, 42), b('Low', false, 54)),
                    art: { chips: [{ label: 'Ra-Seru', cost: 42 }, { label: 'Ra-Seru', cost: 42 }], gauge: 160, left: 76, slots: 5, slotsLeft: 2 }
                },
                target: {
                    screen: 'target', crumbs: ['Begin', 'Vahn', 'Attack', 'Target'], focus: 0,
                    art: { chips: [{ label: 'Arms', cost: 30 }], gauge: 160, left: 130, slots: 5, slotsLeft: 4 },
                    targets: { list: [{ name: 'Frog A' }, { name: 'Frog B' }], cursor: 0 }
                },
                tactics: {
                    screen: 'tactics', crumbs: ['Tactics'],
                    tactics: {
                        rows: [{ name: 'Vahn', pick: 0 }, { name: 'Noa', pick: 1 }, { name: 'Gala', pick: 2 }],
                        options: ['Aggressive', 'Standard', 'Defensive'], row: 1, confirmRow: 3, confirm: 'Confirm'
                    }
                },
                final: {
                    screen: 'final', crumbs: ['Begin'], dpad: pad(b('Begin', true), null, b('Reselect', true), null),
                    summary: [{ who: 'Vahn', what: 'Attack', target: 'Frog A' }, { who: 'Noa', what: 'Attack x3', target: 'Frog A' },
                              { who: 'Gala', what: 'Attack', target: 'Frog B' }]
                }
            };
            const chosen = Object.assign({}, base, screens[screen] || screens.start);
            if (UIState.get() !== 'battle_open') {
                resetUIShell();
                api.open();
            }
            api.render(chosen);
        }
    };

    // Teclado local (as mesmas ações semânticas — vale no Chrome e dentro do jogo, onde só o Escape vem por
    // triggerAnimateOutAndClose). O gamepad chega por handleUIInput.
    window.addEventListener('keydown', (event) => {
        if (UIState.get() !== 'battle_open') return;

        switch (event.key) {
            case 'ArrowUp':    case 'w': case 'W': handleUIInput('up');      event.preventDefault(); break;
            case 'ArrowDown':  case 's': case 'S': handleUIInput('down');    event.preventDefault(); break;
            case 'ArrowLeft':  case 'a': case 'A': handleUIInput('left');    event.preventDefault(); break;
            case 'ArrowRight': case 'd': case 'D': handleUIInput('right');   event.preventDefault(); break;
            case 'Enter': case ' ':                handleUIInput('confirm'); event.preventDefault(); break;
            case 'Escape': case 'Backspace':       handleUIInput('cancel');  event.preventDefault(); break;
            // A lista de Arts: E abre / fecha (o Triângulo do controle), Q / R (ou PageUp / PageDown) trocam a página (L1 / R1)
            case 'e': case 'E':                    handleUIInput('list');     event.preventDefault(); break;
            case 'q': case 'Q': case 'PageUp':     handleUIInput('tab_prev'); event.preventDefault(); break;
            case 'r': case 'R': case 'PageDown':   handleUIInput('tab_next'); event.preventDefault(); break;
            default: break;
        }
    });

    // O d-pad devolve o aperto de uma direção como a mesma ação do teclado
    if (window.JRPGBattleDpad) {
        JRPGBattleDpad.onPress((dir) => send(dir));
    }

    return api;
})();
