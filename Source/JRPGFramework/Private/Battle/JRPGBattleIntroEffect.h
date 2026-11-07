#pragma once

#include "CoreMinimal.h"

/**
 * Uma primitiva da transição: um quad na tela de 320 x 240 do PS1 (os quatro cantos na ordem do GPU: 0 1 2 / 1 2 3), com a UV em
 * texels da captura (a página de 320 x 256: a linha desenhada d fica em v = d + 4, o original soma o 4 e as UVs aqui já o tiraram).
 */
struct FJRPGIntroPrim
{
	enum class EMode : uint8
	{
		/** Opaco, a textura da captura vezes a cor. */
		Opaque,
		/** Aditivo sem textura: a face de sombra do tile (a rampa da textura de sombra nas cores dos cantos). */
		AddShade,
		/** Aditivo, a textura da captura vezes a cor (o pedaço que voa nos estilos de partículas). */
		AddTextured,
		/** Meia mistura sem textura (0,5 B + 0,5 F: o anel da onda de choque). */
		Half
	};

	FVector2f XY[4];
	FVector2f UV[4];
	/** A cor de cada canto (0..1, 1 = 0x80 do PS1). A face de sombra usa uma por canto; as outras, a mesma nas quatro. */
	FVector3f Color[4];
	EMode Mode = EMode::Opaque;
	int32 Ot = 0;
	int32 Seq = 0;
};

/**
 * FJRPGBattleIntroEffect
 * A transição campo -> luta do original (o overlay PROT 0979 field_battle_intro): os cinco estilos, a escolha, a duração e o fade,
 * portados dos kernels do RE (legend-of-legaia-re crates/render-kernels battle_intro*, crates/engine-battle-vm) pela réplica em
 * HTML do usuário (LegaiaRE/viewer/batalha-intro, fora do repositório). Só a simulação: um quadro de tela por Step, as primitivas
 * na tela do PS1 (ou, na cortina, a imagem pronta); quem desenha é o UJRPGBattleTransition.
 *
 * Aproximações (as mesmas da réplica): o seno / cosseno do jogo pela matemática padrão, o LCG do port no lugar do rand do jogo, a
 * rampa da textura de sombra do tile desenhada à mão, o "wash" 0x101010 do scatter e do swirl tardio não modelado.
 */
class FJRPGBattleIntroEffect
{
public:
	/** Os estilos do original (DAT_801D2460). */
	enum EStyle : int32
	{
		Scatter = 0,
		SpinUp = 1,
		Shatter = 2,
		Curtain = 3,
		Swirl = 4
	};

	/**
	 * select_intro_style (FUN_801CE8CC): bScripted é o bit 0x80 das flags da luta (uma luta de script), FirstMonsterId o id do
	 * primeiro monstro da formação (DT_Enemies.MonsterArchiveID), SceneId o mapa (-1 = desconhecido: os dois braços que dependem do
	 * mapa ficam de fora).
	 */
	static void SelectStyle(bool bScripted, int32 FirstMonsterId, int32 SceneId, int32& OutStyle, int32& OutSub);

	/** DAT_801D2458: 132 quadros de tela, 252 no swirl. */
	static int32 DurationFrames(int32 Style);

	/**
	 * O fade do segundo switch de FUN_801CF5BC no quadro Elapsed: falso antes de começar. OutLevel 0..255; bOutWhite = ABR 1 (branco
	 * aditivo), senão ABR 2 (preto subtrativo).
	 */
	static bool FadeAt(int32 Style, int32 Sub, int32 Elapsed, int32& OutLevel, bool& bOutWhite);

	/** Arma o estilo (os inits). Na cortina, SetCurtainSource antes do primeiro Step. */
	void Begin(int32 InStyle, int32 InSub, uint32 Seed);

	/**
	 * A cortina trabalha numa imagem (as duas passadas em CPU): a captura já reduzida para Width x Height, que é 320 x 240 vezes a
	 * escala de trabalho.
	 */
	void SetCurtainSource(const TArray<FColor>& Pixels, int32 Width, int32 Height);

