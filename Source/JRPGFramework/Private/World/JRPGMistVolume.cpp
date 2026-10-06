#include "World/JRPGMistVolume.h"
#include "World/JRPGMistSubsystem.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "UObject/ConstructorHelpers.h"

// Parâmetros do M_JRPGMist. Os escalares vão empacotados em vetores para o Custom node
// ter menos entradas — a ordem dos componentes tem que bater com a receita em
// Docs/Guides/MIST.md.
namespace JRPGMistParams
{
	static const FName Albedo(TEXT("Albedo"));
	static const FName RibbonGlow(TEXT("RibbonGlow"));
	static const FName ShapeA(TEXT("ShapeA"));        // FloorZ, Density, HeightFalloff, NoiseScale
	static const FName ShapeB(TEXT("ShapeB"));        // WindX, WindY, WarpStrength, WarpSpin
	static const FName Ribbons(TEXT("Ribbons"));      // Sharpness, Amount, Stretch, Coverage
	static const FName Scene(TEXT("Scene"));          // PoolDistance, PoolAmount, FlowAround, EdgeFade
	static const FName Interaction(TEXT("Interaction")); // ClearStrength, SwirlStrength, -, -
	static const FName BoxXY(TEXT("BoxXY"));          // MinX, MinY, MaxX, MaxY
	static const FName Flow(TEXT("Flow"));            // Mode, DirX, DirY, FlowSpeed
	static const FName FlowLines(TEXT("FlowLines"));  // Spacing, Width, Coverage, Curve
	static const FName FlowDash(TEXT("FlowDash"));    // TrailLength, TrailFill, AvoidDistance, AvoidStrength
	static const FName FlowEddy(TEXT("FlowEddy"));    // EddyStrength, EddySize, LineDensity, -
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

	UMaterialInterface* Parent = MistMaterial.LoadSynchronous();
	if (!Parent)
	{
		UE_LOG(LogTemp, Warning, TEXT("JRPGMistVolume '%s': MistMaterial não encontrado (%s) — a névoa não vai aparecer."),
			*GetName(), *MistMaterial.ToString());
		MistBox->SetMaterial(0, nullptr);
		MistMID = nullptr;
		return;
	}

	if (!MistMID || MistMID->Parent != Parent)
	{
		MistMID = UMaterialInstanceDynamic::Create(Parent, this);
		MistBox->SetMaterial(0, MistMID);
	}
	ApplyMistParameters();
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
	MistMID->SetVectorParameterValue(JRPGMistParams::ShapeA, FLinearColor(FloorZ, Density, HeightFalloff, NoiseScale));
	// Ground + Flow: o vento da névoa de chão é a direção do fluxo — os dois vão para o mesmo
	// lado; a névoa anda a GroundDriftRatio da velocidade das linhas
	FVector2D GroundWind = Wind;
	if (Mode == EJRPGMistMode::GroundAndFlow)
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

	// Flow / FlowLines / FlowDash mudam de significado com o modo (ver JRPGMistFromPacked2)
	if (Mode == EJRPGMistMode::PulseRings)
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
			: Mode == EJRPGMistMode::GroundAndFlow ? 3.0f : 0.0f;
		MistMID->SetVectorParameterValue(JRPGMistParams::Flow, FLinearColor(ModeValue, FlowDir.X, FlowDir.Y, FlowSpeed));
		MistMID->SetVectorParameterValue(JRPGMistParams::FlowLines, FLinearColor(LineSpacing, LineWidth, LineCoverage, LineCurve));
		MistMID->SetVectorParameterValue(JRPGMistParams::FlowEddy, FLinearColor(EddyStrength, EddySize, LineDensity, 0.0f));
		MistMID->SetVectorParameterValue(JRPGMistParams::FlowDash, FLinearColor(TrailLength, TrailFill, AvoidDistance, AvoidStrength));
	}
}

void AJRPGMistVolume::ApplyModePreset(EJRPGMistMode NewMode)
{
	Mode = NewMode;

	// Presets afinados olhando o resultado no editor (World Map / TestMap). A densidade é por
	// metro; os modos de linha já multiplicam por 3 no shader
	switch (NewMode)
	{
	case EJRPGMistMode::GroundAndFlow:
		Density = 1.0f;
		HeightFalloff = 100.0f;
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

	case EJRPGMistMode::GroundMist:
		Density = 1.0f;
		HeightFalloff = 90.0f;
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
		HeightFalloff = 120.0f;
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

	case EJRPGMistMode::PulseRings:
		Density = 2.0f;
		HeightFalloff = 120.0f;
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

UMaterialParameterCollection* AJRPGMistVolume::GetParameterCollection() const
{
	return ParameterCollection.LoadSynchronous();
}

void AJRPGMistVolume::BeginPlay()
{
	Super::BeginPlay();

	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UJRPGMistSubsystem* Mist = GI->GetSubsystem<UJRPGMistSubsystem>())
		{
			Mist->RegisterVolume(this);
		}
	}
}

void AJRPGMistVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UJRPGMistSubsystem* Mist = GI->GetSubsystem<UJRPGMistSubsystem>())
		{
			Mist->UnregisterVolume(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}
