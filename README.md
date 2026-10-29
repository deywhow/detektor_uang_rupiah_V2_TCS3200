# \# Deteksi Nominal Uang Rupiah dengan TCS3200

# 

# \## Deskripsi

# Proyek ini menggunakan sensor warna TCS3200 dan Arduino Uno R3 untuk mendeteksi nominal uang Rupiah (Rp1.000 hingga Rp100.000) berdasarkan warna. Sistem ini dilengkapi dengan fitur kalibrasi white-balance dan klasifikasi menggunakan metode Euclidean Distance untuk akurasi yang lebih stabil.

# 

# \## Komponen yang Dibutuhkan

# \- 1x Arduino Uno R3

# \- 1x Sensor Warna TCS3200

# \- Kabel jumper secukupnya

# \- Kertas putih polos (untuk kalibrasi white-balance)

# \- Tabung/penutup sensor (opsional, untuk menghalangi cahaya ruangan)

# 

# \## Skema Koneksi Pin (TCS3200 ke Arduino)

# | Pin TCS3200 | Pin Arduino | Keterangan |

# |-------------|-------------|------------|

# | VCC         | 5V          | Daya       |

# | GND         | GND         | Ground     |

# | S0          | D8          | Scaling    |

# | S1          | D9          | Scaling    |

# | S2          | D10         | Filter     |

# | S3          | D11         | Filter     |

# | OUT         | D12         | Output     |

# 

# \## Cara Penggunaan

# 1\. Rangkai komponen sesuai skema koneksi di atas.

# 2\. Buka kodingan di Arduino IDE, lalu Upload ke board Arduino Uno.

# 3\. Buka \*\*Serial Monitor\*\* dengan baud rate `9600`.

# 4\. \*\*Kalibrasi Putih\*\*: Letakkan sensor di atas kertas putih, ketik `w` di Serial Monitor.

# 5\. \*\*Kalibrasi Hitam\*\*: Tutup sensor dengan benda gelap, ketik `b` di Serial Monitor.

# 6\. \*\*Deteksi Uang\*\*: Letakkan uang di bawah sensor. Sistem akan otomatis menampilkan nominal uang yang terdeteksi di Serial Monitor.

# 

# \## Lisensi

# Proyek ini dilisensikan di bawah \[MIT License](https://opensource.org/licenses/MIT).

