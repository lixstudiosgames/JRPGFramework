// Ponte C++
const bridge = (window.ue && window.ue.uebridge) || (window.parent && window.parent.ue && window.parent.ue.uebridge);

function log(msg) {
    console.log(`[JRPG Popup] ${msg}`);
    if (bridge && typeof bridge.echo === "function") {
        bridge.echo(`[JS LOG] ${msg}`);
    }
}

document.addEventListener('DOMContentLoaded', () => {
    // 1. Ler parâmetros da URL (?name=Potion&qty=1)
    const urlParams = new URLSearchParams(window.location.search);
    const itemName = urlParams.get('name') || 'Item';
    const itemQty = urlParams.get('qty') || '1';

    // 2. Atualizar HTML
    const nameEl = document.getElementById('item-name');
    const qtyEl = document.getElementById('item-qty');
    if (nameEl) nameEl.textContent = itemName;
    if (qtyEl) qtyEl.textContent = itemQty;

    // Engatilhar animação de entrada (IN)
    const wrapper = document.getElementById('popup-wrapper');
    if (wrapper) {
        setTimeout(() => {
            wrapper.classList.add('show');
        }, 50);
    }

    // 3. Agendar a animação de saída (OUT) aos 2.5 segundos
    setTimeout(() => {
        if (wrapper) {
            wrapper.classList.remove('show');
            wrapper.classList.add('hide');
        }
    }, 2500);

    // 4. Fechar o menu após a conclusão da animação (3.0 segundos no total)
    setTimeout(() => {
        if (bridge && typeof bridge.closemenu === "function") {
            bridge.closemenu();
        }
    }, 3000);
});
