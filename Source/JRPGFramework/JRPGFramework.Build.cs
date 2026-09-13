using System.IO;
using UnrealBuildTool;

[SupportedTargetTypes(TargetType.Editor, TargetType.Game)]
public class JRPGFramework : ModuleRules
{
	public JRPGFramework(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// Adicionar caminhos públicos de inclusão aqui
			}
		);
				
		PrivateIncludePaths.AddRange(
			new string[] {
				// Adicionar caminhos privados de inclusão aqui
			}
		);
				
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"UMG",
				"Projects",
				"RHI",
				"RenderCore"
			}
		);
				
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"D3D11RHI"     // Para OpenSharedResource1 no side Unreal (Parte 4)
			}
		);

		// --- Libs de sistema D3D11/DXGI (usadas pelo GPUDriver customizado) ---
		PublicSystemLibraries.AddRange(new string[]
		{
			"d3d11.lib",   // D3D11CreateDevice, device context
			"dxgi.lib",    // DXGI shared handles, IDXGIKeyedMutex
		});

		// --- Linker do Ultralight SDK ---
		string UltralightRoot = Path.Combine(ModuleDirectory, "..", "..", "Content", "ThirdParty", "Ultralight");
		string IncludePath = Path.Combine(UltralightRoot, "include");
		string LibPath = Path.Combine(UltralightRoot, "lib");
		string BinPath = Path.Combine(UltralightRoot, "bin");
		string ResourcesPath = Path.Combine(UltralightRoot, "resources");

		PublicIncludePaths.Add(IncludePath);

		// Sem verificação de plataforma pois o SDK já é só Win64
		// Link-time libraries (.lib)
		string[] Libs = { "Ultralight.lib", "UltralightCore.lib", "WebCore.lib", "AppCore.lib" };
		foreach (string Lib in Libs)
		{
			PublicAdditionalLibraries.Add(Path.Combine(LibPath, Lib));
		}

		// Runtime DLLs (.dll) — apenas para empacotamento, carregadas manualmente no código
		string[] Dlls = { "Ultralight.dll", "UltralightCore.dll", "WebCore.dll", "AppCore.dll" };
		foreach (string Dll in Dlls)
		{
			string Src = Path.Combine(BinPath, Dll);
			RuntimeDependencies.Add(Src);
			// Habilita Delay-Load para que o Windows não tente carregar as DLLs antes do subsistema inicializar
			PublicDelayLoadDLLs.Add(Dll);
		}

		// Recursos essenciais (cacert.pem, icudt67l.dat)
		string[] Resources = { "cacert.pem", "icudt67l.dat" };
		foreach (string Res in Resources)
		{
			RuntimeDependencies.Add(Path.Combine(ResourcesPath, Res));
		}

		// --- Assets de runtime da WebUI (shell.html + fontes + imagens) ---
		// OBRIGATÓRIO. O staging do UE copia das pastas Content de plugin apenas
		// .uasset/.umap COZIDOS; arquivos crus (html/ttf/png) ficam de fora do
		// pacote a menos que declarados aqui. Sem isto o build empacotado não tem
		// shell.html: a View do Ultralight carrega um caminho inexistente e a UI
		// fica invisível — no editor funciona porque ele lê direto do projeto.
		//
		// Wildcards de propósito: são expandidos no STAGING, então uma fonte ou
		// retrato novo entra no pacote sem precisar recompilar o módulo.
		// A pasta src/ NÃO entra — é fonte do Extras/build_webui.py, não runtime.
		string WebUIPath = Path.Combine(ModuleDirectory, "..", "..", "Content", "UI", "WebUI");
		RuntimeDependencies.Add(Path.Combine(WebUIPath, "shell.html"));
		RuntimeDependencies.Add(Path.Combine(WebUIPath, "fonts", "*.ttf"));
		RuntimeDependencies.Add(Path.Combine(WebUIPath, "images", "*.png"));

		// --- CÓPIA AUTOMÁTICA DE DLLS PARA EVITAR ERRO DE CARREGAMENTO DO MÓDULO ---
		// O Windows precisa achar as DLLs quando carrega a DLL do plugin. Copiar para as pastas de Binários garante isso.
		string PluginBinariesDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "Binaries", "Win64"));
		foreach (string Dll in Dlls)
		{
			string Src = Path.Combine(BinPath, Dll);
			string Dest = Path.Combine(PluginBinariesDir, Dll);
			CopyFileIfNewer(Src, Dest);
		}

		if (Target.ProjectFile != null)
		{
			string ProjectDir = Path.GetDirectoryName(Target.ProjectFile.FullName);
			string ProjectBinariesDir = Path.GetFullPath(Path.Combine(ProjectDir, "Binaries", "Win64"));
			foreach (string Dll in Dlls)
			{
				string Src = Path.Combine(BinPath, Dll);
				string Dest = Path.Combine(ProjectBinariesDir, Dll);
				CopyFileIfNewer(Src, Dest);
			}
		}
	}

	private void CopyFileIfNewer(string Source, string Dest)
	{
		if (!File.Exists(Source)) return;

		string DestDir = Path.GetDirectoryName(Dest);
		if (!Directory.Exists(DestDir))
		{
			Directory.CreateDirectory(DestDir);
		}

		bool bShouldCopy = true;
		if (File.Exists(Dest))
		{
			FileInfo SrcInfo = new FileInfo(Source);
			FileInfo DestInfo = new FileInfo(Dest);
			if (SrcInfo.Length == DestInfo.Length && SrcInfo.LastWriteTimeUtc == DestInfo.LastWriteTimeUtc)
			{
				bShouldCopy = false;
			}
		}

		if (bShouldCopy)
		{
			try
			{
				File.Copy(Source, Dest, true);
			}
			catch (System.Exception Ex)
			{
				System.Console.WriteLine("JRPGFramework UBT: Falha ao copiar DLL {0} para {1}: {2}", Source, Dest, Ex.Message);
			}
		}
	}
}
