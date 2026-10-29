/*
  DETEKTOR NOMINAL UANG RUPIAH v2 - SENSOR WARNA TCS3200
  Versi ini menyimpan RENTANG warna untuk tiap pecahan (rata-rata + simpangan
  baku hasil kalibrasi), memakai 4 fitur (persen R, G, B dan tingkat terang),
  serta memeriksa otomatis apakah ada dua pecahan yang rentangnya tumpang
  tindih (misalnya Rp1.000 dan Rp2.000).

  Board : Arduino Uno / Nano (5V)

  WIRING:
    TCS3200 VCC -> 5V
    TCS3200 GND -> GND
    TCS3200 S0  -> D4
    TCS3200 S1  -> D5
    TCS3200 S2  -> D6
    TCS3200 S3  -> D7
    TCS3200 OUT -> D8
    TCS3200 OE  -> GND
    LED pada modul dibiarkan menyala

  CARA PAKAI:
    1. Upload, buka Serial Monitor (9600 baud, line ending "Newline").
    2. Kirim  k  untuk KALIBRASI. Urutan: kosong, Rp1.000, 2.000, 5.000,
       10.000, 20.000, 50.000, 100.000.
       - Selama 5 detik pembacaan, GESER uang pelan-pelan di bawah sensor.
       - Setelah tiap pecahan, Anda boleh menambah lembar lain (kirim  l )
         agar rentang mencakup uang yang agak lusuh / berbeda cetakan.
         Untuk Rp1.000 dan Rp2.000 sangat disarankan 2-3 lembar.
    3. Di akhir kalibrasi tampil tabel rentang tiap pecahan dan peringatan
       jika ada pasangan yang sulit dibedakan.
    4. Taruh uang di bawah sensor, nominal tampil otomatis.

  PERINTAH SERIAL:
    k = kalibrasi      p = tampilkan rentang & cek tumpang tindih
    d = mode debug     r = hapus kalibrasi

  Acuan warna uang emisi 2016 (kira-kira):
    Rp1.000 kuning kehijauan | Rp2.000 abu-abu | Rp5.000 coklat
    Rp10.000 ungu | Rp20.000 hijau | Rp50.000 biru | Rp100.000 merah
*/

#include <EEPROM.h>

// Ubah ke 1 jika memakai LCD I2C 16x2 (library LiquidCrystal_I2C)
#define USE_LCD 0

// Skala frekuensi sensor: 2 (resolusi lebih halus, disarankan) atau 20
#define SKALA_FREKUENSI 2

#if USE_LCD
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x27, 16, 2);
#endif

// ---------- Pin ----------
const uint8_t PIN_S0  = 4;
const uint8_t PIN_S1  = 5;
const uint8_t PIN_S2  = 6;
const uint8_t PIN_S3  = 7;
const uint8_t PIN_OUT = 8;

// ---------- Kelas ----------
const uint8_t JUMLAH_KELAS = 8;   // 0 = kosong, 1..7 = nominal
const char* NAMA[JUMLAH_KELAS] = {
  "Kosong",
  "Rp 1.000",
  "Rp 2.000",
  "Rp 5.000",
  "Rp 10.000",
  "Rp 20.000",
  "Rp 50.000",
  "Rp 100.000"
};

// ---------- Fitur ----------
// f[0] = persen merah, f[1] = persen hijau, f[2] = persen biru,
// f[3] = ln(tingkat terang kanal clear)
const uint8_t N_FITUR = 4;

// Batas bawah simpangan baku (agar rentang tidak terlalu sempit)
const float STD_MIN[N_FITUR] = {0.008, 0.008, 0.008, 0.05};

