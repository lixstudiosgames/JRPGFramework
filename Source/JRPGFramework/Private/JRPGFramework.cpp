#include "JRPGFramework.h"
#include "Misc/CommandLine.h"
#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FJRPGFrameworkModule"

void FJRPGFrameworkModule::StartupModule()
{
	// Este código será executado após a inicialização do módulo na memória (mas depois de carregar o CoreUObject).
	
	// A pasta Shaders/ só existe em build com fontes: num pacote cozido os global
	// shaders já vêm compilados no global shader map e o mapeamento não é usado.
	// AddShaderSourceDirectoryMapping tem checkf de diretório existente em algumas
	// configs, então guardar aqui evita um assert no pacote.
	FString ShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("JRPGFramework"))->GetBaseDir(), TEXT("Shaders"));
	if (FPaths::DirectoryExists(ShaderDir))
	{
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/JRPGFramework"), ShaderDir);
	}

	// Força o navegador CEF a rodar puramente por CPU (Software Rendering) para teste de performance
	FCommandLine::Append(TEXT(" -disable-gpu -disable-gpu-compositing"));
}

void FJRPGFrameworkModule::ShutdownModule()
{
	// Esta função pode ser chamada durante o desligamento para limpar o módulo.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FJRPGFrameworkModule, JRPGFramework)
