#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleTypes.h"
#include "JRPGBattleVoices.generated.h"

class UAudioComponent;
class UDataTable;
class USoundBase;
struct FJRPGArtVoiceData;
struct FJRPGBattleVoiceBankData;

/** Que voz o passo pôs no drive, para o slow do grito (AJRPGBattleArena::bShoutSlow*): o original fica lento enquanto o CD lê. */
enum class EJRPGBattleVoiceSlow : uint8
{
	None,
	/** O grito no ArtStarter de uma Art conhecida. */
	KnownArt,
	/** A Art nova (Regular): a fanfarra do starter e o grito dela logo depois. */
	NewArt,
	/** A fanfarra da Hyper no starter. */
	Hyper,
	/** A fanfarra da Super / Miracle no starter. */
	SuperMiracle
};

/**
 * FJRPGBattleVoices
 * O canal de voz da luta: as vozes de CD-XA que não estão na trilha de som dos clips (o grito das Arts, a fanfarra do
 * SpecialStarter, o grunhido do block), como o original as decide (legend-of-legaia-re, battle-action-helpers.md "Battle voice
 * cues"). Só apresentação: os sorteios usam um stream próprio, nunca o Rng da luta. Dados: DT_BattleVoices (os bancos de cada
 * herói) e DT_ArtVoices (os candidatos de cada Art), os dois do Extras/ps1_battle_voices.py; sem as tabelas, fica mudo.
 *
 * Sem o drive de CD do original (que tem um canal só e descarta a fanfarra com outra voz lendo; bug 84): cada voz toca na hora
 * dela. A fanfarra nova corta só a fanfarra anterior; o grito e o grunhido tocam por cima, sem cortar nada. A voz sai
 * VoiceLatencySeconds depois do pedido.
 *
 * O UJRPGBattleSceneSubsystem chama CommitStep a cada passo que o herói que age começa, e NoteBlocked num Block.
 */
USTRUCT()
struct JRPGFRAMEWORK_API FJRPGBattleVoices
{
	GENERATED_BODY()

	/** Carrega as tabelas e semeia o sorteio (começo da luta). */
	void Begin();

	/** Corta a voz e solta as tabelas (fim da luta). */
	void End();

	/**
	 * O herói Character começou o passo StepIndex da Timeline: o commit do clip no original. Devolve os segundos da voz que este
	 * passo pôs para tocar (o trecho que o drive lê), 0 se nenhuma, -1 se não há vozes para este herói (sem as tabelas, ou sem a
	 * linha dele no DT_BattleVoices). OutSlow: que voz foi, para quem chama decidir o slow do grito (AJRPGBattleArena::bShoutSlow*).
	 */
	float CommitStep(UWorld* World, FName Character, const FJRPGActionTimeline& Timeline, int32 StepIndex, EJRPGBattleVoiceSlow& OutSlow);

	/** Um golpe do herói Attacker foi bloqueado: o grunhido dele (XA30). */
	void NoteBlocked(UWorld* World, FName Attacker);

	/** Anda a voz pedida que espera a latência (chamar a cada quadro). */
	void Tick(UWorld* World);

	/** O atraso da voz depois do pedido (o original: ~150 ms de busca no CD). */
	float VoiceLatencySeconds = 0.0f;

	/** As tabelas foram achadas (sem elas, a luta não tem estas vozes). */
	bool HasData() const { return Banks != nullptr && ArtVoices != nullptr; }

private:
	const FJRPGBattleVoiceBankData* FindBank(FName Character) const;
	const FJRPGArtVoiceData* FindArtVoice(FName ArtKey) const;
	/** A trava do grito (ctx+0x243): o byte +0x87 do clip que o passo antes deste pôs. */
	bool IsLatchedBy(const FJRPGBattleVoiceBankData& Bank, const FJRPGActionStep* Previous) const;
	/** FUN_8004C140: o grito da Art ArtKey com a fila seguindo (bFollowed) e a trava; devolve os segundos, 0 se nada tocou. */
	float Shout(UWorld* World, const FJRPGBattleVoiceBankData& Bank, FName ArtKey, bool bFollowed, bool bLatched);
	/** O braço 0x1A de FUN_8004AD80: a fanfarra do SpecialStarter. Devolve os segundos dela, 0 se nada tocou. */
	float Fanfare(UWorld* World, const FJRPGBattleVoiceBankData& Bank, const FJRPGActionTimeline& Timeline, int32 StepIndex);
	/** O passo é o SpecialStarter de uma Art Regular aprendida nesta ação (a que entra pelo dash, "NEW"). */
	bool IsNewRegularStarter(const FJRPGActionTimeline& Timeline, const FJRPGActionStep* Step) const;
	/** Pede uma voz; bFanfare: corta a fanfarra anterior (as outras vozes não cortam nada). */
	void PlayVoice(UWorld* World, const TSoftObjectPtr<USoundBase>& Sound, bool bFanfare);
	void StartVoice(UWorld* World, USoundBase* Sound, bool bFanfare);
	void StopFanfare();
	void StopAll();
	static float SFXVolume(const UWorld* World);

	UPROPERTY()
	TObjectPtr<UDataTable> Banks;

	UPROPERTY()
	TObjectPtr<UDataTable> ArtVoices;

	UPROPERTY()
	TObjectPtr<UDataTable> Arts;

	/** A fanfarra que soa (a próxima a corta). */
	UPROPERTY()
	TObjectPtr<UAudioComponent> FanfareVoice;

	/** Os gritos e grunhidos que soam (só para pará-los no fim da luta). */
	UPROPERTY()
	TArray<TObjectPtr<UAudioComponent>> OtherVoices;

	/** As vozes pedidas que esperam a latência: o som, quando sai (tempo real) e se é fanfarra (arrays paralelos). */
	UPROPERTY()
	TArray<TObjectPtr<USoundBase>> PendingSounds;
	TArray<double> PendingTimes;
	TArray<bool> PendingFanfare;

	/** O último canal de grito sorteado (gp+0xA4A no original: um só, de qualquer herói). */
	int32 LastShoutChannel = INDEX_NONE;

	FRandomStream Random;
};
