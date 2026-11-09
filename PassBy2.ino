// =====================================================================
// PassBy v2 - contagem bidirecional com DOIS sensores ultrassonicos
//   Sensor 1 -> Sensor 2 : ENTRADA
//   Sensor 2 -> Sensor 1 : SAIDA
// ESP32-WROOM-32 + OLED SSD1306 + buzzer + painel web (LittleFS)
// =====================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LittleFS.h>      // armazenamento persistente + arquivos do painel
#include <Preferences.h>   // guarda totais de entradas/saidas e estado do buzzer
#include <time.h>
#include <sys/time.h>

// Wi-Fi
const char *ssid = "Pedro";
const char *password = "1234567890";


// ---------------------------------------------------------------
// HARDWARE (ESP32-WROOM-32)
// ---------------------------------------------------------------
#define TRIG1   19     // Sensor ultrassonico 1 - TRIG
#define ECHO1   25    // Sensor ultrassonico 1 - ECHO
#define TRIG2   5    // Sensor ultrassonico 2 - TRIG
#define ECHO2   18    // Sensor ultrassonico 2 - ECHO
#define BUZZER  23
#define OLED_SDA 21
#define OLED_SCL 22

// OLED
#define LARGURA 128
#define ALTURA 64

// ---------------------------------------------------------------
// PARAMETROS AJUSTAVEIS (calibracao em campo)
// ---------------------------------------------------------------
// Histerese: o sensor "ativa" a ate DISTANCIA_DETECCAO_CM e so "libera"
// quando passa de DISTANCIA_LIBERACAO_CM. Entre os dois valores mantem
// o estado anterior, o que evita oscilacoes e contagens multiplas.
#define DISTANCIA_DETECCAO_CM   10.0f
#define DISTANCIA_LIBERACAO_CM  15.0f

// Tempo maximo entre o 1o sensor detectar e o 2o confirmar (ms).
// Ajuste conforme a distancia entre os sensores e a velocidade dos animais.
#define TEMPO_CONFIRMACAO_MS    1500

// Apos uma passagem confirmada (ou tentativa cancelada com sensor ainda
// ocupado), os DOIS sensores precisam ficar livres por este tempo antes de
// aceitar nova passagem. Cobre "vazios" entre as patas do animal.
#define TEMPO_LIBERACAO_MS      500

// Leituras consecutivas necessarias para ativar / liberar um sensor
// (filtro anti-ruido; use 1 para desligar o filtro).
#define LEITURAS_PARA_ATIVAR    2
#define LEITURAS_PARA_LIBERAR   2

// Intervalo minimo entre dois disparos TRIG (de sensores diferentes).
// As leituras sao sequenciais: S1, pausa, S2, pausa, S1...
// Cada sensor e amostrado a cada ~2x este intervalo (>= 60 ms recomendado).
#define INTERVALO_ENTRE_SENSORES_MS 30

// Timeout do eco (us). 12000 us ~ 2 m. Sem eco = nada na frente do sensor.
#define TIMEOUT_ECO_US          12000UL

// Duracao do bipe (ms). Durante o bipe as leituras ficam em pausa
// para o buzzer nao interferir nos sensores.
#define DURACAO_BIPE_MS         150

// 1 = imprime as distancias no Serial (util para calibrar). 0 = desligado.
#define DEBUG_SENSORES          0

Adafruit_SSD1306 tela(LARGURA, ALTURA, &Wire, -1);

// Servidor web
NetworkServer server(80);
bool servidorIniciado = false;

// ---------------------------------------------------------------
// Totais (independentes)
// ---------------------------------------------------------------
uint32_t totalEntradas = 0;
uint32_t totalSaidas = 0;

// ---------------------------------------------------------------
// Armazenamento do historico
// ---------------------------------------------------------------
// /eventos.bin (v2): sequencia de registros de 8 bytes:
//   uint32 horario Unix | uint8 tipo | 3 bytes reservados
// So se acrescenta ao fim do arquivo (append): um corte de energia
// nunca corrompe registros antigos.
//
// /registros.bin (v1, versao anterior): uint32 por registro
//   (passagem = horario | reinicio = horario com bit 31 ligado).
// Na primeira inicializacao v2 ele e CONVERTIDO (passagem -> ENTRADA,
// reinicio -> REINICIO) e guardado como /registros_v1.bak.
#define ARQ_REG     "/eventos.bin"
#define ARQ_TMP     "/eventos.tmp"
#define ARQ_MIG     "/eventos_mig.tmp"
#define ARQ_V1      "/registros.bin"
#define ARQ_V1_TMP  "/registros.tmp"
#define ARQ_V1_BAK  "/registros_v1.bak"
#define MARCA_REINICIO_V1 0x80000000UL

