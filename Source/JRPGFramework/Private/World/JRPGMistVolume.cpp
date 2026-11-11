#include "World/JRPGMistVolume.h"
#include "World/JRPGMistSubsystem.h"
#include "World/JRPGMistBlocker.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

// Parâmetros do M_JRPGMist. Os escalares vão empacotados em vetores para o Custom node
// ter menos entradas — a ordem dos componentes tem que bater com a receita em
// Docs/Guides/MIST.md.
namespace JRPGMistParams
{
	static const FName Albedo(TEXT("Albedo"));
	static const FName RibbonGlow(TEXT("RibbonGlow"));
	static const FName ShapeA(TEXT("ShapeA"));        // FloorZ, Density, Thickness, NoiseScale
	static const FName ShapeB(TEXT("ShapeB"));        // WindX, WindY, WarpStrength, WarpSpin
	static const FName Ribbons(TEXT("Ribbons"));      // Sharpness, Amount, Stretch, Coverage
	static const FName Scene(TEXT("Scene"));          // PoolDistance, PoolAmount, FlowAround, EdgeFade
	static const FName Interaction(TEXT("Interaction")); // ClearStrength, SwirlStrength, -, -
	static const FName BoxXY(TEXT("BoxXY"));          // MinX, MinY, MaxX, MaxY
	static const FName BoxZ(TEXT("BoxZ"));            // TopZ, TopFade, AirCloudSize, AirStart + AirCoverage
	static const FName SelfLight(TEXT("SelfLight"));  // cor × intensidade; emissive = isto × densidade
	static const FName Flow(TEXT("Flow"));            // Mode, DirX, DirY, FlowSpeed
	static const FName FlowLines(TEXT("FlowLines"));  // Spacing, Width, Coverage, Curve
	static const FName FlowDash(TEXT("FlowDash"));    // TrailLength, TrailFill, AvoidDistance, AvoidStrength
	static const FName FlowEddy(TEXT("FlowEddy"));    // EddyStrength, EddySize, LineDensity, Layer
	static const FName TrailTex(TEXT("TrailTex"));    // textura do rastro (UJRPGMistSubsystem)
	static const FName BlockTex(TEXT("BlockTex"));    // áreas sem névoa (AJRPGMistBlocker), cobre BoxXY
	static const FName Radial(TEXT("Radial"));        // Ligado, CentroX, CentroY, Swirl
	static const FName Extra(TEXT("Extra"));          // OriginFalloff, OriginFalloffMin, -, -
}

AJRPGMistVolume::AJRPGMistVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	// Pivô no centro do chão; a escala do ator é o tamanho (escala 1 = 100 uu). Nada aqui
	// reescreve a escala depois — o gizmo do editor manda
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetRelativeScale3D(FVector(40.0f, 40.0f, 4.0f));
	RootComponent = Root;

	MistBox = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MistBox"));
	MistBox->SetupAttachment(Root);
	// Cubo da engine: 100 uu de lado com pivô no centro — sobe meio cubo para a base ficar no pivô
	MistBox->SetRelativeLocation(FVector(0.0f, 0.0f, 50.0f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		MistBox->SetStaticMesh(CubeMesh.Object);
	}

	// A caixa só existe para a volumetric fog: sem colisão, sem sombra, e principalmente
	// FORA do distance field — senão a névoa enxergaria a própria caixa como parede
	MistBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MistBox->SetCanEverAffectNavigation(false);
	MistBox->SetCastShadow(false);
	MistBox->bAffectDistanceFieldLighting = false;
	MistBox->bAffectDynamicIndirectLighting = false;
	MistBox->bVisibleInRayTracing = false;
	MistBox->bReceivesDecals = false;
	MistBox->SetGenerateOverlapEvents(false);

#if WITH_EDITORONLY_DATA
	EditorOutline = CreateEditorOnlyDefaultSubobject<UBoxComponent>(TEXT("EditorOutline"));
	if (EditorOutline)
	{
		EditorOutline->SetupAttachment(Root);
		EditorOutline->SetRelativeLocation(FVector(0.0f, 0.0f, 50.0f));
		EditorOutline->SetBoxExtent(FVector(50.0f, 50.0f, 50.0f), false);
		EditorOutline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		EditorOutline->SetGenerateOverlapEvents(false);
		EditorOutline->SetCanEverAffectNavigation(false);
		EditorOutline->ShapeColor = FColor(120, 160, 255);
		EditorOutline->SetHiddenInGame(true);
		EditorOutline->bIsEditorOnly = true;
	}

	EditorFlowArrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("EditorFlowArrow"));
	if (EditorFlowArrow)
	{
		EditorFlowArrow->SetupAttachment(Root);
		// Escala absoluta: a escala do ator é o tamanho da caixa, não da seta
		EditorFlowArrow->SetUsingAbsoluteScale(true);
		EditorFlowArrow->SetRelativeLocation(FVector(0.0f, 0.0f, 50.0f));
		EditorFlowArrow->ArrowSize = 4.0f;
		EditorFlowArrow->ArrowColor = FColor(120, 160, 255);
		EditorFlowArrow->SetHiddenInGame(true);
		EditorFlowArrow->bIsEditorOnly = true;
	}
