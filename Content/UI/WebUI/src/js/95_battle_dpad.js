// 95_battle_dpad.js — O D-PAD DO MENU DE LUTA (peça isolada, feita para ser trocada)
//
// Quatro botões em volta de um ícone central: esquerda / cima / direita / baixo. É a ÚNICA peça do menu de luta que
// sabe desenhar o d-pad; o resto (96_battle.js) só chama render(container, pad) e escuta onPress. Para trocar pelos botões
// 3D ao lado do personagem: este arquivo e o 95_battle_dpad.css saem, e quem os substitui lê o MESMO `pad` que o C++ manda
// na view (o d-pad já chega pronto: o rótulo, se está ativo e o custo) e devolve o aperto pelo mesmo onPress(dir) —
// ou fala direto com o FJRPGBattleMenu no C++, que é quem decide o que cada direção faz.
//
// pad = { left, up, right, down }, cada um { label, enabled, cost } ou null (esse lado não é desenhado).
//   enabled:false  -> o botão é desenhado apagado, com um X por cima, e não é selecionável (o C++ recusa o aperto).
//   cost >= 0      -> o custo no gauge, mostrado no canto do botão (os botões da tela Attack).
//
// Ícones são <path> inline (nunca <use>: falha em silêncio no Ultralight).

window.JRPGBattleDpad = (function () {
    const DIRS = ['up', 'left', 'right', 'down'];

    // O X por cima do botão apagado (um traço que atravessa o botão inteiro)
    const X_PATH = 'M6 6 L94 94 M94 6 L6 94';
    // A cruz do ícone central
    const CROSS_PATH = 'M17 4h14v13h13v14H31v13H17V31H4V17h13z';

    let pressHandler = null;

    function esc(text) {
        return String(text === undefined || text === null ? '' : text)
            .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    }

    function buttonHtml(dir, b) {
        if (!b) return '';
        const cost = (b.cost !== undefined && b.cost >= 0) ? `<span class="bdp-cost">${b.cost}</span>` : '';
        const x = b.enabled ? '' :
            `<svg class="bdp-x" viewBox="0 0 100 100" preserveAspectRatio="none"><path d="${X_PATH}"/></svg>`;
        return `<div class="bdp-btn bdp-${dir}${b.enabled ? '' : ' disabled'}" data-dir="${dir}">` +
               `<span class="bdp-label">${esc(b.label)}</span>${cost}${x}</div>`;
    }

    return {
        /** Quem recebe o aperto: fn(dir) com dir = 'left' | 'up' | 'right' | 'down'. */
        onPress(fn) {
            pressHandler = fn;
        },

        /** Desenha o d-pad dentro de `container` (apaga o que havia). */
        render(container, pad) {
            if (!container) return;
            const any = pad && DIRS.some(d => pad[d]);
            container.classList.toggle('bdp-empty', !any);
            if (!any) {
                container.innerHTML = '';
                return;
            }

            container.innerHTML =
                `<svg class="bdp-center" viewBox="0 0 48 48"><path d="${CROSS_PATH}"/></svg>` +
                DIRS.map(d => buttonHtml(d, pad[d])).join('');

            container.querySelectorAll('.bdp-btn').forEach(el => {
                // O mouse só destaca no hover (CSS) e escolhe no clique — nunca move o cursor de navegação
                el.onmouseenter = () => { if (window.InputMode) InputMode.useMouse(); };
                el.onclick = () => { if (pressHandler) pressHandler(el.dataset.dir); };
            });
        },

        clear(container) {
            if (!container) return;
            container.classList.add('bdp-empty');
            container.innerHTML = '';
        }
    };
})();