	/** Um quadro de tela: bRender monta as primitivas (ou a imagem da cortina) do estado de entrada do quadro, depois integra. */
	void Step(bool bRender);

	int32 GetStyle() const { return Style; }
	int32 GetSub() const { return Sub; }
	/** Quantos Step já rodaram (o quadro de tela da transição). */
	int32 GetFrame() const { return Frame; }

	/** As primitivas do último Step, já na ordem de desenho (o painter do OT: o bucket maior primeiro). */
	const TArray<FJRPGIntroPrim>& GetPrims() const { return Prims; }

	bool IsCurtain() const { return Style == Curtain; }
	const TArray<FColor>& GetCurtainImage() const { return CurtainDisplay; }
	int32 GetCurtainWidth() const { return CurtainWidth; }
	int32 GetCurtainHeight() const { return CurtainHeight; }

private:
	// --- O "ambiente" do port: o LCG e o seno / cosseno / atan2 / raiz em 4096 avos -----------------
	uint32 Rng = 1;
	int32 Rand();
	static int32 Heading(int32 X, int32 Z);
	static int32 SinQ(int32 Angle);
	static int32 CosQ(int32 Angle) { return SinQ(Angle + 0x400); }
	static int32 SqrtI(int64 Value);

	struct FScreen
	{
		int32 X = 0;
		int32 Y = 0;
		int32 Sz = 0;
	};
	using FRot = int32[9];
	static void EulerRot(int32 Ax, int32 Ay, int32 Az, FRot& Out);
	static FScreen Project(const FRot& Rot, const int32 (&Tr)[3], int32 X, int32 Y, int32 Z);
	static int32 NClip(const FScreen& A, const FScreen& B, const FScreen& C);

	void AddPrim(const FScreen (&P)[4], const FVector2f (&UV)[4], FJRPGIntroPrim::EMode Mode, const FVector3f& Color, int32 Ot);
	void SortPrims();

	// --- Estilo 2: o tile shatter -------------------------------------------------------------------
	struct FTile
	{
		int32 Progress = 0;
		int32 Pos[3] = { 0, 0, 0 };
		int32 Delay = 0;
		int32 Ang[3] = { 0, 0, 0 };
		int32 Cx[4] = { 0, 0, 0, 0 };
		int32 Cy[4] = { 0, 0, 0, 0 };
		int32 Su[4] = { 0, 0, 0, 0 };
		int32 Sv[4] = { 0, 0, 0, 0 };
		int32 Vx = 0, Vy = 0, Vz = 0;
		int32 SpinX = 0, SpinY = 0;
	};
	TArray<FTile> Tiles;
	void BeginTiles();
	void StepTiles(bool bRender);

	// --- Estilos 0 e 1: os campos de partículas -----------------------------------------------------
	struct FParticle
	{
		uint32 Tint = 0x808080;
		int32 Ang[3] = { 0, 0, 0 };
		int32 Pos[3] = { 0, 0, 0 };
		int32 Spin[3] = { 0, 0, 0 };
		int32 Delay = 0;
		int32 AngVel[3] = { 0, 0, 0 };
		int32 Page = 0;
		int32 V = 0;
	};
	TArray<FParticle> Particles;
	void BeginParticles();
	void StepParticles(bool bRender);

	// --- Estilo 3: a cortina ------------------------------------------------------------------------
	TArray<FColor> CurtainSource;
	TArray<FColor> CurtainInter;
	TArray<FColor> CurtainDisplay;
	int32 CurtainWidth = 0;
	int32 CurtainHeight = 0;
	void StepCurtain();

	// --- Estilo 4: o swirl --------------------------------------------------------------------------
	struct FBand
	{
		int32 Angle = 0xA00;
		int32 Rate = 0;
	};
	TArray<FBand> Bands;
	TArray<FIntVector> SwirlV;
	TArray<FIntPoint> SwirlUV;
	void BeginSwirl();
	void StepSwirl(bool bRender);

	int32 Style = Shatter;
	int32 Sub = 0;
	int32 Frame = 0;
	int32 SeqCounter = 0;
	TArray<FJRPGIntroPrim> Prims;
};
