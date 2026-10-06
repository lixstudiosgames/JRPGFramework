#include "World/JRPGMistVolume.h"
#include "World/JRPGMistSubsystem.h"
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
	MistMID->SetVectorParameterValue(JRPGMistParams::ShapeB, FLinearColor(Wind.X, Wind.Y, WarpStrength, WarpSpin));
	MistMID->SetVectorParameterValue(JRPGMistParams::Ribbons, FLinearColor(RibbonSharpness, RibbonAmount, RibbonStretch, RibbonCoverage));
	MistMID->SetVectorParameterValue(JRPGMistParams::Scene, FLinearColor(PoolDistance, PoolAmount, FlowAround, EdgeFade));
	MistMID->SetVectorParameterValue(JRPGMistParams::Interaction, FLinearColor(ClearStrength, SwirlStrength, 0.0f, 0.0f));
	MistMID->SetVectorParameterValue(JRPGMistParams::BoxXY, FLinearColor(
		Bounds.Min.X, Bounds.Min.Y, Bounds.Max.X, Bounds.Max.Y));
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
