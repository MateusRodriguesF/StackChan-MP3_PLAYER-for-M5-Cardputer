#include <Avatar.h>

#include <HTTPClient.h>
#include <math.h>
/// necessita da biblioteca ESP8266Audio. ( URL : https://github.com/earlephilhower/ESP8266Audio/ )
#include <stdlib.h>
#include <AudioOutput.h>
#include <AudioFileSourceSD.h>
#include <AudioFileSourceID3.h>
#include <AudioGeneratorMP3.h>
#include "M5Cardputer.h"
#include <M5Unified.h>
#include <SPI.h>
#include "handlefile.h"
#include <Avatar.h>
#include <faces/DogFace.h>
#include <faces/BMPFace.h>
#include <faces/OledFace.h>

//-----------------------------------------
#define SD_SPI_SCK_PIN  40
#define SD_SPI_MISO_PIN 39
#define SD_SPI_MOSI_PIN 14
#define SD_SPI_CS_PIN   12
//-----------------------------------------

using namespace m5avatar;
Avatar avatar;

bool showBalloon = false; // Variável para controlar se o balão está visível
unsigned long lastBalloonTime = 0; // Momento em que o balão foi exibido pela última vez
int currentFileIndex = 0; // Índice do arquivo atual que está sendo reproduzido
// Variável para controlar se a reprodução aleatória está ativada
bool randomPlayEnabled = false;

Face* faces[4];
const int facesSize = sizeof(faces) / sizeof(Face*);
int faceIdx = 0;
const Expression expressions[] = {
  Expression::Angry,
  Expression::Sleepy,
  Expression::Happy,
  Expression::Sad,
  Expression::Doubt,
  Expression::Neutral
};
const int expressionsSize = sizeof(expressions) / sizeof(Expression);
int idx = 0;

ColorPalette* cps[4];
const int cpsSize = sizeof(cps) / sizeof(ColorPalette*);
int cpsIdx = 0;
bool isShowingQR = false;

//-----------------------------------------
/// configurar o canal virtual do M5Speaker (0-7)
static constexpr uint8_t m5spk_virtual_channel = 0;
bool isPaused = false;
/// definir o nome do arquivo mp3
static constexpr const char* filename[] =
{
  "/music.mp3"
};
static constexpr const size_t filecount = sizeof(filename) / sizeof(filename[0]);
String filetoplay;
char f[256];
class AudioOutputM5Speaker : public AudioOutput
{
  public:
    AudioOutputM5Speaker(m5::Speaker_Class* m5sound, uint8_t virtual_sound_channel = 0)
    {
      _m5sound = m5sound;
      _virtual_ch = virtual_sound_channel;
    }
    virtual ~AudioOutputM5Speaker(void) {};
    virtual bool begin(void) override { return true; }
    virtual bool ConsumeSample(int16_t sample[2]) override
    {
      if (_tri_buffer_index < tri_buf_size)
      {
        _tri_buffer[_tri_index][_tri_buffer_index  ] = sample[0];
        _tri_buffer[_tri_index][_tri_buffer_index+1] = sample[1];
        _tri_buffer_index += 2;

        return true;
      }

      flush();
      return false;
    }
    virtual void flush(void) override
    {
      if (_tri_buffer_index)
      {
        _m5sound->playRaw(_tri_buffer[_tri_index], _tri_buffer_index, hertz, true, 1, _virtual_ch);
        _tri_index = _tri_index < 2 ? _tri_index + 1 : 0;
        _tri_buffer_index = 0;
      }
    }
    virtual bool stop(void) override
    {
      flush();
      _m5sound->stop(_virtual_ch);
      return true;
    }

    const int16_t* getBuffer(void) const { return _tri_buffer[(_tri_index + 2) % 3]; }
    const uint32_t getUpdateCount(void) const { return _update_count; }
  protected:
    m5::Speaker_Class* _m5sound;
    uint8_t _virtual_ch;
    static constexpr size_t tri_buf_size = 640;
    int16_t _tri_buffer[3][tri_buf_size];
    size_t _tri_buffer_index = 0;
    size_t _tri_index = 0;
    size_t _update_count = 0;
};


