// 97_battle_hud.js — HUD DA LUTA (camada por cima da luta)
//
// Só DESENHA o que o C++ manda, já pronto: nada de regra aqui. Vale em qualquer estado da UI (não passa pelo UIState).
// A única fala com a ponte é a da tela de resultados (o fim da contagem e o "pular"), pelo bridge.onbattleinput do menu da luta.
//
// C++ -> JS (UWebUISubsystem::CallBattleHUD)
//   JRPGBattleHUD.intro({ groups: [{ name: 'Killer Bee * 3', x: 0.42 }], line: 'Ambushed!' })
//       os nomes da abertura: um rótulo por grupo, centrado em x (fração da largura da tela, onde o grupo está), e a linha de
//       formação ('' = nenhuma). Os rótulos que se encostam são afastados igualmente, como o original faz.
//   JRPGBattleHUD.introHide()
//   JRPGBattleHUD.results({ split: true, gold: 120, sounds: { tick, levelUp }, cards: [{ name, lvBefore, lvAfter, expBefore, gained, thresholds: [..],
//                           hp, maxHpBefore, maxHpAfter, mp, maxMpBefore, maxMpAfter }] })
//       a tela de resultados da vitória (UJRPGBattleSceneSubsystem::StartVictoryResults): um card por coluna, da esquerda para a
//       direita (com split, as colunas casam com as vistas da tela dividida e só os divisores do meio têm borda). Um herói por vez:
//       depois de START_DELAY_MS, o EXP ganho desce enquanto o "Next" desce e o total sobe, com o "tic" do contador
//       (sounds.tick, o caminho do asset); passando do próximo level, "LEVEL UP!" (sounds.levelUp) e o Next recomeça. HP / MP em
//       barras e cores como as do menu.
//       thresholds = o EXP total de cada level a partir do de agora + 1 (vazio no teto). O Gold conta junto com o primeiro card.
//   JRPGBattleHUD.resultsHide()
//   JRPGBattleHUD.reset()       tudo some (troca de mapa, reload)
//
// JS -> C++ (bridge.onbattleinput, com a UI em 'battle_open' sem a seção do menu: UWebUISubsystem::OpenBattleUI(false))
//   'results_done'   a contagem do último card acabou (o C++ espera um pouco e sai da luta)
//   'results_skip'   o "pular" (confirmar, cancelar ou o clique no botão): tudo vai para o final e a luta sai já
// Confirmar / cancelar chegam por JRPGBattle.handleInput, que repassa a resultsInput enquanto a tela está de pé.