#endif

	// Soft: o CDO não falha se os assets ainda não foram criados no editor
	MistMaterial = TSoftObjectPtr<UMaterialInterface>(
		FSoftObjectPath(TEXT("/JRPGFramework/World/Mist/MI_JRPGMist_Default.MI_JRPGMist_Default")));
	ParameterCollection = TSoftObjectPtr<UMaterialParameterCollection>(
		FSoftObjectPath(TEXT("/JRPGFramework/World/Mist/MPC_JRPGMist.MPC_JRPGMist")));
}

void AJRPGMistVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	UpdateBoxPlacement();
	if (EnsureMistMID())
	{
		ApplyMistParameters();
		// A caixa pode ter mudado de lugar ou de tamanho: as áreas sem névoa são refeitas
		RefreshBlockers();
	}
}

void AJRPGMistVolume::UpdateBoxPlacement()
{
	// O cubo da engine tem 100 uu com pivô no centro: +50 põe a base no pivô. Na chaminé o pivô
	// é a boca e a caixa desce DripLength abaixo dela (a parte que escorre). A posição relativa
	// é em unidades antes da escala do ator, por isso divide pela escala Z
	const float Below = Mode == EJRPGMistMode::ChimneySmoke ? FMath::Max(DripLength, 0.0f) : 0.0f;
	const float ScaleZ = FMath::Max(FMath::Abs(static_cast<float>(GetActorScale3D().Z)), KINDA_SMALL_NUMBER);
	const FVector Relative(0.0f, 0.0f, 50.0f - Below / ScaleZ);
	MistBox->SetRelativeLocation(Relative);
#if WITH_EDITORONLY_DATA
	if (EditorOutline)
	{
		EditorOutline->SetRelativeLocation(Relative);
	}
#endif
}

bool AJRPGMistVolume::EnsureMistMID()
{
	UMaterialInterface* Parent = MistMaterial.LoadSynchronous();
	if (!Parent)
	{
		UE_LOG(LogTemp, Warning, TEXT("JRPGMistVolume '%s': MistMaterial não encontrado (%s) — a névoa não vai aparecer."),
			*GetName(), *MistMaterial.ToString());
		MistBox->SetMaterial(0, nullptr);
		MistMID = nullptr;
		return false;
	}

	// MistMID é Transient: na cópia do level para o PIE ele chega nulo, mas o MID continua no
	// MistBox. Recupera de lá; sem isto o SetTrailTexture não tinha onde aplicar a textura
	if (!MistMID)
	{
		MistMID = Cast<UMaterialInstanceDynamic>(MistBox->GetMaterial(0));
	}
	if (!MistMID || MistMID->Parent != Parent)
	{
		MistMID = UMaterialInstanceDynamic::Create(Parent, this);
		MistBox->SetMaterial(0, MistMID);
	}
	return true;
}

