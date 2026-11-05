#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "JRPGBattleCamera.generated.h"

class AJRPGBattleArena;
class AJRPGBattlePawn;

/**
 * Uma pose da câmera da luta nas grandezas do original (FUN_801D5854 monta três vetores e o tween anda até eles): um FOCO no
 * mundo, o giro em volta dele, a inclinação, e o TR (a translação no espaço da vista) já em cm.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGBattleCameraPose
{
	GENERATED_BODY()

	/** O ponto que a câmera enquadra (o look-at do original), em mundo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera")
	FVector Focus = FVector::ZeroVector;

	/** Para onde a câmera olha no plano, em graus de mundo (o Yaw de um FRotator). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera")
	float Yaw = 0.0f;

	/** Graus; positivo olha para baixo (o pitch do original). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera")
	float Pitch = 0.0f;

	/**
	 * O TR do original em cm: X = desvio lateral do foco na tela (negativo = foco à esquerda), Y = quanto o olho fica acima
	 * (o foco aparece abaixo do centro da tela), Z = a distância do olho até o foco, ao longo da vista.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera")
	FVector Offset = FVector::ZeroVector;
};

/** O enquadramento que a câmera da luta está seguindo agora (os casos de FUN_801D5854). */
UENUM(BlueprintType)
enum class EJRPGBattleShot : uint8
{
	/** Caso 9: a formação inteira, de longe, girando devagar (o prompt da rodada, a confirmação, entre as ações). */
	Far,
	/** Caso 0: o close do herói comandado (o menu dele e a entrada das Arts), de frente, um pouco de lado. */
	Member,
	/** Caso 1: o cursor de alvo num inimigo: por trás e acima do herói, olhando o inimigo. */
	TargetEnemy
};

/**
 * AJRPGBattleCamera
 * A câmera da luta (Docs/Reference/BATTLE_RE_SWEEP.md seção 7): a arena a cria na montagem quando não há BattleCamera posta à mão
 * (AJRPGBattleArena::bUseBattleCamera). A CADA quadro ela escolhe o enquadramento pelo estado da luta, monta a pose dele com os
 * combatentes vivos e anda até ela com o ease-out do original: cada passo de 30 Hz cobre 1/ChaseSteps do que falta, re-armado todo
 * quadro, então a câmera persegue um alvo que anda e nunca para seca numa troca de estado.
 *
 * Unidades: os números do original estão em unidades do PS1; as translações da câmera estão na escala 4x da luta
 * (cm = TR / 4 * cm por unidade), e "prescale" = x 1.6 (o 0x800 do plano geral vira 3276).
 *
 * Um Blueprint filho (AJRPGBattleArena::BattleCameraClass) muda os números, ou sobrescreve BuildShotPose.
 */
UCLASS(Blueprintable)
class JRPGFRAMEWORK_API AJRPGBattleCamera : public ACameraActor
{
	GENERATED_BODY()

public:
	AJRPGBattleCamera(const FObjectInitializer& ObjectInitializer);

	virtual void Tick(float DeltaSeconds) override;

	/** A arena da luta: lê os combatentes dela, segue o recentrar da formação e já se põe na primeira pose (sem deslizar). */
	void InitForArena(AJRPGBattleArena* InArena);

