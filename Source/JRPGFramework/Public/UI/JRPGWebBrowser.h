#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "JRPGWebBrowser.generated.h"

// Forward-declaration do Slate widget interno — NÃO incluir SDK em .h público

class SUltralightBrowser;

UCLASS(BlueprintType, Blueprintable)
class JRPGFRAMEWORK_API UJRPGWebBrowser : public UWidget
{
	GENERATED_BODY()

public:
	UJRPGWebBrowser();

	/** Registra um UObject no contexto JS (mantido para compatibilidade com o codigo existente) */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebBrowser")
	void BindUObject(const FString& Name, UObject* Object, bool bRecurse = true);

	/** Habilita ou desabilita a transparência do navegador */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebBrowser")
	void SetSupportsTransparency(bool bInSupportsTransparency);

	/** Carrega uma URL especifica (ex: file:///.../menu.html) */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebBrowser")
	void LoadURL(const FString& URL);

	/** Carrega uma string HTML diretamente */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebBrowser")
	void LoadString(const FString& Contents, const FString& DummyURL);

	/** Executa codigo Javascript */
	UFUNCTION(BlueprintCallable, Category = "JRPG|WebBrowser")
	void ExecuteJavascript(const FString& Script);

	// Cache de ponteiro da textura para impedir que seja coletada pelo GC
	UPROPERTY()
	UTexture2D* RenderTargetTexture;

	void SetRenderTargetTexture(UTexture2D* InTexture) { RenderTargetTexture = InTexture; }

	/** Retorna o Slate widget interno para focagem direta do teclado */
	TSharedPtr<SWidget> GetSlateWidget() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	TSharedPtr<SUltralightBrowser> UltralightBrowserWidget;

	UPROPERTY(EditAnywhere, Category = "JRPG|WebBrowser")
	FString InitialURL;

	bool bSupportsTransparency;
};
