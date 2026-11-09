Projeto Ciberfísico - PassBy
1. Sobre o Projeto

Este projeto tem como objetivo desenvolver um sistema ciberfísico utilizando o ESP32, sensores e atuadores, permitindo a comunicação entre o hardware e uma aplicação cliente.

Problema que o projeto resolve: Atualmente, muitos produtores rurais realizam a contagem de animais de maneira manual, principalmente durante a entrada e movimentação dos animais nos currais. Esse processo pode apresentar falhas humanas, como contagens incorretas, esquecimento de animais e divergências nos registros.
Esses erros podem prejudicar o controle do rebanho e dificultar o gerenciamento das atividades da propriedade rural.
Diante desse cenário, nosso projeto propõe o desenvolvimento de um sistema automatizado capaz de identificar a passagem dos animais por meio de dois sensores ultrassônicos, realizar a contagem através de um microcontrolador ESP32 e disponibilizar os dados em uma plataforma web.
Dessa forma, busca-se proporcionar maior precisão, praticidade e eficiência no controle de entrada de animais no curral.

2. Integrantes da Equipe:
Luma Lima
Pedro Henrique Andrade
Pedro Iago Duarte de Moraes
Rayka Souza
Victor Souza

4. Arquitetura da Solução:
O sistema é composto por uma aplicação cliente que se comunica com um servidor hospedado no ESP32. O microcontrolador recebe informações dos sensores e controla os atuadores.

!Arquitetura do projeto

4. Hardware Utilizado
Componente	Função
ESP32	Microcontrolador principal: <img width="600" height="600" alt="image" src="https://github.com/user-attachments/assets/e50d7b2e-d693-4381-b612-89484b95c0ba" />
2 sensores ultrassônicos: <img width="2000" height="2000" alt="image" src="https://github.com/user-attachments/assets/6be0ddc6-01a5-4cf6-a579-f1c80e4b3561" />
Tela OLED:	<img width="1200" height="1200" alt="image" src="https://github.com/user-attachments/assets/dd63cba6-1b6c-4416-9500-0ac1beaabf79" />
Buzzer: <img width="616" height="616" alt="image" src="https://github.com/user-attachments/assets/e574e60d-57f7-40c3-af1b-c4d5f8600e33" />
ProtoBoard: <img width="1000" height="1000" alt="image" src="https://github.com/user-attachments/assets/197029ab-bf03-45b3-a0a7-9f38d1110418" />

Esquemático do Circuito

!Esquemático

Pinagem
Componente	Pino ESP32
[Sensor]	GPIO XX
[Atuador]	GPIO XX
5. Comunicação e API

O sistema utiliza comunicação HTTP entre a aplicação cliente e o ESP32.

Endpoints
Método	Endpoint	Descrição
GET	/status	Consulta o status do sistema
GET	/sensor	Consulta os dados dos sensores
POST	/controle	Envia comandos aos atuadores

Os endpoints acima são exemplos e devem ser ajustados conforme a implementação real.

6. Instalação e Execução
ESP32
Instale a Arduino IDE.
Configure o suporte à placa ESP32.
Abra src/esp32/esp32_server.ino.
Configure as credenciais de Wi-Fi.
Conecte o ESP32 ao computador.
Compile e carregue o código.
Aplicação Cliente
Acesse src/client/.
Abra o arquivo index.html no navegador.
Certifique-se de que o ESP32 esteja conectado à rede.
Configure o endereço IP do ESP32 na aplicação, se necessário.
7. Demonstração

[Insira aqui o link do vídeo demonstrativo.]

8. Tecnologias Utilizadas
ESP32
C/C++
Arduino IDE
HTML
JavaScript
HTTP
9. Histórico de Desenvolvimento

O desenvolvimento foi realizado de forma colaborativa, com commits individuais de cada integrante da equipe.
