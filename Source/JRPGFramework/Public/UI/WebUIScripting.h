#pragma once

#include "CoreMinimal.h"

// Helpers de geração segura de código JavaScript para injeção via ExecuteJS.
// Apenas manipulação de FString — NUNCA incluir headers do SDK Ultralight aqui
// (regra dos headers públicos do plugin).

namespace JRPGWebUI
{
	/**
	 * Converte um texto arbitrário num literal JavaScript completo entre aspas
	 * simples, pronto para interpolar numa chamada de função:
	 *   O'Hara "rara"  ->  'O\'Hara \"rara\"'
	 *
	 * Escapa \ ' " \n \r \t, os separadores de linha U+2028/U+2029 e demais
	 * caracteres de controle (< 0x20) como \uXXXX. O restante do Unicode passa
	 * intacto (a conversão TCHAR->UTF8 acontece no EvaluateScript da UL thread).
	 *
	 * OBRIGATÓRIO para qualquer string vinda de dados do jogo (nome de item,
	 * texto de diálogo, etc.) antes de montar código para ExecuteJS.
	 */
	JRPGFRAMEWORK_API FString ToJSStringLiteral(const FString& In);

	/**
	 * O mesmo, para um FName OPCIONAL — campo que pode legitimamente estar
	 * vazio, como TeachesArt ou Summons de um item que não ensina nem invoca
	 * nada.
	 *
	 * `FName::ToString()` de um NAME_None devolve a palavra **"None"**, que do
	 * outro lado é uma string cheia e portanto *truthy*: o `if (it.art)` do JS
	 * passava e a descrição do item exibia "Teaches Art: None" numa espada.
	 * Aqui NAME_None vira string vazia, que é falsy — a linha simplesmente não
	 * aparece.
	 *
	 * Use este para qualquer FName que possa estar vazio. Para a CHAVE de uma
	 * linha de DataTable (que nunca é None) o ToJSStringLiteral normal serve.
	 */
	JRPGFRAMEWORK_API FString OptionalNameToJS(FName In);
}