#define FFT_SIZE 256
class fft_t
{
  float _wr[FFT_SIZE + 1];
  float _wi[FFT_SIZE + 1];
  float _fr[FFT_SIZE + 1];
  float _fi[FFT_SIZE + 1];
  uint16_t _br[FFT_SIZE + 1];
  size_t _ie;

public:
  fft_t(void)
  {
#ifndef M_PI
#define M_PI 3.141592653
#endif
    _ie = logf( (float)FFT_SIZE ) / log(2.0) + 0.5;
    static constexpr float omega = 2.0f * M_PI / FFT_SIZE;
    static constexpr int s4 = FFT_SIZE / 4;
    static constexpr int s2 = FFT_SIZE / 2;
    for ( int i = 1 ; i < s4 ; ++i)
    {
    float f = cosf(omega * i);
      _wi[s4 + i] = f;
      _wi[s4 - i] = f;
      _wr[     i] = f;
      _wr[s2 - i] = -f;
    }
    _wi[s4] = _wr[0] = 1;

    size_t je = 1;
    _br[0] = 0;
    _br[1] = FFT_SIZE / 2;
    for ( size_t i = 0 ; i < _ie - 1 ; ++i )
    {
      _br[ je << 1 ] = _br[ je ] >> 1;
      je = je << 1;
      for ( size_t j = 1 ; j < je ; ++j )
      {
        _br[je + j] = _br[je] + _br[j];
      }
    }
  }

  void exec(const int16_t* in)
  {
    memset(_fi, 0, sizeof(_fi));
    for ( size_t j = 0 ; j < FFT_SIZE / 2 ; ++j )
    {
      float basej = 0.25 * (1.0-_wr[j]);
      size_t r = FFT_SIZE - j - 1;

      /// realiza a janela han e converte estéreo para mono.
      _fr[_br[j]] = basej * (in[j * 2] + in[j * 2 + 1]);
      _fr[_br[r]] = basej * (in[r * 2] + in[r * 2 + 1]);
    }

    size_t s = 1;
    size_t i = 0;
    do
    {
      size_t ke = s;
      s <<= 1;
      size_t je = FFT_SIZE / s;
      size_t j = 0;
      do
      {
        size_t k = 0;
        do
        {
          size_t l = s * j + k;
          size_t m = ke * (2 * j + 1) + k;
          size_t p = je * k;
          float Wxmr = _fr[m] * _wr[p] + _fi[m] * _wi[p];
          float Wxmi = _fi[m] * _wr[p] - _fr[m] * _wi[p];
          _fr[m] = _fr[l] - Wxmr;
          _fi[m] = _fi[l] - Wxmi;
          _fr[l] += Wxmr;
          _fi[l] += Wxmi;
        } while ( ++k < ke) ;
      } while ( ++j < je );
    } while ( ++i < _ie );
  }

  uint32_t get(size_t index)
  {
    return (index < FFT_SIZE / 2) ? (uint32_t)sqrtf(_fr[ index ] * _fr[ index ] + _fi[ index ] * _fi[ index ]) : 0u;
  }
};
static constexpr size_t WAVE_SIZE = 320;
static AudioFileSourceSD file;
static AudioOutputM5Speaker out(&M5Cardputer.Speaker, m5spk_virtual_channel);
static AudioGeneratorMP3 mp3;
static AudioFileSourceID3* id3 = nullptr;
static fft_t fft;
static bool fft_enabled = false;
static bool wave_enabled = false;
static uint16_t prev_y[(FFT_SIZE / 2)+1];
static uint16_t peak_y[(FFT_SIZE / 2)+1];
static int16_t wave_y[WAVE_SIZE];
static int16_t wave_h[WAVE_SIZE];
static int16_t raw_data[WAVE_SIZE * 2];
static int header_height = 0;
static size_t fileindex = 0;
uint32_t paused_at = 0;

