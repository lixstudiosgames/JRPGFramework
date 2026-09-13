// 00_core.js - Núcleo do Shell: ponte C++, log e som
// ÚNICO lugar onde a ponte é resolvida. SEMPRE lazy: entre sessões PIE o
// BindBridge do C++ RECRIA window.ue.uebridge no documento vivo, então
// capturar a ponte numa const no load deixaria a referência velha (stale).

function getBridge() {
    return (window.ue && window.ue.uebridge) || window.uebridge || null;
}

// Escala responsiva: os layouts das telas são desenhados num canvas fixo de
// 1920x1080 e escalados para caber na janela via transform scale(--scale-factor).
// A View do Ultralight acompanha o tamanho real do viewport (resize dinâmico
// no Tick do SUltralightBrowser), então recalculamos a escala aqui.
function updateUIScaleFactor() {
    if (!window.innerWidth || !window.innerHeight) return;
    const scale = Math.min(window.innerWidth / 1920, window.innerHeight / 1080);
    document.documentElement.style.setProperty('--scale-factor', String(scale));
}

window.addEventListener('resize', updateUIScaleFactor);
updateUIScaleFactor();

// Força alguns repaints logo após esconder uma tela: mexe na âncora de repaint
// (#ul-repaint-anchor) por ~300ms para garantir que o frame final (página
// "vazia") seja desenhado e apresentado pelo pipeline do Ultralight.
function kickUIRepaint() {
    const anchor = document.getElementById('ul-repaint-anchor');
    if (!anchor) return;
    let ticks = 0;
    const step = () => {
        anchor.style.opacity = (ticks % 2) ? '1' : '0.9';
        if (++ticks < 6) setTimeout(step, 50);
    };
    step();
}

// Log utilitário para debug (console + echo no log do Unreal)
function log(msg) {
    console.log(`[JRPG UI] ${msg}`);
    const bridge = getBridge();
    if (bridge && typeof bridge.echo === "function") {
        bridge.echo(`[JS LOG] ${msg}`);
    }
}

// Cache de áudio HTML5 — apenas FALLBACK para testes em browser comum.
// No jogo o som toca via ponte nativa (bridge.playsfx -> assets UE).
const sfx = {};
try {
    sfx.Open = new Audio('../SFX/SFX_Open.wav');
    sfx.Close = new Audio('../SFX/SFX_Close.wav');
    sfx.Next = new Audio('../SFX/SFX_Next.wav');
    sfx.Item = new Audio('../SFX/SFX_Item.WAV');
} catch (e) {
    console.warn("Áudio HTML5 não suportado ou bloqueado:", e);
}

// Toca efeito sonoro de forma segura com tratamento de exceções
function playSound(soundName) {
    // Tenta tocar via ponte nativa C++ da Unreal Engine para máxima fidelidade e latência zero
    const bridge = getBridge();
    if (bridge && typeof bridge.playsfx === "function") {
        bridge.playsfx(soundName);
        return;
    }

    // Fallback para áudio HTML5 clássico caso esteja rodando em browser comum
    try {
        const audio = sfx[soundName];
        if (audio) {
            audio.currentTime = 0;
            audio.play().catch(err => console.log(`Erro ao reproduzir som ${soundName}:`, err));
        }
    } catch (e) {
        console.warn(`Falha crítica ao executar som ${soundName}:`, e);
    }
}
