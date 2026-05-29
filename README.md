# BVYT
# Baixador de vídeos
App simples para baixar vídeos usando yt-dlp.

O projeto inteiro está em PT-BR!

# Como compilar:
Visual Studio (MSVC) (sim eu sei que é ruim mas é o que tem no momento); 
Instale o Visual Studio 2022 com o workload "Desktop development with C++"; 
Execute `build.bat` (duplo clique); 
O executável será gerado em `build\\BVYT.exe`.

# Como funciona
O `.exe` compilado não depende de nada externo; 
Na primeira execução, o app baixa automaticamente o `yt-dlp.exe` e salva em `%APPDATA%\\BVYT\\`; 
Nas próximas execuções, o yt-dlp já está presente e o download começa imediatamente.

# Requisitos de sistema
- Torradeira com Windows instalado
- Internet
- + de 10Mb disponível

# Dependências
`comctl32`	
`wininet`	
`shell32`	
`yt-dlp`	
basicamente MSVC com Desktop development with C++, Windows SDK e YT-DLP (que é baixado automaticamente)


# Se você for alterar o código, pelo amor, NÃO BAIXA O VSC DA MICROSOFT STORE! não tem o Windows SDK nele e é um saco para colocar manualmente, VAI DAR ERRO NESSA PORRA

# ATENÇÃO
# ESSE PROJETO UTILIZA IA!!!!!
Sessões do código foram escritas com a bomba da ia do google e o claude, eu sou terrível em escrever códigos mas mesmo assim eu ainda faço apps. Se você é totalmente contra o uso de ia, não utilize o projeto.

GPL-3.0
