#include "JRPGBattleIntroEffect.h"

// Os números e a ordem dos passos são os do port do RE (crates/render-kernels/src/battle_intro*.rs, crates/engine-battle-vm) pela
// réplica do usuário: não "arrume" um shift ou um truncamento sem conferir lá (o visual depende deles).

namespace
{
	constexpr int32 ScreenW = 320;
	constexpr int32 ScreenH = 240;
	constexpr int32 OffsetX = 160;
	constexpr int32 OffsetY = 114;
	constexpr int32 GteH = 0x80;
	constexpr int32 NearOtz = 0x10;

	int32 I16(int32 Value)
	{
		return static_cast<int16>(static_cast<uint16>(Value & 0xFFFF));
	}

	/** Shift para a direita arredondando para zero (o sra com o viés dos negativos). */
	int32 ShrTZ(int32 Value, int32 Bits)
	{
		const int32 Bias = (1 << Bits) - 1;
		return (Value < 0 ? Value + Bias : Value) >> Bits;
	}

	int32 Shr4(int32 Value)
	{
		return (Value < 0 ? Value + 15 : Value) >> 4;
	}

	/** Divisão inteira para baixo (o Math.floor da réplica). */
	int64 FloorDiv(int64 A, int64 B)
	{
		int64 Q = A / B;
		if ((A % B != 0) && ((A < 0) != (B < 0)))
		{
			--Q;
		}
		return Q;
	}

	/** As dez faces de um tile (cantos 0..3 na frente, 4..7 atrás): as quatro laterais duas vezes, uma de sombra (aditiva) e uma opaca. */
	struct FTileFace
	{
		int32 Corners[4];
		bool bShade;
		int32 Grey;
	};
	const FTileFace TileFaces[] = {
		{ { 1, 5, 3, 7 }, true, 0x60 }, { { 4, 0, 6, 2 }, true, 0x40 },
		{ { 4, 5, 0, 1 }, true, 0x30 }, { { 2, 3, 6, 7 }, true, 0x20 },
		{ { 0, 1, 2, 3 }, false, 0x80 },
		{ { 1, 5, 3, 7 }, false, 0x60 }, { { 4, 0, 6, 2 }, false, 0x60 },
		{ { 4, 5, 0, 1 }, false, 0x60 }, { { 2, 3, 6, 7 }, false, 0x60 },
		{ { 6, 7, 4, 5 }, false, 0x20 },
	};

	/**
	 * A rampa da textura de sombra nos quatro cantos do quad (as UVs (0,0) (64,0) (0,64) (64,64)): no jogo é o canto 64 x 64 de uma
	 * textura de personagem; aqui a rampa da réplica, (y + x / 4) / 80 - 0,35 sobre 0,65, vezes 190, num cinza azulado.
	 */
	FVector3f ShadeCorner(int32 Corner)
	{
		const float X = (Corner == 1 || Corner == 3) ? 64.0f : 0.0f;
		const float Y = Corner >= 2 ? 64.0f : 0.0f;
		const float T = FMath::Clamp((FMath::Max(0.0f, (Y + X * 0.25f) / 80.0f - 0.35f)) / 0.65f, 0.0f, 1.0f);
		const float K = T * 190.0f / 255.0f;
		return FVector3f(K * 0.92f, K * 0.96f, K);
	}

	/** Os dois campos de partículas: as sementes (FUN_801CFBB4 / FUN_801D0164) e o tick (FUN_801CFDA0 / FUN_801D0370). */
	struct FParticleStyle
	{
		int32 RowShift, ColX0, ColDX, ZScale, Fall;
		bool bRadial;
		int32 AngVel[3];
		int32 Quad, DelayScale, RotShift, RotZShift, Prescale;
		bool bSpinUp, bDecay, bBounded, bNearer;
	};
	const FParticleStyle StyleA = { 2, -0x1400, 0x100, 0x1000, -0x50, false, { 0x40, 0x40, 0x20 }, 0x100, 0x6E, 1, 1, 0, false, false, true, false };
	const FParticleStyle StyleB = { 3, -0x2800, 0x200, 0x2000, -8, true, { 0x20, 0x40, 0 }, 0x40, 0x40, 4, 3, 3, true, true, false, true };
	constexpr int32 ParticleTickCount = 0x488;

	/** A rampa do fade de cada estilo: o lead antes do fim, a inclinação e o ABR (1 branco aditivo, 2 preto subtrativo). */
	struct FFadeRamp
	{
		int32 Lead, Slope, Abr;
	};
	const FFadeRamp FadeRamps[] = { { 0x18, 12, 2 }, { 0x18, 16, 1 }, { 0x1C, 16, 2 }, { 0x40, 4, 2 }, { 0x20, 10, 1 } };