// ---------- Parameter ----------
const uint8_t  JUMLAH_SAMPEL    = 8;      // pembacaan per kanal
const uint16_t DURASI_KALIBRASI = 5000;   // ms per lembar
const uint8_t  MAKS_LEMBAR      = 4;      // maksimal lembar per nominal
const float    K_RENTANG        = 3.0;    // rentang = rata-rata +/- K * simpangan baku
const float    RASIO_MARGIN     = 1.25;   // juara 1 harus lebih dekat dari juara 2
const uint8_t  PEMBACAAN_STABIL = 3;
const uint16_t MAGIC            = 0xB7D1;

// ---------- Data kalibrasi (EEPROM) ----------
struct Referensi {
  uint16_t magic;
  float mean[JUMLAH_KELAS][N_FITUR];
  float sd[JUMLAH_KELAS][N_FITUR];
};

Referensi ref;
bool terkalibrasi = false;
bool modeDebug    = false;

int8_t  terakhir     = -3;
int8_t  terumumkan   = -3;
uint8_t hitungStabil = 0;

uint8_t kandA = 0, kandB = 0;            // kandidat saat hasil ragu-ragu
float   rmsKelas[JUMLAH_KELAS];          // untuk debug
bool    luarRentang[JUMLAH_KELAS];       // untuk debug

// =====================================================================
// LCD (opsional)
// =====================================================================
void tampilkan(const char* baris1, const char* baris2) {
#if USE_LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(baris1);
  lcd.setCursor(0, 1);
  lcd.print(baris2);
#else
  (void)baris1;
  (void)baris2;
#endif
}

// =====================================================================
// Pembacaan sensor
// =====================================================================
// Rata-rata lebar pulsa (mikrodetik). Nilai kecil = cahaya kuat.
float bacaKanal(bool s2, bool s3) {
  digitalWrite(PIN_S2, s2);
  digitalWrite(PIN_S3, s3);
  delay(15);                       // waktu filter berpindah

  unsigned long total = 0;
  uint8_t valid = 0;
  for (uint8_t i = 0; i < JUMLAH_SAMPEL; i++) {
    unsigned long p = pulseIn(PIN_OUT, LOW, 40000UL);
    if (p > 0) {
      total += p;
      valid++;
    }
  }
  if (valid == 0) return 40000.0;  // tidak ada sinyal
  return (float)total / valid;
}

void bacaFitur(float f[N_FITUR]) {
  float pr = bacaKanal(LOW,  LOW);    // merah
  float pg = bacaKanal(HIGH, HIGH);   // hijau
  float pb = bacaKanal(LOW,  HIGH);   // biru
  float pc = bacaKanal(HIGH, LOW);    // clear (tanpa filter)

  // ubah lebar pulsa menjadi frekuensi relatif (besar = terang)
  float fr = 1000000.0 / pr;
  float fg = 1000000.0 / pg;
  float fb = 1000000.0 / pb;
  float fc = 1000000.0 / pc;

  float total = fr + fg + fb;
  f[0] = fr / total;
  f[1] = fg / total;
  f[2] = fb / total;
  f[3] = log(fc);
}

// =====================================================================
// Rentang & klasifikasi
// =====================================================================
float sdEfektif(uint8_t k, uint8_t i) {
  return max(ref.sd[k][i], STD_MIN[i]);
}

// Return: -1 = tidak dikenali, -2 = ragu (kandA/kandB), 0 = kosong, 1..7 = nominal
int8_t klasifikasi(const float f[N_FITUR]) {
  float terbaik = 1e9, kedua = 1e9;
  int8_t a = -1, b = -1;

  for (uint8_t k = 0; k < JUMLAH_KELAS; k++) {
    float maxZ = 0, sum = 0;
    for (uint8_t i = 0; i < N_FITUR; i++) {
      float z = fabs(f[i] - ref.mean[k][i]) / sdEfektif(k, i);
      if (z > maxZ) maxZ = z;
      sum += z * z;
    }
    float rms = sqrt(sum / N_FITUR);
    rmsKelas[k] = rms;
    luarRentang[k] = (maxZ > K_RENTANG);
    if (luarRentang[k]) continue;          // di luar rentang kelas ini

    if (rms < terbaik) {
      kedua = terbaik; b = a;
      terbaik = rms;   a = k;
    } else if (rms < kedua) {
      kedua = rms;     b = k;
    }
  }

  if (a < 0) return -1;
  if (b >= 0 && kedua < terbaik * RASIO_MARGIN) {
    kandA = a;
    kandB = b;
    return -2;
  }
  return a;
}

