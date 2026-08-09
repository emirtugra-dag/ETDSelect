# 📸 ETDSelect

<p align="center">
  <img src="assets/logo.ico" width="128" height="128" alt="ETDSelect Logo"><br>
  <b>Ultra-lightweight, zero-latency, tray-iconless screenshot & annotation tool for Windows</b>
</p>

<p align="center">
  <a href="https://github.com/emirtugra-dag/ETDSelect/blob/main/LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License: MIT"></a>
  <a href="https://microsoft.com/windows"><img src="https://img.shields.io/badge/Platform-Windows-lightgrey.svg" alt="Platform: Windows"></a>
  <a href="https://en.wikipedia.org/wiki/C%2B%2B17"><img src="https://img.shields.io/badge/C%2B%2B-17-blue.svg" alt="C++ Standard: 17"></a>
  <a href="#-language--dil"><img src="https://img.shields.io/badge/Language-TR%20%7C%20EN-brightgreen.svg" alt="Language: TR | EN"></a>
  <a href="https://github.com/emirtugra-dag/ETDSelect/releases/tag/v1.0.0"><img src="https://img.shields.io/badge/Release-v1.0.0-orange.svg" alt="Release: v1.0.0"></a>
</p>

---

## 💾 Direct Downloads / Doğrudan İndirme

- **⚙️ Setup / Kurulum Sihirbazı:** [Download ETDSelect_Setup.exe](releases/ETDSelect_Setup.exe) *(Recommended)*
- **🚀 Portable / Taşınabilir Sürüm:** [Download ETDSelect.exe](releases/ETDSelect.exe) *(No installation needed)*

---

## 🌐 Language / Dil
- [English](#-english)
- [Türkçe](#-türkçe)

---

## 🇬🇧 English

### 🌟 Key Features

- **⚡ Zero-Flicker Transition:** Seamless 0ms transition from capture overlay to editor. The screen never flickers or flashes black.
- **📋 Dual PNG & DIB Clipboard Support:** Images copy directly in `PNG`, `image/png`, `CF_DIB`, and `CF_BITMAP` formats for 100% compatibility with Discord, WhatsApp, Telegram, MS Word, Slack, Photoshop, and MS Paint.
- **🖐️ Move & 8-Point Resizing:** Drag selection using handles or move the entire selection with 🖐️ Move Tool, `Space + Drag`, or Middle Mouse Click.
- **🎨 Rich Annotation Tools:**
  - ✏️ Freehand Pen
  - ↗️ Arrow Tool
  - 🔲 Hollow & Filled Rectangles
  - 💬 Text Tool (Direct inline typing)
  - 🧩 Mosaic / Pixelate Blur (Sensorship)
  - 🖍️ Semi-Transparent Highlighter
  - 🧽 Eraser Tool
  - 🎨 12-Color Swatch Picker & Custom Palette
- **🌐 Automatic Bilingual Support:** Auto-detects System OS language (Turkish / English) or user-configurable in Settings.
- **🔔 Windows Toast Notifications:** Native notifications on copy and save.
- **🚫 Zero Tray Icon:** Stays 100% hidden in the background until invoked by hotkey.

### ⌨️ Keyboard Shortcuts

| Shortcut | Description |
| --- | --- |
| `Ctrl + Shift + S` / `PrtScn` | Start screenshot capture (Customizable) |
| `Space + Drag` | Move selection rectangle |
| `Ctrl + Z` | Undo last drawing action |
| `Ctrl + C` | Copy PNG to clipboard and close |
| `Ctrl + S` | Save image to Pictures folder and close |
| `ESC` | Cancel selection / Close |

### 🛠️ Build Instructions

To compile with MinGW GCC C++17:

```cmd
git clone https://github.com/emirtugra-dag/ETDSelect.git
cd ETDSelect
build.bat
```

---

## 🇹🇷 Türkçe

### 🌟 Öne Çıkan Özellikler

- **⚡ Sıfır Kırpışma (Zero-Flicker) Ekran Devri:** Seçim ekranından çizim ekranına geçerken 0ms kesintisiz görsel aktarım. Ekran asla kararmaz veya parlamaz.
- **📋 Gelişmiş PNG & DIB Pano Desteği:** Görseller doğrudan `PNG`, `image/png`, `CF_DIB` ve `CF_BITMAP` formatlarında panoya kopyalanır. Discord, WhatsApp, Telegram, Word, Slack, Photoshop ve Paint ile %100 uyumludur.
- **🖐️ Taşıma ve 8 Noktalı Boyutlandırma:** Seçim kutusunu Tutamaçlarından çekerek yeniden boyutlandırın veya 🖐️ Taşıma Aracı / `Space + Sürükle` / Orta Fare Tuşu ile ekranda serbestçe taşıyın.
- **🎨 Zengin Çizim Araçları:** Kalem, Ok, İçi Boş/Dolu Dikdörtgen, Metin Ekleme, Mozayik (Sansür), Fosforlu Kalem, Silgi ve 12 Renkli Palet.
- **🌐 Otomatik Türkçe & İngilizce Desteği:** Sistem dilini otomatik algılar veya Ayarlar menüsünden değiştirilebilir.
- **🔔 Windows Toast Bildirimleri:** Kopyalama ve kaydetme işlemlerinde Windows Toast bildirimleri gösterilir.
- **🚫 Tepsi Simgesiz (Zero Tray Icon):** Görev çubuğunu veya sistem tepsisini meşgul etmez.

### ⌨️ Kısayol Tuşları

| Kısayol | Açıklama |
| --- | --- |
| `Ctrl + Shift + S` / `PrtScn` | Ekran alıntısı başlat (Özelleştirilebilir) |
| `Space + Sürükle` | Seçim dikdörtgenini taşı |
| `Ctrl + Z` | Son çizim adımını geri al |
| `Ctrl + C` | Panoya PNG olarak kopyala ve kapat |
| `Ctrl + S` | Resimler klasörüne kaydet ve kapat |
| `ESC` | Seçimi iptal et / Kapat |

---

## 👤 Developer & License

- **Developer:** Emir Tuğra Dağ
- **License:** [MIT License](LICENSE)

*Disclaimer: This is a personal open-source project. The developer is under no obligation to provide ongoing updates or support. Use at your own risk.*

---

<sub>🤖 Bu proje **Gemini 3.6 Flash High** ve **Claude Opus 4.6** kullanılarak **Antigravity** üzerinde oluşturulmuştur.</sub><br>
<sub>🤖 This project was created on **Antigravity** using **Gemini 3.6 Flash High** and **Claude Opus 4.6**.</sub>