	constexpr int32 SwirlBands = 16;
	constexpr int32 SwirlCols = 33;
	constexpr int32 SwirlVertsPerBand = 198;
	constexpr int32 SwirlVertsPerHalf = 99;
}

// =================================================================================================
// A escolha, a duração e o fade
// =================================================================================================

void FJRPGBattleIntroEffect::SelectStyle(bool bScripted, int32 FirstMonsterId, int32 SceneId, int32& OutStyle, int32& OutSub)
{
	// FUN_801CE8CC 0x801CE97C..0x801CEB38, na ordem do original (duas stores de delay slot deixam a luta de script no spin-up)
	int32 S = Shatter;
	int32 B = 0;
	const int32 Id = FirstMonsterId;
	if (bScripted)
	{
		S = SpinUp;
		if (((Id - 0x13) & 0xFF) < 3)
		{
			S = Shatter;
			B = 1;
		}
	}
	if (Id == 0x4C)
	{
		S = Scatter;
		B = 0;
	}
	else if (Id == 0x88)
	{
		S = Scatter;
		B = 1;
	}
	else if (Id == 0xB3)
	{
		S = Scatter;
		B = 2;
	}
	if (Id == 0x4F || Id == 0xAF || Id == 0xA5)
	{
		S = Curtain;
	}
	if ((Id == 0x3E || Id == 0x3F) && (SceneId == 3 || SceneId == 0x0C || SceneId == 0x15))
	{
		S = Shatter;
		B = 2;
	}
	if (Id == 0x80 && SceneId == 0x9B)
	{
		S = Shatter;
		B = 2;
	}
	if (Id == 0xB5)
	{
		S = Swirl;
	}
	OutStyle = S;
	OutSub = B;
}

int32 FJRPGBattleIntroEffect::DurationFrames(int32 InStyle)
{
	return InStyle == Swirl ? 252 : 132;
}

bool FJRPGBattleIntroEffect::FadeAt(int32 InStyle, int32 InSub, int32 Elapsed, int32& OutLevel, bool& bOutWhite)
{
	const FFadeRamp& Ramp = FadeRamps[FMath::Clamp(InStyle, 0, 4)];
	const int32 Total = DurationFrames(InStyle);
	if (Total - Ramp.Lead >= Elapsed)
	{
		return false;
	}
	OutLevel = FMath::Clamp((Elapsed + Ramp.Lead - Total) * Ramp.Slope, 0, 255);
	bOutWhite = (InStyle == Scatter && InSub == 2) ? true : Ramp.Abr == 1;
	return true;
}

// =================================================================================================
// O ambiente: o LCG, os ângulos em 4096 avos, a GTE
// =================================================================================================

int32 FJRPGBattleIntroEffect::Rand()
{
	Rng = Rng * 0x19660Du + 0x3C6EF35Fu;
	return static_cast<int32>((Rng >> 16) & 0x7FFF);
}

int32 FJRPGBattleIntroEffect::Heading(int32 X, int32 Z)
{
	const int32 H = static_cast<int32>(FMath::Atan2(static_cast<double>(Z), static_cast<double>(X)) * 4096.0 / UE_DOUBLE_TWO_PI);
	return ((H % 4096) + 4096) % 4096;
}

int32 FJRPGBattleIntroEffect::SinQ(int32 Angle)
{
	return static_cast<int32>(FMath::Sin((Angle & 0xFFF) * UE_DOUBLE_TWO_PI / 4096.0) * 4096.0);
}

int32 FJRPGBattleIntroEffect::SqrtI(int64 Value)
{
	return Value <= 0 ? 0 : static_cast<int32>(FMath::Sqrt(static_cast<double>(Value)));
}

void FJRPGBattleIntroEffect::EulerRot(int32 Ax, int32 Ay, int32 Az, FRot& Out)
{
	// euler_rot_psx (FUN_80026988): Rx * Ry * Rz, cada produto truncado em q3.12
	const int32 Cx = CosQ(Ax), Sx = SinQ(Ax), Cy = CosQ(Ay), Sy = SinQ(Ay), Cz = CosQ(Az), Sz = SinQ(Az);
	auto Q = [](int64 V) { return static_cast<int32>(V >> 12); };
	const int32 A = Q(static_cast<int64>(Cz) * -Sy);
	const int32 B = Q(static_cast<int64>(Sz) * -Sy);
	Out[0] = Q(static_cast<int64>(Cz) * Cy);
	Out[1] = Q(-(static_cast<int64>(Sz) * Cy));
	Out[2] = Sy;
	Out[3] = Q(static_cast<int64>(Sz) * Cx) - Q(static_cast<int64>(A) * Sx);
	Out[4] = Q(static_cast<int64>(Cz) * Cx) + Q(static_cast<int64>(B) * Sx);
	Out[5] = Q(-(static_cast<int64>(Cy) * Sx));
	Out[6] = Q(static_cast<int64>(Sz) * Sx) + Q(static_cast<int64>(A) * Cx);
	Out[7] = Q(static_cast<int64>(Cz) * Sx) - Q(static_cast<int64>(B) * Cx);
	Out[8] = Q(static_cast<int64>(Cy) * Cx);
}