void AJRPGMistVolume::ApplyMistParameters()
{
	if (!MistMID)
	{
		return;
	}

	// O pivô é o chão da névoa; as bordas XY esmaecem pela caixa alinhada aos eixos que
	// envolve o cubo (com yaw, as bordas seguem essa caixa, não o cubo girado)
	const float FloorZ = GetActorLocation().Z;
	const FBox Bounds = MistBox->CalcBounds(MistBox->GetComponentTransform()).GetBox();

	MistMID->SetVectorParameterValue(JRPGMistParams::Albedo, Albedo);
	MistMID->SetVectorParameterValue(JRPGMistParams::RibbonGlow, RibbonGlow);
	MistMID->SetVectorParameterValue(JRPGMistParams::ShapeA, FLinearColor(FloorZ, Density, Thickness, NoiseScale));
	// Ground + Flow e Air: o vento da névoa (de chão ou das nuvens) é a direção do fluxo — os
	// dois vão para o mesmo lado; a névoa anda a GroundDriftRatio da velocidade das linhas
	FVector2D GroundWind = Wind;
	if (Mode == EJRPGMistMode::GroundAndFlow || Mode == EJRPGMistMode::AirMist)
	{
		GroundWind = GetFlowDirection() * FlowSpeed * GroundDriftRatio;
	}
	MistMID->SetVectorParameterValue(JRPGMistParams::ShapeB, FLinearColor(GroundWind.X, GroundWind.Y, WarpStrength, WarpSpin));
	MistMID->SetVectorParameterValue(JRPGMistParams::Ribbons, FLinearColor(RibbonSharpness, RibbonAmount, RibbonStretch, RibbonCoverage));
	MistMID->SetVectorParameterValue(JRPGMistParams::Scene, FLinearColor(PoolDistance, PoolAmount, FlowAround, EdgeFade));
	// Interaction.zw = curva das Flow Lines (os outros modos ignoram)
	MistMID->SetVectorParameterValue(JRPGMistParams::Interaction, FLinearColor(ClearStrength, SwirlStrength, CurveLength, CurveDrift));
	MistMID->SetVectorParameterValue(JRPGMistParams::BoxXY, FLinearColor(
		Bounds.Min.X, Bounds.Min.Y, Bounds.Max.X, Bounds.Max.Y));
	// Air: os parâmetros das nuvens vão nos campos que sobravam (sem mexer no material).
	// BoxZ.w = AirStart inteiro + AirCoverage na parte fracionária. Ver JRPGMistFromPacked4
	const bool bAir = Mode == EJRPGMistMode::AirMist;
	const float AirStartPacked = FMath::FloorToFloat(FMath::Clamp(AirStart, 0.0f, 100000.0f))
		+ FMath::Clamp(AirCoverage, 0.0f, 1.0f) * 0.99f;
	MistMID->SetVectorParameterValue(JRPGMistParams::BoxZ, FLinearColor(
		Bounds.Max.Z, TopFade, bAir ? FMath::Max(AirCloudSize, 50.0f) : 0.0f, bAir ? AirStartPacked : 0.0f));

	// Flow / FlowLines / FlowDash mudam de significado com o modo (ver JRPGMistFromPacked2 e
	// JRPGMistEvaluateAll)
	if (Mode == EJRPGMistMode::ChimneySmoke)
	{
		// ShapeB = direção do vento (para onde a fumaça inclina), Bend, Turbulence
		const FVector Mouth = GetActorLocation();
		const FVector2D Lean = GetFlowDirection();
		MistMID->SetVectorParameterValue(JRPGMistParams::ShapeB, FLinearColor(Lean.X, Lean.Y, PlumeBend, PlumeTurbulence));
		MistMID->SetVectorParameterValue(JRPGMistParams::Flow, FLinearColor(5.0f, Mouth.X, Mouth.Y, RiseSpeed));
		MistMID->SetVectorParameterValue(JRPGMistParams::FlowLines, FLinearColor(PlumeRadius, PlumeSpread, PuffSize, 0.0f));
		MistMID->SetVectorParameterValue(JRPGMistParams::FlowDash, FLinearColor(DripDensity, DripLength, DripSpeed, DripHug));
	}
	else if (Mode == EJRPGMistMode::PulseRings)
	{
		const FVector Center = GetActorLocation();
		MistMID->SetVectorParameterValue(JRPGMistParams::Flow, FLinearColor(2.0f, Center.X, Center.Y, PulseSpeed));
		MistMID->SetVectorParameterValue(JRPGMistParams::FlowLines, FLinearColor(PulseInterval, RingWidth, PulseMaxRadius, RingWiggle));
		MistMID->SetVectorParameterValue(JRPGMistParams::FlowDash, FLinearColor(float(RadialLines), RadialAmount, RadialSharpness, RadialSpin));
	}
	else
	{
		const FVector2D FlowDir = GetFlowDirection();
		const float ModeValue = Mode == EJRPGMistMode::FlowLines ? 1.0f
			: Mode == EJRPGMistMode::GroundAndFlow ? 3.0f
			: bAir ? 4.0f : 0.0f;
		MistMID->SetVectorParameterValue(JRPGMistParams::Flow, FLinearColor(ModeValue, FlowDir.X, FlowDir.Y, FlowSpeed));
		MistMID->SetVectorParameterValue(JRPGMistParams::FlowLines, FLinearColor(LineSpacing, LineWidth, LineCoverage, LineCurve));
		MistMID->SetVectorParameterValue(JRPGMistParams::FlowDash, FLinearColor(TrailLength, TrailFill, AvoidDistance, AvoidStrength));
	}

	CurrentSelfLight = ComputeSelfLight();
	MistMID->SetVectorParameterValue(JRPGMistParams::SelfLight, SelfLightColor * CurrentSelfLight);

	// Camada (todos os modos): TopSoftness + 2 com Follow Ground — ver JRPGMistLayer no shader
	const float LayerPacked = FMath::Clamp(TopSoftness, 0.05f, 1.0f) + (bFollowGround ? 2.0f : 0.0f);
	MistMID->SetVectorParameterValue(JRPGMistParams::FlowEddy, FLinearColor(EddyStrength, EddySize, LineDensity, LayerPacked));

	if (TrailTexture)
	{
		MistMID->SetTextureParameterValue(JRPGMistParams::TrailTex, TrailTexture);
	}
	if (BlockTexture)
	{
		MistMID->SetTextureParameterValue(JRPGMistParams::BlockTex, BlockTexture);
	}

	// Origem: a névoa inteira (chão, ar, linhas) sai do ponto. As velocidades continuam as do
	// volume — a da névoa de chão / das nuvens já está em ShapeB.xy (o shader usa o comprimento)
	const bool bSpread = IsSpreadingFromOrigin();
	const FVector Origin = GetSpreadOrigin();
	MistMID->SetVectorParameterValue(JRPGMistParams::Radial, bSpread
		? FLinearColor(1.0f, Origin.X, Origin.Y, SpreadSwirl) : FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	MistMID->SetVectorParameterValue(JRPGMistParams::Extra, FLinearColor(
		bSpread ? OriginFalloff : 0.0f, OriginFalloffMin, 0.0f, 0.0f));
}

void AJRPGMistVolume::SetTrailTexture(UTexture* Texture)
{
	TrailTexture = Texture;
	if (MistMID && TrailTexture)
	{
		MistMID->SetTextureParameterValue(JRPGMistParams::TrailTex, TrailTexture);
	}
}

FBox2D AJRPGMistVolume::GetMistBoundsXY() const
{
	// Igual ao BoxXY de ApplyMistParameters: a caixa alinhada aos eixos que envolve o cubo
	const FBox Bounds = MistBox->CalcBounds(MistBox->GetComponentTransform()).GetBox();
	return FBox2D(FVector2D(Bounds.Min.X, Bounds.Min.Y), FVector2D(Bounds.Max.X, Bounds.Max.Y));
}

void AJRPGMistVolume::HandleBlockerChanged(const AJRPGMistBlocker* Blocker, bool bShapeChanged, bool bRemoving)
{
	const bool bUses = BlockLayers.ContainsByPredicate([Blocker](const FBlockLayer& Layer) { return Layer.Blocker.Get() == Blocker; });
	if (bShapeChanged || bRemoving)
	{
		// Refaz se ele encosta agora ou encostava antes (saiu de cima da caixa)
		if (bUses || (!bRemoving && Blocker && GetMistBoundsXY().Intersect(Blocker->GetAreaBounds())))
		{
			RefreshBlockers(bRemoving ? Blocker : nullptr);
		}
		return;
	}
	if (bUses)
	{
		UpdateBlockMask();
	}
}

void AJRPGMistVolume::RefreshBlockers(const AJRPGMistBlocker* Ignore)
{
	UWorld* World = GetWorld();
	if (!World || !MistBox || !EnsureMistMID())
	{
		return;
	}

	BlockLayers.Reset();
	const FBox2D Bounds = GetMistBoundsXY();
	const FVector2D Size = Bounds.GetSize();
	if (Size.X < 1.0 || Size.Y < 1.0)
	{
		return;
	}
	const int32 ResX = FMath::Clamp(FMath::CeilToInt(Size.X / BlockTexelSize), 4, 256);
	const int32 ResY = FMath::Clamp(FMath::CeilToInt(Size.Y / BlockTexelSize), 4, 256);

	for (TActorIterator<AJRPGMistBlocker> It(World); It; ++It)
	{
		const AJRPGMistBlocker* Blocker = *It;
		if (Blocker == Ignore || !IsValid(Blocker))
		{
			continue;
		}
		const FBox2D Area = Blocker->GetAreaBounds();
		if (!Area.bIsValid || !Area.Intersect(Bounds))
		{
			continue;
		}
		// Distância de cada texel (no centro dele) até a borda; fora da caixa do blocker nem calcula
		FBlockLayer& Layer = BlockLayers.AddDefaulted_GetRef();
		Layer.Blocker = Blocker;
		Layer.Dist.SetNumUninitialized(ResX * ResY);
		for (int32 y = 0; y < ResY; ++y)
		{
			for (int32 x = 0; x < ResX; ++x)
			{
				const FVector2D P = Bounds.Min + FVector2D((x + 0.5) / ResX * Size.X, (y + 0.5) / ResY * Size.Y);
				Layer.Dist[y * ResX + x] = Area.IsInside(P) ? Blocker->GetSignedDistance(P) : -1.0f;
			}
		}
	}

	// Sem blocker: textura mínima zerada. Não dá para só deixar de ligar — o MID copiado do editor
	// para o PIE ainda aponta para a textura do preview
	const bool bAny = BlockLayers.Num() > 0;
	EnsureBlockTexture(bAny ? ResX : 4, bAny ? ResY : 4);
	UpdateBlockMask(true);
}

void AJRPGMistVolume::EnsureBlockTexture(int32 SizeX, int32 SizeY)
{
	if (!BlockTexture || BlockResX != SizeX || BlockResY != SizeY)
	{
		// G8 linear: o shader lê .r direto. Transient — não vira asset. Nome único: um volume por
		// textura, e NewObject com um nome que já existe no pacote transient reaproveitaria a de outro
		const FName Name = MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), FName(TEXT("JRPGMistBlock")));
		UTexture2D* NewTexture = UTexture2D::CreateTransient(SizeX, SizeY, PF_G8, Name);
		if (!NewTexture)
		{
			UE_LOG(LogTemp, Warning, TEXT("JRPGMistVolume '%s': não foi possível criar a textura das áreas sem névoa."), *GetName());
			return;
		}
		NewTexture->SRGB = false;
		NewTexture->Filter = TF_Bilinear;
		NewTexture->AddressX = TA_Clamp;
		NewTexture->AddressY = TA_Clamp;
		NewTexture->UpdateResource();
		BlockTexture = NewTexture;
		BlockResX = SizeX;
		BlockResY = SizeY;
		BlockPixels.Reset();
	}
	if (MistMID)
	{
		MistMID->SetTextureParameterValue(JRPGMistParams::BlockTex, BlockTexture);
	}
}