	/** O enquadramento de agora. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle Camera")
	EJRPGBattleShot GetShot() const { return Shot; }

	/** A pose em que a câmera está (já andada) e a pose que ela persegue. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle Camera")
	FJRPGBattleCameraPose GetLivePose() const { return Live; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle Camera")
	FJRPGBattleCameraPose GetTargetPose() const { return Target; }

	/** Vai direto para a pose alvo de agora, sem o ease-out (um corte). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle Camera")
	void SnapToTarget();

	// --- Lente ---------------------------------------------------------------------------------

	/**
	 * A lente do original (H = 256 numa tela de 320 x 240: 64 graus na horizontal em 4:3, uns 50 na vertical), com a vertical presa:
	 * uma tela mais larga mostra mais dos lados, nunca menos em cima e embaixo. Desligado, a câmera fica com a lente do componente.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Lens")
	bool bOriginalLens = true;

	/** Graus na horizontal numa tela 4:3 (o original: 64). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Lens", meta = (ClampMin = "10.0", ClampMax = "170.0", EditCondition = "bOriginalLens"))
	float FieldOfView43 = 64.0f;

	// --- O movimento ---------------------------------------------------------------------------

	/**
	 * O ease-out: cada passo de 30 Hz anda 1/ChaseSteps do que falta (o a3 = 0xC do original: 6 passos de 2 quadros). Maior = mais
	 * devagar. 0 = corte seco.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Motion", meta = (ClampMin = "0.0"))
	float ChaseSteps = 6.0f;

	/** O mesmo para o plano geral (o a3 = 0xE do caso 9: 7 passos). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Motion", meta = (ClampMin = "0.0"))
	float FarChaseSteps = 7.0f;

	/**
	 * Multiplica toda distância do olho ao foco (o TR.z de todo enquadramento): 1 = a do original. Mais = mais longe (os modelos
	 * novos, uma tela grande).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Motion", meta = (ClampMin = "0.1"))
	float DistanceScale = 1.0f;

	/**
	 * Inverte o lado dos giros presos a um personagem (o original é Y para baixo, e o giro dele é "frente do personagem menos o
	 * ângulo"): o close do menu sai do outro lado do rosto, e o mesmo para os da ação.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Motion")
	bool bMirrorYaw = false;

	// --- Plano geral (caso 9) e órbita --------------------------------------------------------

	/**
	 * O giro do primeiro quadro, em graus (0 = de trás da party, olhando para os inimigos). O original abre em 0, com as duas
	 * fileiras uma atrás da outra na tela, e sai disso pela órbita; um pouco de lado já abre com as duas à vista.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Far")
	float StartYaw = 20.0f;

	/** A órbita do plano geral: o giro anda sozinho enquanto ninguém age (o original: -2 unidades por quadro, uns -10,5 graus/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Far")
	bool bOrbit = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Far", meta = (EditCondition = "bOrbit"))
	float OrbitDegreesPerSecond = -10.546875f;

	/** Inclinação do plano geral em unidades do original (4096 = 360 graus): 32 = 2,8 graus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Far")
	float FarPitchUnits = 32.0f;

	/** O TR.y do plano geral (1280: o olho uns 139 cm acima do foco). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Far")
	float FarLiftUnits = 1280.0f;

	/** Distância = prescale(max(largura * FarSpanFactor, FarMinSpanUnits)): 1 contra 1 nos lugares de ±800 dá 7680 (uns 831 cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Far", meta = (ClampMin = "0.0"))
	float FarSpanFactor = 3.0f;

	/** O piso da distância do plano geral, antes do prescale (0x800: a formação encolhida quando o atacante chega no alvo). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Far", meta = (ClampMin = "0.0"))
	float FarMinSpanUnits = 2048.0f;

	// --- Menu (casos 0 e 1) ---------------------------------------------------------------------

	/**
	 * Usar os closes do menu (o herói comandado, o cursor de alvo). Desligado, o menu fica no plano geral. Precisa da UI de comando
	 * (UJRPGBattleUISubsystem); pelos comandos de console fica sempre no plano geral.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Menu")
	bool bMenuShots = true;

	/** Caso 0: o giro em volta do herói, em unidades do original (0x8F0: de frente, 21 graus de lado). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Menu")
	float MemberYawUnits = 2288.0f;

	/** Caso 0: inclinação (32 = 2,8 graus). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Menu")
	float MemberPitchUnits = 32.0f;

	/** Caso 0: o desvio lateral (-0x200: o herói à esquerda da tela, o menu à direita). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Menu")
	float MemberSideUnits = -512.0f;

	/** Caso 0: a distância, antes do prescale (0x600: uns 266 cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Menu", meta = (ClampMin = "0.0"))
	float MemberDepthUnits = 1536.0f;

	/**
	 * Caso 0: a altura do olho por herói (a tabela 0x801F4D2C do original, pela chave do combatente): Vahn 1152 (uns 125 cm),
	 * Noa 960, Gala 1408. Quem não está aqui usa MemberDefaultLiftUnits.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Menu")
	TMap<FName, float> MemberLiftUnits;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Menu")
	float MemberDefaultLiftUnits = 1152.0f;

	/** Caso 1: inclinação (0x100 = 22,5 graus, olhando de cima). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Menu")
	float TargetPitchUnits = 256.0f;

	/** Caso 1: a altura do olho (0x600, uns 166 cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Menu")
	float TargetLiftUnits = 1536.0f;

	/** Caso 1: a distância, antes do prescale (0x800: uns 355 cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Menu", meta = (ClampMin = "0.0"))
	float TargetDepthUnits = 2048.0f;

	/** Mostrar na tela o enquadramento e a pose (debug no PIE). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Battle Camera|Debug")
	bool bPrintDebug = false;

protected:
	/**
	 * O enquadramento de agora (e de quem: o slot do personagem e o do alvo, INDEX_NONE quando não há) e a pose alvo dele,
	 * re-armada todo quadro como no original. Um filho em C++ pode trocar a escolha inteira.
	 */
	virtual EJRPGBattleShot ChooseShot(int32& OutSlot, int32& OutTargetSlot) const;
	virtual bool BuildShotPose(EJRPGBattleShot InShot, float DeltaSeconds, FJRPGBattleCameraPose& OutPose);