#define TIPO_ENTRADA  1
#define TIPO_SAIDA    2
#define TIPO_REINICIO 3

#define MAX_REGISTROS 10000        // 80 KB; acima disso descarta os mais antigos
#define MANTER_REGISTROS 8000
#define INTERVALO_GRAVACAO 10000   // grava em lote a cada 10 s

struct __attribute__((packed)) Registro {
  uint32_t t;
  uint8_t tipo;
  uint8_t rsv[3];
};
#define TAM_REG ((size_t)sizeof(Registro))   // 8

struct Pendente {            // evento ocorrido antes de haver horario valido
  unsigned long ms;
  uint8_t tipo;
};

Preferences prefs;
bool buzzerAtivo = true;
uint32_t rev = 1;            // muda a cada alteracao (o painel usa para atualizar)

Registro bufReg[32];
uint8_t nBuf = 0;
Pendente pend[64];
uint8_t nPend = 0;
bool pendenteGravacao = false;
unsigned long sujoDesde = 0;
bool ntpIniciado = false;

// ---------------------------------------------------------------
// Sensores e maquina de estados
// ---------------------------------------------------------------
// <<LOGICA_BEGIN>>
enum EstadoPassagem : uint8_t {
  ESTADO_LIVRE,              // nada em andamento
  ESTADO_ENTRADA_PENDENTE,   // S1 detectou primeiro, aguardando S2
  ESTADO_SAIDA_PENDENTE,     // S2 detectou primeiro, aguardando S1
  ESTADO_AGUARDA_LIBERACAO   // aguarda os dois sensores liberarem
};

struct Sensor {
  uint8_t trig, echo;
  bool ativo;                // estado filtrado (com histerese + debounce)
  uint8_t cntAtiva, cntLibera;
  unsigned long tInicio;     // 1a leitura abaixo do limite na sequencia atual
  unsigned long tAtivou;     // instante em que foi confirmado como ativo
};

Sensor sensor1 = {TRIG1, ECHO1, false, 0, 0, 0, 0};
Sensor sensor2 = {TRIG2, ECHO2, false, 0, 0, 0, 0};

EstadoPassagem estado = ESTADO_LIVRE;
unsigned long tPendente = 0;     // instante em que a tentativa comecou
unsigned long tLivreDesde = 0;
bool contandoLivre = false;

void confirmarPassagem(uint8_t tipo);   // definida mais abaixo

// Aplica histerese e filtro a UMA leitura de distancia (d < 0 = sem eco)
void atualizarSensor(Sensor &s, float d, unsigned long agora) {
  bool dentro = (d > 0 && d <= DISTANCIA_DETECCAO_CM);
  bool fora = (d < 0 || d >= DISTANCIA_LIBERACAO_CM);

  if (!s.ativo) {
    if (dentro) {
      if (s.cntAtiva == 0) s.tInicio = agora;
      if (++s.cntAtiva >= LEITURAS_PARA_ATIVAR) {
        s.ativo = true;
        s.tAtivou = agora;
        s.cntLibera = 0;
      }
    } else {
      s.cntAtiva = 0;
    }
  } else {
    if (fora) {
      if (++s.cntLibera >= LEITURAS_PARA_LIBERAR) {
        s.ativo = false;
        s.cntAtiva = 0;
        s.cntLibera = 0;
      }
    } else {
      s.cntLibera = 0;   // entre 10 e 15 cm: mantem o estado (histerese)
    }
  }
}