void AJRPGMistVolume::UpdateBlockMask(bool bForceUpload)
{
	if (!BlockTexture || !BlockTexture->GetResource() || BlockResX <= 0 || BlockResY <= 0)
	{
		return;
	}

	// Cada texel fica com o blocker que mais tira névoa ali
	const int32 Count = BlockResX * BlockResY;
	TArray<uint8> NewPixels;
	NewPixels.SetNumZeroed(Count);
	for (const FBlockLayer& Layer : BlockLayers)
	{
		const AJRPGMistBlocker* Blocker = Layer.Blocker.Get();
		if (!Blocker || !Blocker->IsAffectingMist() || Layer.Dist.Num() != Count)
		{
			continue;
		}
		for (int32 i = 0; i < Count; ++i)
		{
			if (Layer.Dist[i] > 0.0f)
			{
				const uint8 Value = static_cast<uint8>(FMath::RoundToInt(Blocker->GetBlockAmount(Layer.Dist[i]) * 255.0f));
				NewPixels[i] = FMath::Max(NewPixels[i], Value);
			}
		}
	}
	if (!bForceUpload && NewPixels == BlockPixels)
	{
		return;
	}
	BlockPixels = MoveTemp(NewPixels);

	uint8* Data = new uint8[Count];
	FMemory::Memcpy(Data, BlockPixels.GetData(), Count);
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, BlockResX, BlockResY);
	BlockTexture->UpdateTextureRegions(0, 1, Region, BlockResX, 1, Data,
		[](uint8* SrcData, const FUpdateTextureRegion2D* Regions)
		{
			delete[] SrcData;
			delete Regions;
		});
}

