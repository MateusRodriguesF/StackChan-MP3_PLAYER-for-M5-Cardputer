#include <SD.h>
String files[2000] =
    {};
int no_of_files = 0;
bool stop_scan = false;
void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
  // printf_log("Listing directory: %s\n", dirname);
  if (stop_scan)
  {
    return;
  }
  File root = fs.open(dirname);
  if (!root)
  {
    // println_log("Failed to open directory");
    return;
  }
  if (!root.isDirectory())
  {
    // println_log("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file)
  {
    bool scan = true;
      String filename = file.name();
      String filepath = file.path();
      if (filename.lastIndexOf(".mp3") > 0 && filepath.length() < 256)
        if (filepath != "")
        {
          files[no_of_files++] = filepath;
          String filename = file.name();
        }
    if (M5Cardputer.Keyboard.isChange())
    {
      if (M5Cardputer.Keyboard.isKeyPressed('s'))
      {
        levels = 0;
        return;
      }
    }
    file = root.openNextFile();
  }
}