FJRPGBattleIntroEffect::FScreen FJRPGBattleIntroEffect::Project(const FRot& Rot, const int32 (&Tr)[3], int32 X, int32 Y, int32 Z)
{
	// RTPS: gira, soma a translação, divide por SZ com H = 0x80 e satura como o hardware
	const int64 Vx = FloorDiv(static_cast<int64>(Rot[0]) * X + static_cast<int64>(Rot[1]) * Y + static_cast<int64>(Rot[2]) * Z, 4096) + Tr[0];
	const int64 Vy = FloorDiv(static_cast<int64>(Rot[3]) * X + static_cast<int64>(Rot[4]) * Y + static_cast<int64>(Rot[5]) * Z, 4096) + Tr[1];
	const int64 Vz = FloorDiv(static_cast<int64>(Rot[6]) * X + static_cast<int64>(Rot[7]) * Y + static_cast<int64>(Rot[8]) * Z, 4096) + Tr[2];
	FScreen Out;
	Out.Sz = static_cast<int32>(FMath::Clamp<int64>(Vz, 0, 0xFFFF));
	const double F = Out.Sz * 2 <= GteH ? 1.99998 : FMath::Min(static_cast<double>(GteH) / Out.Sz, 1.99998);
	const int32 Sx = OffsetX + FMath::FloorToInt32(static_cast<double>(FMath::Clamp<int64>(Vx, -32768, 32767)) * F);
	const int32 Sy = OffsetY + FMath::FloorToInt32(static_cast<double>(FMath::Clamp<int64>(Vy, -32768, 32767)) * F);
	Out.X = FMath::Clamp(Sx, -1024, 1023);
	Out.Y = FMath::Clamp(Sy, -1024, 1023);
	return Out;
}

int32 FJRPGBattleIntroEffect::NClip(const FScreen& A, const FScreen& B, const FScreen& C)
{
	return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
}

void FJRPGBattleIntroEffect::AddPrim(const FScreen (&P)[4], const FVector2f (&UV)[4], FJRPGIntroPrim::EMode Mode, const FVector3f& Color, int32 Ot)
{
	FJRPGIntroPrim& Prim = Prims.AddDefaulted_GetRef();
	for (int32 k = 0; k < 4; ++k)
	{
		Prim.XY[k] = FVector2f(static_cast<float>(P[k].X), static_cast<float>(P[k].Y));
		Prim.UV[k] = UV[k];
		Prim.Color[k] = Color;
	}
	Prim.Mode = Mode;
	Prim.Ot = Ot;
	Prim.Seq = SeqCounter++;
}

void FJRPGBattleIntroEffect::SortPrims()
{
	// O painter: o bucket maior desenha primeiro; no empate, o enviado depois desenha antes (como o AddPrim do PS1)
	Prims.Sort([](const FJRPGIntroPrim& A, const FJRPGIntroPrim& B)
	{
		return A.Ot != B.Ot ? A.Ot > B.Ot : A.Seq > B.Seq;
	});
}

// =================================================================================================
// O ciclo
// =================================================================================================

void FJRPGBattleIntroEffect::Begin(int32 InStyle, int32 InSub, uint32 Seed)
{
	Style = FMath::Clamp(InStyle, 0, 4);
	Sub = FMath::Max(InSub, 0);
	Rng = Seed | 1u;
	Frame = 0;
	Prims.Reset();
	Tiles.Reset();
	Particles.Reset();
	Bands.Reset();
	CurtainInter.Reset();
	CurtainDisplay.Reset();
	switch (Style)
	{
	case Shatter:
		BeginTiles();
		break;
	case Scatter:
	case SpinUp:
		BeginParticles();
		break;
	case Swirl:
		BeginSwirl();
		break;
	default:
		break;
	}
}