void MDCallback(void *cbData, const char *type, bool isUnicode, const char *string)
{
  (void)cbData;
  if (string[0] == 0) { return; }
  if (strcmp(type, "eof") == 0)
  {
    M5Cardputer.Display.display();
    return;
  }
  // int y = M5Cardputer.Display.getCursorY();
  // if (y+1 >= header_height) { return; }
  // M5Cardputer.Display.fillRect(0, y, M5Cardputer.Display.width(), 12, M5Cardputer.Display.getBaseColor());
  // M5Cardputer.Display.printf("%s: %s", type, string);
  // M5Cardputer.Display.setCursor(0, y+12);
}

void stop(void)
{
  if (id3 == nullptr) return;
  out.stop();
  mp3.stop();
  id3->RegisterMetadataCB(nullptr, nullptr);
  id3->close();
  file.close();
  delete id3;
  id3 = nullptr;
}

void play(const char* fname)
{
  if (id3 != nullptr) { stop(); }
  M5Cardputer.Display.setCursor(0, 8);
  file.open(fname);
  id3 = new AudioFileSourceID3(&file);
  id3->RegisterMetadataCB(MDCallback, (void*)"ID3TAG");
  id3->open(fname);
  mp3.begin(id3, &out);
}

void pauseme(void){
  paused_at = id3->getPos();
  mp3.stop();
  isPaused = true;
}
void resume(const char* fname){
    if (id3 != nullptr) { stop(); }
  M5Cardputer.Display.setCursor(0, 8);
  file.open(fname);
  id3 = new AudioFileSourceID3(&file);
  id3->RegisterMetadataCB(MDCallback, (void*)"ID3TAG");
  id3->open(fname);
  id3->seek( paused_at,1 );
  isPaused = false;
  mp3.begin(id3, &out);
}

// Função para obter um índice aleatório dentro do intervalo válido de arquivos MP3
int getRandomIndex() {
  return rand() % no_of_files;
}

//SETUP-SETUP-SETUP-SETUP-SETUP-SETUP-SETUP-SETUP-SETUP-SETUP-SETUP-SETUP-SETUP-SETUP-SETUP-SETUP-SETUP-SETUP-SETUP-SETUP-SETUP-SETUP-SETUP-SETUP
void setup(void)
{ 
  M5.begin();
  auto cfg = M5.config();
  M5Cardputer.begin(cfg);

  M5Cardputer.Display.setRotation(1);
  avatar.setPosition(-50, -45);
  avatar.setEyeOpenRatio(0.7);
  avatar.setScale(0.7);

  avatar.setScale(0.7);
  // M5.Lcd.clear();

  cfg.external_speaker.hat_spk = true;

  faces[0] = avatar.getFace();
  faces[1] = new DogFace();
  faces[2] = new BMPFace();
  faces[3] = new OledFace();

  cps[0] = new ColorPalette();
  cps[1] = new ColorPalette();
  cps[2] = new ColorPalette();
  cps[3] = new ColorPalette();
  // cps[4] = new ColorPalette();
  cps[1]->set(COLOR_PRIMARY, TFT_YELLOW);
  cps[1]->set(COLOR_BACKGROUND, TFT_DARKCYAN);
  cps[2]->set(COLOR_PRIMARY, TFT_DARKGREY);
  cps[2]->set(COLOR_BACKGROUND, TFT_WHITE);
  cps[3]->set(COLOR_PRIMARY, TFT_RED);
  cps[3]->set(COLOR_BACKGROUND, TFT_PINK);
  // cps[4]->set(COLOR_PRIMARY, TFT_BLACK);
  // cps[4]->set(COLOR_BACKGROUND, TFT_WHITE);
  avatar.init(); // inicia o desenho
  avatar.setColorPalette(*cps[0]);

  { /// configuração personalizada
    auto spk_cfg = M5Cardputer.Speaker.config();
    /// Aumentar a taxa de amostragem melhorará a qualidade do som em vez de aumentar a carga da CPU.
    spk_cfg.sample_rate = 128000; // padrão: 64000 (64kHz)  e.g. 48000 , 50000 , 80000 , 96000 , 100000 , 128000 , 144000 , 192000 , 200000
    spk_cfg.task_pinned_core = APP_CPU_NUM;
    M5Cardputer.Speaker.config(spk_cfg);
  }
    SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);

    if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
        // Imprime uma mensagem se a inicialização do cartão SD falhar ou se o cartão SD não existir.
        avatar.setPosition(-50, -45);
        M5Cardputer.Display.setTextColor(TFT_WHITE);
        // avatar.setColorPalette(*cps[4]);
        avatar.setSpeechText("SD Not Found!!");
        avatar.setMouthOpenRatio(0.7);
        delay(200);
        avatar.setMouthOpenRatio(0);
        // M5Cardputer.Display.width() / 2,M5Cardputer.Display.height() / 2);
    while (1);
    }
    listDir(SD, "/", 2);
    fileindex = 0;
}

