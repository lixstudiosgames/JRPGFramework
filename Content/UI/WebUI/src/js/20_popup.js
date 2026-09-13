// 20_popup.js - Popup de item recebido
// Overlay passivo: NUNCA toma input do jogo, então não notifica o C++ ao
// terminar (não há input a restaurar). Reentrante: um novo showItemPopup no
// meio da animação reinicia a timeline de forma limpa.

window.JRPGPopup = (function () {
    let tShow = null;
    let tHide = null;
    let tEnd = null;

    function clearTimers() {
        if (tShow) { clearTimeout(tShow); tShow = null; }
        if (tHide) { clearTimeout(tHide); tHide = null; }
        if (tEnd)  { clearTimeout(tEnd);  tEnd = null; }
    }

    return {
        show(name, qty) {
            const screen = document.getElementById('screen-popup');
            const wrapper = document.getElementById('popup-wrapper');
            if (!screen || !wrapper) return;

            // Reinicia qualquer animação em andamento
            clearTimers();
            wrapper.classList.remove('show', 'hide');
            void wrapper.offsetHeight; // Força reflow para reiniciar a transição

            // textContent (nunca innerHTML): o nome do item é dado externo
            const nameEl = document.getElementById('item-name');
            const qtyEl = document.getElementById('item-qty');
            if (nameEl) nameEl.textContent = name;
            if (qtyEl) qtyEl.textContent = qty;

            screen.classList.add('active');

            // Timeline: entrada -> espera -> saída -> limpeza
            tShow = setTimeout(() => {
                wrapper.classList.add('show');
            }, 50);

            tHide = setTimeout(() => {
                wrapper.classList.remove('show');
                wrapper.classList.add('hide');
            }, 2500);

            tEnd = setTimeout(() => {
                wrapper.classList.remove('hide');
                screen.classList.remove('active');
            }, 3000);
        },

        // Limpa timers e classes (chamado pelo resetUIShell)
        reset() {
            clearTimers();
            const wrapper = document.getElementById('popup-wrapper');
            if (wrapper) wrapper.classList.remove('show', 'hide');
        }
    };
})();

// Função global chamada pelo C++ (UWebUISubsystem::ShowItemPopup)
window.showItemPopup = function (name, qty) {
    log(`showItemPopup: ${name} x${qty}`);
    playSound('Item');
    JRPGPopup.show(name, qty);
};