window.JRPGBattleHUD = (function () {

    const el = (id) => document.getElementById(id);

    // O original: separa dois rótulos que ficam a menos de 20 px (de 320) e prende cada um a 6 px da borda; aqui, no canvas de 1920
    const MIN_GAP = 40;
    const EDGE = 36;
    const STAGE_W = 1920;

    // A tela de resultados: quanto dura a contagem de um card (ms, pelo EXP ganho), a pausa do LEVEL UP e entre os cards
    const COUNT_MIN_MS = 700;
    const COUNT_MAX_MS = 1600;
    const LEVELUP_PAUSE_MS = 900;
    const BETWEEN_CARDS_MS = 350;
    const GOLD_MS = 800;
    /** A espera depois de a tela dividir (a câmera trocar), antes do primeiro card começar a contar. */
    const START_DELAY_MS = 1100;
    /** O "tic" do contador: no máximo um a cada tanto, enquanto um número anda. */
    const TICK_EVERY_MS = 55;

    function esc(text) {
        return String(text === undefined || text === null ? '' : text)
            .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    }

    /** Uma fração da largura da tela para x no canvas 1920 (o canvas é centrado e escalado por --scale-factor). */
    function screenFractionToStage(fraction) {
        // O updateUIScaleFactor (00_core.js) põe o valor inline no <html>
        const scale = parseFloat(document.documentElement.style.getPropertyValue('--scale-factor')) || 1;
        const width = window.innerWidth || STAGE_W;
        const stageLeft = (width - STAGE_W * scale) / 2;
        return (fraction * width - stageLeft) / scale;
    }

    /** O passo de relaxamento do original: separa os pares que se encostam até uma passada não mexer em nada. */
    function relax(items) {
        for (let pass = 0; pass < 32; pass++) {
            let moved = false;
            items.sort((a, b) => a.left - b.left);
            for (let i = 0; i + 1 < items.length; i++) {
                const a = items[i], b = items[i + 1];
                const gap = b.left - (a.left + a.width);
                if (gap < MIN_GAP) {
                    const push = (MIN_GAP - gap) / 2;
                    a.left -= push;
                    b.left += push;
                    moved = true;
                }
            }
            for (const it of items) {
                const clamped = Math.min(Math.max(it.left, EDGE), STAGE_W - EDGE - it.width);
                if (clamped !== it.left) {
                    it.left = clamped;
                    moved = true;
                }
            }
            if (!moved) break;
        }
    }

    function repaint() {
        if (typeof kickUIRepaint === 'function') kickUIRepaint();
    }

    function sendBridge(action) {
        const bridge = (typeof getBridge === 'function') ? getBridge() : null;
        if (bridge && typeof bridge.onbattleinput === 'function') {
            bridge.onbattleinput(action, 0, 0);
        } else if (typeof log === 'function') {
            log(`JRPGBattleHUD: ponte offline, '${action}' não foi enviado`);
        }
    }

    // ------------------------------------------------------------------
    // A tela de resultados da vitória
    // ------------------------------------------------------------------

    /** O estado da tela de pé (null fechada). */
    let rs = null;

    function setText(card, cls, value) {
        const n = card.el.querySelector('.' + cls);
        if (n) n.textContent = String(value);
    }

    /** As cores do HP como no menu (updateCharacterStatus, 10_menu.js): a barra e o número pela fração de vida. */
    function hpClasses(pct) {
        if (pct >= 50) return { bar: 'hp-high', txt: 'hp-high-text' };
        if (pct >= 20) return { bar: 'hp-med', txt: 'hp-med-text' };
        if (pct >= 5) return { bar: 'hp-low', txt: 'hp-warn-text' };
        return { bar: 'hp-low', txt: 'hp-low-text' };
    }

    /** As barras de HP e MP do card contra o máximo de agora (ele sobe no level-up). */
    function drawBars(card, maxHp, maxMp) {
        const c = card.data;
        const hpPct = maxHp > 0 ? Math.max(0, Math.min(100, (c.hp / maxHp) * 100)) : 0;
        const mpPct = maxMp > 0 ? Math.max(0, Math.min(100, (c.mp / maxMp) * 100)) : 0;
        const cls = hpClasses(hpPct);
        const hpBar = card.el.querySelector('.bar-fill.hp');
        if (hpBar) {
            hpBar.style.width = hpPct + '%';
            hpBar.classList.remove('hp-high', 'hp-med', 'hp-low');
            hpBar.classList.add(cls.bar);
        }
        const hpCur = card.el.querySelector('.v-hp');
        if (hpCur) {
            hpCur.classList.remove('hp-high-text', 'hp-med-text', 'hp-warn-text', 'hp-low-text');
            hpCur.classList.add(cls.txt);
        }
        const mpBar = card.el.querySelector('.bar-fill.mp');
        if (mpBar) mpBar.style.width = mpPct + '%';
        setText(card, 'v-maxhp', maxHp);
        setText(card, 'v-maxmp', maxMp);
    }

    /** Um som da tela (o caminho do asset que o C++ mandou; vazio = mudo). */
    function play(path) {
        if (path && typeof playSound === 'function') playSound(path);
    }

    /** Um "tic" do contador, no máximo um a cada TICK_EVERY_MS. */
    function tick(ts) {
        if (!rs || ts - rs.lastTick < TICK_EVERY_MS) return;
        rs.lastTick = ts;
        play(rs.sounds.tick);
    }

    /** O card com shown pontos do EXP ganho já passados para o total. */
    function drawCard(card, shown) {
        const c = card.data;
        const exp = c.expBefore + shown;
        setText(card, 'v-gain', '+' + Math.max(c.gained - shown, 0));
        setText(card, 'v-total', exp);
        const th = c.thresholds || [];
        setText(card, 'v-next', card.step < th.length ? Math.max(th[card.step] - exp, 0) : '—');
        setText(card, 'v-lv', card.lv);
    }

    function showLevelUp(card) {
        const pop = card.el.querySelector('.bh-rs-levelup');
        if (!pop) return;
        pop.classList.remove('show');
        void pop.offsetWidth;   // reinicia a animação num segundo level-up do mesmo card
        pop.classList.add('show');
    }

    function levelUp(card) {
        const c = card.data;
        card.lv += 1;
        card.step += 1;
        // O HP / MP máximos de depois (o C++ só manda o de antes e o final)
        if (card.lv >= c.lvAfter) {
            drawBars(card, c.maxHpAfter, c.maxMpAfter);
        }
        card.el.classList.add('leveled');
        showLevelUp(card);
        play(rs.sounds.levelUp);
    }

    function runGold() {
        if (!rs) return;
        const target = rs.gold;
        let t0 = null;
        const frame = (ts) => {
            if (!rs || rs.skipped) return;
            if (t0 === null) t0 = ts;
            const p = Math.min((ts - t0) / GOLD_MS, 1);
            const node = rs.root.querySelector('.v-gold');
            if (node) node.textContent = '+' + Math.floor(target * p);
            if (p < 1) {
                if (target > 0) tick(ts);
                requestAnimationFrame(frame);
            }
        };
        requestAnimationFrame(frame);
    }

    function runCard(index) {
        if (!rs || rs.skipped) return;
        if (index >= rs.cards.length) {
            finishCount();
            return;
        }
        const card = rs.cards[index];
        const c = card.data;
        const th = c.thresholds || [];
        card.el.classList.add('current');
        const duration = c.gained > 0 ? Math.min(Math.max(COUNT_MIN_MS + c.gained * 2, COUNT_MIN_MS), COUNT_MAX_MS) : 300;
        let t0 = null;
        const frame = (ts) => {
            if (!rs || rs.skipped) return;
            if (t0 === null) t0 = ts;
            const p = Math.min((ts - t0) / duration, 1);
            const shown = Math.floor(c.gained * p);
            // Passou do próximo level: para no número dele, LEVEL UP! e segue depois da pausa
            if (card.lv < c.lvAfter && card.step < th.length && c.expBefore + shown >= th[card.step]) {
                levelUp(card);
                // O número do level que acabou de passar; o Next já mede até o seguinte
                drawCard(card, Math.min(th[card.step - 1] - c.expBefore, c.gained));
                rs.timer = setTimeout(() => {
                    t0 += LEVELUP_PAUSE_MS;
                    requestAnimationFrame(frame);
                }, LEVELUP_PAUSE_MS);
                return;
            }
            drawCard(card, shown);
            if (p < 1) {
                if (c.gained > 0) tick(ts);
                requestAnimationFrame(frame);
                return;
            }
            card.el.classList.remove('current');
            rs.timer = setTimeout(() => runCard(index + 1), BETWEEN_CARDS_MS);
        };
        requestAnimationFrame(frame);
    }

    function finishCount() {
        if (!rs || rs.finished) return;
        rs.finished = true;
        sendBridge('results_done');
        repaint();
    }

    /** Tudo no final de uma vez (o "pular"). */
    function jumpToEnd() {
        if (!rs) return;
        rs.cards.forEach((card) => {
            const c = card.data;
            while (card.lv < c.lvAfter) {
                card.lv += 1;
                card.step += 1;
            }
            if (c.lvAfter > c.lvBefore) {
                card.el.classList.add('leveled');
                const pop = card.el.querySelector('.bh-rs-levelup');
                if (pop) pop.classList.add('stay');
            }
            drawBars(card, c.maxHpAfter, c.maxMpAfter);
            drawCard(card, c.gained);
            card.el.classList.remove('current');
        });
        const gold = rs.root.querySelector('.v-gold');
        if (gold) gold.textContent = '+' + rs.gold;
    }

    function skip() {
        if (!rs || rs.skipped) return;
        rs.skipped = true;
        if (rs.timer) clearTimeout(rs.timer);
        jumpToEnd();
        sendBridge('results_skip');
        repaint();
    }

    /** Uma barra como as do menu (as classes bar-track / bar-fill / bar-values do 10_menu.css). */
    function barHtml(kind, label, cur, max) {
        return '<div class="bh-rs-bar">' +
            '<span class="bar-label ' + kind + '-text">' + label + '</span>' +
            '<div class="bar-track"><div class="bar-fill ' + kind + (kind === 'mp' ? ' mp-high' : '') + '"></div></div>' +
            '<span class="bar-values"><span class="cur v-' + kind + '">' + (cur | 0) + '</span><span class="sep">/</span>' +
                '<span class="max v-max' + kind + '">' + (max | 0) + '</span></span>' +
        '</div>';
    }

    function cardHtml(c) {
        return '<div class="bh-rs-card">' +
            '<div class="bh-rs-head"><span class="bh-rs-name">' + esc(c.name) + '</span>' +
                '<span class="bh-rs-lv">Lv <b class="v-lv">' + (c.lvBefore | 0) + '</b></span></div>' +
            barHtml('hp', 'HP', c.hp, c.maxHpBefore) +
            barHtml('mp', 'MP', c.mp, c.maxMpBefore) +
            '<div class="bh-rs-sep"></div>' +
            '<div class="bh-rs-row bh-rs-gain"><span>EXP</span><b class="v-gain">+' + (c.gained | 0) + '</b></div>' +
            '<div class="bh-rs-row"><span>Next</span><b class="v-next"></b></div>' +
            '<div class="bh-rs-row"><span>Total</span><b class="v-total">' + (c.expBefore | 0) + '</b></div>' +
            '<div class="bh-rs-levelup">LEVEL UP!</div>' +
        '</div>';
    }

    const api = {
        intro(data) {
            const names = el('bh-names');
            const line = el('bh-line');
            if (!names || !line || !data) return;

            names.innerHTML = '';
            const items = [];
            (data.groups || []).forEach((g) => {
                const node = document.createElement('div');
                node.className = 'bh-name';
                node.innerHTML = esc(g.name);
                names.appendChild(node);
                items.push({ node, centre: screenFractionToStage(Number(g.x) || 0.5), width: 0, left: 0 });
            });
            // A largura só existe depois de entrar no documento
            items.forEach((it) => {
                it.width = it.node.offsetWidth || 0;
                it.left = it.centre - it.width / 2;
            });
            relax(items);
            items.forEach((it) => { it.node.style.left = Math.round(it.left) + 'px'; });

            if (data.line) {
                line.innerHTML = esc(data.line);
                line.classList.add('active');
            } else {
                line.innerHTML = '';
                line.classList.remove('active');
            }
            repaint();
        },

        introHide() {
            const names = el('bh-names');
            const line = el('bh-line');
            if (names) names.innerHTML = '';
            if (line) {
                line.innerHTML = '';
                line.classList.remove('active');
            }
            repaint();
        },

        results(data) {
            const root = el('bh-results');
            if (!root || !data) return;
            api.resultsHide();
            root.classList.toggle('split', !!data.split);
            root.innerHTML =
                '<div class="bh-rs-cols"></div>' +
                '<div class="bh-rs-gold">Gold <b class="v-gold">+0</b></div>' +
                '<div class="bh-rs-skip"><span class="bh-rs-key">Enter</span> Skip</div>';
            const colsBox = root.querySelector('.bh-rs-cols');
            const cards = (data.cards || []).map((c) => {
                const col = document.createElement('div');
                col.className = 'bh-rs-col';
                col.innerHTML = cardHtml(c);
                colsBox.appendChild(col);
                return { data: c, el: col, lv: c.lvBefore | 0, step: 0 };
            });
            const skipBtn = root.querySelector('.bh-rs-skip');
            if (skipBtn) {
                skipBtn.onmouseenter = () => { if (window.InputMode) InputMode.useMouse(); };
                skipBtn.onclick = () => skip();
            }
            root.classList.add('active');
            rs = { root, cards, gold: data.gold | 0, sounds: data.sounds || {}, skipped: false, finished: false, timer: null,
                   lastTick: -1e9 };
            cards.forEach((card) => {
                drawBars(card, card.data.maxHpBefore, card.data.maxMpBefore);
                drawCard(card, 0);
            });
            // A câmera acabou de trocar para as colunas: os cards entram parados e a contagem espera um pouco
            const state = rs;
            rs.timer = setTimeout(() => {
                if (rs !== state || rs.skipped) return;
                runGold();
                runCard(0);
            }, START_DELAY_MS);
            repaint();
        },

        resultsHide() {
            if (rs && rs.timer) clearTimeout(rs.timer);
            rs = null;
            const root = el('bh-results');
            if (root) {
                root.innerHTML = '';
                root.classList.remove('active', 'split');
            }
            repaint();
        },

        /** A tela de resultados está de pé (o JRPGBattle.handleInput repassa o confirmar / cancelar). */
        resultsActive() {
            return !!rs;
        },

        resultsInput(action) {
            if (!rs) return false;
            if (action === 'confirm' || action === 'cancel') {
                skip();
                return true;
            }
            return true;   // as outras teclas não fazem nada com a tela de pé
        },

        reset() {
            api.introHide();
            api.resultsHide();
        },

        /** Só para ver no Chrome: JRPGBattleHUD.preview() */
        preview() {
            api.intro({
                groups: [{ name: 'Killer Bee * 3', x: 0.38 }, { name: 'Queen Bee', x: 0.46 }],
                line: 'Ambushed!'
            });
        },

        /** Só para ver no Chrome: JRPGBattleHUD.previewResults() */
        previewResults() {
            api.results({ split: true, gold: 245, cards: [
                { name: 'Noa', lvBefore: 7, lvAfter: 8, expBefore: 2210, gained: 180, thresholds: [2300, 2900],
                  hp: 40, maxHpBefore: 384, maxHpAfter: 410, mp: 47, maxMpBefore: 47, maxMpAfter: 50 },
                { name: 'Vahn', lvBefore: 5, lvAfter: 7, expBefore: 980, gained: 650, thresholds: [1100, 1500, 2000],
                  hp: 300, maxHpBefore: 340, maxHpAfter: 397, mp: 60, maxMpBefore: 52, maxMpAfter: 60 },
                { name: 'Gala', lvBefore: 6, lvAfter: 6, expBefore: 1520, gained: 180, thresholds: [1900],
                  hp: 90, maxHpBefore: 445, maxHpAfter: 445, mp: 80, maxMpBefore: 80, maxMpAfter: 80 }
            ] });
        }
    };

    return api;
})();
