// 90_devmenu.js — DEV MENU
//
// Tela de teste. Todo botão monta uma linha de comando e manda pelo MESMO
// caminho da ponte (bridge.ondevcommand); o C++ responde chamando
// JRPGDev.result(texto). Botão novo = só mais um data-cmd no HTML.
//
// A ponte é resolvida SEMPRE lazy via getBridge() — capturar numa const fica
// stale entre sessões PIE.

window.JRPGDev = (function () {

    const section = () => document.getElementById('screen-dev');
    const out = () => document.getElementById('dev-out');

    let tabIndex = 0;
    let wired = false;
    const history = [];
    let historyPos = -1;

    // ------------------------------------------------------------------
    // console
    // ------------------------------------------------------------------

    function write(text, cls) {
        const el = out();
        if (!el) return;
        const line = document.createElement('span');
        if (cls) line.className = cls;
        line.textContent = text + '\n';
        el.appendChild(line);
        el.scrollTop = el.scrollHeight;
    }

    function send(cmd) {
        if (!cmd) return;
        write('> ' + cmd, 'dev-echo');

        history.push(cmd);
        historyPos = history.length;

        const bridge = getBridge();
        if (bridge && typeof bridge.ondevcommand === 'function') {
            bridge.ondevcommand(cmd);
        } else {
            // Fora do jogo (Chrome) a ponte não existe: dá para conferir o
            // layout mesmo assim.
            write('(sem ponte — rodando fora do jogo)', 'dev-fail');
        }
    }

    // ------------------------------------------------------------------
    // templates: "prog.stat {char} {stat} {level}" -> lê os campos da tela
    // ------------------------------------------------------------------

    const FIELDS = {
        char:  'dev-char',
        stat:  'dev-stat',
        level: 'dev-level',
        item:  'dev-item',
        qty:   'dev-qty',
        flag:  'dev-flag',
        slot:  'dev-slot',
        shop:  'dev-shop',
        member: 'dev-member',
        plevel: 'dev-plevel',
        pstat:  'dev-pstat',
        pval:   'dev-pval'
    };

    function fill(tpl) {
        return tpl.replace(/\{(\w+)\}/g, (whole, key) => {
            const id = FIELDS[key];
            const el = id && document.getElementById(id);
            if (!el) return whole;
            // espaço quebraria o parse do lado do C++ (split por espaço)
            return String(el.value).trim().replace(/\s+/g, '_');
        });
    }

    // ------------------------------------------------------------------
    // abas
    // ------------------------------------------------------------------

    function showTab(i) {
        const root = section();
        if (!root) return;
        const tabs = root.querySelectorAll('.dev-tab');
        const panels = root.querySelectorAll('.dev-panel');
        if (!tabs.length) return;

        tabIndex = (i + tabs.length) % tabs.length;
        tabs.forEach((t, n) => t.classList.toggle('selected', n === tabIndex));
        panels.forEach((p, n) => p.classList.toggle('active', n === tabIndex));
    }

    // ------------------------------------------------------------------
    // wiring (uma vez só — o documento sobrevive entre sessões PIE)
    // ------------------------------------------------------------------

    function wire() {
        if (wired) return;
        const root = section();
        if (!root) return;
        wired = true;

        root.querySelectorAll('.dev-tab').forEach(tab => {
            tab.onclick = () => showTab(parseInt(tab.dataset.tab, 10) || 0);
        });

        root.querySelectorAll('[data-cmd]').forEach(btn => {
            btn.onclick = () => send(btn.dataset.cmd);
        });

        root.querySelectorAll('[data-tpl]').forEach(btn => {
            btn.onclick = () => send(fill(btn.dataset.tpl));
        });

        const close = document.getElementById('dev-close');
        if (close) close.onclick = () => api.close();

        const clear = document.getElementById('dev-clear');
        if (clear) clear.onclick = () => { const el = out(); if (el) el.textContent = ''; };

        const cmd = document.getElementById('dev-cmd');
        if (cmd) {
            cmd.onkeydown = (e) => {
                if (e.key === 'Enter') {
                    send(cmd.value.trim());
                    cmd.value = '';
                } else if (e.key === 'ArrowUp' && history.length) {
                    historyPos = Math.max(0, historyPos - 1);
                    cmd.value = history[historyPos] || '';
                } else if (e.key === 'ArrowDown' && history.length) {
                    historyPos = Math.min(history.length, historyPos + 1);
                    cmd.value = history[historyPos] || '';
                }
                // Esc cai no dispatcher global (99_boot) e fecha a tela
            };
        }
    }

    // ------------------------------------------------------------------
    // API pública (chamada pelo C++ e pelo dispatcher de input)
    // ------------------------------------------------------------------

    const api = {
        open() {
            if (!UIState.canOpen('dev')) {
                log(`JRPGDev.open ignorado: estado atual '${UIState.get()}' não permite.`);
                return;
            }
            if (typeof updateUIScaleFactor === 'function') updateUIScaleFactor();

            wire();
            showTab(tabIndex);

            const root = section();
            if (root) root.classList.add('active');
            UIState.set('dev_open');

            // O dev menu é ferramenta de mouse: garante o ponteiro visível.
            if (window.InputMode) InputMode.useMouse();

            // Abrir de uma página quase-vazia pode perder o frame no Ultralight.
            if (typeof kickUIRepaint === 'function') kickUIRepaint();
        },

        /** O C++ devolveu a saída de um comando. */
        result(text) {
            write(String(text === undefined ? '' : text));
        },

        close() {
            const root = section();
            if (root) root.classList.remove('active');
            UIState.set('hud_only');

            const bridge = getBridge();
            if (bridge && typeof bridge.closemenu === 'function') {
                bridge.closemenu();
            }
            if (typeof kickUIRepaint === 'function') kickUIRepaint();
        },

        /** Esconde sem falar com o C++ — usado quando outra tela assume. */
        forceCloseIfVisible() {
            const root = section();
            if (root) root.classList.remove('active');
        },

        handleInput(action) {
            if (action === 'cancel') { api.close(); return; }
            if (action === 'left')  { showTab(tabIndex - 1); return; }
            if (action === 'right') { showTab(tabIndex + 1); return; }
        },

        reset() {
            const root = section();
            if (root) root.classList.remove('active');
            const el = out();
            if (el) el.textContent = '';
            tabIndex = 0;
            history.length = 0;
            historyPos = -1;
        }
    };

    return api;
})();


