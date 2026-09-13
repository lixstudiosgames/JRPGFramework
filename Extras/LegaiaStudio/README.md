# LegaiaStudio

Painel local para gerenciar os dados do JRPGFramework.

```
python Extras/LegaiaStudio/studio.py
```

Sobe em `http://127.0.0.1:8787` e abre o navegador. `--port N` troca a porta,
`--no-browser` não abre. Ctrl+C para parar.

Sem dependências: só a biblioteca padrão do Python.

## O que dá para fazer

**Editar por ficha, não por planilha.** Painel lateral com as DataTables, lista
dos registros no meio e a ficha do selecionado à direita. Cada campo ganha o
editor do seu tipo: número para `int32`, dropdown para enum (com os valores
reais do C++), toggle para `bool`, textarea para descrição.

**Estoque das lojas por seleção.** Em `DT_Shops`, `Inventory` e `FeaturedItems`
não são texto: aparecem como lista de itens com nome e preço resolvidos da
`DT_Items`, com botão para adicionar (abre um seletor com busca, marcando o que
a loja já vende) e para remover. Item que não existe na `DT_Items` aparece
destacado em vermelho.

**Criar e apagar linhas.** `+ Novo` pede o RowName; `Apagar` pede confirmação e
avisa se a linha estiver em uso (item citado por alguma loja, por exemplo).

**Salvar sob controle.** Nada vai para o disco enquanto você não clicar em
Salvar; a sidebar marca com `•` a tabela com alterações pendentes. Ao salvar, o
CSV anterior é copiado para `Docs/Data/_backup/` com data e hora.

**Builds com log ao vivo.** `Build WebUI` e `Build Plugin` rodam os scripts de
`Extras/` e transmitem a saída num painel embaixo. Enquanto roda, a interface
fica travada — evita salvar CSV no meio de um empacotamento.

**Validação cruzada** — o que só apareceria como warning no log do Unreal em
runtime: item de loja inexistente, `FeaturedItems` fora do `Inventory`, RowName
repetido e coluna do CSV que não bate com o struct C++.

## Como o schema é descoberto

As colunas dos CSVs são as `UPROPERTY` dos structs `FTableRowBase`. O
`studio.py` parseia os headers em `Source/JRPGFramework/Public/` e extrai
campos, tipos e valores dos enums (`EItemCategory`, `EUseContext`,
`EArmorSlot`, …). Mexeu no struct, o Studio acompanha no próximo F5 — o cache
é invalidado pelo mtime dos headers.

## Escrita do CSV

O gravador **preserva o formato de cada arquivo**: `DT_Items` e `DT_Shops` são
CRLF, `DT_CameraPresets` é LF. Salvar sem alterar nada devolve o arquivo
idêntico byte a byte (testado nas três tabelas) — assim o diff mostra só o que
você realmente mudou.

A escrita é atômica (arquivo temporário + `os.replace`): uma queda no meio
deixa o CSV antigo íntegro em vez de meio arquivo.

## Adicionar uma tabela nova

1. Trazer o `.toml` do LegaiaRE para `Docs/Data/gamedata/`.
2. Pedir ao Claude Code o struct `FTableRowBase` e o CSV inicial. Traduzir
   TOML → schema de DataTable é decisão de design, não conversão mecânica: o
   `generate_csv.py` atual tem regra por tabela (mapeamento de categoria,
   tabela de art books, heurística de `UseContext`).
3. Registrar a tabela em `TABLES`, no topo do `studio.py` — e, se ela
   referenciar outra, em `REFERENCES` (é o que liga o seletor de itens).
4. Importar a DataTable na Unreal. Daí em diante, editar por aqui.

## Configuração

O caminho do projeto Unreal (usado para saber se a `.uasset` foi importada) tem
um default no `studio.py`. Para mudar sem editar o script, crie um
`studio.config.json` nesta pasta:

```json
{ "unreal_project": "D:\\JOGOS\\PROJETOS UNREAL\\5.8\\Legaia" }
```

Caminho inexistente não quebra nada — o painel só marca as DataTables como
"não configurado".

## Cuidados

- **`Extras/generate_csv.py` e `generate_shops_csv.py` reescrevem os CSVs
  inteiros a partir dos TOMLs.** Rodar um deles apaga o que você editou aqui.
  Por isso eles **não** estão entre os botões de build — são importadores de
  uma vez só.
- **Bind fixo em 127.0.0.1.** A ferramenta escreve no código-fonte do plugin e
  dispara builds; exposta na rede seria execução remota na máquina de dev.
- O Studio está no `ignore_patterns` do `build_plugin.py`: não vai no plugin
  empacotado. `Docs/` continua indo, porque quem usa o plugin precisa dos CSVs.

## Nota de design

Interface de ferramenta, não de jogo: tipografia de sistema, superfícies
neutras e um único accent. O tema Dark Realms ficou na WebUI — aqui o que
importa é densidade de informação e legibilidade.

## Tabelas

| DataTable          | Struct             | Origem                          |
|--------------------|--------------------|---------------------------------|
| `DT_Items`         | `FItemData`        | `gamedata/items.toml` + armor/weapons/accessories |
| `DT_Shops`         | `FShopData`        | `gamedata/shops.toml`           |
| `DT_Characters`    | `FCharacterData`   | `gamedata/characters.toml`      |
| `DT_CameraPresets` | `FCameraPresetRow` | escrita à mão                   |

`DT_Characters` traz o que o `characters.toml` tem: identidade, Ra-Seru,
afinidade elemental (`TArray<EJRPGElement>`, editada por chips) e classes de
arma favoritas. **Os stats ficaram de fora de propósito** — HP/MP/AP base e
curva de ATK/UDF/LDF por level entram quando as fórmulas de progressão forem
definidas; coluna vazia numa DataTable só atrapalha.
