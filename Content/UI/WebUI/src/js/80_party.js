// 80_party.js — TELA DE PARTY (formação)
//
// Abre do menu de pausa e volta para ele ao fechar — mesmo padrão de
// Options/Save/Load: o menu se esconde, esta tela sobe por cima, e ao sair o
// menu reaparece como estava.
//
// Payload do C++ (JRPGParty.open):
//   { max: 3, members: [ {id,name,portrait,lv,hp,maxhp,mp,maxmp,ap,maxap,
//                         atk,udf,ldf,spd,int,agl,
//                         active, available, dead} ] }
//
// HP e MP são sempre ATUAL / MÁXIMO, e o máximo é o do personagem naquele
// level — não um teto fixo.

window.JRPGParty = (function () {

    const section = () => document.getElementById('screen-party');
    const layout = () => document.getElementById('party-layout');
    const list = () => document.getElementById('party-list');
    const panel = () => document.getElementById('party-panel');

    let data = null;
    let index = 0;
    let isClosing = false;

    // ------------------------------------------------------------------
    // render
    // ------------------------------------------------------------------

    function pct(cur, max) {
        if (!max || max <= 0) return 0;
        return Math.max(0, Math.min(100, (cur / max) * 100));
    }

    function barRow(label, cls, cur, max) {
        return `<div class="party-bar-row">
            <span class="party-bar-label ${cls}-text">${label}</span>
            <div class="party-bar-track">
                <div class="party-bar-fill ${cls}" style="width:${pct(cur, max)}%"></div>
            </div>
            <span class="party-bar-values"><span class="cur">${cur}</span><span class="sep">/</span><span class="max">${max}</span></span>
        </div>`;
    }

    function statCell(label, value) {
        return `<div class="party-stat"><span>${label}</span><span>${value}</span></div>`;
    }

    function rowHTML(m) {
        const classes = ['party-row'];
        if (!m.available) classes.push('unavailable');
        else if (!m.active) classes.push('benched');
        if (m.dead) classes.push('dead');

        const badge = !m.available
            ? '<span class="party-badge">AWAY</span>'
            : (m.active ? '<span class="party-badge on">ACTIVE</span>'
                        : '<span class="party-badge">RESERVE</span>');

        // Atributos em dois lados: ofensa/defesa a esquerda, velocidade/mente
        // a direita. Sem cantoneiras aqui — quem tem e o card que envolve tudo.
        return `<div class="${classes.join(' ')}" data-id="${m.id}">
            <img class="party-portrait" src="images/${m.portrait}.png" alt=""
                 onerror="this.classList.add('no-art')">

            <div class="party-mid">
                <div class="party-ident">
                    <span class="party-name">${m.name}</span>
                    ${badge}
                </div>
                <div class="party-bars">
                    ${barRow('HP', 'hp', m.hp, m.maxhp)}
                    ${barRow('MP', 'mp', m.mp, m.maxmp)}
                    ${barRow('AP', 'ap', m.ap, m.maxap)}
                </div>
            </div>

            <div class="party-right">
                <div class="party-lv">LV<b>${m.lv}</b></div>
                <div class="party-stats">
                    <div class="party-stats-col">
                        ${statCell('ATK', m.atk)}
                        ${statCell('UDF', m.udf)}
                        ${statCell('LDF', m.ldf)}
                    </div>
                    <div class="party-stats-col">
                        ${statCell('SPD', m.spd)}
                        ${statCell('INT', m.int)}
                        ${statCell('AGL', m.agl)}
                    </div>
                </div>
            </div>
        </div>`;
    }

    function build(animate) {
        const el = list();
        if (!el) return;

        const members = (data && data.members) || [];
        el.innerHTML = members.map(rowHTML).join('');

        const pnl = panel();
        if (pnl) pnl.classList.toggle('is-empty', members.length === 0);

        if (animate) {
            if (pnl) {
                pnl.classList.remove('panel-in');
                void pnl.offsetWidth;
                pnl.classList.add('panel-in');
            }
            el.querySelectorAll('.party-row').forEach((row, i) => {
                row.classList.add('row-rise');
                row.style.animationDelay = `${0.05 * i + 0.20}s`;
            });
        }

        // Hover só destaca; o clique é que seleciona E confirma.
        el.querySelectorAll('.party-row').forEach((row, i) => {
            row.onmouseenter = () => {
                if (window.InputMode) InputMode.useMouse();
                highlight(i, false);
            };
            row.onclick = () => { highlight(i, false); toggleCurrent(); };
        });

        updateCount();
        highlight(Math.min(index, Math.max(0, members.length - 1)), false);
    }

    function updateCount() {
        const el = document.getElementById('party-count');
        if (!el || !data) return;
        const ativos = (data.members || []).filter(m => m.active).length;
        el.textContent = `${ativos} / ${data.max}`;
    }

    function highlight(i, sound) {
        const rows = list() ? list().querySelectorAll('.party-row') : [];
        if (!rows.length) return;

        index = (i + rows.length) % rows.length;
        rows.forEach((r, n) => r.classList.toggle('selected', n === index));
        if (sound) playSound('Next');
    }

    // ------------------------------------------------------------------
    // ativar / desativar
    // ------------------------------------------------------------------

    function toggleCurrent() {
        const members = (data && data.members) || [];
        const m = members[index];
        if (!m) return;

        if (!m.available) {
            playSound('Cancel');
            log(`${m.id} não está disponível na história.`);
            return;
        }

        const bridge = getBridge();
        if (bridge && typeof bridge.onpartytoggle === 'function') {
            // O C++ valida (formação cheia, último ativo) e devolve o payload
            // novo por JRPGParty.update() — a UI não decide sozinha.
            bridge.onpartytoggle(m.id, !m.active);
        } else {
            // Ponte offline (Chrome): espelha localmente só para ver o layout.
            m.active = !m.active;
            build(false);
            playSound('Select');
        }
    }

    // ------------------------------------------------------------------
    // API pública
    // ------------------------------------------------------------------

    const api = {
        open(payload) {
            if (!UIState.canOpen('party')) {
                log(`JRPGParty.open ignorado: estado atual '${UIState.get()}' não permite.`);
                return;
            }
            if (typeof updateUIScaleFactor === 'function') updateUIScaleFactor();

            data = payload || { max: 3, members: [] };
            index = 0;
            isClosing = false;

            const sec = section();
            if (sec) sec.classList.add('active');
            UIState.set('party_open');

            build(true);
            playSound('Open');

            const lay = layout();
            if (lay) {
                lay.classList.remove('anim-out');
                lay.style.opacity = '1';
            }

            if (typeof kickUIRepaint === 'function') kickUIRepaint();
        },

        /** O C++ mandou o estado novo depois de um toggle. */
        update(payload) {
            if (!payload) return;
            data = payload;
            build(false);

            const row = list() ? list().querySelectorAll('.party-row')[index] : null;
            if (row) {
                row.classList.remove('toggled');
                void row.offsetWidth;           // reinicia a animacao
                row.classList.add('toggled');
            }
            playSound('Select');
        },

        /** O C++ recusou o toggle (formação cheia, último ativo…). */
        onToggleRejected(reason) {
            playSound('Cancel');
            log(`Party: toggle recusado — ${reason || 'sem motivo informado'}`);
        },

        close() {
            if (isClosing) return;
            isClosing = true;

            const lay = layout();
            const sec = section();

            const finish = () => {
                if (sec) sec.classList.remove('active');
                if (lay) {
                    lay.classList.remove('anim-out');
                    lay.style.opacity = '0';
                }
                UIState.set('menu_open');

                // AVISA O C++. Sem isto o subsystem continua achando que a tela
                // de Party está aberta, e a próxima tela que o menu tentar abrir
                // é recusada por "estado atual não permite" — o menu trava.
                // Todas as telas que voltam para o menu fazem isto.
                const bridge = getBridge();
                if (bridge && typeof bridge.onuistatechanged === 'function') {
                    bridge.onuistatechanged('menu_open');
                }

                // Devolve o menu de pausa exatamente como estava.
                if (window.JRPGUI && typeof JRPGUI.reopenFromOptions === 'function') {
                    JRPGUI.reopenFromOptions();
                }
                if (typeof kickUIRepaint === 'function') kickUIRepaint();
                isClosing = false;
            };

            playSound('Cancel');
            if (lay) {
                lay.classList.add('anim-out');
                setTimeout(finish, 260);
            } else {
                finish();
            }
        },

        forceCloseIfVisible() {
            const sec = section();
            if (sec) sec.classList.remove('active');
            const lay = layout();
            if (lay) {
                lay.classList.remove('anim-out');
                lay.style.opacity = '0';
            }
            isClosing = false;
        },

        handleInput(action) {
            if (isClosing) return;
            switch (action) {
                case 'up':      highlight(index - 1, true); break;
                case 'down':    highlight(index + 1, true); break;
                case 'confirm': toggleCurrent(); break;
                case 'cancel':  api.close(); break;
                default: break;
            }
        },

        reset() {
            const sec = section();
            if (sec) sec.classList.remove('active');
            const lay = layout();
            if (lay) {
                lay.classList.remove('anim-out');
                lay.style.opacity = '0';
            }
            const el = list();
            if (el) el.innerHTML = '';
            data = null;
            index = 0;
            isClosing = false;
        }
    };

    // --- Teclado local (mesmas ações semânticas — testável em browser comum) ---
    //
    // Dentro do jogo o Escape chega por triggerAnimateOutAndClose(), injetado
    // pelo C++. Fora dele, é ESTE listener que faz a tela responder — por isso
    // toda seção do shell tem o seu.
    window.addEventListener('keydown', (event) => {
        if (UIState.get() !== 'party_open') return;

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
            default:
                break;
        }
    });

    return api;
})();