void AJRPGMistVolume::ApplyModePreset(EJRPGMistMode NewMode)
{
	Mode = NewMode;

	// Presets afinados olhando o resultado no editor (World Map / TestMap). A densidade é por
	// metro; os modos de linha já multiplicam por 3 no shader. As bordas voltam ao padrão (a
	// chaminé usa bordas mais curtas)
	EdgeFade = 400.0f;
	TopFade = 150.0f;
	switch (NewMode)
	{
	case EJRPGMistMode::GroundAndFlow:
		Density = 1.0f;
		Thickness = 220.0f;
		TopSoftness = 0.7f;
		NoiseScale = 2400.0f;
		WarpStrength = 0.5f;
		WarpSpin = 0.05f;
		RibbonSharpness = 10.0f;
		RibbonAmount = 0.7f;
		RibbonStretch = 3.5f;
		RibbonCoverage = 0.55f;
		FlowSpeed = 300.0f;
		LineSpacing = 260.0f;
		LineWidth = 55.0f;
		LineCoverage = 0.45f;
		LineCurve = 300.0f;
		CurveLength = 1600.0f;
		CurveDrift = 0.15f;
		EddyStrength = 180.0f;
		EddySize = 5000.0f;
		LineDensity = 0.8f;
		GroundDriftRatio = 0.3f;
		TrailLength = 1800.0f;
		TrailFill = 0.75f;
		AvoidDistance = 250.0f;
		AvoidStrength = 1.0f;
		break;

	case EJRPGMistMode::AirMist:
		// Leve: a câmera olha através de toda a altura da caixa. As linhas ganham peso porque
		// são finas perto das nuvens
		Density = 0.35f;
		NoiseScale = 2400.0f;
		WarpStrength = 0.5f;
		WarpSpin = 0.05f;
		RibbonSharpness = 10.0f;
		RibbonAmount = 0.7f;
		RibbonStretch = 3.5f;
		RibbonCoverage = 0.55f;
		AirCloudSize = 350.0f;
		AirCoverage = 0.45f;
		AirStart = 150.0f;
		FlowSpeed = 300.0f;
		LineSpacing = 260.0f;
		LineWidth = 55.0f;
		LineCoverage = 0.45f;
		LineCurve = 300.0f;
		CurveLength = 1600.0f;
		CurveDrift = 0.15f;
		EddyStrength = 180.0f;
		EddySize = 5000.0f;
		LineDensity = 2.0f;
		GroundDriftRatio = 0.3f;
		TrailLength = 1800.0f;
		TrailFill = 0.75f;
		AvoidDistance = 250.0f;
		AvoidStrength = 1.0f;
		break;

	case EJRPGMistMode::GroundMist:
		Density = 1.0f;
		Thickness = 200.0f;
		TopSoftness = 0.7f;
		NoiseScale = 2400.0f;
		Wind = FVector2D(25.0f, 10.0f);
		WarpStrength = 0.5f;
		WarpSpin = 0.05f;
		RibbonSharpness = 10.0f;
		RibbonAmount = 0.7f;
		RibbonStretch = 3.5f;
		RibbonCoverage = 0.55f;
		break;

	case EJRPGMistMode::FlowLines:
		Density = 2.0f;
		Thickness = 180.0f;
		TopSoftness = 0.6f;
		FlowSpeed = 300.0f;
		LineSpacing = 260.0f;
		LineWidth = 55.0f;
		LineCoverage = 0.5f;
		LineCurve = 300.0f;
		EddyStrength = 180.0f;
		EddySize = 5000.0f;
		LineDensity = 1.0f;
		CurveLength = 1600.0f;
		CurveDrift = 0.15f;
		TrailLength = 1800.0f;
		TrailFill = 0.75f;
		AvoidDistance = 250.0f;
		AvoidStrength = 1.0f;
		break;

	case EJRPGMistMode::ChimneySmoke:
		// Uma caixa por chaminé: 1200 x 1200 uu, 1200 de altura (900 de pluma + 300 escorrendo)
		Density = 3.0f;
		EdgeFade = 150.0f;
		TopFade = 300.0f;
		PlumeRadius = 100.0f;
		PlumeSpread = 0.25f;
		RiseSpeed = 60.0f;
		PuffSize = 140.0f;
		PlumeBend = 0.25f;
		PlumeTurbulence = 0.5f;
		DripDensity = 0.7f;
		DripLength = 300.0f;
		DripSpeed = 30.0f;
		DripHug = 70.0f;
		SetActorScale3D(FVector(12.0f, 12.0f, 12.0f));
		break;

	case EJRPGMistMode::PulseRings:
		Density = 2.0f;
		Thickness = 180.0f;
		TopSoftness = 0.6f;
		NoiseScale = 2400.0f;
		PulseInterval = 1.5f;
		PulseSpeed = 400.0f;
		RingWidth = 70.0f;
		PulseMaxRadius = 1800.0f;
		RingWiggle = 40.0f;
		RadialLines = 12;
		RadialAmount = 0.4f;
		RadialSharpness = 6.0f;
		RadialSpin = 0.1f;
		break;
	}

	ApplyMistParameters();
}