//LOOP-LOOP-LOOP-LOOP-LOOP-LOOP-LOOP-LOOP-LOOP-LOOP-LOOP-LOOP-LOOP-LOOP-LOOP-LOOP-LOOP-LOOP-LOOP-LOOP-LOOP-LOOP-LOOP-LOOP
void loop(void) {
if (mp3.isRunning()) {
    if (!mp3.loop()) { 
      mp3.stop(); 
      if (randomPlayEnabled) { // Se a reprodução aleatória estiver ativada
        currentFileIndex = getRandomIndex(); // Seleciona um arquivo aleatório
      } else { // Se a reprodução aleatória não estiver ativada, avança para o próximo arquivo na lista
        currentFileIndex++;
        if (currentFileIndex >= no_of_files) { 
          currentFileIndex = 0; // Volta para o primeiro arquivo se atingir o final da lista
        }
      }
      filetoplay = files[currentFileIndex];
      String filetoplay20 = filetoplay.substring(0, 26);
      filetoplay.toCharArray(f,256);
      play(f);
      const char *filetoplay_cstr = filetoplay20.c_str();
      M5Cardputer.Display.setTextColor(TFT_WHITE);
      avatar.setSpeechText(filetoplay_cstr);
      showBalloon = true;
      lastBalloonTime = millis(); // Registra o momento em que o balão foi exibido
      avatar.setMouthOpenRatio(0.7);
      delay(200);
      avatar.setMouthOpenRatio(0);
    }
    } else {
    delay(1);
  }
  M5Cardputer.update();
  if (M5Cardputer.BtnA.wasClicked()) {
    M5Cardputer.Speaker.tone(1000, 100);
    stop();
    if (++fileindex >= no_of_files) { fileindex = 0; }
    filetoplay = files[fileindex];
    filetoplay.toCharArray(f,256);
    play(f);
    }
    if (M5Cardputer.Keyboard.isChange()) {
    avatar.setPosition(-50, -45);
    size_t v = M5Cardputer.Speaker.getVolume();
    if (M5Cardputer.Keyboard.isKeyPressed('p')) {
      if (isPaused) {
        filetoplay = files[fileindex];
        filetoplay.toCharArray(f,256);
        stop();
        resume(f);
      } else {
        pauseme();
      }
    }
    if (M5Cardputer.Keyboard.isKeyPressed('n')) {
      if (++fileindex >= no_of_files) { fileindex = 0; }
      filetoplay = files[fileindex];
      // Pegando os primeiros 20 caracteres do nome do arquivo
      String filetoplay20 = filetoplay.substring(0, 26);
      filetoplay.toCharArray(f,256);
      play(f);
      // Exibindo o balão com o nome do arquivo
      const char *filetoplay_cstr = filetoplay20.c_str();
      M5Cardputer.Display.setTextColor(TFT_WHITE);
      avatar.setSpeechText(filetoplay_cstr);
      showBalloon = true;
      lastBalloonTime = millis(); // Registra o momento em que o balão foi exibido
      avatar.setMouthOpenRatio(0.7);
      delay(200);
      avatar.setMouthOpenRatio(0);
    }
    if (M5Cardputer.Keyboard.isKeyPressed('b')) {
      if (--fileindex < 0) { fileindex = no_of_files; }
      filetoplay = files[fileindex];
      // Pegando os primeiros 20 caracteres do nome do arquivo
      String filetoplay20 = filetoplay.substring(0, 26);
      filetoplay.toCharArray(f,256);
      play(f);
      // Exibindo o balão com o nome do arquivo
      const char *filetoplay_cstr = filetoplay20.c_str();
      M5Cardputer.Display.setTextColor(TFT_WHITE);
      avatar.setSpeechText(filetoplay_cstr);
      showBalloon = true;
      lastBalloonTime = millis(); // Registra o momento em que o balão foi exibido
      avatar.setMouthOpenRatio(0.7);
      delay(200);
      avatar.setMouthOpenRatio(0);
    }
    if (showBalloon && (millis() - lastBalloonTime >= 5000)) {
      // Limpa o balão após 5 segundos
      avatar.setSpeechText("");
      showBalloon = false; // Marca o balão como não visível
    }
    if (M5Cardputer.Keyboard.isKeyPressed(';')) {
      v += 10;
    }
    if (M5Cardputer.Keyboard.isKeyPressed('1')) {
      avatar.setPosition(-50, -45);
      avatar.setFace(faces[faceIdx]);
      faceIdx = (faceIdx + 1) % facesSize;
    }
    if (M5Cardputer.Keyboard.isKeyPressed('2')) {
      avatar.setPosition(-50, -45);
      avatar.setColorPalette(*cps[cpsIdx]);
      cpsIdx = (cpsIdx + 1) % cpsSize;
    }
    if (M5Cardputer.Keyboard.isKeyPressed('3')) {
      avatar.setPosition(-50, -45);
      avatar.setExpression(expressions[idx]);
      idx = (idx + 1) % expressionsSize;
    }
      // Verifica se a tecla "-" foi pressionada para diminuir o brilho
    if (M5Cardputer.Keyboard.isKeyPressed('-')) {
      int currentBrightness = M5.Lcd.getBrightness();
      currentBrightness -= 10; // Reduz o brilho em 10 unidades
      if (currentBrightness < 0) {
        currentBrightness = 0;
      }
      M5.Lcd.setBrightness(currentBrightness);
    }
    // Verifica se a tecla "=" foi pressionada para aumentar o brilho
    if (M5Cardputer.Keyboard.isKeyPressed('=')) {
      int currentBrightness = M5.Lcd.getBrightness();
      currentBrightness += 10; // Aumenta o brilho em 10 unidades
      if (currentBrightness > 255) {
        currentBrightness = 255;
      }
      M5.Lcd.setBrightness(currentBrightness);
    }
    // Lógica para verificar se uma tecla específica foi pressionada
    if (M5Cardputer.Keyboard.isKeyPressed('s')) {
      randomPlayEnabled = !randomPlayEnabled; // Alterna o estado da reprodução aleatória

      // Feedback visual para indicar quando o modo aleatório foi ativado ou desativado
      if (randomPlayEnabled) {
        avatar.setSpeechText("Shuffle ON");
      } else {
        avatar.setSpeechText("Shuffle OFF");
      }
      showBalloon = true;
      lastBalloonTime = millis(); // Registra o momento em que o balão foi exibido
      avatar.setMouthOpenRatio(0.7);
      delay(200);
      avatar.setMouthOpenRatio(0);
    }
  }
}