// ============================================================
// JRPGDemo — TESTE NO NAVEGADOR (sem o Unreal)
//
// O dev menu acima nao funciona fora do jogo: os botoes dele so montam uma
// linha de comando e mandam para bridge.ondevcommand, que e do C++. Fora do
// jogo nao ha ponte, entao nada acontece.
//
// Estes atalhos montam o payload e chamam a tela direto. No console:
//
//     JRPGDemo.party()      // tela de Party com 4 personagens
//     JRPGDemo.party(1)     // so o Vahn
//     JRPGDemo.menu(3)      // menu de pausa com 3 cards de status
//     JRPGDemo.dev()        // abre o dev menu (visual; os botoes ficam mudos)
// ============================================================

window.JRPGDemo = (function () {

    // Fichas de exemplo, na ordem do roster.
    const FICHAS = [
        { id:'Vahn',  name:'Vahn',  portrait:'vahn',  lv:24, hp:1180, maxhp:1402,
          mp:180, maxmp:232, ap:48, maxap:100,
          atk:122, udf:106, ldf:104, spd:120, int:112, agl:143,
          active:true,  available:true,  dead:false },
        { id:'Noa',   name:'Noa',   portrait:'noa',   lv:22, hp:0,    maxhp:1180,
          mp:90,  maxmp:170, ap:12, maxap:100,
          atk:104, udf:92,  ldf:99,  spd:148, int:88,  agl:158,
          active:true,  available:true,  dead:true },
        { id:'Gala',  name:'Gala',  portrait:'gala',  lv:20, hp:1290, maxhp:1290,
          mp:210, maxmp:240, ap:0,  maxap:100,
          atk:130, udf:130, ldf:110, spd:96,  int:120, agl:118,
          active:false, available:true,  dead:false },
        { id:'Terra', name:'Terra', portrait:'terra', lv:18, hp:900,  maxhp:900,
          mp:230, maxmp:230, ap:0,  maxap:100,
          atk:96,  udf:70,  ldf:64,  spd:130, int:80,  agl:214,
          active:false, available:false, dead:false }
    ];

    function fatiar(quantos) {
        const n = Math.max(1, Math.min(FICHAS.length, quantos || FICHAS.length));
        // copia, para mexer no console nao sujar as fichas de origem
        return JSON.parse(JSON.stringify(FICHAS.slice(0, n)));
    }

    return {
        /** Abre a tela de Party com N personagens (default: todos). */
        party(quantos, max) {
            UIState.set('menu_open');       // a Party so abre a partir do menu
            JRPGParty.open({ max: max || 3, members: fatiar(quantos) });
            return 'JRPGParty.open() chamado';
        },

        /** Abre o menu de pausa com N cards de status. */
        menu(quantos) {
            UIState.set('hud_only');
            JRPGSetParty(fatiar(quantos).map(m => ({
                id: m.id, name: m.name, portrait: m.portrait, lv: m.lv,
                hp: m.hp, maxhp: m.maxhp, mp: m.mp, maxmp: m.maxmp,
                ap: m.ap, maxap: m.maxap
            })));
            JRPGUI.openMenu();
            updateGoldTime(4820, 8047);
            return 'JRPGUI.openMenu() chamado';
        },

        /** Abre a tela de Itens com um inventario de exemplo. */
        items(gold) {
            UIState.set('menu_open');       // a tela so abre a partir do menu
            JRPGItems.open({
                gold: (gold === undefined) ? 4820 : gold,
                items: [
                    { id:'healing_leaf', name:'Healing Leaf', cat:'consumable', qty:12, ord:9,
                      price:20, desc:'Uma folha comum que fecha ferimentos leves.',
                      healhp:100, healmp:0, healap:false, cure:false, revive:false, all:false,
                      atk:0, udf:0, ldf:0, effect:'', effectvalue:0, status:'', element:'',
                      art:'', summon:'', key:false, candiscard:true },
                    { id:'magic_leaf', name:'Magic Leaf', cat:'consumable', qty:5, ord:14,
                      price:60, desc:'Restaura a energia magica.',
                      healhp:0, healmp:50, healap:false, cure:false, revive:false, all:false,
                      atk:0, udf:0, ldf:0, effect:'', effectvalue:0, status:'', element:'',
                      art:'', summon:'', key:false, candiscard:true },
                    { id:'revival_ring', name:'Revival Ring', cat:'accessory', qty:1, ord:22,
                      price:9000, desc:'Traz o portador de volta uma unica vez.',
                      healhp:0, healmp:0, healap:false, cure:false, revive:false, all:false,
                      atk:0, udf:0, ldf:0, effect:'revive_once', effectvalue:0, status:'',
                      element:'', art:'', summon:'', key:false, candiscard:true },
                    { id:'power_ring', name:'Power Ring', cat:'accessory', qty:1, ord:7,
                      price:6000, desc:'O poder corre pelo braco de quem usa.',
                      healhp:0, healmp:0, healap:false, cure:false, revive:false, all:false,
                      atk:0, udf:0, ldf:0, effect:'attack_pct', effectvalue:20, status:'',
                      element:'', art:'', summon:'', key:false, candiscard:true },
                    { id:'silent_bell', name:'Silent Bell', cat:'accessory', qty:1, ord:3,
                      price:4000, desc:'Os monstros parecem nao notar voce.',
                      healhp:0, healmp:0, healap:false, cure:false, revive:false, all:false,
                      atk:0, udf:0, ldf:0, effect:'encounter_pct', effectvalue:50, status:'',
                      element:'', art:'', summon:'', key:false, candiscard:true },
                    { id:'battle_axe', name:'Battle Axe', cat:'weapon', qty:1, ord:18,
                      price:3200, desc:'Pesado, lento, e resolve.',
                      healhp:0, healmp:0, healap:false, cure:false, revive:false, all:false,
                      atk:98, udf:0, ldf:0, effect:'', effectvalue:0, status:'', element:'',
                      art:'', summon:'', key:false, candiscard:true },
                    { id:'iron_armor', name:'Iron Armor', cat:'armor', qty:1, ord:11,
                      price:2400, desc:'Protecao honesta, sem firulas.',
                      healhp:0, healmp:0, healap:false, cure:false, revive:false, all:false,
                      atk:0, udf:34, ldf:28, effect:'', effectvalue:0, status:'', element:'',
                      art:'', summon:'', key:false, candiscard:true },
                    { id:'tornado_flame', name:'Tornado Flame', cat:'artbook', qty:1, ord:20,
                      price:1500, desc:'Ensina uma Art de fogo.',
                      healhp:0, healmp:0, healap:false, cure:false, revive:false, all:false,
                      atk:0, udf:0, ldf:0, effect:'', effectvalue:0, status:'', element:'',
                      art:'Tornado Flame', summon:'', key:false, candiscard:false },
                    { id:'platinum_card', name:'Platinum Card', cat:'key', qty:1, ord:25,
                      price:0, desc:'Abre a prateleira de tras das lojas.',
                      healhp:0, healmp:0, healap:false, cure:false, revive:false, all:false,
                      atk:0, udf:0, ldf:0, effect:'', effectvalue:0, status:'', element:'',
                      art:'', summon:'', key:true, candiscard:false }
                ]
            });
            return 'JRPGItems.open() chamado';
        },

        /** Abre o dev menu (o layout funciona; os comandos precisam do C++). */
        dev() {
            UIState.set('hud_only');
            JRPGDev.open();
            JRPGDev.result('(sem C++: os botoes nao executam nada aqui)');
            return 'JRPGDev.open() chamado';
        },

        /** As fichas de exemplo, se voce quiser editar antes de abrir. */
        fichas: FICHAS
    };
})();