#if WITH_EDITOR
void AJRPGMistVolume::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Trocar o modo no dropdown carrega o preset dele antes do OnConstruction reaplicar tudo
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(AJRPGMistVolume, Mode))
	{
		ApplyModePreset(Mode);
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

bool AJRPGMistVolume::IsSpreadingFromOrigin() const
{
	return bSpreadFromOrigin && Mode != EJRPGMistMode::PulseRings && Mode != EJRPGMistMode::ChimneySmoke;
}

FVector AJRPGMistVolume::GetSpreadOrigin() const
{
	return IsValid(SpreadOrigin) ? SpreadOrigin->GetActorLocation() : GetActorLocation();
}

FVector2D AJRPGMistVolume::GetFlowDirection() const
{
	FVector Dir = GetActorForwardVector();
	if (IsValid(FlowTarget))
	{
		Dir = FlowTarget->GetActorLocation() - GetActorLocation();
	}
	const FVector2D Dir2D(Dir.X, Dir.Y);
	return Dir2D.IsNearlyZero() ? FVector2D(1.0f, 0.0f) : Dir2D.GetSafeNormal();
}

float AJRPGMistVolume::GetSceneLightLevel() const
{
	// A mais forte das directional lights visíveis deste mundo (no UDS: sol ou lua)
	const UWorld* World = GetWorld();
	float Level = 0.0f;
	for (TObjectIterator<UDirectionalLightComponent> It; It; ++It)
	{
		const UDirectionalLightComponent* Light = *It;
		if (!Light || Light->IsTemplate() || Light->GetWorld() != World || !Light->IsVisible() || !Light->bAffectsWorld)
		{
			continue;
		}
		Level = FMath::Max(Level, Light->Intensity * FLinearColor(Light->LightColor).GetLuminance());
	}
	return Level;
}

float AJRPGMistVolume::ComputeSelfLight() const
{
	float Value = SelfLight;
	if (bAutoNightBoost)
	{
		const float Daylight = FMath::Clamp(GetSceneLightLevel() / FMath::Max(DaylightLevel, 0.01f), 0.0f, 1.0f);
		Value += NightSelfLight * (1.0f - Daylight);
	}
	return Value;
}

void AJRPGMistVolume::UpdateSelfLight(bool bSnap)
{
	if (!MistMID)
	{
		return;
	}
	// Transição suave no dia dinâmico: o UDS muda a luz aos poucos, mas uma troca de clima pode pular
	const float Target = ComputeSelfLight();
	CurrentSelfLight = bSnap ? Target : FMath::FInterpTo(CurrentSelfLight, Target, 0.5f, 2.0f);
	MistMID->SetVectorParameterValue(JRPGMistParams::SelfLight, SelfLightColor * CurrentSelfLight);
}

void AJRPGMistVolume::SetSelfLight(float NewSelfLight)
{
	SelfLight = FMath::Max(NewSelfLight, 0.0f);
	UpdateSelfLight(true);
}

UMaterialParameterCollection* AJRPGMistVolume::GetParameterCollection() const
{
	return ParameterCollection.LoadSynchronous();
}

void AJRPGMistVolume::BeginPlay()
{
	Super::BeginPlay();

	// Antes de registrar: o subsistema passa a textura do rastro para o MID no registro
	if (EnsureMistMID())
	{
		ApplyMistParameters();
		// Os blockers que já começaram avisam de novo no BeginPlay deles; os que ainda não, depois
		RefreshBlockers();
	}

	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UJRPGMistSubsystem* Mist = GI->GetSubsystem<UJRPGMistSubsystem>())
		{
			Mist->RegisterVolume(this);
		}
	}

	if (bAutoNightBoost && bTrackTimeOfDay)
	{
		GetWorldTimerManager().SetTimer(SelfLightTimer, this, &AJRPGMistVolume::TrackSelfLight, 0.5f, true);
	}
	else if (bAutoNightBoost)
	{
		// Uma medição só. Espera um instante: o UDS posiciona sol e lua no BeginPlay dele, que
		// pode rodar depois deste — medir já agora pegaria a luz sem a hora aplicada
		GetWorldTimerManager().SetTimer(SelfLightTimer, this, &AJRPGMistVolume::MeasureSelfLightOnce, 0.3f, false);
	}
}

void AJRPGMistVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(SelfLightTimer);

	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UJRPGMistSubsystem* Mist = GI->GetSubsystem<UJRPGMistSubsystem>())
		{
			Mist->UnregisterVolume(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}