void FJRPGBattleIntroEffect::SetCurtainSource(const TArray<FColor>& Pixels, int32 Width, int32 Height)
{
	CurtainSource = Pixels;
	CurtainWidth = Width;
	CurtainHeight = Height;
	CurtainInter.Reset();
	CurtainDisplay.Reset();
}

void FJRPGBattleIntroEffect::Step(bool bRender)
{
	Prims.Reset();
	SeqCounter = 0;
	switch (Style)
	{
	case Shatter:
		StepTiles(bRender);
		break;
	case Scatter:
	case SpinUp:
		StepParticles(bRender);
		break;
	case Curtain:
		StepCurtain();
		break;
	case Swirl:
		StepSwirl(bRender);
		break;
	default:
		break;
	}
	if (bRender)
	{
		SortPrims();
	}
	++Frame;
}

// =================================================================================================
// Estilo 2: o tile shatter (FUN_801D081C / FUN_801D0D24 / FUN_801D0E54)
// =================================================================================================

void FJRPGBattleIntroEffect::BeginTiles()
{
	// A grade de 17 x 17 cantos: só os de dentro tremem (o contorno fica retangular)
	TArray<FIntPoint> Verts;
	Verts.SetNum(17 * 17);
	for (int32 Row = 0; Row < 17; ++Row)
	{
		for (int32 Col = 0; Col < 17; ++Col)
		{
			int32 X = -0xA00 + Col * 0x140;
			int32 Y = -0x800 + Row * 0x100;
			if (Col != 0 && Col < 16 && Row != 0 && Row < 16)
			{
				X += (2 - Rand() % 3) * 0x50;
				Y += (2 - Rand() % 3) * 0x40;
			}
			Verts[Col + Row * 17] = FIntPoint(I16(X), I16(Y));
		}
	}
	const int32 Offsets[4] = { 0, 1, 17, 18 };
	Tiles.SetNum(16 * 16);
	int32 Index = 0;
	for (int32 Row = 0; Row < 16; ++Row)
	{
		for (int32 Col = 0; Col < 16; ++Col)
		{
			FTile& T = Tiles[Index++];
			const int32 X = -0x960 + Col * 0x140;
			const int32 StoredY = -0x6E0 + Row * 0x100;
			const int32 PivotY = -0x780 + Row * 0x100;
			T.Pos[0] = X;
			T.Pos[1] = StoredY;
			T.Pos[2] = 0x880;
			for (int32 k = 0; k < 4; ++k)
			{
				const FIntPoint V = Verts[Col + Row * 17 + Offsets[k]];
				T.Cx[k] = I16(V.X - X);
				T.Cy[k] = I16(V.Y - PivotY);
				T.Su[k] = Shr4(V.X + 0xA00);
				const int32 Rv = Shr4(V.Y + 0x800);
				T.Sv[k] = Rv >= 256 ? 255 : Rv;   // a borda de baixo dá a volta no original; aqui fica em 0xFF (como o port)
			}
			const int32 H = Heading(X, PivotY);
			const int32 S = SinQ(H);
			const int32 C = CosQ(H);
			T.SpinX = S >> 6;
			T.SpinY = C >> 6;
			if (Sub == 0)
			{
				T.Vz = -0x60;
				T.Delay = Rand() % 5000;
			}
			else if (Sub == 1)
			{
				T.Vz = 0x60;
				T.Delay = Rand() % 4000;
			}
			else
			{
				T.Vx = S >> 5;
				T.Vy = C >> 5;
				T.Vz = -0x20;
				T.Delay = SqrtI(static_cast<int64>(X) * X + static_cast<int64>(PivotY) * PivotY) >> 5;
			}
			T.Delay = I16(T.Delay);
		}
	}
}