// Maquina de estados da passagem. Chamada a cada ciclo do loop.
void atualizarMaquina(unsigned long agora) {
  switch (estado) {

    case ESTADO_LIVRE:
      // Os sensores sao lidos um por vez, entao so um deles muda por chamada.
      if (sensor1.ativo) {
        estado = ESTADO_ENTRADA_PENDENTE;
        tPendente = sensor1.tAtivou;
        Serial.println("S1 detectou: possivel ENTRADA");
      } else if (sensor2.ativo) {
        estado = ESTADO_SAIDA_PENDENTE;
        tPendente = sensor2.tAtivou;
        Serial.println("S2 detectou: possivel SAIDA");
      }
      break;

    case ESTADO_ENTRADA_PENDENTE:
      if (sensor2.ativo) {
        confirmarPassagem(TIPO_ENTRADA);
        estado = ESTADO_AGUARDA_LIBERACAO;
        contandoLivre = false;
      } else if (agora - tPendente >= TEMPO_CONFIRMACAO_MS) {
        Serial.println("Entrada cancelada (S2 nao confirmou)");
        estado = (sensor1.ativo || sensor2.ativo) ? ESTADO_AGUARDA_LIBERACAO
                                                  : ESTADO_LIVRE;
        contandoLivre = false;
      }
      break;

    case ESTADO_SAIDA_PENDENTE:
      if (sensor1.ativo) {
        confirmarPassagem(TIPO_SAIDA);
        estado = ESTADO_AGUARDA_LIBERACAO;
        contandoLivre = false;
      } else if (agora - tPendente >= TEMPO_CONFIRMACAO_MS) {
        Serial.println("Saida cancelada (S1 nao confirmou)");
        estado = (sensor1.ativo || sensor2.ativo) ? ESTADO_AGUARDA_LIBERACAO
                                                  : ESTADO_LIVRE;
        contandoLivre = false;
      }
      break;

    case ESTADO_AGUARDA_LIBERACAO:
      // Pronto para nova passagem somente com os DOIS sensores livres
      // de forma estavel por TEMPO_LIBERACAO_MS.
      if (!sensor1.ativo && !sensor2.ativo) {
        if (!contandoLivre) {
          contandoLivre = true;
          tLivreDesde = agora;
        } else if (agora - tLivreDesde >= TEMPO_LIBERACAO_MS) {
          estado = ESTADO_LIVRE;
          contandoLivre = false;
          Serial.println("Sensores livres: pronto");
        }
      } else {
        contandoLivre = false;
      }
      break;
  }
}
// <<LOGICA_END>>

// Leitura sequencial: um sensor por vez, com pausa entre os disparos
unsigned long tUltimaLeitura = 0;
uint8_t sensorDaVez = 0;

// Buzzer sem bloqueio
bool bipeAtivo = false;
unsigned long tBipeFim = 0;

bool horaValida() {
  return time(nullptr) > 1700000000;
}

// ---------------------------------------------------------------
// Historico
// ---------------------------------------------------------------
// Copia parte do arquivo de registros para o temporario e o substitui
void reescreverRegistros(size_t inicio, size_t fim) {
  File o = LittleFS.open(ARQ_REG, "r");
  File d = LittleFS.open(ARQ_TMP, "w");
  if (!o || !d) {
    if (o) o.close();
    if (d) d.close();
    return;
  }
  o.seek(inicio);
  uint8_t b[256];
  size_t resta = fim - inicio;
  while (resta > 0) {
    size_t n = o.read(b, resta < sizeof(b) ? resta : sizeof(b));
    if (n == 0) break;
    d.write(b, n);
    resta -= n;
  }
  o.close();
  d.close();
  LittleFS.remove(ARQ_REG);
  LittleFS.rename(ARQ_TMP, ARQ_REG);
}

void gravarRegistros() {
  if (nBuf == 0) return;
  File f = LittleFS.open(ARQ_REG, FILE_APPEND);
  if (!f) {
    Serial.println("Erro ao abrir registros");
    return;
  }
  size_t esperado = nBuf * TAM_REG;
  size_t escrito = f.write((uint8_t *)bufReg, esperado);
  size_t tamanho = f.size();
  f.close();
  if (escrito == esperado) nBuf = 0;

  // Limite de armazenamento
  if (tamanho > (size_t)MAX_REGISTROS * TAM_REG) {
    reescreverRegistros(tamanho - (size_t)MANTER_REGISTROS * TAM_REG, tamanho);
  }
}

void gravarTudo() {
  gravarRegistros();
  prefs.putUInt("ent", totalEntradas);
  prefs.putUInt("sai", totalSaidas);
  pendenteGravacao = false;
}

void marcarSujo() {
  if (!pendenteGravacao) {
    pendenteGravacao = true;
    sujoDesde = millis();
  }
}