// =====================================================================
// Laporan rentang
// =====================================================================
void cetakRentangPersen(const __FlashStringHelper* label, uint8_t k, uint8_t i) {
  float s  = sdEfektif(k, i);
  float lo = (ref.mean[k][i] - K_RENTANG * s) * 100.0;
  float hi = (ref.mean[k][i] + K_RENTANG * s) * 100.0;
  if (lo < 0) lo = 0;
  if (hi > 100) hi = 100;
  Serial.print(F("   "));
  Serial.print(label);
  Serial.print(F(" : "));
  Serial.print(lo, 1);
  Serial.print(F(" - "));
  Serial.print(hi, 1);
  Serial.println(F(" %"));
}

void cetakRentang(uint8_t k) {
  Serial.println(NAMA[k]);
  cetakRentangPersen(F("Merah"), k, 0);
  cetakRentangPersen(F("Hijau"), k, 1);
  cetakRentangPersen(F("Biru "), k, 2);

  float s  = sdEfektif(k, 3);
  float lo = exp(ref.mean[k][3] - K_RENTANG * s);
  float hi = exp(ref.mean[k][3] + K_RENTANG * s);
  Serial.print(F("   Terang: "));
  Serial.print(lo, 0);
  Serial.print(F(" - "));
  Serial.print(hi, 0);
  Serial.println(F(" (unit relatif)"));
}

// Dua kelas dianggap terpisah jika pada minimal satu fitur
// |selisih rata-rata| > K * (sd_a + sd_b), artinya rentangnya tidak beririsan.
void cekTumpangTindih() {
  bool ada = false;
  Serial.println();
  Serial.println(F("--- CEK PEMISAHAN ANTAR KELAS ---"));
  for (uint8_t a = 0; a < JUMLAH_KELAS; a++) {
    for (uint8_t b = a + 1; b < JUMLAH_KELAS; b++) {
      float terbaik = 0;
      for (uint8_t i = 0; i < N_FITUR; i++) {
        float sep = fabs(ref.mean[a][i] - ref.mean[b][i]) /
                    (sdEfektif(a, i) + sdEfektif(b, i));
        if (sep > terbaik) terbaik = sep;
      }
      if (terbaik < K_RENTANG) {
        ada = true;
        Serial.print(F("PERINGATAN: "));
        Serial.print(NAMA[a]);
        Serial.print(F(" dan "));
        Serial.print(NAMA[b]);
        Serial.print(F(" rentangnya tumpang tindih (skor pemisahan "));
        Serial.print(terbaik, 1);
        Serial.print(F(", minimal "));
        Serial.print(K_RENTANG, 1);
        Serial.println(F(")."));
      }
    }
  }
  if (!ada) {
    Serial.println(F("Bagus: semua pecahan rentangnya terpisah."));
  } else {
    Serial.println(F("Saran: kalibrasi ulang dengan 2-3 lembar tiap nominal,"));
    Serial.println(F("pastikan sensor di kotak tertutup & jarak uang tetap."));
  }
  Serial.println();
}

void laporanKalibrasi() {
  Serial.println();
  Serial.println(F("--- RENTANG WARNA TIAP KELAS ---"));
  for (uint8_t k = 0; k < JUMLAH_KELAS; k++) {
    cetakRentang(k);
  }
  cekTumpangTindih();
}

// =====================================================================
// Kalibrasi
// =====================================================================
void bersihkanSerial() {
  while (Serial.available()) Serial.read();
}