void FJRPGBattleIntroEffect::StepTiles(bool bRender)
{
	const int32 Scaled = Frame * 60;
	for (FTile& T : Tiles)
	{
		if (bRender && T.Progress < 0x1000)
		{
			FRot Rot;
			EulerRot(T.Ang[0], T.Ang[1], T.Ang[2], Rot);
			FScreen P[8];
			for (int32 k = 0; k < 8; ++k)
			{
				P[k] = Project(Rot, T.Pos, T.Cx[k & 3], T.Cy[k & 3], k < 4 ? -0x80 : 0x80);
			}
			for (const FTileFace& Face : TileFaces)
			{
				const FScreen Q[4] = { P[Face.Corners[0]], P[Face.Corners[1]], P[Face.Corners[2]], P[Face.Corners[3]] };
				// Faces de um lado só
				if (NClip(Q[0], Q[1], Q[2]) <= 0 && NClip(Q[1], Q[2], Q[3]) >= 0)
				{
					continue;
				}
				const int32 Otz = (Q[0].Sz + Q[1].Sz + Q[2].Sz + Q[3].Sz) >> 2;
				if (Otz < NearOtz)
				{
					continue;
				}
				const float G = Face.Grey / 128.0f;
				FVector2f UV[4];
				for (int32 j = 0; j < 4; ++j)
				{
					// As faces com UV do registro dividem os quatro pares guardados no tile, na ordem v0..v3
					UV[j] = Face.bShade ? FVector2f::ZeroVector : FVector2f(static_cast<float>(T.Su[j]), static_cast<float>(T.Sv[j] - 4));
				}
				AddPrim(Q, UV, Face.bShade ? FJRPGIntroPrim::EMode::AddShade : FJRPGIntroPrim::EMode::Opaque, FVector3f(G, G, G), Otz);
				if (Face.bShade)
				{
					FJRPGIntroPrim& Prim = Prims.Last();
					for (int32 j = 0; j < 4; ++j)
					{
						Prim.Color[j] = ShadeCorner(j) * G;
					}
				}
			}
		}
		// step_tile: dois portões, nesta ordem
		if (T.Progress >= 0x1000 || T.Delay >= Scaled)
		{
			continue;
		}
		T.Progress = I16(T.Progress + 0x40);
		T.Pos[0] = I16(T.Pos[0] + (T.Vx >> 4));
		T.Pos[1] = I16(T.Pos[1] + (T.Vy >> 4));
		T.Pos[2] = I16(T.Pos[2] + T.Vz);
		T.Ang[0] = I16(T.Ang[0] + T.SpinX);
		T.Vx = I16(T.Vx << 1);   // as velocidades x / y dobram a cada quadro
		T.Vy = I16(T.Vy << 1);
		T.Ang[1] = I16(T.Ang[1] + T.SpinY);
	}
}

// =================================================================================================
// Estilos 0 e 1: os campos de partículas (FUN_801CFDA0 / FUN_801D0370, o anel FUN_801D1CFC)
// =================================================================================================

void FJRPGBattleIntroEffect::BeginParticles()
{
	const FParticleStyle& S = Style == SpinUp ? StyleB : StyleA;
	Particles.Reserve(32 * 40);
	int32 RunV = 4;
	for (int32 Row = 0; Row < 32; ++Row)
	{
		const int32 Cz = -0x390 + Row * 0x40;
		for (int32 Col = 0; Col < 40; ++Col)
		{
			const int32 Cxp = -0x500 + Col * 0x40;
			const int32 H = Heading(Cxp, Cz);
			int32 Delay;
			if (S.bRadial)
			{
				const int32 D = SqrtI(static_cast<int64>(Cxp) * Cxp + static_cast<int64>(Cz) * Cz);
				Delay = (D >> 4) + Rand() % 2000;
			}
			else
			{
				Delay = (Col + Row) * 0x40;
			}
			FParticle& P = Particles.AddDefaulted_GetRef();
			P.Pos[0] = I16(S.ColX0 + Col * S.ColDX);
			P.Pos[1] = I16(Cz << S.RowShift);
			P.Pos[2] = S.ZScale;
			const int32 Sin = SinQ(H);
			const int32 Cos = CosQ(H);
			// A velocidade pela direção da célula: o estilo A divide por 0x50 (para zero), o B faz >> 6
			P.Spin[0] = I16(Style == SpinUp ? (Sin >> 6) : (Sin / 0x50));
			P.Spin[1] = I16(Style == SpinUp ? (Cos >> 6) : (Cos / 0x50));
			P.Spin[2] = S.Fall;
			P.Delay = I16(Delay);
			P.AngVel[0] = S.AngVel[0];
			P.AngVel[1] = S.AngVel[1];
			P.AngVel[2] = S.AngVel[2];
			P.Page = Col * 8;
			P.V = RunV;
		}
		RunV += 8;
	}
}