void adicionarRegistro(uint32_t t, uint8_t tipo) {
  if (nBuf >= 32) gravarRegistros();
  if (nBuf < 32) {
    bufReg[nBuf].t = t;
    bufReg[nBuf].tipo = tipo;
    bufReg[nBuf].rsv[0] = bufReg[nBuf].rsv[1] = bufReg[nBuf].rsv[2] = 0;
    nBuf++;
  }
}

// Registra um evento (entrada, saida ou reinicio) no historico
void registrarEvento(uint8_t tipo) {
  rev++;
  if (horaValida()) {
    adicionarRegistro((uint32_t)time(nullptr), tipo);
  } else {
    if (nPend >= 64) {
      memmove(pend, pend + 1, 63 * sizeof(Pendente));
      nPend = 63;
    }
    pend[nPend].ms = millis();
    pend[nPend].tipo = tipo;
    nPend++;
  }
  marcarSujo();
}

// Quando o horario fica disponivel, converte os eventos pendentes
void converterPendentes() {
  if (nPend == 0 || !horaValida()) return;
  uint32_t agora = (uint32_t)time(nullptr);
  for (uint8_t i = 0; i < nPend; i++) {
    adicionarRegistro(agora - (millis() - pend[i].ms) / 1000, pend[i].tipo);
  }
  nPend = 0;
  rev++;
  marcarSujo();
}

void manterArmazenamento() {
  converterPendentes();
  if (pendenteGravacao && millis() - sujoDesde >= INTERVALO_GRAVACAO) {
    gravarTudo();
  }
}

// ---------------------------------------------------------------
// Compatibilidade com a versao anterior
// ---------------------------------------------------------------
// Converte /registros.bin (v1) para /eventos.bin (v2) sem perder dados.
// O arquivo antigo e mantido como /registros_v1.bak.
void migrarHistoricoV1() {
  // sobra de corte de energia da limpeza do firmware v1
  if (!LittleFS.exists(ARQ_V1) && LittleFS.exists(ARQ_V1_TMP)) {
    LittleFS.rename(ARQ_V1_TMP, ARQ_V1);
  }
  // sobra de migracao interrompida: recomeca
  if (LittleFS.exists(ARQ_MIG)) LittleFS.remove(ARQ_MIG);

  if (!LittleFS.exists(ARQ_V1)) return;

  if (LittleFS.exists(ARQ_REG)) {
    // v2 ja existe (corte de energia entre as duas etapas): so guarda o antigo
    LittleFS.remove(ARQ_V1_BAK);
    LittleFS.rename(ARQ_V1, ARQ_V1_BAK);
    return;
  }

  File o = LittleFS.open(ARQ_V1, "r");
  File d = LittleFS.open(ARQ_MIG, "w");
  if (!o || !d) {
    if (o) o.close();
    if (d) d.close();
    Serial.println("Migracao v1 adiada: erro ao abrir arquivos");
    return;
  }

  bool ok = true;
  uint32_t convertidos = 0;
  uint32_t v;
  while (o.read((uint8_t *)&v, 4) == 4) {
    Registro r;
    r.t = v & ~MARCA_REINICIO_V1;
    r.tipo = (v & MARCA_REINICIO_V1) ? TIPO_REINICIO : TIPO_ENTRADA;
    r.rsv[0] = r.rsv[1] = r.rsv[2] = 0;
    if (d.write((uint8_t *)&r, TAM_REG) != TAM_REG) {
      ok = false;
      break;
    }
    convertidos++;
  }
  o.close();
  d.close();

  if (ok) {
    LittleFS.remove(ARQ_V1_BAK);
    LittleFS.rename(ARQ_MIG, ARQ_REG);
    LittleFS.rename(ARQ_V1, ARQ_V1_BAK);
    Serial.print("Historico v1 migrado: ");
    Serial.print(convertidos);
    Serial.println(" registros (antigas passagens = entradas)");
  } else {
    LittleFS.remove(ARQ_MIG);   // o arquivo antigo permanece intacto
    Serial.println("Migracao v1 falhou (espaco?). Dados antigos preservados.");
  }
}

// ---------------------------------------------------------------
// Sensores
// ---------------------------------------------------------------
// Mede a distancia (cm). Retorna -1 se nao houver eco no alcance.
float medirDistancia(uint8_t trig, uint8_t echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(3);

  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  unsigned long duracao = pulseIn(echo, HIGH, TIMEOUT_ECO_US);

  if (duracao == 0) {
    return -1;
  }

  return duracao * 0.0343f / 2.0f;
}

