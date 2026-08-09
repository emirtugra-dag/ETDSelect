# 📸 ETDSelect

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows-lightgrey.svg)](https://microsoft.com/windows)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)

**Windows için Ultra Hafif, Sıfır Gecikmeli ve Tepsi Simgesiz Ekran Alıntısı Aracı**

**ETDSelect**, Windows 10/11 için Saf C++ Win32 API ve GDI+ ile yazılmış, hiçbir ağır kütüphane veya framework gerektirmeyen (~7 MB RAM, %0 CPU kullanımı) minimalist bir ekran alıntısı ve çizim aracıdır.

---

## 🌟 Öne Çıkan Özellikler

- **⚡ Sıfır Kırpışma (Zero-Flicker) Ekran Devri:** Seçim ekranından çizim ekranına geçerken 0ms kesintisiz görsel aktarım. Ekran asla kararmaz veya parlamaz.
- **📋 Gelişmiş PNG & DIB Pano Desteği:** Görseller doğrudan `PNG`, `image/png`, `CF_DIB` ve `CF_BITMAP` formatlarında panoya kopyalanır. Discord, WhatsApp, Telegram, Word, Slack, Photoshop ve Paint ile %100 uyumludur.
- **🖐️ Taşıma ve 8 Noktalı Boyutlandırma:** Seçim kutusunu Tutamaçlarından çekerek yeniden boyutlandırın veya 🖐️ Taşıma Aracı / `Space + Sürükle` / Orta Fare Tuşu ile ekranda serbestçe taşıyın.
- **🎨 Zengin Çizim Araçları:**
  - ✏️ Serbest Çizim (Kalem)
  - ↗️ Ok Çizimi
  - 🔲 İçi Boş ve Dolgulu Dikdörtgen
  - 💬 Metin Ekleme (Ekranda doğrudan düzenleme)
  - 🧩 Mozayik / Pikselleştirme (Sansürleme)
  - 🖍️ Şeffaf Fosforlu Kalem (Highlighter)
  - 🧽 Silgi
  - 🎨 12 Renkli Hızlı Renk Seçici & Özel Renk Paleti
- **🔔 Windows Bildirimleri:** Kopyalama ve kaydetme işlemlerinde Windows Toast bildirimleri gösterilir.
- **🚫 Tepsi Simgesiz (Zero Tray Icon):** Görev çubuğunu veya sistem tepsisini meşgul etmez.

---

## ⌨️ Kısayol Tuşları

| Kısayol | Açıklama |
| --- | --- |
| `Ctrl + Shift + S` / `PrtScn` | Ekran alıntısı başlat (Özelleştirilebilir) |
| `Space + Sürükle` | Seçim dikdörtgenini taşı |
| `Ctrl + Z` | Son çizim adımını geri al |
| `Ctrl + C` | Panoya PNG olarak kopyala ve kapat |
| `Ctrl + S` | Resimler klasörüne kaydet ve kapat |
| `ESC` | Seçimi iptal et / Kapat |

---

## 🛠️ Derleme talimatları (Build)

Projeyi MinGW GCC C++17 derleyicisi ile derlemek için:

1. MinGW-w64 derleyicisinin yüklü olduğundan emin olun.
2. Repoyu bilgisayarınıza klonlayın:
   ```bash
   git clone https://github.com/KULLANICI_ADI/ETDSelect.git
   cd ETDSelect
   ```
3. Proje dizinindeki `build.bat` dosyasını çalıştırın veya `make` komutunu verin:
   ```cmd
   build.bat
   ```

---

## 👤 Geliştirici & Lisans

- **Yapımcı:** Emir Tuğra Dağ
- **Lisans:** [MIT License](LICENSE)

*Açıklama: Bu proje kişisel olarak geliştirilmiş açık kaynaklı bir araçtır. Geliştirici projeye sürekli destek vermekle yükümlü değildir.*
