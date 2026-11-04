#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Arts/JRPGArtSequence.h"
#include "Arts/JRPGArtsRules.h"
#include "JRPGArtTester.generated.h"

class USkeletalMeshComponent;
class UTextRenderComponent;
class UAnimSequence;

/**
 * AJRPGArtTester
 * Bancada de teste das Arts do DT_Arts: põe o modelo de batalha do herói no mapa e toca uma Art
 * do jeito que ela sai na luta. Tudo se escolhe no Details; Play / Stop / Play All também
 * funcionam fora do Play, no viewport do editor (Realtime ligado).
 *
 * A fila (golpes, starter, Art, Sequence), a velocidade do jogo e o Chain Cut vêm do
 * UJRPGArtSequenceLibrary — a mesma regra que a luta usa. Aqui fica só o tocar: um clip atrás do
 * outro, cada um começando onde o corpo terminou o anterior.
 *
 * Só para teste: não entra na luta. Guia: Docs/Guides/ARTS.md.
 */
UCLASS(Blueprintable)
class JRPGFRAMEWORK_API AJRPGArtTester : public AActor
{
	GENERATED_BODY()

public:
	AJRPGArtTester();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Art Tester")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Art Tester")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	/** O nome da Art e o passo atual, em cima da cabeça. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Art Tester")
	TObjectPtr<UTextRenderComponent> Label;

	/** O herói (row do DT_Characters). Filtra a lista de Arts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester", meta = (GetOptions = "GetHeroOptions"))
	FName Hero = TEXT("Vahn");

	/** A Art (row do DT_Arts) que Play toca. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester", meta = (GetOptions = "GetArtOptions"))
	FName Art;

	/**
	 * Um turno montado à mão: Arts do herói e botões soltos (Arms, Ra-Seru, High, Low = um golpe
	 * simples), tocados em ordem por Play Combo. Ex.: Somersault, Arms, Hyper Elbow.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester", meta = (GetOptions = "GetComboOptions"))
	TArray<FName> Combo;

	/**
	 * Os botões como o jogador digita, e as regras decidem o turno (Play Input): "Arms, Ra-Seru,
	 * Arms", "A R A", ou as setas do controle ("← → ←", "Left Right Left"), estas pela mão do herói —
	 * a Noa é canhota. O que as regras reconhecem aparece em Recognition enquanto se digita.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester|Input")
	FString Input;

	/** O AP do herói no começo do turno do Input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester|Input", meta = (ClampMin = "0", ClampMax = "100"))
	int32 InputAP = 0;

	/** Depois de Play Input, o AP vira o do fim do turno (para o próximo Play Input continuar dali). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester|Input")
	bool bCarryAP = true;

	/** As Arts que o herói já sabe no tester (não é o save). Play Input acrescenta as aprendidas no uso. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester|Input", meta = (GetOptions = "GetArtOptions"))
	TArray<FName> KnownArts;

	/** Trata toda Art do herói como já sabida (as Supers saem logo). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester|Input")
	bool bKnowAllArts = false;

	/** Play Input põe em KnownArts as Regular aprendidas no uso (como o save, no jogo). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester|Input")
	bool bLearnOnPlay = true;

	/** O herói tem Ra-Seru (o original só deixa Hyper e Miracle saírem com ele). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester|Input")
	bool bHasRaSeru = true;

	/** O gauge do turno, como a luta semeia do AGL (cada botão custa 30). 0 = sem limite. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester|Input", meta = (ClampMin = "0"))
	int32 InputGauge = 0;

	/** O que as regras fazem do Input: as Arts, a Super e o que tem dentro, o AP a cada passo, o que caiu (só leitura). */
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Art Tester|Input")
	TArray<FString> Recognition;

	/**
	 * Os botões antes do último saem como golpes simples, e o último vira starter + Art — como no
	 * jogo (Iron Head: High, Low, Low = SwingU, SwingD, ArtStarter, Ironhead). Desligado = só
	 * starter + Art.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester")
	bool bPlayLeadingSwings = true;

	/**
	 * Com outro passo depois, corta o clip no Chain Cut dele, como o jogo (os golpes emendam logo
	 * depois do acerto, sem a volta à guarda). Desligado = cada clip toca inteiro.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester")
	bool bChainCut = true;

	/** Refaz a escala de velocidade do jogo (8 / 4 / 2). Desligado = o RateScale de cada clip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester")
	bool bGameSpeed = true;

	/** Velocidade geral por cima de tudo (1 = a do jogo). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester", meta = (ClampMin = "0.05", ClampMax = "4.0"))
	float PlayRate = 1.0f;

	/**
	 * Cada clip começa onde o corpo terminou o anterior, na direção da frente do herói: o ator anda
	 * para o quadro 0 do próximo cair no lugar do último (pelo FollowBone). Clips que já emendam
	 * (starter -> Art) não mexem; depois da Slash Kick o Idle continua lá na frente. De lado não:
	 * o jogo só leva o ator ao longo da face.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester")
	bool bFollowAnimation = true;

	/** No fim de cada Art, volta ao lugar onde o primeiro Play começou. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester")
	bool bReturnHomeAfterArt = false;

	/** Pausa entre um passo e o próximo, em segundos (0 = emenda como no jogo). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester", meta = (ClampMin = "0.0"))
	float PauseBetweenSteps = 0.0f;

	/** Pausa entre uma Art e a próxima (Loop e Play All), em segundos. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester", meta = (ClampMin = "0.0"))
	float PauseBetweenArts = 1.0f;

	/** Repete a Art (ou o Combo, ou a lista do Play All) sem parar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester")
	bool bLoop = false;

	/** No Play, começa a tocar sozinho. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester")
	bool bPlayOnBeginPlay = true;

	/** Heróis canhotos: Arms é o golpe da direita e Ra-Seru o da esquerda. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester|Setup")
	TArray<FName> LeftHandedHeroes = { TEXT("Noa") };

	/** O bone do corpo que o bFollowAnimation alinha de um clip para o outro. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester|Setup")
	FName FollowBone = TEXT("part_00");

	/** Giro da malha para olhar para +X do ator (os modelos do PS1 olham para -Y). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester|Setup")
	float MeshYawOffset = 90.0f;

	/** Altura do texto acima dos pés, em cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Tester|Setup")
	float LabelHeight = 240.0f;

	/** O que a Art escolhida toca, passo a passo (só leitura). */
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Art Tester")
	TArray<FJRPGArtStep> Steps;

	/** O passo tocando agora (só leitura). */
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "Art Tester")
	FString NowPlaying;

	/** Toca a Art escolhida. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Art Tester")
	void Play();

	/** Toca o Combo, emendado como um turno. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Art Tester")
	void PlayCombo();

	/**
	 * Fecha o Input como a luta fecha a janela de Arts — o gauge, o commit, o reconhecimento, o AP — e
	 * toca o turno que sai. As Regular novas entram em KnownArts (bLearnOnPlay).
	 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Art Tester|Input")
	void PlayInput();

	/** Esquece as Arts do tester e zera o AP. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Art Tester|Input")
	void ResetInputState();

	/** Para e volta ao Idle, onde estiver. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Art Tester")
	void Stop();

	/** Toca todas as Arts do herói (menos as Parts), uma atrás da outra. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Art Tester")
	void PlayAll();

	/** Volta o ator para onde estava no primeiro Play (o bFollowAnimation o leva para longe). */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Art Tester")
	void ResetPosition();

	/** Escolhe a próxima Art do herói e toca. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Art Tester")
	void NextArt();

	/** Escolhe a Art anterior do herói e toca. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Art Tester")
	void PreviousArt();

	UFUNCTION()
	TArray<FString> GetHeroOptions() const;

	UFUNCTION()
	TArray<FString> GetArtOptions() const;

	UFUNCTION()
	TArray<FString> GetComboOptions() const;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

private:
	bool IsLeftHanded() const { return LeftHandedHeroes.Contains(Hero); }

	/** O bone seguido, no mundo, com a pose do clip atual já avaliada. */
	FVector FollowBoneLocation() const;

	void ApplyHeroMesh();

	/** O Input em botões, cortado no gauge. Falso se uma palavra não é botão. */
	bool ReadInput(TArray<EJRPGArtButton>& OutButtons, int32& OutDropped) const;

	/** O turno do Input com o estado do tester (Arts sabidas, AP, Ra-Seru). */
	FJRPGArtChain BuildInputChain(const TArray<EJRPGArtButton>& Buttons) const;

	/** Refaz Recognition a partir do Input. */
	void UpdateRecognition();

	void StartArt(FName ArtKey);
	void StartSteps(const FString& Title);
	void StartStep(int32 Index);
	void FinishArt();
	void GoHome();

	/**
	 * Troca o clip na mesma hora: avalia a pose nova já, e com bAlign leva o ator para o corpo
	 * continuar onde a pose anterior o deixou — tudo no mesmo quadro, sem um quadro de pose velha
	 * no lugar novo (que o motion blur desenha como rastro).
	 */
	void PlayClipNow(UAnimSequence* Clip, bool bLooping, float Rate, bool bAlign);
	void PlayIdle(bool bAlign);
	void UpdateLabel();

	/** O herói cuja malha está posta (para não recarregar a cada OnConstruction). */
	FName LoadedHero;

	/** O que está tocando (o nome da Art ou "Combo"), para o texto. */
	FString CurrentTitle;

	/** Tocando o Combo (o Loop repete o Combo). */
	bool bComboMode = false;

	/** Tocando o Input (o Loop repete o Input, um turno depois do outro). */
	bool bInputMode = false;

	/** Fila do Play All (vazia = só a Art escolhida). */
	TArray<FName> Queue;
	int32 QueueIndex = 0;

	int32 StepIndex = INDEX_NONE;
	bool bRunning = false;

	/** Onde o ator estava no primeiro Play (o bFollowAnimation o leva para longe dali). */
	FVector Home = FVector::ZeroVector;
	bool bHasHome = false;

	/** Pausa em andamento (entre passos ou entre Arts) e o que vem depois dela. */
	float WaitLeft = 0.0f;
	bool bWaitingNextArt = false;

	/** Tempo no passo atual (garante a troca se o clip não avisar o fim). */
	float StepElapsed = 0.0f;
	float StepMaxTime = 0.0f;
};
