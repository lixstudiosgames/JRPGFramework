// 85_items.js — TELA DE ITENS (inventário)
//
// Espelha a loja de propósito: mesmos ícones de categoria, mesma barra de
// filtros, mesma navegação (←/→ troca o filtro sem confirmar, ↑/↓ anda na
// lista). A diferença é o filtro "Recentes" e o botão de jogar fora.
//
// Payload do C++ (JRPGItems.open):
//   { gold, items: [ {id,name,cat,qty,ord,price,desc,
//                     healhp,healmp,healap,cure,revive,all,
//                     atk,udf,ldf,
//                     effect,effectvalue,status,element,art,summon,key} ] }
//
// `ord` é o carimbo de aquisição (só cresce) — é o que ordena "Recentes".
// Os efeitos chegam CRUS; quem traduz para texto é este arquivo.
// Campo opcional vazio chega como '' — NUNCA como "None". Ver
// JRPGWebUI::OptionalNameToJS no C++.

window.JRPGItems = (function () {

    const section = () => document.getElementById('screen-items');
    const layout = () => document.getElementById('items-layout');
    const rows = () => document.getElementById('items-list-rows');

    // Mesmos paths da loja — ícone é <path> inline, nunca <use>.
    const CATEGORY_ICONS = {
        all:        'M4 4h7v7H4z M13 4h7v7h-7z M4 13h7v7H4z M13 13h7v7h-7z',
        recent:     'M12 3a9 9 0 1 0 9 9 M12 3v9l6 3 M12 3l-3 3 M12 3l3 3',
        consumable: 'M9 2h6v3l-1 1v3l4 8a3 3 0 0 1-3 4H9a3 3 0 0 1-3-4l4-8V6L9 5z',
        permanent:  'M12 2l2.9 6.3 6.6.8-4.9 4.6 1.3 6.7L12 17l-5.9 3.4 1.3-6.7L2.5 9.1l6.6-.8z',
        artbook:    'M12 6c-2-1.6-5-2.2-8-2.2v14c3 0 6 .6 8 2.2 2-1.6 5-2.2 8-2.2v-14c-3 0-6 .6-8 2.2z M12 6v14',
        key:        'M14 3a6 6 0 1 1-5.2 9L3 18v3h3l1-2h2l1-2h2l1.2-1.2A6 6 0 0 1 14 3z M16 7h.01',
        lure:       'M12 3v7a5 5 0 1 1-5 5 M12 3l3 3 M12 3L9 6',
        weapon:     'M12 2l2.2 3.4V14h-4.4V5.4z M7 14h10 M12 14v6 M9.8 20h4.4',
        armor:      'M12 2l8 3v6c0 5-3.5 9-8 11-4.5-2-8-6-8-11V5z',
        accessory:  'M12 2l5 6-5 8-5-8z M7 8h10'
    };

    const CATEGORY_LABELS = {
        all: 'All', recent: 'Recent', consumable: 'Item', permanent: 'Boost',
        artbook: 'Art', key: 'Key', lure: 'Lure', weapon: 'Weapon',
        armor: 'Armor', accessory: 'Accessory'
    };

    // "recent" logo depois de "all": é o filtro que mais se usa depois de
    // pegar coisa nova. Categorias sem nenhum item são puladas.
    const FILTER_ORDER = ['all', 'recent', 'consumable', 'weapon', 'armor',
                          'accessory', 'artbook', 'permanent', 'lure', 'key'];

    // ------------------------------------------------------------------
    // EFEITOS — o que o item melhora
    //
    // Só o efeito em si; nada de "melhor para o Vahn" (isso é da loja).
    // O mapa cobre os EffectClass que existem no DT_Items; um valor novo cai
    // no fallback e ainda aparece, em vez de sumir da tela.
    // ------------------------------------------------------------------

    // REGRA: efeito que mexe num dos 8 atributos usa a SIGLA — a mesma que
    // aparece na tela de Party e nas boxes da loja. O resto (XP, gold,
    // encontros, drop) continua em texto: não tem sigla, e inventar uma só
    // deixaria a tela ilegível.
    const EFEITOS = {
        // As Waters — valor ABSOLUTO e PERMANENTE, sobem o atributo base.
        // Nao confundir com os _pct logo abaixo, que sao percentuais de
        // acessorio e valem so enquanto estiver equipado.
        hp_max:            { txt: 'Max HP',              perm: true },
        mp_max:            { txt: 'Max MP',              perm: true },
        attack:            { txt: 'ATK',                 perm: true },
        defense:           { txt: 'UDF / LDF',           perm: true },
        speed:             { txt: 'SPD',                 perm: true },
        intelligence:      { txt: 'INT',                 perm: true },
        all_stats:         { txt: 'All Stats',           perm: true },

        hp_max_pct:        { txt: 'Max HP',              pct: true },
        mp_max_pct:        { txt: 'Max MP',              pct: true },
        attack_pct:        { txt: 'ATK',                 pct: true },
        defense_pct:       { txt: 'UDF / LDF',           pct: true },
        udf_pct:           { txt: 'UDF',                 pct: true },
        ldf_pct:           { txt: 'LDF',                 pct: true },
        speed_pct:         { txt: 'SPD',                 pct: true },
        agility_pct:       { txt: 'AGL',                 pct: true },
        intelligence_pct:  { txt: 'INT',                 pct: true },
        art_power_pct:     { txt: 'Art Power',           pct: true },
        xp_pct:            { txt: 'XP Gain',             pct: true },
        gold_pct:          { txt: 'Gold Gain',           pct: true },
        ap_accrual_pct:    { txt: 'AP Gain',             pct: true },
        item_drop_chance:  { txt: 'Drop Rate',           pct: true },
        seru_absorb_chance:{ txt: 'Seru Absorb',         pct: true },
        counter_chance:    { txt: 'Counter Rate',        pct: true },
        escape_chance:     { txt: 'Escape Rate',         pct: true },
        ambush_off_pct:    { txt: 'Ambush Attack',       pct: true },
        ambush_def_pct:    { txt: 'Ambush Defense',      pct: true },
        escape_def_pct:    { txt: 'Escape Defense',      pct: true },

        // Estes são bons QUANDO CAEM — o valor vem negativo ou é redução.
        mp_cost_pct:       { txt: 'MP Cost',             pct: true, menorMelhor: true },
        ap_cost_pct:       { txt: 'AP Cost',             pct: true, menorMelhor: true },
        encounter_pct:     { txt: 'Encounters',          pct: true, menorMelhor: true },

        hp_per_step:       { txt: 'HP per Step' },
        mp_per_step:       { txt: 'MP per Step' },
        ap_per_step:       { txt: 'AP per Step' },
        hp_per_turn:       { txt: 'HP per Turn' },
        mp_per_turn:       { txt: 'MP per Turn' },

        // Sem número: são interruptores.
        revive_once:       { txt: 'Revive Once',              flag: true },
        ap_freeze_100:     { txt: 'AP Always Full',           flag: true },
        first_attack:      { txt: 'Always First',             flag: true },
        last_attack:       { txt: 'Always Last',              flag: true },
        double_attack:     { txt: 'Double Attack',            flag: true },
        ignore_defense:    { txt: 'Ignores Defense',          flag: true },
        berserk:           { txt: 'Berserk',                   flag: true },
        escape_block:      { txt: 'Blocks Escape',            flag: true },
        all_status_def:    { txt: 'All Status Guard',         flag: true },
        all_elemental_def: { txt: 'All Element Guard',        flag: true },
        elemental_def:     { txt: 'Element Guard',            usaElemento: true },
        protect_status:    { txt: 'Guards vs',                usaStatus: true },
        summon_seru:       { txt: 'Summons',                  usaSummon: true }
    };

    function linhaEfeito(label, valor, classe) {
        return `<div class="items-effect ${classe || ''}">
            <span class="items-effect-label">${label}</span>
            <span class="items-effect-value ${classe === 'down' ? 'down' : ''}">${valor}</span>
        </div>`;
    }

    /**
     * Um campo de texto OPCIONAL está realmente preenchido?
     *
     * Rede de segurança para FName vazio: `FName::ToString()` de um NAME_None
     * devolve a palavra "None", que é uma string cheia e portanto truthy. O
     * C++ já manda '' nesses campos (JRPGWebUI::OptionalNameToJS), mas se
     * qualquer payload novo esquecer, o pior que acontece é a linha sumir —
     * e não uma espada anunciar "Teaches Art: None".
     */
    function temValor(v) {
        return !!v && String(v).toLowerCase() !== 'none';
    }

    /** Monta a lista de "o que este item melhora". */
    function efeitosDe(it) {
        const out = [];

        if (it.healhp > 0) out.push(linhaEfeito('HP', `+${it.healhp}`));
        if (it.healmp > 0) out.push(linhaEfeito('MP', `+${it.healmp}`));
        if (it.healap)     out.push(linhaEfeito('AP', 'full', 'flag'));
        if (it.revive)     out.push(linhaEfeito('Revive', 'yes', 'flag'));
        if (it.cure)       out.push(linhaEfeito('Cure Status', 'yes', 'flag'));
        if (it.all)        out.push(linhaEfeito('Target', 'whole party', 'flag'));

        if (it.atk) out.push(linhaEfeito('ATK', `+${it.atk}`));
        if (it.udf) out.push(linhaEfeito('UDF', `+${it.udf}`));
        if (it.ldf) out.push(linhaEfeito('LDF', `+${it.ldf}`));

        // O EffectClass 'summon_seru' ja imprime a linha Summons mais abaixo —
        // sem esta guarda o acessorio mostrava "Summons = Gimard" duas vezes.
        const efeitoJaMostraSummon = EFEITOS[it.effect] && EFEITOS[it.effect].usaSummon;

        if (temValor(it.art))    out.push(linhaEfeito('Teaches Art', it.art, 'flag'));
        if (temValor(it.summon) && !efeitoJaMostraSummon) {
            out.push(linhaEfeito('Summons', it.summon, 'flag'));
        }

        const e = it.effect && EFEITOS[it.effect];
        if (e) {
            if (e.flag) {
                out.push(linhaEfeito(e.txt, 'yes', 'flag'));
            } else if (e.usaElemento) {
                out.push(linhaEfeito(e.txt, temValor(it.element) ? it.element : '—', 'flag'));
            } else if (e.usaStatus) {
                out.push(linhaEfeito(e.txt, temValor(it.status) ? it.status : '—', 'flag'));
            } else if (e.usaSummon) {
                out.push(linhaEfeito(e.txt, temValor(it.summon) ? it.summon : '—', 'flag'));
            } else {
                const v = it.effectvalue || 0;
                const sufixo = e.pct ? '%' : (e.perm ? ' permanently' : '');
                // "menor é melhor" (custo, encontros): mostrar como redução.
                const sinal = e.menorMelhor ? '−' : (v < 0 ? '−' : '+');
                const classe = e.menorMelhor ? '' : (v < 0 ? 'down' : '');
                out.push(linhaEfeito(e.txt, `${sinal}${Math.abs(v)}${sufixo}`, classe));
            }
        } else if (it.effect) {
            // EffectClass que ainda não traduzimos: mostra cru em vez de sumir.
            out.push(linhaEfeito(it.effect, it.effectvalue || '—', 'flag'));
        }

        return out.join('');
    }

    // ------------------------------------------------------------------
    // estado
    // ------------------------------------------------------------------

    let data = null;
    let filtros = [];
    let filtroIndex = 0;
    let listaAtual = [];
    let itemIndex = 0;
    let isClosing = false;
    let confirmAberto = false;
    let confirmSim = false;

    function icone(cat, cls) {
        const path = CATEGORY_ICONS[cat] || CATEGORY_ICONS.consumable;
        return `<svg class="${cls || ''}" viewBox="0 0 24 24" fill="none" ` +
               `stroke="currentColor" stroke-width="1.6" stroke-linejoin="round" ` +
               `stroke-linecap="round"><path d="${path}"/></svg>`;
    }

    // ------------------------------------------------------------------
    // filtros
    // ------------------------------------------------------------------

    function itensDoFiltro(f) {
        const todos = (data && data.items) || [];
        if (f === 'all') return todos.slice();
        if (f === 'recent') {
            // Maior carimbo primeiro. É por isso que o C++ manda `ord`.
            return todos.slice().sort((a, b) => (b.ord || 0) - (a.ord || 0));
        }
        return todos.filter(i => i.cat === f);
    }

    function montarFiltros() {
        const todos = (data && data.items) || [];
        // Categoria sem item nenhum não vira botão.
        filtros = FILTER_ORDER.filter(f =>
            f === 'all' || f === 'recent' || todos.some(i => i.cat === f));
        if (filtros.length === 0) filtros = ['all'];
        if (filtroIndex >= filtros.length) filtroIndex = 0;

        const box = document.getElementById('items-filters');
        if (!box) return;

        box.innerHTML = filtros.map((f, i) => {
            const n = itensDoFiltro(f).length;
            return `<div class="items-filter ${i === filtroIndex ? 'selected' : ''}" data-f="${f}">
                ${icone(f)}
                <span>${CATEGORY_LABELS[f] || f}</span>
                <span class="items-filter-count">${n}</span>
            </div>`;
        }).join('');

        box.querySelectorAll('.items-filter').forEach((el, i) => {
            el.onmouseenter = () => { if (window.InputMode) InputMode.useMouse(); };
            el.onclick = () => trocarFiltro(i);
        });
    }

    function trocarFiltro(i) {
        if (!filtros.length) return;
        filtroIndex = (i + filtros.length) % filtros.length;
        itemIndex = 0;
        montarFiltros();
        montarLista(true);
        playSound('Next');
    }

    // ------------------------------------------------------------------
    // lista
    // ------------------------------------------------------------------

    function montarLista(animar) {
        const el = rows();
        if (!el) return;

        listaAtual = itensDoFiltro(filtros[filtroIndex] || 'all');

        const lista = document.getElementById('items-list');
        if (lista) lista.classList.toggle('is-empty', listaAtual.length === 0);

        el.innerHTML = listaAtual.map(it => `
            <div class="items-row ${it.candiscard === false ? 'locked' : ''}" data-id="${it.id}">
                ${icone(it.cat)}
                <span class="items-row-name">${it.name}</span>
                <span class="items-row-dots"></span>
                ${it.candiscard === false ? '<span class="items-row-lock">BOUND</span>' : ''}
                <span class="items-row-qty">${it.qty}</span>
            </div>`).join('');

        if (animar) {
            el.querySelectorAll('.items-row').forEach((r, i) => {
                r.classList.add('row-rise');
                r.style.animationDelay = `${0.03 * i + 0.10}s`;
            });
        }

        el.querySelectorAll('.items-row').forEach((r, i) => {
            r.onmouseenter = () => {
                if (window.InputMode) InputMode.useMouse();
                selecionar(i, false);
            };
        });

        selecionar(Math.min(itemIndex, Math.max(0, listaAtual.length - 1)), false);
    }

    function selecionar(i, som) {
        const el = rows();
        const linhas = el ? el.querySelectorAll('.items-row') : [];
        if (!linhas.length) {
            mostrarDetalhe(null);
            return;
        }
        itemIndex = (i + linhas.length) % linhas.length;
        linhas.forEach((r, n) => r.classList.toggle('selected', n === itemIndex));
        mostrarDetalhe(listaAtual[itemIndex]);
        if (som) playSound('Next');
    }

    function mostrarDetalhe(it) {
        const nome = document.getElementById('items-detail-name');
        const cat = document.getElementById('items-detail-cat');
        const ic = document.getElementById('items-detail-icon');
        const ef = document.getElementById('items-detail-effects');
        const desc = document.getElementById('items-detail-desc');
        const qtd = document.getElementById('items-detail-qty');

        if (!it) {
            if (nome) nome.textContent = '—';
            if (cat) cat.textContent = '';
            if (ic) ic.innerHTML = '';
            if (ef) ef.innerHTML = '';
            if (desc) desc.textContent = '';
            if (qtd) qtd.textContent = '0';
            return;
        }

        if (nome) nome.textContent = it.name;
        if (cat) cat.textContent = (CATEGORY_LABELS[it.cat] || it.cat).toUpperCase();
        if (ic) ic.innerHTML = icone(it.cat);
        if (ef) ef.innerHTML = efeitosDe(it);
        if (desc) desc.textContent = it.desc || '';
        if (qtd) qtd.textContent = it.qty;
    }

    // ------------------------------------------------------------------
    // jogar fora
    // ------------------------------------------------------------------

    function abrirConfirm() {
        const it = listaAtual[itemIndex];
        if (!it) return;

        // A resposta vem PRONTA do C++ (InventorySubsystem::CanDiscardItem).
        // Deduzir aqui pela categoria deixaria a UI e a regra em desacordo —
        // foi o que acontecia com Art Book, que o JS liberava e o C++ recusava.
        if (it.candiscard === false) {
            playSound('Cancel');
            log(`${it.id} não pode ser descartado.`);
            return;
        }

        confirmAberto = true;
        confirmSim = false;
        const box = document.getElementById('items-confirm');
        const txt = document.getElementById('items-confirm-text');
        if (txt) txt.textContent = `Discard ${it.name}?`;
        if (box) box.classList.add('active');
        pintarConfirm();
        playSound('Select');
    }

    function pintarConfirm() {
        document.querySelectorAll('.items-confirm-item').forEach(el => {
            el.classList.toggle('selected', (el.dataset.answer === 'yes') === confirmSim);
        });
    }

    function fecharConfirm() {
        confirmAberto = false;
        const box = document.getElementById('items-confirm');
        if (box) box.classList.remove('active');
    }

    function confirmarDescarte() {
        const it = listaAtual[itemIndex];
        fecharConfirm();
        if (!confirmSim || !it) {
            playSound('Cancel');
            return;
        }

        const bridge = getBridge();
        if (bridge && typeof bridge.onitemdiscard === 'function') {
            // Quem valida é o C++; a UI só redesenha com o que voltar.
            bridge.onitemdiscard(it.id, 1);
        } else {
            // Ponte offline (Chrome): espelha localmente só para ver o fluxo.
            it.qty -= 1;
            if (it.qty <= 0) {
                data.items = data.items.filter(x => x.id !== it.id);
            }
            montarFiltros();
            montarLista(false);
        }
        playSound('Item');
    }

    // ------------------------------------------------------------------
    // API pública
    // ------------------------------------------------------------------

    const api = {
        open(payload) {
            if (!UIState.canOpen('items')) {
                log(`JRPGItems.open ignorado: estado atual '${UIState.get()}' não permite.`);
                return;
            }
            if (typeof updateUIScaleFactor === 'function') updateUIScaleFactor();

            data = payload || { gold: 0, items: [] };
            filtroIndex = 0;
            itemIndex = 0;
            isClosing = false;
            fecharConfirm();

            const sec = section();
            if (sec) sec.classList.add('active');
            UIState.set('items_open');

            const g = document.getElementById('items-gold');
            if (g) g.textContent = data.gold;

            const pnl = document.getElementById('items-panel');
            if (pnl) {
                pnl.classList.remove('panel-in');
                void pnl.offsetWidth;
                pnl.classList.add('panel-in');
            }

            montarFiltros();
            montarLista(true);
            playSound('Open');

            const lay = layout();
            if (lay) {
                lay.classList.remove('anim-out');
                lay.style.opacity = '1';
            }
            if (typeof kickUIRepaint === 'function') kickUIRepaint();
        },

        /** O C++ mandou o inventário novo (depois de um descarte). */
        update(payload) {
            if (!payload) return;
            data = payload;
            const g = document.getElementById('items-gold');
            if (g) g.textContent = data.gold;
            montarFiltros();
            montarLista(false);
        },

        onDiscardRejected(motivo) {
            playSound('Cancel');
            log(`Itens: descarte recusado — ${motivo || 'sem motivo informado'}`);
        },

        close() {
            if (isClosing) return;
            isClosing = true;
            fecharConfirm();

            const lay = layout();
            const sec = section();

            const fim = () => {
                if (sec) sec.classList.remove('active');
                if (lay) {
                    lay.classList.remove('anim-out');
                    lay.style.opacity = '0';
                }
                UIState.set('menu_open');

                // AVISA O C++: sem isto o subsystem continua achando que esta
                // tela está aberta e recusa a próxima que o menu tentar abrir.
                const bridge = getBridge();
                if (bridge && typeof bridge.onuistatechanged === 'function') {
                    bridge.onuistatechanged('menu_open');
                }
                if (window.JRPGUI && typeof JRPGUI.reopenFromOptions === 'function') {
                    JRPGUI.reopenFromOptions();
                }
                if (typeof kickUIRepaint === 'function') kickUIRepaint();
                isClosing = false;
            };

            playSound('Cancel');
            if (lay) {
                lay.classList.add('anim-out');
                setTimeout(fim, 260);
            } else {
                fim();
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
            fecharConfirm();
            isClosing = false;
        },

        handleInput(action) {
            if (isClosing) return;

            if (confirmAberto) {
                switch (action) {
                    case 'left':
                    case 'right':
                        confirmSim = !confirmSim;
                        pintarConfirm();
                        playSound('Next');
                        break;
                    case 'confirm': confirmarDescarte(); break;
                    case 'cancel':  fecharConfirm(); playSound('Cancel'); break;
                    default: break;
                }
                return;
            }

            switch (action) {
                case 'left':    trocarFiltro(filtroIndex - 1); break;
                case 'right':   trocarFiltro(filtroIndex + 1); break;
                case 'up':      selecionar(itemIndex - 1, true); break;
                case 'down':    selecionar(itemIndex + 1, true); break;
                case 'discard': abrirConfirm(); break;
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
            const el = rows();
            if (el) el.innerHTML = '';
            fecharConfirm();
            data = null;
            filtroIndex = 0;
            itemIndex = 0;
            isClosing = false;
        }
    };

    // Cliques no Sim/Não do overlay
    document.addEventListener('click', (e) => {
        const alvo = e.target.closest && e.target.closest('.items-confirm-item');
        if (!alvo || !confirmAberto) return;
        confirmSim = alvo.dataset.answer === 'yes';
        pintarConfirm();
        confirmarDescarte();
    });

    // --- Teclado local (mesmas ações semânticas — testável em browser comum) ---
    // Dentro do jogo o Escape chega por triggerAnimateOutAndClose(); fora dele
    // é ESTE listener que faz a tela responder.
    window.addEventListener('keydown', (event) => {
        if (UIState.get() !== 'items_open') return;

        switch (event.key) {
            case 'ArrowUp':    case 'w': case 'W': handleUIInput('up');    event.preventDefault(); break;
            case 'ArrowDown':  case 's': case 'S': handleUIInput('down');  event.preventDefault(); break;
            case 'ArrowLeft':  case 'a': case 'A': handleUIInput('left');  event.preventDefault(); break;
            case 'ArrowRight': case 'd': case 'D': handleUIInput('right'); event.preventDefault(); break;
            case 'Enter': case ' ': handleUIInput('confirm'); event.preventDefault(); break;
            case 'Escape': handleUIInput('cancel'); event.preventDefault(); break;
            case 'Delete': case 'Backspace': handleUIInput('discard'); event.preventDefault(); break;
            default: break;
        }
    });

    return api;
})();