void FJRPGBattleIntroEffect::StepParticles(bool bRender)
{
	const FParticleStyle& T = Style == SpinUp ? StyleB : StyleA;
	const int32 Scaled = Frame * T.DelayScale;
	for (int32 i = 0; i < ParticleTickCount && i < Particles.Num(); ++i)
	{
		FParticle& P = Particles[i];
		if (P.Tint >> 24)
		{
			continue;   // o byte alto não é 0: o tick pula a partícula inteira
		}
		// O pacote usa o estado de entrada do quadro; a integração vem depois
		const uint32 TintNow = P.Tint;
		const int32 PosL[3] = { P.Pos[0], P.Pos[1], P.Pos[2] };
		const int32 AngL[3] = { P.Ang[0], P.Ang[1], P.Ang[2] };
		const bool bMoved = P.Delay < Scaled;
		if (bMoved)
		{
			if (T.bDecay)
			{
				P.Tint = P.Tint - 0x50505u;
			}
			if (T.bSpinUp)
			{
				for (int32 k = 0; k < 3; ++k)
				{
					P.Spin[k] = I16(P.Spin[k] + (P.Spin[k] >> 3) + (P.Spin[k] >> 2));   // x 1,375 por quadro
				}
			}
			P.Pos[0] = I16(P.Pos[0] + ShrTZ(P.Spin[0], T.RotShift));
			P.Pos[1] = I16(P.Pos[1] + ShrTZ(P.Spin[1], T.RotShift));
			P.Pos[2] = I16(P.Pos[2] + ShrTZ(P.Spin[2], T.RotZShift));
			for (int32 k = 0; k < 3; ++k)
			{
				P.Ang[k] = I16(P.Ang[k] + ShrTZ(P.AngVel[k], 1));
			}
		}
		if (!bRender)
		{
			continue;
		}
		const int32 Tr[3] = { PosL[0] >> T.Prescale, PosL[1] >> T.Prescale, PosL[2] >> T.Prescale };
		FRot Rot;
		EulerRot(AngL[0], AngL[1], AngL[2], Rot);
		const int32 Q = T.Quad;
		const FScreen Pc[4] = { Project(Rot, Tr, 0, 0, 0), Project(Rot, Tr, Q, 0, 0), Project(Rot, Tr, 0, Q, 0), Project(Rot, Tr, Q, Q, 0) };
		const int32 Depth = Pc[3].Sz >> 2;
		const bool bDepthOk = T.bBounded ? static_cast<uint32>(Depth - 0x81) < 0x3F5Bu : Depth > 0x80;
		if (!bDepthOk || !(Pc[0].X > -8 && Pc[0].X < 0x148 && Pc[0].Y > -8 && Pc[0].Y < 0xF8))
		{
			continue;
		}
		const float U = static_cast<float>(P.Page);
		const float V = static_cast<float>(P.V - 4);   // tira o +4 da página da captura
		const FVector2f UV[4] = { FVector2f(U, V), FVector2f(U + 8, V), FVector2f(U, V + 8), FVector2f(U + 8, V + 8) };
		const FVector3f Color((TintNow & 0xFF) / 128.0f, ((TintNow >> 8) & 0xFF) / 128.0f, ((TintNow >> 16) & 0xFF) / 128.0f);
		AddPrim(Pc, UV, bMoved ? FJRPGIntroPrim::EMode::AddTextured : FJRPGIntroPrim::EMode::Opaque, Color, bMoved && T.bNearer ? 396 : 400);
	}

	// O anel da onda de choque do spin-up (FUN_801D1CFC): 96 segmentos, a fase = relógio x 0xA0, só nos 25 primeiros quadros
	if (T.bSpinUp && bRender)
	{
		const int32 Phase = Frame * 0xA0;
		if (Phase >= 1 && Phase <= 0x1000)
		{
			const int32 Seg = 0x60;
			const int32 StepA = 0x1000 / Seg;
			const int32 Half = StepA >> 1;
			FRot Identity = { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 };
			const int32 Tr[3] = { 0, 0, 0x800 };
			auto Vert = [&](int32 k, int32 R, int32 Z)
			{
				const int32 A = ((k % Seg) * StepA - Half - 0x400) & 0xFFF;
				const int32 X = static_cast<int32>((static_cast<int64>((CosQ(A) * 0xE00) >> 12) * R) >> 12);
				const int32 Y = static_cast<int32>((static_cast<int64>((SinQ(A) * 0xE00) >> 12) * R) >> 12);
				return Project(Identity, Tr, X, Y, Z);
			};
			const float Level = ((0x30 * (0x1000 - Phase)) >> 12) / 255.0f;
			const FVector2f NoUV[4] = { FVector2f::ZeroVector, FVector2f::ZeroVector, FVector2f::ZeroVector, FVector2f::ZeroVector };
			for (int32 k = 0; k < Seg; ++k)
			{
				const FScreen Pc[4] = { Vert(k, Phase, 0), Vert(k, Phase + 2, 0xC8), Vert(k + 1, Phase, 0), Vert(k + 1, Phase + 2, 0xC8) };
				const int32 Otz = (Pc[0].Sz + Pc[1].Sz + Pc[2].Sz + Pc[3].Sz) >> 2;
				if (Otz < NearOtz)
				{
					continue;
				}
				AddPrim(Pc, NoUV, FJRPGIntroPrim::EMode::Half, FVector3f(Level, Level, Level), Otz);
			}
		}
	}
}

