#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "JRPGFieldPartyComponent.generated.h"

class AJRPGFieldCharacter;
class AJRPGFieldPlayerController;
class UPartySubsystem;
struct FCollisionQueryParams;

/** O jogador passou a controlar outro membro da party (NewLeader); OldLeader virou seguidor. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFieldLeaderChanged, AJRPGFieldCharacter*, NewLeader, AJRPGFieldCharacter*, OldLeader);

/** Como os seguidores andam atrás do líder. */
UENUM(BlueprintType)
enum class EJRPGPartyFollowMode : uint8
{
	/**
	 * Ficam parados enquanto o líder está perto; quando ele se afasta, vão pelo navmesh (MoveTo)
	 * até perto dele, cada um para um lado. Mapa sem navmesh: usa Trail.
	 */
	Leash,
	/** Andam pela trilha do líder, em fila, cortando caminho quando dá. Não precisa de navmesh. */
	Trail
};

/**
 * UJRPGFieldPartyComponent
 * A party andando no mapa. Mora no AJRPGFieldPlayerController: o controller sobrevive à troca
 * de personagem, o pawn não.
 *
 * - Põe no mapa a formação do UPartySubsystem. O líder é o pawn do jogador; os outros são o
 *   MESMO AJRPGFieldCharacter possuído por um AJRPGPartyFollowerController, em modo seguidor
 *   (atravessa pawns, não dispara trigger nenhum do mapa). Recrutou alguém, mexeu na formação
 *   no menu: o mapa acompanha (OnPartyChanged).
 * - Seguir (FollowMode):
 *   - Leash (padrão, com navmesh): o seguidor fica parado enquanto o líder está perto. Passou de
 *     LeashStartDistance, ele sai (com um pequeno atraso, cada um o seu) e vai pelo navmesh até
 *     um lugar atrás e ao lado do líder, acompanhando a velocidade dele; longe, corre.
 *   - Trail: o líder deixa uma trilha e cada seguidor anda por ela, FollowSpacing atrás do da
 *     frente, cortando caminho quando a reta está livre. Não precisa de navmesh.
 *   Com o líder parado, eles se juntam ao lado dele (GatherAngles) — a posição só vale se cair
 *   no navmesh e não houver parede entre ela, o líder e o seguidor.
 * - Segurança: o líder deu um salto (portal, load, cutscene) ou um seguidor ficou longe ou
 *   preso — ele teleporta para o lugar dele.
 * - Trocar de personagem (CycleLeader, o R3): o controle vai para o próximo da formação onde
 *   ele estiver, a câmera desliza até ele e o antigo líder vira seguidor ali mesmo. O escolhido
 *   fica no UPartySubsystem (SetFieldLeader) e vale para o próximo mapa; o save não guarda.
 *
 * Cada membro precisa do seu Blueprint (filho de AJRPGFieldCharacter) em MemberClasses. Quem
 * não tem classe não aparece no mapa. Um mapa sem party: bShowParty no AJRPGFieldMapSettings.
 *
 * Guia: Docs/Guides/PLAYER.md (Party in the field).
 */
