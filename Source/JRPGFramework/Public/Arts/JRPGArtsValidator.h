#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Arts/ArtsData.h"
#include "JRPGArtsValidator.generated.h"

class UDataTable;

UENUM(BlueprintType)
enum class EJRPGArtIssueSeverity : uint8
{
	Warning  UMETA(DisplayName = "Warning"),
	Error    UMETA(DisplayName = "Error")
};

/** Um problema numa linha do DT_Arts. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGArtIssue
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	EJRPGArtIssueSeverity Severity = EJRPGArtIssueSeverity::Error;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	FName ArtKey;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arts")
	FString Message;
};

/**
 * UJRPGArtsValidator
 * As regras que uma linha do DT_Arts tem que cumprir para a Art funcionar como dado — o que garante
 * que uma Art nova é só uma linha (BATTLE_ROADMAP.md › Phase 6 › Making new Arts). Erros quebram a
 * Art (o teste de automação JRPG.Arts.Validator falha); avisos são listados.
 *
 * Erros: duas Arts do herói com o mesmo Command; Regular / Hyper que nunca é reconhecida (digitar o
 * Command dela dá outra coisa); passo da Sequence que não existe, é de outro herói ou volta para a
 * linha; Part com Command, ou Art sem; especial sem APCost, Regular com; HitPower e HitTarget de
 * tamanhos diferentes; Anim que não carrega ou não é do esqueleto de batalha do herói; Super do
 * disco cujo Command não tokeniza na cadeia do original. Avisos: clip com notifies de golpe numa
 * quantidade diferente do HitPower; Super ChainKnown cujo Command não tem Art nenhuma (sairia sem o
 * jogador saber nada — dê Unlock = Taught); Unlock sem sentido para o Kind.
 *
 * Roda no teste de automação, no Data Validation do asset (FJRPGArtData::IsDataValid) e pelo
 * Extras/arts_rules.py no LegaiaStudio / gerador (o mesmo conjunto, menos o que precisa de asset).
 */
UCLASS()
class JRPGFRAMEWORK_API UJRPGArtsValidator : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Todas as linhas da tabela. bCheckAssets: carrega os Anim (esqueleto, notifies de golpe). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Arts")
	static TArray<FJRPGArtIssue> ValidateArts(const UDataTable* Arts, bool bCheckAssets = true);

	/** Uma linha, com as regras que olham as outras (Command repetido, Sequence, Supers). */
	static void ValidateRow(const UDataTable* Arts, FName Key, const FJRPGArtData& Row, bool bCheckAssets, TArray<FJRPGArtIssue>& Out);

	/** Só o que a linha sozinha diz (sem a tabela): o Data Validation de uma linha fora do DT_Arts. */
	static void ValidateRowLocal(FName Key, const FJRPGArtData& Row, TArray<FJRPGArtIssue>& Out);

	/**
	 * As cadeias das Supers do disco (RE docs/formats/art-data.md › "Super Arts", do find de
	 * crates/art/src/super_art.rs): o que o Command de cada uma tem que tokenizar.
	 */
	static const TMap<FName, TArray<FName>>& DiscSuperChains();

	/** Os clips básicos que uma Sequence pode citar: SwingL/R/D/U, ArtStarter, SpecialStarter. */
	static bool IsBasicStep(FName Step);
};