// =================================================================================================
// Estilo 3: a cortina (FUN_801D11D0): duas passadas em CPU, como o port, na escala de trabalho
// =================================================================================================

void FJRPGBattleIntroEffect::StepCurtain()
{
	const int32 W = CurtainWidth;
	const int32 H = CurtainHeight;
	if (W <= 0 || H <= 0 || CurtainSource.Num() != W * H)
	{
		return;
	}
	const int32 Scale = FMath::Max(W / ScreenW, 1);
	const int32 CenterX = W / 2;
	const int32 CenterY = H / 2;
	const int32 Clock = Frame;
	auto Decay = [](TArray<FColor>& Image, int32 Step)
	{
		for (FColor& C : Image)
		{
			C.R = static_cast<uint8>(FMath::Max(0, C.R - Step));
			C.G = static_cast<uint8>(FMath::Max(0, C.G - Step));
			C.B = static_cast<uint8>(FMath::Max(0, C.B - Step));
		}
	};
	auto Warp = [Clock](int32 Offset) { return static_cast<int32>(static_cast<int64>(Offset) * (Clock + 28) / 28); };

	// 1) As colunas no intermediário (ele não é limpo: cai 0x80 por canal a cada quadro)
	if (CurtainInter.Num() != W * H)
	{
		CurtainInter.Init(FColor(0, 0, 0, 255), W * H);
	}
	else
	{
		Decay(CurtainInter, 128);
	}
	for (int32 Col = 0; Col < W; ++Col)
	{
		const int32 Off = Col - CenterX;
		const int32 Warped = Warp(Off);
		const int32 X = Warped + CenterX;
		if (X < 0 || X >= W)
		{
			continue;   // fora da tela
		}
		// O estiramento é medido em pixels do PS1 (o deslocamento / escala); o levantamento volta à escala de trabalho
		const int32 Stretch = static_cast<int32>(static_cast<int64>(FMath::Abs(Off)) * Clock / Scale) >> 5;
		const int32 Lift = ShrTZ(Stretch * 0x78, 12) * Scale;
		const int32 Y0 = -Lift;
		const int32 Height = FMath::Max(1, static_cast<int32>(static_cast<int64>(H) * (Stretch + 0x1000) / 0x1000));
		const int32 From = FMath::Max(0, Y0);
		const int32 To = FMath::Min(H, Y0 + Height);
		for (int32 Dy = From; Dy < To; ++Dy)
		{
			const int32 V = FMath::Min(H - 1, static_cast<int32>(static_cast<int64>(Dy - Y0) * H / Height));
			CurtainInter[Dy * W + X] = CurtainSource[V * W + Col];
		}
	}

	// 2) As linhas na tela (a tela também não é limpa: cai 8 por canal a cada quadro, e os vãos mostram o rastro)
	if (CurtainDisplay.Num() != W * H)
	{
		CurtainDisplay = CurtainSource;
	}
	Decay(CurtainDisplay, 8);
	for (int32 Row = 0; Row < H; ++Row)
	{
		const int32 Y = Warp(Row - CenterY) + CenterY;
		if (Y < 0 || Y >= H)
		{
			continue;
		}
		FMemory::Memcpy(&CurtainDisplay[Y * W], &CurtainInter[Row * W], sizeof(FColor) * W);
	}
}

// =================================================================================================
// Estilo 4: o swirl (FUN_801D1564 / FUN_801D1888 / FUN_801D1A20)
// =================================================================================================

