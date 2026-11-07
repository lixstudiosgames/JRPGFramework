// 97_battle_hud.js — HUD DA LUTA (camada sem input)
//
// Só DESENHA. Nada de regra nem de input aqui: o C++ manda o que mostrar, já pronto, e esta camada nunca chama a ponte.
// Vale em qualquer estado da UI (não passa pelo UIState).
//
// C++ -> JS (UWebUISubsystem::CallBattleHUD)
//   JRPGBattleHUD.intro({ groups: [{ name: 'Killer Bee * 3', x: 0.42 }], line: 'Ambushed!' })
//       os nomes da abertura: um rótulo por grupo, centrado em x (fração da largura da tela, onde o grupo está), e a linha de
//       formação ('' = nenhuma). Os rótulos que se encostam são afastados igualmente, como o original faz.
//   JRPGBattleHUD.introHide()
//   JRPGBattleHUD.reset()       tudo some (troca de mapa, reload)

window.JRPGBattleHUD = (function () {

    const el = (id) => document.getElementById(id);

    // O original: separa dois rótulos que ficam a menos de 20 px (de 320) e prende cada um a 6 px da borda; aqui, no canvas de 1920
    const MIN_GAP = 40;
    const EDGE = 36;
    const STAGE_W = 1920;

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
            if (typeof kickUIRepaint === 'function') kickUIRepaint();
        },

        introHide() {
            const names = el('bh-names');
            const line = el('bh-line');
            if (names) names.innerHTML = '';
            if (line) {
                line.innerHTML = '';
                line.classList.remove('active');
            }
            if (typeof kickUIRepaint === 'function') kickUIRepaint();
        },

        reset() {
            api.introHide();
        },

        /** Só para ver no Chrome: JRPGBattleHUD.preview() */
        preview() {
            api.intro({
                groups: [{ name: 'Killer Bee * 3', x: 0.38 }, { name: 'Queen Bee', x: 0.46 }],
                line: 'Ambushed!'
            });
        }
    };

    return api;
})();
