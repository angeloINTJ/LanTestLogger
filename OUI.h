// =============================================
// OUI.h — IEEE OUI Lookup (39434 OUIs)
// =============================================
// Dependencias: LittleFS
//
// Arquivos no LittleFS (coloque na raiz via picotool ou flash):
//   /oui.dat        (115 KB) — prefixos ordenados, obrigatorio
//   /ouinames.dat   (977 KB) — prefixos + nomes, opcional
//
// Uso:
//   ouiInit();                     // init, opcionalmente carrega RAM
//   int idx = ouiLookup(mac);      // busca binaria → indice ou -1
//   String nome = ouiGetName(idx); // fabricante (lento sem RAM cache)
//   int total = ouiGetCount();     // 39434
// =============================================

#include <LittleFS.h>

File ouiFile;
File ouiNamesFile;
uint32_t ouiCount = 0;
uint32_t ouiNamesStrStart = 0;
uint32_t ouiNamesCount = 0;
bool ouiReady = false;
uint8_t *ouiRAM = nullptr;

// Inicializa. Se loadRAM=true, carrega 115 KB dos prefixos na heap
void ouiInit(bool loadRAM = false) {
  ouiReady = false;
  ouiCount = 0;

  if (LittleFS.exists("/oui.dat")) {
    ouiFile = LittleFS.open("/oui.dat", "r");
    if (ouiFile) {
      uint8_t hdr[4];
      if (ouiFile.read(hdr, 4) == 4) {
        memcpy(&ouiCount, hdr, 4);
        ouiReady = true;

        if (loadRAM && ouiCount > 0) {
          uint32_t prefixSize = ouiCount * 3;
          ouiRAM = (uint8_t*)malloc(prefixSize);
          if (ouiRAM) {
            ouiFile.seek(4);
            ouiFile.read(ouiRAM, prefixSize);
          }
        }
      }
    }
  }

  if (LittleFS.exists("/ouinames.dat")) {
    ouiNamesFile = LittleFS.open("/ouinames.dat", "r");
    if (ouiNamesFile) {
      uint8_t hdr[8];
      if (ouiNamesFile.read(hdr, 8) == 8) {
        uint32_t noff;
        memcpy(&ouiNamesCount, hdr, 4);
        memcpy(&noff, hdr + 4, 4);
        ouiNamesStrStart = noff;
      }
    }
  }
}

uint32_t ouiGetCount() { return ouiCount; }
bool ouiIsReady() { return ouiReady; }

// Busca binaria rapida (RAM se disponivel, senao LittleFS)
int ouiLookup(const uint8_t *mac) {
  if (!ouiReady || ouiCount == 0) return -1;

  uint32_t lo = 0, hi = ouiCount - 1;
  uint8_t key[3] = {mac[0], mac[1], mac[2]};

  while (lo <= hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    if (mid >= ouiCount) break;

    uint8_t mid3[3];
    if (ouiRAM) {
      // Leitura direta da RAM — instantaneo
      memcpy(mid3, ouiRAM + mid * 3, 3);
    } else {
      // Leitura via LittleFS — ~16 seeks por lookup
      ouiFile.seek(4 + mid * 3);
      if (ouiFile.read(mid3, 3) != 3) return -1;
    }

    int cmp = 0;
    for (int i = 0; i < 3; i++) {
      if (key[i] < mid3[i]) { cmp = -1; break; }
      if (key[i] > mid3[i]) { cmp = 1; break; }
    }

    if (cmp == 0) return (int)mid;
    if (cmp < 0) hi = mid - 1;
    else lo = mid + 1;
  }
  return -1;
}

// Busca por string "XX:XX:XX:XX:XX:XX"
int ouiLookupStr(const String &macStr) {
  uint8_t mac[6];
  if (sscanf(macStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
             &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6) {
    return ouiLookup(mac);
  }
  return -1;
}

// Obtem nome do fabricante pelo indice
// Com arquivo de nomes: scan sequencial (lento p/ indices altos)
// Sem arquivo: retorna prefixo em hex
String ouiGetName(int idx) {
  if (!ouiNamesFile || idx < 0 || (uint32_t)idx >= ouiCount) {
    if (idx >= 0 && ouiFile) {
      uint8_t prefix[3];
      if (ouiRAM) {
        memcpy(prefix, ouiRAM + idx * 3, 3);
      } else {
        ouiFile.seek(4 + idx * 3);
        ouiFile.read(prefix, 3);
      }
      char buf[20];
      snprintf(buf, sizeof(buf), "OUI:%02X:%02X:%02X",
               prefix[0], prefix[1], prefix[2]);
      return String(buf);
    }
    return "N/A";
  }

  // Scan ate o nome[idx] na string table
  uint32_t strPos = ouiNamesStrStart;
  ouiNamesFile.seek(strPos);
  char c;
  uint32_t current = 0;
  while (current < (uint32_t)idx && ouiNamesFile.available()) {
    c = (char)ouiNamesFile.read();
    if (c == '\0') current++;
  }
  String name = "";
  while (ouiNamesFile.available()) {
    c = (char)ouiNamesFile.read();
    if (c == '\0') break;
    name += c;
  }
  if (name.length() == 0) {
    uint8_t prefix[3];
    ouiNamesFile.seek(8 + idx * 3);
    ouiNamesFile.read(prefix, 3);
    char buf[20];
    snprintf(buf, sizeof(buf), "OUI:%02X:%02X:%02X",
             prefix[0], prefix[1], prefix[2]);
    return String(buf);
  }
  return name;
}

// Atalho: busca OUI e retorna nome
String ouiLookupName(const uint8_t *mac) {
  int idx = ouiLookup(mac);
  return ouiGetName(idx);
}

String ouiLookupNameStr(const String &macStr) {
  int idx = ouiLookupStr(macStr);
  return ouiGetName(idx);
}
