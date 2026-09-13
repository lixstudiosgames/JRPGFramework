// 60_shop.js - Tela de Loja (Shop)
//
// Fluxo: o C++ chama JRPGShop.open(payload) e a tela JÁ ABRE na lista.
// Não existe mais a tela intermediária de escolha Buy/Sell: as duas viraram
// ABAS no topo, trocadas por Q/E (teclado) e LB/RB (controle) — as mesmas
// ações 'tab_prev'/'tab_next' de Options e Save/Load.
//
// Navegação na lista:
//   up/down     -> item
//   left/right  -> filtro de categoria (aplica na hora, sem confirmar)
//   confirm     -> seletor de quantidade -> bridge.onshopbuy/onshopsell
//
// Payload do C++:
//   {name, town, gold,
//    buy:[{id,name,desc,stats,cat,price,featured,owned,atk,udf,ldf,best,others}],
//    sell:[...idem...],
//    party:[{id,name,atk,udf,ldf}]}   // vazio hoje -> placeholders
//
// A ponte é SEMPRE resolvida via getBridge() (00_core.js). Sem setInterval.

window.JRPGShop = (function () {
    const MAX_BUY_QTY = 99;
    const TABS = ['buy', 'sell'];
    const GOLD_ANIM_MS = 420;        // duração do contador de gold
    const GOLD_STEP_MS = 24;         // intervalo entre passos do contador

    // Party placeholder até o PartySubsystem ter estado real. Mesmo contrato do
    // card de detalhes do Save/Load: se o payload trouxer party, ela ganha.
    const PLACEHOLDER_PARTY = [
        { id: 'vahn', name: 'Vahn', atk: 200, udf: 48, ldf: 36 },
        { id: 'noa',  name: 'Noa',  atk: 180, udf: 52, ldf: 40 },
        { id: 'gala', name: 'Gala', atk: 210, udf: 61, ldf: 44 }
    ];

    // Ícones por categoria. Os slugs casam com ItemCategoryToJS (C++,
    // WebUISubsystem.cpp) — mudou lá, muda aqui.
    // Desenhados como <path> inline em vez de <use> num sprite: o Ultralight
    // não tem devtools, e um <use> que falhe some sem erro nenhum.
    const CATEGORY_ICONS = {
        all:        'M4 4h7v7H4z M13 4h7v7h-7z M4 13h7v7H4z M13 13h7v7h-7z',
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
        all: 'All', consumable: 'Item', permanent: 'Boost', artbook: 'Art',
        key: 'Key', lure: 'Lure', weapon: 'Weapon', armor: 'Armor',
        accessory: 'Accessory'
    };

    // Ordem fixa dos filtros na barra (os ausentes no estoque são pulados)
    const FILTER_ORDER = ['all', 'consumable', 'weapon', 'armor', 'accessory',
                          'artbook', 'permanent', 'lure', 'key'];

    // Setas do delta: VERDE sobe, VERMELHO desce (a de descer é a mesma seta
    // girada 180° no CSS). Casa com --shop-up / --shop-down em 60_shop.css —
    // mudou lá, muda aqui.
    const ARROW_FILL = { up: '#35c04a', down: '#e2586b' };

    // --- Estado interno ---
    let data = null;
    let view = null;                 // 'list' | 'qty' | null (fechado)
    let tabIndex = 0;                // 0 = Buy, 1 = Sell
    let filters = ['all'];           // slugs presentes no estoque da aba atual
    let filterIndex = 0;
    let listIndex = 0;
    let qty = 1;
    let rowEls = [];
    let isClosing = false;
    let goldShown = 0;               // valor que o contador está exibindo
    let goldTimer = 0;               // handle do setTimeout do contador (0 = parado)

    // --- Referências de DOM ---
    const section = document.getElementById('screen-shop');
    const layout = document.getElementById('shop-layout');
    const listCard = document.getElementById('shop-list');
    const rowsEl = document.getElementById('shop-list-rows');
    const filtersEl = document.getElementById('shop-filters');
    const partyEl = document.getElementById('shop-party');
    const qtyOverlay = document.getElementById('shop-qty');
    const tabBar = document.getElementById('shop-tabbar');
    const tabItems = document.querySelectorAll('#shop-tabs .jrpg-tab');
    const arrowPrev = document.querySelector('#shop-tabbar .arrow-left');
    const arrowNext = document.querySelector('#shop-tabbar .arrow-right');

    // Blocos que entram/saem em cascata (a cascata em si é transition-delay
    // no CSS). O nome da loja anima separado dos demais dentro do header.
    const animBlocks = [
        document.getElementById('shop-header'),
        document.getElementById('shop-shopname'),
        document.getElementById('shop-topbar'),
        listCard,
        document.getElementById('shop-detail'),
        partyEl
    ];

    function animateIn() {
        animBlocks.forEach(el => { if (el) el.classList.add('anim-in'); });
    }

    function animateOut() {
        animBlocks.forEach(el => { if (el) el.classList.remove('anim-in'); });
    }

    // ============================================================
    // HELPERS
    // ============================================================

    function currentTab() {
        return TABS[tabIndex];
    }

    /** Todos os itens da aba atual, sem filtro de categoria. */
    function tabItemsList() {
        if (!data) return [];
        return (currentTab() === 'buy' ? data.buy : data.sell) || [];
    }

    /** Itens realmente exibidos: aba atual + filtro de categoria ativo. */
    function currentList() {
        const cat = filters[filterIndex] || 'all';
        const list = tabItemsList();
        return cat === 'all' ? list : list.filter(it => (it.cat || 'consumable') === cat);
    }

    function selectedItem() {
        const list = currentList();
        return list.length > 0 ? list[Math.min(listIndex, list.length - 1)] : null;
    }

    function maxQtyFor(item) {
        if (!item) return 1;
        return currentTab() === 'sell' ? Math.max(1, item.owned) : MAX_BUY_QTY;
    }

    function party() {
        return (data && data.party && data.party.length > 0) ? data.party : PLACEHOLDER_PARTY;
    }

    function iconSvg(cat, cls) {
        const path = CATEGORY_ICONS[cat] || CATEGORY_ICONS.consumable;
        return `<svg class="${cls || 'shop-icon'}" viewBox="0 0 24 24" fill="none" ` +
               `stroke="currentColor" stroke-width="1.6" stroke-linejoin="round" ` +
               `stroke-linecap="round"><path d="${path}"/></svg>`;
    }

    // Seta do design (sombra preta deslocada + corpo colorido), inline para não
    // depender de <use>/sprite no Ultralight.
    function arrowSvg(dir) {
        const fill = ARROW_FILL[dir] || ARROW_FILL.up;
        const d = 'M 8,0 L 16,8 L 11,8 L 11,16 L 5,16 L 5,8 L 0,8 Z';
        return `<svg class="shop-stat-arrow${dir === 'down' ? ' arrow-down' : ''}" ` +
               `viewBox="0 0 20 20">` +
               `<path d="${d}" fill="#111111" transform="translate(2,2)"/>` +
               `<path d="${d}" fill="${fill}"/></svg>`;
    }

    /**
     * O que o item selecionado soma neste personagem.
     * Retorna null quando ele não pode equipar (ou o item não é equipamento):
     * aí o box mostra só os valores atuais, sem seta.
     */
    function deltaFor(member, item) {
        if (!item) return null;
        const isGear = (item.cat === 'weapon' || item.cat === 'armor' || item.cat === 'accessory');
        if (!isGear) return null;

        // best vazio = serve para todos; senão só o best e os others equipam
        const best = (item.best || '').toLowerCase();
        const others = (item.others || []).map(o => String(o).toLowerCase());
        const who = String(member.name || member.id || '').toLowerCase();
        if (best && who !== best && others.indexOf(who) === -1) return null;

        return { atk: item.atk || 0, udf: item.udf || 0, ldf: item.ldf || 0 };
    }

    // ============================================================
    // RENDERIZAÇÃO
    // ============================================================

    /**
     * Gold com contador: em vez de trocar o número de uma vez, corre do valor
     * anterior até o novo. Só roda enquanto anima (nada de rAF permanente, que
     * faria a UL thread redesenhar a sessão inteira).
     * instant = true no open, onde não há "valor anterior" que faça sentido.
     */
    function renderGold(target, instant) {
        const el = document.getElementById('shop-gold');
        if (goldTimer) {
            clearTimeout(goldTimer);
            goldTimer = 0;
        }

        if (instant || goldShown === target) {
            goldShown = target;
            el.textContent = String(target);
            return;
        }

        // setTimeout e não requestAnimationFrame: no Ultralight o rAF só roda
        // quando a página está com damage, e o contador precisa de passos
        // regulares mesmo com a tela parada. Contar PASSOS (em vez de ler o
        // relógio) também mantém o número previsível se um tick atrasar.
        const start = goldShown;
        const steps = Math.max(1, Math.round(GOLD_ANIM_MS / GOLD_STEP_MS));
        let step = 0;

        const tick = () => {
            step++;
            const t = Math.min(1, step / steps);
            // easeOutCubic: arranca rápido e assenta no valor final
            const eased = 1 - Math.pow(1 - t, 3);
            goldShown = Math.round(start + (target - start) * eased);
            el.textContent = String(goldShown);

            if (t < 1) {
                goldTimer = setTimeout(tick, GOLD_STEP_MS);
            } else {
                goldTimer = 0;
                goldShown = target;
                el.textContent = String(target);
            }
        };
        goldTimer = setTimeout(tick, GOLD_STEP_MS);
    }

    function renderHeader(instantGold) {
        if (!data) return;
        document.getElementById('shop-title').textContent = data.name || 'Shop';
        document.getElementById('shop-town').textContent = data.town || '';
        renderGold(data.gold || 0, instantGold);
    }

    /**
     * Monta a barra de filtros com 'all' + só as categorias que a aba atual
     * realmente tem. Preserva o filtro ativo quando ele ainda existe; se sumiu
     * (trocou de aba), volta para 'all'.
     */
    function buildFilters() {
        const previous = filters[filterIndex] || 'all';

        const present = {};
        tabItemsList().forEach(it => { present[it.cat || 'consumable'] = true; });
        filters = FILTER_ORDER.filter(cat => cat === 'all' || present[cat]);

        const found = filters.indexOf(previous);
        filterIndex = found >= 0 ? found : 0;

        filtersEl.innerHTML = '';
        filters.forEach((cat, idx) => {
            const el = document.createElement('div');
            el.className = 'shop-filter' + (idx === filterIndex ? ' selected' : '');
            el.setAttribute('data-cat', cat);
            el.innerHTML = iconSvg(cat) + `<span>${CATEGORY_LABELS[cat] || cat}</span>`;

            // Hover só destaca (CSS); o clique é que troca o filtro.
            el.addEventListener('click', () => {
                if (view === 'list') setFilter(idx);
            });

            filtersEl.appendChild(el);
        });
    }

    function highlightFilters() {
        filtersEl.querySelectorAll('.shop-filter').forEach((el, idx) => {
            el.classList.toggle('selected', idx === filterIndex);
        });
    }

    // animate = true só ao entrar na tela / trocar de aba ou filtro. O rebuild
    // depois de uma transação passa false, senão a lista pisca a cada compra.
    function buildList(animate) {
        rowsEl.innerHTML = '';
        rowEls = [];

        const list = currentList();
        listCard.classList.toggle('list-empty', list.length === 0);
        if (listIndex >= list.length) {
            listIndex = Math.max(0, list.length - 1);
        }

        list.forEach((item, idx) => {
            const row = document.createElement('div');
            row.className = 'shop-row'
                + (item.featured ? ' featured-item' : '')
                + (animate ? ' row-rise' : '');
            if (currentTab() === 'buy' && data && item.price > data.gold) {
                row.classList.add('cant-afford');
            }
            if (animate) {
                row.style.animationDelay = (idx * 22) + 'ms';
            }

            const name = document.createElement('span');
            name.className = 'shop-row-name';
            if (item.featured) {
                const star = document.createElement('span');
                star.className = 'featured-star';
                star.textContent = '★';
                name.appendChild(star);
            }
            name.appendChild(document.createTextNode(item.name));

            const price = document.createElement('span');
            price.className = 'shop-row-price';
            price.textContent = `${item.price} G`;

            // Só o número: 0 fica apagado, ter algum acende.
            const owned = document.createElement('span');
            owned.className = 'shop-row-owned' + (item.owned > 0 ? '' : ' owned-none');
            owned.textContent = String(item.owned);

            row.innerHTML = iconSvg(item.cat || 'consumable');
            row.appendChild(name);
            row.appendChild(price);
            row.appendChild(owned);

            row.addEventListener('click', () => {
                if (view === 'list') {
                    selectRow(idx);
                    enterQty();
                }
            });

            rowsEl.appendChild(row);
            rowEls.push(row);
        });

        highlightRow();
    }

    function highlightRow() {
        rowEls.forEach((el, idx) => {
            el.classList.toggle('selected', idx === listIndex);
        });

        const sel = rowEls[listIndex];
        if (sel && typeof sel.scrollIntoView === 'function') {
            sel.scrollIntoView({ block: 'nearest' });
        }
        updateDetail();
        renderParty();
    }

    function updateDetail() {
        const item = selectedItem();
        const cat = item ? (item.cat || 'consumable') : 'all';

        document.getElementById('shop-detail-icon').innerHTML = item ? iconSvg(cat) : '';
        document.getElementById('shop-detail-name').textContent = item ? item.name : '—';
        document.getElementById('shop-detail-cat').textContent =
            item ? (CATEGORY_LABELS[cat] || cat) : '';
        document.getElementById('shop-detail-desc').textContent = item ? (item.desc || '') : '';

        // "Best for" só faz sentido em equipamento: um consumível não tem dono
        // preferido, e a linha vazia só polui o card.
        const isGear = !!item && (cat === 'weapon' || cat === 'armor' || cat === 'accessory');
        const bestRow = document.getElementById('shop-detail-best');
        bestRow.style.display = isGear ? 'flex' : 'none';
        if (isGear) {
            document.getElementById('shop-detail-best-name').textContent = item.best ? item.best : 'All';
        }

        // Bônus com o valor em cor de atributo (ATK vermelho, UDF azul, LDF verde)
        const bonusEl = document.getElementById('shop-detail-bonus');
        bonusEl.innerHTML = '';
        if (item) {
            const bonuses = [
                { key: 'atk', label: 'ATK', value: item.atk || 0 },
                { key: 'udf', label: 'UDF', value: item.udf || 0 },
                { key: 'ldf', label: 'LDF', value: item.ldf || 0 }
            ].filter(b => b.value !== 0);

            bonuses.forEach(b => {
                const span = document.createElement('span');
                span.className = 'shop-bonus';
                span.innerHTML =
                    `<span class="shop-bonus-label">${b.label}</span>` +
                    `<span class="shop-bonus-val stat-${b.key}">` +
                    `${b.value > 0 ? '+' : ''}${b.value}</span>`;
                bonusEl.appendChild(span);
            });
        }
    }

    /**
     * Os 3 boxes ficam SEMPRE visíveis com os valores atuais. A seta e o valor
     * novo só entram quando o item selecionado é equipamento que aquele
     * personagem consegue usar (ver deltaFor).
     */
    function renderParty() {
        const item = selectedItem();
        partyEl.innerHTML = '';

        party().forEach(member => {
            const delta = deltaFor(member, item);
            const box = document.createElement('div');
            box.className = 'shop-char jrpg-card' + (delta ? ' can-equip' : '');

            const rows = ['atk', 'udf', 'ldf'].map(key => {
                const current = member[key] || 0;
                const diff = delta ? (delta[key] || 0) : 0;

                let tail = '';
                if (diff !== 0) {
                    const dir = diff > 0 ? 'up' : 'down';
                    tail = arrowSvg(dir) +
                        `<span class="shop-stat-new delta-${dir}">${current + diff}</span>`;
                }

                return `<div class="shop-stat">` +
                    `<span class="shop-stat-label stat-${key}">${key.toUpperCase()}</span>` +
                    `<span class="shop-stat-cur">${current}</span>${tail}</div>`;
            }).join('');

            box.innerHTML =
                `<span class="corner tl"></span><span class="corner tr"></span>` +
                `<span class="corner bl"></span><span class="corner br"></span>` +
                `<div class="shop-char-portrait"><img src="images/${member.id}.png" alt=""></div>` +
                `<div class="shop-char-info">` +
                `<div class="shop-char-name">${member.name || member.id}</div>` +
                rows +
                `</div>`;

            partyEl.appendChild(box);
        });
    }

    function renderQty() {
        const item = selectedItem();
        if (!item) return;

        document.getElementById('shop-qty-item').textContent = item.name;
        document.getElementById('shop-qty-value').textContent = String(qty);

        const total = item.price * qty;
        const totalEl = document.getElementById('shop-qty-total-value');
        totalEl.textContent = `${total} G`;
        totalEl.classList.toggle('cant-afford', currentTab() === 'buy' && data && total > data.gold);
    }

    function showView(newView) {
        view = newView;
        qtyOverlay.classList.toggle('view-active', newView === 'qty');
    }

    function highlightTab() {
        tabItems.forEach((item, idx) => {
            item.classList.toggle('selected', idx === tabIndex);
        });
    }

    // Feedback visual da setinha ao trocar de aba (mesmo truque de Options)
    function kickArrow(el) {
        if (!el) return;
        el.classList.remove('arrow-kick');
        void el.offsetHeight;
        el.classList.add('arrow-kick');
    }

    // ============================================================
    // AÇÕES
    // ============================================================

    function setTab(index, silent) {
        const previous = tabIndex;
        if (index < 0) index = TABS.length - 1;
        if (index >= TABS.length) index = 0;
        if (index === tabIndex) return;

        tabIndex = index;
        listIndex = 0;
        highlightTab();
        buildFilters();
        buildList(true);

        if (!silent) {
            playSound('Select');
            const wentBack = (index === TABS.length - 1 && previous === 0) ||
                             (index < previous && !(index === 0 && previous === TABS.length - 1));
            kickArrow(wentBack ? arrowPrev : arrowNext);
        }
    }

    // O filtro aplica na hora, sem confirmar (pedido do design).
    function setFilter(index) {
        if (filters.length === 0) return;
        if (index < 0) index = filters.length - 1;
        if (index >= filters.length) index = 0;
        if (index === filterIndex) return;

        playSound('Next');
        filterIndex = index;
        listIndex = 0;
        highlightFilters();
        buildList(true);
    }

    function selectRow(idx) {
        if (idx === listIndex) return;
        playSound('Next');
        listIndex = idx;
        highlightRow();
    }

    function enterQty() {
        const item = selectedItem();
        if (!item) {
            playSound('Cancel');
            return;
        }
        playSound('Select');
        qty = 1;
        renderQty();
        showView('qty');
    }

    function changeQty(delta) {
        const item = selectedItem();
        if (!item) return;
        const max = maxQtyFor(item);
        const next = Math.max(1, Math.min(max, qty + delta));
        if (next !== qty) {
            playSound('Next');
            qty = next;
            renderQty();
        }
    }

    function confirmQty() {
        const item = selectedItem();
        if (!item) return;

        const bridge = getBridge();
        const fn = currentTab() === 'buy' ? 'onshopbuy' : 'onshopsell';
        log(`Shop: ${fn}(${item.id}, ${qty})`);

        showView('list');
        if (bridge && typeof bridge[fn] === 'function') {
            bridge[fn](item.id, qty);
        } else {
            log('Ponte offline: transação simulada no modo de teste web.');
            api.onTransactionResult(true);
        }
    }

    // ============================================================
    // API PÚBLICA (chamada pelo C++ e pelo dispatcher de input)
    // ============================================================

    const api = {
        // C++: abre a loja JÁ NA LISTA, aba Buy, filtro All.
        open(payload) {
            if (!UIState.canOpen('shop')) {
                log(`JRPGShop.open ignorado: estado atual '${UIState.get()}' não permite abrir.`);
                return;
            }

            // Garante a escala correta mesmo se a janela mudou com a UI escondida
            if (typeof updateUIScaleFactor === 'function') updateUIScaleFactor();

            data = payload || null;
            isClosing = false;
            tabIndex = 0;
            filterIndex = 0;
            listIndex = 0;
            qty = 1;

            // flare-instant: o flare da aba selecionada nasce no tamanho final.
            // Sem isso o `width: 0 -> 82%` roda ao sair de display:none e o
            // flare pisca como um ponto antes de virar faixa.
            section.classList.add('flare-instant');
            section.classList.add('active');
            void section.offsetHeight;
            setTimeout(() => section.classList.remove('flare-instant'), 80);

            UIState.set('shop_open');

            renderHeader(true);
            highlightTab();
            buildFilters();
            // Sem a cascata das linhas aqui: a lista inteira já entra deslizando
            // como bloco, e as duas animações juntas ficam carregadas. O
            // row-rise fica para as trocas de aba/filtro.
            buildList(false);
            showView('list');

            playSound('Open');
            layout.style.opacity = '1';

            // O reflow estabelece o estado inicial (deslocado) com a seção já
            // visível — sem ele o browser não tem de onde transicionar e os
            // blocos aparecem prontos, sem animação.
            animateOut();
            void layout.offsetHeight;
            animateIn();

            // Garante a apresentação dos primeiros frames (abrir a partir de uma
            // página quase-vazia às vezes perdia o frame e a loja ficava invisível
            // até a primeira navegação)
            if (typeof kickUIRepaint === 'function') kickUIRepaint();
        },

        // C++: payload atualizado após uma transação (gold/possuídos/aba de venda)
        update(payload) {
            data = payload || data;
            renderHeader(false);
            if (view === 'list' || view === 'qty') {
                // A lista de venda pode ter encolhido (vendeu tudo de um item)
                buildFilters();
                buildList(false);
                if (view === 'qty') {
                    // Transação concluída — volta para a lista
                    showView('list');
                }
            }
        },

        // C++: resultado da transação (feedback sonoro)
        onTransactionResult(ok) {
            playSound(ok ? 'Item' : 'Cancel');
            if (!ok) log('Shop: transação recusada (ver log do Unreal).');
        },

        // Fecha a loja devolvendo o input ao jogo (padrão timed-close do shell)
        close() {
            if (view === null || isClosing) return;
            isClosing = true;

            playSound('Close');
            // Os blocos saem primeiro; o layout só some no fim, senão o fade
            // global engoliria a animação de saída.
            animateOut();

            setTimeout(() => {
                layout.style.opacity = '0';
                section.classList.remove('active');
                section.classList.remove('flare-instant');
                showView(null);
                UIState.set('hud_only');
                isClosing = false;

                // Garante a apresentação do frame final (página vazia)
                if (typeof kickUIRepaint === 'function') kickUIRepaint();

                const bridge = getBridge();
                if (bridge && typeof bridge.closemenu === 'function') {
                    bridge.closemenu();
                } else {
                    log('Ponte offline: loja fechada no modo de teste web.');
                }
            }, 260);
        },

        // Entrada semântica (teclado e gamepad convergem aqui).
        // Abas por 'tab_prev'/'tab_next' (Q/E, LB/RB); left/right ficam livres
        // para os filtros de categoria.
        handleInput(action) {
            if (view === null || isClosing) return;

            switch (view) {
                case 'list': {
                    const list = currentList();
                    if (action === 'tab_prev') {
                        setTab(tabIndex - 1);
                    } else if (action === 'tab_next') {
                        setTab(tabIndex + 1);
                    } else if (action === 'left') {
                        setFilter(filterIndex - 1);
                    } else if (action === 'right') {
                        setFilter(filterIndex + 1);
                    } else if (action === 'up' && list.length > 0) {
                        selectRow((listIndex + list.length - 1) % list.length);
                    } else if (action === 'down' && list.length > 0) {
                        selectRow((listIndex + 1) % list.length);
                    } else if (action === 'confirm') {
                        enterQty();
                    } else if (action === 'cancel') {
                        // Não há mais tela de escolha para voltar: cancela fecha
                        api.close();
                    }
                    break;
                }

                case 'qty':
                    if (action === 'left') {
                        changeQty(-1);
                    } else if (action === 'right') {
                        changeQty(1);
                    } else if (action === 'up') {
                        changeQty(10);
                    } else if (action === 'down') {
                        changeQty(-10);
                    } else if (action === 'confirm') {
                        confirmQty();
                    } else if (action === 'cancel') {
                        playSound('Cancel');
                        showView('list');
                    }
                    break;
            }
        },

        // Limpa estado herdado (chamado pelo resetUIShell entre sessões PIE)
        reset() {
            data = null;
            view = null;
            tabIndex = 0;
            filters = ['all'];
            filterIndex = 0;
            listIndex = 0;
            qty = 1;
            rowEls = [];
            isClosing = false;
            if (goldTimer) { clearTimeout(goldTimer); goldTimer = 0; }
            goldShown = 0;
            layout.style.opacity = '0';
            section.classList.remove('active');
            section.classList.remove('flare-instant');
            qtyOverlay.classList.remove('view-active');
            animateOut();
        }
    };

    // --- Mouse nas abas (as setinhas ◀ ▶ são só visuais, sem clique) ---

    tabItems.forEach((item, idx) => {
        item.addEventListener('click', () => {
            if (view !== 'list') return;
            setTab(idx);
        });
    });

    // --- Mouse no seletor de quantidade ---

    const qtyMinus = document.getElementById('shop-qty-minus');
    const qtyPlus = document.getElementById('shop-qty-plus');
    const qtyCard = document.getElementById('shop-qty-card');
    if (qtyMinus) qtyMinus.addEventListener('click', (e) => {
        if (view === 'qty') { e.stopPropagation(); changeQty(-1); }
    });
    if (qtyPlus) qtyPlus.addEventListener('click', (e) => {
        if (view === 'qty') { e.stopPropagation(); changeQty(1); }
    });
    if (qtyCard) qtyCard.addEventListener('click', (e) => {
        // Clique no card (fora das setas) confirma a transação
        if (view === 'qty' && e.target !== qtyMinus && e.target !== qtyPlus) confirmQty();
    });

    // --- Teclado local (mesmas ações semânticas — testável em browser comum) ---

    window.addEventListener('keydown', (event) => {
        if (UIState.get() !== 'shop_open') return;

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
