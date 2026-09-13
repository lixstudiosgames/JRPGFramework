#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WebContainerWidget.generated.h"

class UJRPGWebBrowser;

/**
 * UWebContainerWidget
 * Widget UMG C++ que hospeda o componente WebBrowser.
 * Ele inicializa o navegador com o index.html local e vincula o objeto de ponte C++.
 */
UCLASS(BlueprintType, Blueprintable)
class JRPGFRAMEWORK_API UWebContainerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;

	/** Carrega uma página HTML específica pelo nome relativo (ex: menu.html, chest.html) */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void LoadHTMLPage(const FString& PageName);

	/**
	 * Executa código JavaScript na página atual do navegador.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebUI")
	void ExecuteJS(const FString& JSCode);

	/**
	 * Retorna a referência do componente WebBrowser.
	 */
	UJRPGWebBrowser* GetWebBrowser() const { return WebBrowser; }

protected:
	/**
	 * Referência do componente WebBrowser. Pode ser vinculada automaticamente do Blueprint
	 * se possuir o mesmo nome de elemento ("WebBrowser").
	 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "JRPG|WebUI")
	UJRPGWebBrowser* WebBrowser;
};