// Le UM sensor por chamada, alternando S1 / S2, sem bloquear o loop
void lerSensores() {
  if (bipeAtivo) return;   // buzzer ligado: nao dispara os sensores
  unsigned long agora = millis();
  if (agora - tUltimaLeitura < INTERVALO_ENTRE_SENSORES_MS) return;

  Sensor &s = (sensorDaVez == 0) ? sensor1 : sensor2;
  float d = medirDistancia(s.trig, s.echo);
  atualizarSensor(s, d, agora);

#if DEBUG_SENSORES
  Serial.print("S");
  Serial.print(sensorDaVez + 1);
  Serial.print(": ");
  Serial.print(d);
  Serial.print(" cm  ativo=");
  Serial.println(s.ativo);
#endif

  tUltimaLeitura = millis();   // a pausa conta a partir do fim do eco
  sensorDaVez ^= 1;
}

// ---------------------------------------------------------------
// OLED (mostra somente ENTRADAS)
// ---------------------------------------------------------------
void mostrarContador() {
  tela.clearDisplay();
  tela.setTextColor(SSD1306_WHITE);

  tela.setTextSize(1);
  tela.setCursor(20, 8);
  tela.println("PASSBY - CONTADOR");

  tela.setTextSize(3);
  tela.setCursor(45, 30);
  tela.println(totalEntradas);

  tela.display();
}

// ---------------------------------------------------------------
// Buzzer (sem delay)
// ---------------------------------------------------------------
void iniciarBipe() {
  digitalWrite(BUZZER, HIGH);
  tBipeFim = millis() + DURACAO_BIPE_MS;
  bipeAtivo = true;
}

void atualizarBipe() {
  if (bipeAtivo && (long)(millis() - tBipeFim) >= 0) {
    digitalWrite(BUZZER, LOW);
    bipeAtivo = false;
    tUltimaLeitura = millis();   // pequena pausa antes de voltar a medir
  }
}

// ---------------------------------------------------------------
// Passagem confirmada pelos dois sensores
// ---------------------------------------------------------------
void confirmarPassagem(uint8_t tipo) {
  if (tipo == TIPO_ENTRADA) {
    totalEntradas++;
    registrarEvento(TIPO_ENTRADA);
    mostrarContador();                 // OLED: so entradas
    Serial.print("ENTRADA confirmada. Total entradas: ");
    Serial.println(totalEntradas);
  } else {
    totalSaidas++;                     // saida NAO altera o total de entradas
    registrarEvento(TIPO_SAIDA);
    Serial.print("SAIDA confirmada. Total saidas: ");
    Serial.println(totalSaidas);
  }
  if (buzzerAtivo) iniciarBipe();      // respeita a configuracao do painel
}

// ---------------------------------------------------------------
// Servidor HTTP (arquivos do painel + API)
// ---------------------------------------------------------------
String parametro(const String &consulta, const String &chave) {
  int i = consulta.indexOf(chave + "=");
  if (i < 0) return "";
  i += chave.length() + 1;
  int f = consulta.indexOf('&', i);
  return f < 0 ? consulta.substring(i) : consulta.substring(i, f);
}

void enviarCabecalho(NetworkClient &c, const char *tipo, int codigo = 200) {
  c.print("HTTP/1.1 ");
  c.print(codigo);
  c.print(codigo == 200 ? " OK" : " Error");
  c.print("\r\nContent-Type: ");
  c.print(tipo);
  c.print("\r\nConnection: close\r\nCache-Control: no-cache\r\n\r\n");
}

void enviarJson(NetworkClient &c, const String &corpo, int codigo = 200) {
  enviarCabecalho(c, "application/json", codigo);
  c.print(corpo);
}

// Envia o vetor de horarios dos eventos de um tipo, lendo o arquivo em blocos
void enviarLista(NetworkClient &c, uint8_t tipo) {
  String saida = "[";
  bool primeiro = true;

  auto adicionar = [&](const Registro &r) {
    if (r.tipo != tipo) return;
    if (!primeiro) saida += ",";
    saida += String(r.t);
    primeiro = false;
    if (saida.length() > 900) {
      c.print(saida);
      saida = "";
    }
  };

  File f = LittleFS.open(ARQ_REG, "r");
  if (f) {
    Registro blk[32];
    int n;
    while ((n = f.read((uint8_t *)blk, sizeof(blk))) >= (int)TAM_REG) {
      for (int i = 0; i < n / (int)TAM_REG; i++) adicionar(blk[i]);
    }
    f.close();
  }
  for (uint8_t i = 0; i < nBuf; i++) adicionar(bufReg[i]);

  saida += "]";
  c.print(saida);
}

