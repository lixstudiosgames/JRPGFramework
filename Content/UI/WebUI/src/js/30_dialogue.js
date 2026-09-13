// 30_dialogue.js - Sistema de diálogo (PLACEHOLDER)
// A assinatura já existe para o nó Blueprint (OpenDialogue) funcionar;
// a implementação visual virá quando o sistema de diálogo for desenvolvido.

window.openDialogue = function (speaker, text, options) {
    const optionCount = Array.isArray(options) ? options.length : 0;
    log(`openDialogue (placeholder): falante='${speaker}', texto='${text}', ${optionCount} opção(ões).`);

    // Futuro:
    // if (!UIState.canOpen('dialogue')) return;
    // preencher #dialogue-speaker/#dialogue-text/#dialogue-options,
    // ativar #screen-dialogue e UIState.set('dialogue_active').
};
