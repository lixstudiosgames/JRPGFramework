import os
import shutil
import subprocess
import sys

# O console do Windows abre em cp1252 e os emojis das mensagens de status
# levantavam UnicodeEncodeError, escondendo a causa real de uma falha de build.
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

# --- Configuracao da Engine / caminhos (UE 5.8.1) ---
ENGINE_DIR = r"D:\JOGOS\UE_5.8"

# Raiz do plugin = pasta acima de Extras/, para o script acompanhar a pasta em
# que ele estiver (CODE\5.8\JRPGFramework) sem depender de caminho fixo.
PLUGIN_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Pasta de trabalho do build, irma da pasta do plugin (CODE\5.8\Build)
BUILD_ROOT = os.path.join(os.path.dirname(PLUGIN_DIR), "Build")

# Projeto de destino para a copia final do plugin compilado
PROJECT_PLUGINS_DIR = r"D:\JOGOS\PROJETOS UNREAL\5.8\Legaia\Plugins\JRPGFramework"


def run_build(non_interactive):
    src_dir = PLUGIN_DIR
    build_dir = BUILD_ROOT
    
    # Pasta temporaria de origem do plugin
    temp_source_dir = os.path.join(build_dir, "TempSource", "JRPGFramework")
    
    # Pasta temporaria onde o UAT vai fazer o package
    compilado_temp = os.path.join(build_dir, "compilado_temp")
    
    # Pasta final que o usuario quer (limpa, sem HostProject e pronta para copiar)
    final_output_dir = os.path.join(build_dir, "compilado", "JRPGFramework")
    
    uat_path = os.path.join(ENGINE_DIR, "Engine", "Build", "BatchFiles", "RunUAT.bat")
    
    print("==================================================")
    print("      COMPILADOR DO PLUGIN JRPGFRAMEWORK          ")
    print("==================================================")
    if not non_interactive:
        input("Pressione [ENTER] para começar a compilação...")
    print("\n--- INICIANDO PROCESSO DE COMPILACAO DO PLUGIN ---")
    
    # 1. Limpeza total de execucoes anteriores
    for folder in [temp_source_dir, compilado_temp, os.path.join(build_dir, "compilado")]:
        if os.path.exists(folder):
            print(f"Limpando pasta antiga: {folder}")
            try:
                shutil.rmtree(folder)
            except Exception as e:
                print(f"Aviso ao deletar {folder}: {e}")
                
    # 2. Copia os arquivos originais para a pasta temporaria de origem
    print(f"\nCopiando arquivos originais do framework...")
    
    def ignore_patterns(path, names):
        # Ignora pastas temporarias, git e builds antigas para manter a copia limpa e rapida.
        # LegaiaStudio e ferramenta de autoria (roda no navegador, fora do jogo) —
        # nao faz sentido empacotar junto. Docs/ FICA: quem usa o plugin precisa
        # dos CSVs para importar as DataTables.
        ignored = []
        for name in names:
            if name in ['.git', 'Binaries', 'Intermediate', 'Saved', 'compilado', '.vs',
                        'HostProject', 'TempSource', 'compilado_temp', 'LegaiaStudio']:
                ignored.append(name)
        return ignored

    try:
        shutil.copytree(src_dir, temp_source_dir, ignore=ignore_patterns)
        print("Copia realizada com sucesso!")
    except Exception as e:
        print(f"Erro ao copiar arquivos para a pasta temporaria de build: {e}")
        return False

    # 3. Executa a compilação com o RunUAT
    uplugin_path = os.path.join(temp_source_dir, "JRPGFramework.uplugin")
    cmd = [
        uat_path,
        "BuildPlugin",
        f"-Plugin={uplugin_path}",
        f"-Package={compilado_temp}",
        # -Rocket era workaround do ArgumentNullException do BuildPlugin na 5.3.2.
        # Na 5.8 o UAT nao le mais esse parametro e o bug nao existe, entao saiu.
        "-nocompileuat"
    ]
    
    print(f"\nExecutando comando RunUAT:\n{' '.join(cmd)}\n")
    
    try:
        process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding='utf-8',
            errors='replace'
        )
        
        while True:
            output = process.stdout.readline()
            if output == '' and process.poll() is not None:
                break
            if output:
                try:
                    print(output.strip())
                except UnicodeEncodeError:
                    print(output.encode('ascii', errors='replace').decode('ascii').strip())
                
        rc = process.poll()
        if rc == 0:
            print("\nOrganizando arquivos finais na pasta correta...")
            # Copia os arquivos compilados da pasta temporaria para a pasta final limpa
            os.makedirs(final_output_dir, exist_ok=True)
            
            # Copia apenas o que importa (ignorando HostProject se o UAT tiver deixado lá)
            for item in os.listdir(compilado_temp):
                s = os.path.join(compilado_temp, item)
                d = os.path.join(final_output_dir, item)
                if item == "HostProject":
                    continue # Ignora completamente a pasta HostProject do UAT
                if os.path.isdir(s):
                    shutil.copytree(s, d)
                else:
                    shutil.copy2(s, d)
            
            # Limpa as pastas temporarias para nao ocupar espaço
            try:
                shutil.rmtree(temp_source_dir)
                shutil.rmtree(compilado_temp)
            except Exception as e:
                pass
                
            print("\n==================================================")
            print("✅ COMPILACAO CONCLUIDA!")
            print(f"📂 {final_output_dir}")
            print("==================================================")
            return True
        else:
            print(f"\n❌ Falha na compilação do plugin. Código de retorno: {rc}")
            return False
            
    except Exception as e:
        err_msg = str(e)
        try:
            print(f"Erro inesperado ao executar o RunUAT: {err_msg}")
        except UnicodeEncodeError:
            print(f"Erro inesperado ao executar o RunUAT: {err_msg.encode('ascii', errors='replace').decode('ascii')}")
        return False

def main():
    non_interactive = "--non-interactive" in sys.argv
    if non_interactive:
        sys.exit(0 if run_build(non_interactive) else 1)

    while True:
        success = run_build(non_interactive)
        if success:
            print("\n==================================================")
            print("🎉 BUILD CONCLUIDO COM SUCESSO!")
            print("==================================================")
            
            src_folder = os.path.join(BUILD_ROOT, "compilado", "JRPGFramework")
            dst_folder = PROJECT_PLUGINS_DIR
            
            print(f"\nOrigem:  {src_folder}")
            print(f"Destino: {dst_folder}")
            
            resposta = input("\nDeseja copiar o plugin para a pasta do projeto? (S/N): ").strip().lower()
            
            if resposta == 's':
                print("\nCopiando arquivos...")
                try:
                    for root, dirs, files in os.walk(src_folder):
                        rel_path = os.path.relpath(root, src_folder)
                        target_dir = os.path.join(dst_folder, rel_path)
                        os.makedirs(target_dir, exist_ok=True)
                        for file in files:
                            src_file = os.path.join(root, file)
                            dst_file = os.path.join(target_dir, file)
                            shutil.copy2(src_file, dst_file)
                    print("✅ Plugin copiado com sucesso para a pasta do projeto!")
                except Exception as e:
                    print(f"❌ Erro ao copiar: {e}")
            else:
                print("\nAbrindo as pastas para copia manual...")
                os.startfile(src_folder)
                os.startfile(dst_folder)
            
            input("\nPressione [ENTER] para sair...")
            break
        else:
            print("\n==================================================")
            opcao = input("Deseja tentar compilar novamente? (S/N): ").strip().lower()
            if opcao != 's':
                break

if __name__ == "__main__":
    main()