void enviarArquivo(NetworkClient &c, String caminho) {
  if (caminho == "/") caminho = "/index.html";

  if (caminho.indexOf("..") >= 0 || !LittleFS.exists(caminho)) {
    enviarJson(c, "{\"erro\":\"nao encontrado\"}", 404);
    return;
  }

  const char *tipo = "text/plain";
  if (caminho.endsWith(".html")) tipo = "text/html; charset=utf-8";
  else if (caminho.endsWith(".css")) tipo = "text/css";
  else if (caminho.endsWith(".js")) tipo = "application/javascript";

  File f = LittleFS.open(caminho, "r");
  enviarCabecalho(c, tipo);
  uint8_t b[512];
  while (f.available()) {
    int n = f.read(b, sizeof(b));
    if (n <= 0) break;
    c.write(b, n);
  }
  f.close();
}

void atenderCliente(NetworkClient &client) {
  client.setTimeout(400);

  // Le a primeira linha da requisicao HTTP
  String requisicao = client.readStringUntil('\n');
  requisicao.trim();

  // Descarta os demais cabecalhos
  while (client.connected()) {
    String linha = client.readStringUntil('\n');

    if (linha.length() <= 1) {
      break;
    }
  }

  int a = requisicao.indexOf(' ');
  int b = requisicao.indexOf(' ', a + 1);
  if (a < 0 || b < 0) return;

  String metodo = requisicao.substring(0, a);
  String caminho = requisicao.substring(a + 1, b);
  String consulta = "";
  int q = caminho.indexOf('?');
  if (q >= 0) {
    consulta = caminho.substring(q + 1);
    caminho = caminho.substring(0, q);
  }

  if (caminho == "/api/status") {
    // "count" e mantido por compatibilidade (= entradas)
    String s = "{\"in\":" + String(totalEntradas) +
               ",\"out\":" + String(totalSaidas) +
               ",\"count\":" + String(totalEntradas) +
               ",\"buzzer\":" + (buzzerAtivo ? "true" : "false") +
               ",\"rev\":" + String(rev) +
               ",\"now\":" + String((uint32_t)time(nullptr)) +
               ",\"timeOk\":" + (horaValida() ? "true" : "false") + "}";
    enviarJson(client, s);

  } else if (caminho == "/api/data") {
    enviarCabecalho(client, "application/json");
    client.print("{\"events\":");          // entradas
    enviarLista(client, TIPO_ENTRADA);
    client.print(",\"exits\":");           // saidas
    enviarLista(client, TIPO_SAIDA);
    client.print(",\"resets\":");          // reinicios dos contadores
    enviarLista(client, TIPO_REINICIO);
    client.print("}");

  } else if (caminho.startsWith("/api/")) {
    if (metodo != "POST") {
      enviarJson(client, "{\"erro\":\"use POST\"}", 405);
      return;
    }

    if (caminho == "/api/buzzer") {
      buzzerAtivo = (parametro(consulta, "on") == "1");
      prefs.putBool("buz", buzzerAtivo);
      rev++;

    } else if (caminho == "/api/reset") {
      // Zera os DOIS totais; o historico de eventos e preservado
      totalEntradas = 0;
      totalSaidas = 0;
      registrarEvento(TIPO_REINICIO);
      gravarTudo();
      rev++;
      mostrarContador();

    } else if (caminho == "/api/clear") {
      // Apaga somente o historico; os totais nao sao alterados
      nBuf = 0;
      nPend = 0;
      LittleFS.remove(ARQ_REG);
      LittleFS.remove(ARQ_V1_BAK);
      rev++;

    } else if (caminho == "/api/time") {
      long t = parametro(consulta, "t").toInt();
      if (!horaValida() && t > 1700000000L) {
        struct timeval tv = {(time_t)t, 0};
        settimeofday(&tv, NULL);
        converterPendentes();
      }
    } else {
      enviarJson(client, "{\"erro\":\"nao encontrado\"}", 404);
      return;
    }
    enviarJson(client, "{\"ok\":true}");

  } else {
    enviarArquivo(client, caminho);
  }
}