UCLASS(ClassGroup = (JRPG), meta = (BlueprintSpawnableComponent))
class JRPGFRAMEWORK_API UJRPGFieldPartyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UJRPGFieldPartyComponent();

	/** O Blueprint de cada membro no Field, pela key de DT_Characters (Vahn -> BP_Vahn...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Party")
	TMap<FName, TSubclassOf<AJRPGFieldCharacter>> MemberClasses;

	// --- Seguir ---

	/** Leash: pelo navmesh, só quando o líder se afasta. Trail: pela trilha dele, em fila. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Follow")
	EJRPGPartyFollowMode FollowMode = EJRPGPartyFollowMode::Leash;

	/** Leash: parado, o seguidor só sai andando quando o líder passa desta distância, em uu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Leash", meta = (ClampMin = "100.0"))
	float LeashStartDistance = 350.0f;

	/** Leash: mais longe que isto do líder, o seguidor corre (volta a andar perto da metade). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Leash", meta = (ClampMin = "100.0"))
	float LeashRunDistance = 650.0f;

	/** Leash: com o líder parado, o seguidor para a esta distância do lugar dele ao lado, em uu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Leash", meta = (ClampMin = "10.0"))
	float LeashArriveRadius = 40.0f;

	/** Leash: sem lugar ao lado válido (parede, beirada), o seguidor para a esta distância do líder. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Leash", meta = (ClampMin = "50.0"))
	float LeashStopDistance = 200.0f;

	/** Leash: atraso para sair andando, sorteado entre os dois a cada saída (s). Nada de todos no mesmo frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Leash", meta = (ClampMin = "0.0"))
	float LeashReactionMin = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Leash", meta = (ClampMin = "0.0"))
	float LeashReactionMax = 0.4f;

	/** Leash: de quanto em quanto tempo o caminho é refeito, se o lugar de destino mudou (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Leash", meta = (ClampMin = "0.05"))
	float LeashRepathInterval = 0.3f;

	/** Leash: o caminho só é refeito se o destino andou mais que isto desde o último, em uu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Leash", meta = (ClampMin = "0.0"))
	float LeashRepathDistance = 100.0f;

	/** Trail: distância pela trilha entre um membro e o da frente, em uu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Follow", meta = (ClampMin = "30.0"))
	float FollowSpacing = 120.0f;

	/** O seguidor anda esta fração mais rápido que o líder, para alcançá-lo depois de uma curva ou parada. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Follow", meta = (ClampMin = "1.0", ClampMax = "2.0"))
	float FollowerSpeedScale = 1.15f;

	/** Trail: mais longe que isto do lugar dele na trilha, o seguidor corre mesmo com o líder andando. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Follow", meta = (ClampMin = "0.0"))
	float CatchUpDistance = 300.0f;

	/**
	 * Atalhos na trilha: quando o caminho por ela até o lugar do seguidor é bem mais longo que a
	 * reta (o líder deu uma volta e passou do lado dele), ele vai reto até o ponto mais adiantado
	 * da trilha que enxerga sem parede no meio — pelo navmesh, ou por um sweep sem ele.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Follow")
	bool bTrailShortcuts = true;

	/** Quanto o atalho precisa economizar (trilha menos a reta) para valer, em uu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Follow", meta = (EditCondition = "bTrailShortcuts", ClampMin = "50.0"))
	float ShortcutMinSaving = 250.0f;

	/** O atalho só olha pontos da trilha até esta distância em linha reta, em uu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Follow", meta = (EditCondition = "bTrailShortcuts", ClampMin = "100.0"))
	float ShortcutMaxDistance = 900.0f;

	// --- Juntar ao lado ---

	/** Segundos com o líder parado até os seguidores se juntarem ao lado dele. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Gather", meta = (ClampMin = "0.0"))
	float GatherDelay = 0.5f;

	/** Distância do líder até o lugar de cada um, em uu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Gather", meta = (ClampMin = "30.0"))
	float GatherDistance = 110.0f;

	/**
	 * O lugar de cada seguidor em volta do líder parado, em graus a partir da frente dele
	 * (0 = na frente, 90 = à direita, 180 = atrás). O primeiro seguidor usa o primeiro ângulo,
	 * e assim por diante; passando do fim, volta ao começo numa roda mais afastada.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Gather")
	TArray<float> GatherAngles;

	// --- Segurança ---

	/** Mais longe que isto do líder, o seguidor teleporta para o lugar dele. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Safety", meta = (ClampMin = "200.0"))
	float TeleportDistance = 1500.0f;

	/** Segundos parado tentando andar (porta fechou, NPC no caminho) até o seguidor teleportar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Safety", meta = (ClampMin = "0.2"))
	float StuckTime = 1.5f;

	// --- Troca de personagem ---

	/** Duração do deslize da câmera até o novo personagem (0 = corte seco). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Switch", meta = (ClampMin = "0.0"))
	float SwitchBlendTime = 0.4f;

	/** O R3 só passa o controle para quem está até esta distância do líder, em uu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Party|Switch", meta = (ClampMin = "0.0"))
	float MaxSwitchDistance = 1000.0f;

	// --- API ---

	/** Passa o controle para o próximo da formação (o R3). False se não há para quem trocar. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Field|Party")
	bool CycleLeader();

	/** Passa o controle para um membro específico que está no mapa, a qualquer distância. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Field|Party")
	bool SwitchLeader(FName Character);

	/** O personagem que o jogador controla agora. */
	UFUNCTION(BlueprintPure, Category = "JRPG|Field|Party")
	AJRPGFieldCharacter* GetLeader() const;

	/** O personagem de um membro no mapa (líder ou seguidor), ou nullptr. Para cutscenes. */
	UFUNCTION(BlueprintPure, Category = "JRPG|Field|Party")
	AJRPGFieldCharacter* GetPartyPawn(FName Character) const;

	/** Os seguidores no mapa, na ordem da formação. */
	UFUNCTION(BlueprintPure, Category = "JRPG|Field|Party")
	TArray<AJRPGFieldCharacter*> GetFollowers() const;

	/**
	 * Liga/desliga o seguir. Desligado, os seguidores ficam parados onde estão: uma cutscene
	 * pode movê-los sem a party brigar com o Sequencer. Religar não teleporta ninguém.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Field|Party")
	void SetFollowEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "JRPG|Field|Party")
	bool IsFollowEnabled() const { return bFollowEnabled; }

	/** Teleporta os seguidores para perto do líder (fim de cutscene, teleporte feito à mão). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Field|Party")
	void SnapFollowersToLeader();

	/** Confere o mapa com a formação: põe quem falta, tira quem saiu. Roda sozinho no OnPartyChanged. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Field|Party")
	void RefreshParty();

	/** Disparado depois de cada troca de personagem. */
	UPROPERTY(BlueprintAssignable, Category = "Party")
	FOnFieldLeaderChanged OnLeaderChanged;

	/** O controller possuiu um pawn (começo do mapa, portal, load). Chamado pelo AJRPGFieldPlayerController. */
	void HandlePawnPossessed(APawn* NewPawn);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Um ponto da trilha do líder e quanto ele já tinha andado até ali (uu). */
	struct FTrailPoint
	{
		FVector Location = FVector::ZeroVector;
		float Distance = 0.0f;
	};

	/** O estado de cada seguidor. */
	struct FFollower
	{
		TWeakObjectPtr<AJRPGFieldCharacter> Pawn;
		/** Até onde da trilha ele já chegou (mesma unidade de FTrailPoint::Distance). */
		float Cursor = 0.0f;
		/** O lugar ao lado do líder parado (bHasSlot = há um lugar válido). */
		FVector Slot = FVector::ZeroVector;
		bool bHasSlot = false;
		float StuckSeconds = 0.0f;
		/** Atalho em andamento: indo reto até o ponto ShortcutTo da trilha. */
		bool bShortcut = false;
		float ShortcutTo = 0.0f;
		/** Segundos até procurar atalho de novo. */
		float ShortcutCooldown = 0.0f;
		/** Leash: andando atrás do líder (false = parado esperando ele se afastar). */
		bool bLeashMoving = false;
		/** Leash: segundos até sair andando (< 0 = ainda não decidiu sair). */
		float ReactionTimer = -1.0f;
		/** Leash: o destino do último MoveTo e os segundos até poder refazê-lo. */
		FVector MoveGoal = FVector::ZeroVector;
		float RepathTimer = 0.0f;
	};

	UFUNCTION()
	void HandlePartyChanged();

	/** Arruma o mapa para o pawn que o controller acabou de possuir (um tick depois). */
	void SetupForPossessedPawn();

	AJRPGFieldPlayerController* GetFieldController() const;
	UPartySubsystem* GetParty() const;

	/** Quem deveria estar no mapa além do líder, na ordem da formação. */
	TArray<FName> GetWantedFollowers(FName LeaderKey) const;

	/** O membro no mapa; se não está, spawna como seguidor perto do líder. */
	AJRPGFieldCharacter* FindOrSpawnMember(FName Character);

	/** Spawna o membro como seguidor em cima do líder (atravessa pawns, então cabe). */
	AJRPGFieldCharacter* SpawnMember(FName Character);
	void DestroyMember(AJRPGFieldCharacter* Member);

	/**
	 * A troca em si. bSwapPlaces = o novo líder fica exatamente onde o antigo estava (começo de
	 * mapa, alguém saindo da formação); sem isso, cada um fica onde está.
	 */
	bool DoSwitchLeader(AJRPGFieldCharacter* NewLeader, bool bSwapPlaces, float BlendTime);

	/** Refaz a lista de seguidores na ordem da formação, mantendo o estado de quem continua. */
	void RebuildFollowers();

	// --- Trilha ---

	void ResetTrail(const FVector& LeaderLocation);
	void RecordTrail(const AJRPGFieldCharacter* Leader);
	float GetTrailHead() const { return Trail.Num() > 0 ? Trail.Last().Distance : 0.0f; }
	FVector SampleTrail(float Distance) const;

	/**
	 * Até onde da trilha o seguidor chegou: o ponto dela mais perto de Location, entre um pouco
	 * antes do Cursor e o Desired. Pular adiante (o líder voltou por cima dele) só vale se ele
	 * está em cima da trilha — senão um trecho do outro lado de uma parede contaria.
	 */
	float ProjectOntoTrail(const FVector& Location, float Cursor, float Desired) const;

	// --- Seguidores ---

	void UpdateFollower(FFollower& Follower, int32 Index, const AJRPGFieldCharacter* Leader,
		float LeaderSpeed, bool bGathering, bool bLeash, float DeltaTime);

	/** Modo Leash: sair quando o líder se afasta, MoveTo até o lugar dele, parar ao chegar. */
	void UpdateLeashFollower(FFollower& Follower, int32 Index, const AJRPGFieldCharacter* Leader,
		float LeaderSpeed, bool bGathering, float DeltaTime);

	/** Cancela o MoveTo do seguidor e volta ao estado parado do Leash. */
	void StopLeashMove(FFollower& Follower) const;

	/** Outro membro da party (ou o líder) em cima do Member: atravessam pawns, então precisa afastar. */
	bool IsCrowded(const AJRPGFieldCharacter* Member) const;

	/** O mapa tem navmesh (o Leash precisa dele). */
	bool HasNavMesh() const;

	/**
	 * Procura um atalho (bTrailShortcuts): do lugar dele na trilha para trás, o primeiro ponto que
	 * economiza ShortcutMinSaving e que ele alcança em linha reta. Achou: liga bShortcut.
	 */
	void TryTrailShortcut(FFollower& Follower, const AJRPGFieldCharacter* Leader,
		const AJRPGFieldCharacter* Member, float Desired) const;

	/**
	 * Dá para ir reto de From até To? Com navmesh, sem borda dele no meio (parede, buraco); sem
	 * navmesh, uma esfera na altura do corpo não bate em nada e há chão ao longo do caminho.
	 */
	bool IsStraightPathClear(const AJRPGFieldCharacter* Member, const FVector& From, const FVector& To) const;

	/** Consulta de colisão que ignora todos os membros da party no mapa. */
	FCollisionQueryParams MakePartyQueryParams() const;

	/** Os lugares ao lado do líder parado. bCheckFromMember: o seguidor precisa chegar em linha reta (Trail). */
	void ComputeGatherSlots(const AJRPGFieldCharacter* Leader, bool bCheckFromMember);

	/** O ponto em volta do líder parado que é do seguidor Index (GatherAngles), sem validar. */
	FVector GetGatherPoint(int32 Index, const AJRPGFieldCharacter* Leader) const;

	/**
	 * Um lugar para Member ficar perto de Wanted: no navmesh (se o mapa tem) e sem parede até o
	 * líder nem (bCheckFromMember) até onde o Member está. Devolve o centro da cápsula dele.
	 */
	bool FindStandSpot(const AJRPGFieldCharacter* Leader, const AJRPGFieldCharacter* Member,
		const FVector& Wanted, bool bCheckFromMember, FVector& OutSpot) const;

	/** Para onde teleportar o seguidor Index: o lugar dele na trilha, ao lado do líder, ou o próprio líder. */
	FVector GetSnapSpot(int32 Index, const AJRPGFieldCharacter* Leader, const AJRPGFieldCharacter* Member) const;

	void TeleportMember(AJRPGFieldCharacter* Member, const FVector& Location, const FRotator& Rotation) const;

	/** Leva uma posição de centro de cápsula do líder para a altura do centro da cápsula do Member. */
	static FVector ToMemberHeight(const FVector& LeaderCenter, const AJRPGFieldCharacter* Leader, const AJRPGFieldCharacter* Member);

	/** Os membros no mapa (líder incluso), pela key. */
	TMap<FName, TWeakObjectPtr<AJRPGFieldCharacter>> PartyPawns;

	TArray<FFollower> Followers;

	/** Mais antigo primeiro. O último ponto é sempre a posição de agora do líder. */
	TArray<FTrailPoint> Trail;

	bool bFollowEnabled = true;
	bool bSwitching = false;
	bool bPartyAllowedInMap = true;
	bool bGatherSlotsValid = false;
	float LeaderIdleSeconds = 0.0f;
};