char tungguKarakter() {
  bersihkanSerial();
  while (!Serial.available()) { }
  char c = Serial.read();
  delay(50);
  bersihkanSerial();
  return c;
}

void kalibrasiKelas(uint8_t k) {
  float mean[N_FITUR] = {0, 0, 0, 0};
  float m2[N_FITUR]   = {0, 0, 0, 0};
  uint16_t n = 0;
  uint8_t lembar = 0;
  bool lagi = false;

  do {
    lembar++;
    Serial.print(F("Siapkan: "));
    Serial.print(NAMA[k]);
    if (k > 0) {
      Serial.print(F(" (lembar ke-"));
      Serial.print(lembar);
      Serial.print(F(")"));
    }
    Serial.println();
    if (k == 0) {
      Serial.println(F("   Kosongkan tempat uang, lalu kirim karakter apa saja."));
    } else {
      Serial.println(F("   Taruh uang di bawah sensor, kirim karakter apa saja,"));
      Serial.println(F("   lalu GESER uang pelan-pelan selama 5 detik."));
    }
    tampilkan("Kalibrasi:", NAMA[k]);
    tungguKarakter();

    unsigned long mulai = millis();
    while (millis() - mulai < DURASI_KALIBRASI) {
      float f[N_FITUR];
      bacaFitur(f);
      n++;
      for (uint8_t i = 0; i < N_FITUR; i++) {      // algoritma Welford
        float d = f[i] - mean[i];
        mean[i] += d / n;
        m2[i]   += d * (f[i] - mean[i]);
      }
    }
    Serial.println(F("   Pembacaan selesai."));

    lagi = false;
    if (k > 0 && lembar < MAKS_LEMBAR) {
      Serial.println(F("   Tambah lembar lain untuk nominal ini? kirim 'l' = tambah, lainnya = lanjut"));
      char c = tungguKarakter();
      lagi = (c == 'l' || c == 'L');
    }
  } while (lagi);

  for (uint8_t i = 0; i < N_FITUR; i++) {
    ref.mean[k][i] = mean[i];
    ref.sd[k][i]   = (n > 1) ? sqrt(m2[i] / (n - 1)) : 0.0;
  }
  Serial.print(F("   Tersimpan ("));
  Serial.print(n);
  Serial.println(F(" pembacaan)."));
  Serial.println();
}

void kalibrasi() {
  Serial.println();
  Serial.println(F("=== MODE KALIBRASI ==="));
  for (uint8_t k = 0; k < JUMLAH_KELAS; k++) {
    Serial.print(F("Langkah "));
    Serial.print(k + 1);
    Serial.print(F("/"));
    Serial.println(JUMLAH_KELAS);
    kalibrasiKelas(k);
  }

  ref.magic = MAGIC;
  EEPROM.put(0, ref);
  terkalibrasi = true;
  terakhir = -3;
  terumumkan = -3;
  hitungStabil = 0;

  Serial.println(F("Kalibrasi selesai & disimpan ke EEPROM."));
  laporanKalibrasi();
  tampilkan("Kalibrasi selesai", "Taruh uang...");
}

void hapusKalibrasi() {
  ref.magic = 0;
  EEPROM.put(0, ref);
  terkalibrasi = false;
  Serial.println(F("Data kalibrasi dihapus. Kirim 'k' untuk kalibrasi."));
}

// =====================================================================
// Perintah serial
// =====================================================================
void cekPerintah() {
  if (!Serial.available()) return;
  char c = Serial.read();
  bersihkanSerial();

  if (c == 'k' || c == 'K') {
    kalibrasi();
  } else if (c == 'p' || c == 'P') {
    if (terkalibrasi) laporanKalibrasi();
    else Serial.println(F("Belum ada kalibrasi."));
  } else if (c == 'd' || c == 'D') {
    modeDebug = !modeDebug;
    Serial.print(F("Mode debug: "));
    Serial.println(modeDebug ? F("AKTIF") : F("MATI"));
  } else if (c == 'r' || c == 'R') {
    hapusKalibrasi();
  }
}