void setup() {
  Serial.begin(115200);

  // Configura os componentes
  pinMode(TRIG1, OUTPUT);
  pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT);
  pinMode(ECHO2, INPUT);
  pinMode(BUZZER, OUTPUT);

  digitalWrite(TRIG1, LOW);
  digitalWrite(TRIG2, LOW);
  digitalWrite(BUZZER, LOW);

  // Armazenamento
  prefs.begin("passby", false);
  buzzerAtivo = prefs.getBool("buz", true);
  if (prefs.isKey("ent")) {
    totalEntradas = prefs.getUInt("ent", 0);
    totalSaidas = prefs.getUInt("sai", 0);
  } else {
    // Primeira execucao da v2: o contador antigo ("cnt") vira o total de entradas
    totalEntradas = prefs.getUInt("cnt", 0);
    totalSaidas = 0;
    prefs.putUInt("ent", totalEntradas);
    prefs.putUInt("sai", 0);
  }

  if (!LittleFS.begin(true)) {
    Serial.println("Erro ao montar LittleFS!");
  } else {
    // Historico da versao anterior: converte sem descartar nada
    migrarHistoricoV1();

    // Recupera de um corte de energia durante a limpeza de registros
    if (!LittleFS.exists(ARQ_REG) && LittleFS.exists(ARQ_TMP)) {
      LittleFS.rename(ARQ_TMP, ARQ_REG);
    }
    // Descarta registro incompleto no fim do arquivo
    if (LittleFS.exists(ARQ_REG)) {
      File f = LittleFS.open(ARQ_REG, "r");
      size_t t = f.size();
      f.close();
      if (t % TAM_REG != 0) reescreverRegistros(0, t - (t % TAM_REG));
    }
  }

  // Inicia o OLED
  Wire.begin(OLED_SDA, OLED_SCL);

  if (!tela.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Erro ao iniciar OLED!");

    while (true) {
      delay(10);
    }
  }

  mostrarContador();

  // Configura o Wi-Fi para tentar reconectar automaticamente
  WiFi.setAutoReconnect(true);
  IPAddress local_IP(10, 236, 223, 180);
IPAddress gateway(10, 236, 223, 200);
IPAddress subnet(255, 255, 255, 0);

WiFi.config(local_IP, gateway, subnet);

WiFi.begin(ssid, password);
  WiFi.begin(ssid, password);

  // Tenta conectar por no maximo 10 segundos
  Serial.println("Tentando conectar ao Wi-Fi...");

  unsigned long inicio = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - inicio < 10000) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wi-Fi conectado!");
  } else {
    Serial.println("Wi-Fi indisponivel.");
    Serial.println("PassBy funcionando em modo offline.");
  }

  // O loop comeca mesmo sem Wi-Fi
}

void loop() {
  // 1. Sensores: leitura sequencial + maquina de estados + buzzer
  lerSensores();
  atualizarMaquina(millis());
  atualizarBipe();

  // 2. Inicia o servidor quando o Wi-Fi estiver conectado
  if (WiFi.status() == WL_CONNECTED && !servidorIniciado) {
  server.begin();
  servidorIniciado = true;

  Serial.println("Wi-Fi conectado!");

  Serial.print("IP do ESP32: ");
  Serial.println(WiFi.localIP());

  Serial.print("Gateway: ");
  Serial.println(WiFi.gatewayIP());

  Serial.print("Mascara: ");
  Serial.println(WiFi.subnetMask());

  Serial.println("Servidor PassBy iniciado!");
}

  // Tenta obter o horario pela internet (se nao houver, o painel envia)
  if (WiFi.status() == WL_CONNECTED && !ntpIniciado) {
    configTime(-3 * 3600, 0, "pool.ntp.org", "time.google.com");
    ntpIniciado = true;
  }

  // 3. Atende clientes somente quando o servidor estiver disponivel
  if (servidorIniciado && WiFi.status() == WL_CONNECTED) {
    NetworkClient client = server.accept();

    if (client) {
      atenderCliente(client);
      client.stop();
    }
  }

  manterArmazenamento();   // grava em lote, sem atrapalhar os sensores

  delay(1);                // cede tempo ao sistema (sem bloqueio relevante)
}
