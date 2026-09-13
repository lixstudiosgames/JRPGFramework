#include "UI/WebContainerWidget.h"
#include "UI/JRPGWebBrowser.h"
#include "UI/WebUISubsystem.h"
#include "UI/WebUIBridge.h"
#include "Blueprint/WidgetTree.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Engine/GameInstance.h"
#include "Engine/UserInterfaceSettings.h"

TSharedRef<SWidget> UWebContainerWidget::RebuildWidget()
{
	if (!IsDesignTime() && !WebBrowser)
	{
		// Se o componente não foi vinculado no Blueprint, cria dinamicamente em C++
		WebBrowser = NewObject<UJRPGWebBrowser>(this, UJRPGWebBrowser::StaticClass());
		if (WebBrowser)
		{
			WebBrowser->SetSupportsTransparency(true);
			if (WidgetTree)
			{
				WidgetTree->RootWidget = WebBrowser;
				UE_LOG(LogTemp, Log, TEXT("WebContainerWidget: Componente JRPGWebBrowser criado dinamicamente no WidgetTree (RebuildWidget)."));
			}
		}
	}

	return Super::RebuildWidget();
}

bool UWebContainerWidget::Initialize()
{
	SetIsFocusable(true);
	return Super::Initialize();
}

void UWebContainerWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// (RenderFocusRule = Never agora é configurado 1x no WebUISubsystem::Initialize)

	if (WebBrowser)
	{
		WebBrowser->SetSupportsTransparency(true); // Habilita transparência

		// Vincula a ponte C++ JS-C++
		UGameInstance* GI = GetGameInstance();
		if (GI)
		{
			UWebUISubsystem* UISubsystem = GI->GetSubsystem<UWebUISubsystem>();
			if (UISubsystem && UISubsystem->GetBridge())
			{
				WebBrowser->BindUObject(TEXT("uebridge"), UISubsystem->GetBridge(), true);
				UE_LOG(LogTemp, Log, TEXT("WebContainerWidget: WebUIBridge vinculado com sucesso no NativeConstruct (Ultralight)."));
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("WebContainerWidget: Componente WebBrowser não está instanciado no NativeConstruct."));
	}
}

void UWebContainerWidget::LoadHTMLPage(const FString& PageName)
{
	if (WebBrowser)
	{
		FString CleanPageName = PageName;
		FString QueryString = TEXT("");

		// Divide a página e a query string no caractere '?'
		int32 QueryIndex;
		if (PageName.FindChar('?', QueryIndex))
		{
			CleanPageName = PageName.Left(QueryIndex);
			QueryString = PageName.Mid(QueryIndex);
		}

		// Resolve o caminho absoluto do arquivo HTML limpo
		FString AbsoluteFilePath;
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("JRPGFramework"));
		if (Plugin.IsValid())
		{
			FString PluginContentDir = Plugin->GetContentDir();
			AbsoluteFilePath = FPaths::ConvertRelativePathToFull(PluginContentDir / FString::Printf(TEXT("UI/WebUI/%s"), *CleanPageName));
		}
		else
		{
			AbsoluteFilePath = FPaths::ConvertRelativePathToFull(FPaths::EnginePluginsDir() / FString::Printf(TEXT("JRPGFramework/Content/UI/WebUI/%s"), *CleanPageName));
		}

		// O Ultralight não reporta 404 de file:/// — ele simplesmente renderiza nada.
		// Sem este aviso, um asset que não foi para o pacote vira uma tela preta
		// silenciosa e indistinguível de um bug de renderização (foi exatamente o
		// que aconteceu: Content/UI/WebUI não estava nas RuntimeDependencies do
		// Build.cs, então o build empacotado não tinha shell.html).
		if (!FPaths::FileExists(AbsoluteFilePath))
		{
			UE_LOG(LogTemp, Error,
				TEXT("WebContainerWidget: '%s' NAO EXISTE em disco ('%s'). A UI ficara em branco. ")
				TEXT("Em build empacotado, confira as RuntimeDependencies de Content/UI/WebUI no JRPGFramework.Build.cs."),
				*CleanPageName, *AbsoluteFilePath);
		}

		FString URL = FString::Printf(TEXT("file:///%s%s"), *AbsoluteFilePath, *QueryString);
		URL.ReplaceInline(TEXT("\\"), TEXT("/"));

		UE_LOG(LogTemp, Log, TEXT("WebContainerWidget: Carregando HTML do Ultralight via URL: '%s'"), *URL);
		WebBrowser->LoadURL(URL);
	}
}

void UWebContainerWidget::ExecuteJS(const FString& JSCode)
{
	if (WebBrowser)
	{
		WebBrowser->ExecuteJavascript(JSCode);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("WebContainerWidget: Falha ao executar JS. WebBrowser não está instanciado."));
	}
}