// =====================================================================
// Umumkan hasil
// =====================================================================
void umumkan(int8_t hasil) {
  if (hasil == -1) {
    Serial.println(F("Tidak dikenali (di luar semua rentang). Ratakan uang / cek posisi."));
    tampilkan("Tidak dikenali", "Ratakan uang");
  } else if (hasil == -2) {
    Serial.print(F("Ragu-ragu: mirip "));
    Serial.print(NAMA[kandA]);
    Serial.print(F(" atau "));
    Serial.println(NAMA[kandB]);
    tampilkan("Ragu-ragu", "Ratakan uang");
  } else if (hasil == 0) {
    Serial.println(F("Tempat uang kosong."));
    tampilkan("Siap mendeteksi", "Taruh uang...");
  } else {
    Serial.print(F(">>> TERDETEKSI: "));
    Serial.println(NAMA[hasil]);
    tampilkan("Uang terdeteksi:", NAMA[hasil]);
  }
}

void cetakDebug(const float f[N_FITUR]) {
  Serial.print(F("[DEBUG] R="));
  Serial.print(f[0] * 100.0, 1);
  Serial.print(F("% G="));
  Serial.print(f[1] * 100.0, 1);
  Serial.print(F("% B="));
  Serial.print(f[2] * 100.0, 1);
  Serial.print(F("% Terang="));
  Serial.print(exp(f[3]), 0);
  Serial.print(F(" | skor tiap kelas (kecil=dekat, x=di luar rentang): "));
  for (uint8_t k = 0; k < JUMLAH_KELAS; k++) {
    Serial.print(k);
    Serial.print(luarRentang[k] ? F("x:") : F(":"));
    Serial.print(rmsKelas[k], 1);
    Serial.print(' ');
  }
  Serial.println();
}

// =====================================================================
// setup & loop
// =====================================================================
void setup() {
  pinMode(PIN_S0, OUTPUT);
  pinMode(PIN_S1, OUTPUT);
  pinMode(PIN_S2, OUTPUT);
  pinMode(PIN_S3, OUTPUT);
  pinMode(PIN_OUT, INPUT);

#if SKALA_FREKUENSI == 20
  digitalWrite(PIN_S0, HIGH);   // 20%
  digitalWrite(PIN_S1, LOW);
#else
  digitalWrite(PIN_S0, LOW);    // 2%
  digitalWrite(PIN_S1, HIGH);
#endif

  Serial.begin(9600);

#if USE_LCD
  lcd.init();
  lcd.backlight();
#endif

  Serial.println(F("Detektor Uang Rupiah v2 - TCS3200"));
  Serial.println(F("Perintah: k = kalibrasi, p = rentang, d = debug, r = hapus"));

  EEPROM.get(0, ref);
  terkalibrasi = (ref.magic == MAGIC);

  if (terkalibrasi) {
    Serial.println(F("Data kalibrasi ditemukan. Siap mendeteksi."));
    tampilkan("Siap mendeteksi", "Taruh uang...");
  } else {
    Serial.println(F("Belum ada kalibrasi. Kirim 'k' untuk memulai."));
    tampilkan("Belum kalibrasi", "Kirim 'k' serial");
  }
}

void loop() {
  cekPerintah();

  if (!terkalibrasi) {
    delay(200);
    return;
  }

  float f[N_FITUR];
  bacaFitur(f);
  int8_t hasil = klasifikasi(f);

  if (modeDebug) cetakDebug(f);

  // hasil harus sama beberapa kali berturut-turut agar stabil
  if (hasil == terakhir) {
    if (hitungStabil < 255) hitungStabil++;
  } else {
    terakhir = hasil;
    hitungStabil = 1;
  }

  if (hitungStabil >= PEMBACAAN_STABIL && terakhir != terumumkan) {
    terumumkan = terakhir;
    umumkan(terakhir);
  }

  delay(50);
}