void FJRPGBattleIntroEffect::BeginSwirl()
{
	Bands.SetNum(SwirlBands);
	for (int32 b = 0; b < SwirlBands; ++b)
	{
		// As bandas pares nascem negativas: se afastam do jogador
		const int32 Signed = (b % 2 == 0) ? -(6 - b) : 6 - b;
		Bands[b].Angle = 0xA00;
		Bands[b].Rate = Signed * 0x1400 + 0xA00;
	}
	SwirlV.Init(FIntVector::ZeroValue, SwirlBands * SwirlVertsPerBand);
	SwirlUV.Init(FIntPoint::ZeroValue, SwirlBands * SwirlVertsPerBand);
	for (int32 b = 0; b < SwirlBands; ++b)
	{
		const int32 Inner = 4 + b * 0x10;
		const int32 Outer = 0x14 + b * 0x10;
		const int32 Base = b * SwirlVertsPerBand;
		for (int32 c = 0; c < SwirlCols; ++c)
		{
			const int32 E = c * 0x40;
			const int32 Tx = SinQ(E);
			const int32 Ty = CosQ(E);
			const int32 X0 = FMath::Clamp((Tx * Inner) >> 8, -0xA00, 0xA00);
			const int32 Y0 = FMath::Clamp((Ty * Inner) >> 8, -0x760, 0x760);
			const int32 X1 = FMath::Clamp((Tx * Outer) >> 8, -0xA00, 0xA00);
			const int32 Y1 = FMath::Clamp((Ty * Outer) >> 8, -0x760, 0x760);
			const int32 U0 = ((X0 >> 4) + 0x20) & 0xFF;
			const int32 V0 = ((Y0 >> 4) + 0x76) & 0xFF;
			const int32 U1 = ((X1 >> 4) + 0x20) & 0xFF;
			const int32 V1 = ((Y1 >> 4) + 0x76) & 0xFF;
			const int32 P = Base + c * 3;
			SwirlV[P] = FIntVector(X0, Y0, 0);
			SwirlV[P + 1] = FIntVector(X1, Y1, 0);
			SwirlV[P + 2] = FIntVector(X0, Y0, 0x1000);
			SwirlUV[P] = FIntPoint(U0, V0);
			SwirlUV[P + 1] = FIntPoint(U1, V1);
			SwirlUV[P + 2] = FIntPoint(U1, V1);   // o 3o vértice usa o texel do raio de fora, como no disco
			const int32 M = Base + SwirlVertsPerHalf + c * 3;
			SwirlV[M] = FIntVector(-X0, Y0, 0);
			SwirlV[M + 1] = FIntVector(-X1, Y1, 0);
			SwirlV[M + 2] = FIntVector(-X0, Y0, 0x1000);
			const int32 Mu0 = (-0x61 - (X0 >> 4)) & 0xFF;
			const int32 Mu1 = (-0x61 - (X1 >> 4)) & 0xFF;
			SwirlUV[M] = FIntPoint(Mu0, V0);
			SwirlUV[M + 1] = FIntPoint(Mu1, V1);
			SwirlUV[M + 2] = FIntPoint(Mu1, V1);
		}
	}
}

void FJRPGBattleIntroEffect::StepSwirl(bool bRender)
{
	const int32 Clock = Frame;
	const bool bLate = Clock >= 0x5A;
	const int32 A = I16((Clock - 0x3C) * 4 * 16);
	FRot Rot = { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 };
	if (bLate)
	{
		EulerRot(A, 0, A, Rot);   // do quadro 0x5A em diante a malha também rola
	}
	for (int32 b = 0; b < SwirlBands; ++b)
	{
		FBand& Band = Bands[b];
		if (bRender && Band.Angle > 0x80 && b < 12)
		{
			const int32 Tr[3] = { 0, 0, Band.Angle };
			for (int32 HalfIndex = 0; HalfIndex < 2; ++HalfIndex)
			{
				const int32 First = b * SwirlVertsPerBand + HalfIndex * SwirlVertsPerHalf;
				const int32 XOff = HalfIndex ? 0 : 128;   // a página 0x117 = a metade direita da captura, 0x115 = a esquerda
				for (int32 Seg = 0; Seg < SwirlCols - 1; ++Seg)
				{
					const int32 P = First + Seg * 3;
					const int32 Quads[2][4] = { { P, P + 1, P + 3, P + 4 }, { P + 2, P, P + 5, P + 3 } };
					const float Greys[2] = { 1.0f, 0.5f };
					for (int32 q = 0; q < 2; ++q)
					{
						FScreen Pc[4];
						FVector2f UV[4];
						for (int32 k = 0; k < 4; ++k)
						{
							const FIntVector& V = SwirlV[Quads[q][k]];
							Pc[k] = Project(Rot, Tr, V.X, V.Y, V.Z);
							const FIntPoint& T = SwirlUV[Quads[q][k]];
							UV[k] = FVector2f(static_cast<float>(T.X + XOff), static_cast<float>(T.Y - 4));
						}
						const int32 Otz = (Pc[0].Sz + Pc[1].Sz + Pc[2].Sz + Pc[3].Sz) >> 2;
						if (Otz < NearOtz)
						{
							continue;
						}
						AddPrim(Pc, UV, FJRPGIntroPrim::EMode::Opaque, FVector3f(Greys[q], Greys[q], Greys[q]), Otz);
					}
				}
			}
		}
		// angle += (taxa x passo) >> 8, truncando para zero
		const int32 D = Band.Rate;
		Band.Angle += (D < 0 ? D + 0xFF : D) >> 8;
	}
}