	/** Caso 9: a caixa da formação inteira; o giro é o da câmera (passa direto), a órbita anda ele. */
	bool BuildFarPose(float DeltaSeconds, FJRPGBattleCameraPose& OutPose);
	/** Caso 0: o close do herói ShotSlot. */
	bool BuildMemberPose(FJRPGBattleCameraPose& OutPose) const;
	/** Caso 1: do herói ShotSlot para o inimigo ShotTargetSlot. */
	bool BuildTargetEnemyPose(FJRPGBattleCameraPose& OutPose) const;

	/**
	 * O giro do original preso à frente de um personagem ("ângulo - frente do personagem", em unidades) como o Yaw de mundo da
	 * câmera: o ângulo é medido a partir de olhar para o rosto dele (0x800 = de frente, 0 = por trás), bMirrorYaw troca o lado.
	 */
	float FacingRelativeYaw(const AJRPGBattlePawn* Pawn, float Units) const;

	/** O pawn de um slot (nulo se não há ou não está à vista). */
	AJRPGBattlePawn* PawnForSlot(int32 Slot) const;

	/** Unidades do original (4096 = 360 graus) para graus. */
	static float UnitsToDegrees(float Units) { return Units * (360.0f / 4096.0f); }
	/** Uma translação da câmera (escala 4x da luta) em cm. */
	float CameraUnitsToCm(float Units) const;
	/** O prescale do original: x 1.6 (uma profundidade crua para a escala da câmera). */
	static float Prescale(float Units) { return Units * 1.6f; }

	/** Os combatentes à vista (vivos e caídos que ainda aparecem). */
	TArray<AJRPGBattlePawn*> GetVisiblePawns() const;
	/** Onde estão os pés do pawn (o ator está no meio da cápsula). */
	static FVector FeetOf(const AJRPGBattlePawn* Pawn);

private:
	/** A formação andou (o recentrar): o foco anda junto no mesmo quadro, como o "focus pair" do original, e ninguém vê o salto. */
	UFUNCTION()
	void HandleFormationShifted(FVector Translation, int32 Frames);

	/** Anda Live até Target com o ease-out (Steps = os passos de 30 Hz; 0 = corte). */
	void Chase(float DeltaSeconds, float Steps);
	/** Põe o ator na pose Live. */
	void ApplyLivePose();
	void ApplyLens();

	TWeakObjectPtr<AJRPGBattleArena> Arena;
	FJRPGBattleCameraPose Live;
	FJRPGBattleCameraPose Target;
	EJRPGBattleShot Shot = EJRPGBattleShot::Far;
	/** De quem é o enquadramento de agora e o alvo dele (INDEX_NONE quando não há). */
	int32 ShotSlot = INDEX_NONE;
	int32 ShotTargetSlot = INDEX_NONE;
	bool bHasPose = false;
};
