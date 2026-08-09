#include "i18n.h"
#include "app.h"
#include <cstring>

struct StringEntry {
    const char* key;
    const wchar_t* tr;
    const wchar_t* en;
};

static const StringEntry g_strings[] = {
    // Toolbar Tooltips
    { "TOOL_MOVE",        L"Seçimi Taşı (Uzay / Orta Fare)\nSpace veya orta fare ile sürükleyin.", L"Move Selection (Space / Middle Click)\nDrag with Space bar or middle mouse button." },
    { "TOOL_DRAW",        L"Serbest Çizim (Kalem)\nSerbest çizim kalemi.",                       L"Freehand Draw (Pen)\nDraw freehand lines." },
    { "TOOL_ARROW",       L"Ok Çizimi\nİşaret oku çizin.",                                     L"Arrow Tool\nDraw directional arrow." },
    { "TOOL_RECT_HOLLOW", L"Dikdörtgen\nİçi boş dikdörtgen çizin.",                            L"Rectangle Tool\nDraw hollow rectangle." },
    { "TOOL_RECT_FILLED", L"Dolu Dikdörtgen\nİçi dolu şeffaf dikdörtgen çizin.",                L"Filled Rectangle\nDraw filled semi-transparent rectangle." },
    { "TOOL_TEXT",        L"Metin Ekle\nTıklayıp metin yazın.",                                L"Text Tool\nClick to type text." },
    { "TOOL_MOSAIC",      L"Mozayik (Sansür)\nHassas alanları pikselleştirin.",                 L"Mosaic / Blur\nPixelate sensitive areas." },
    { "TOOL_HIGHLIGHTER", L"Fosforlu Kalem\nVurgulayıcı şeffaf kalem.",                       L"Highlighter\nTransparent marker." },
    { "TOOL_ERASER",      L"Silgi\nÇizimleri temizleyin.",                                    L"Eraser Tool\nErase drawn annotations." },
    { "TOOL_LASSO",       L"Kement Seçimi\nKement ile alan seçin.",                            L"Lasso Tool\nFreehand lasso selection." },
    { "TOOL_COLOR",       L"Renk Seçimi\nÇizim rengini değiştirin.",                           L"Select Color\nChange active drawing color." },
    { "TOOL_THICKNESS",   L"Çizim Kalınlığı\nÇizim çizgi kalınlığını ayarlayın.",              L"Line Thickness\nAdjust drawing thickness." },
    { "TOOL_SETTINGS",    L"Ayarlar\nKısayol ve sistem ayarları.",                             L"Settings\nShortcut and system settings." },
    { "TOOL_SAVE",        L"Kaydet (Ctrl+S)\nResimler klasörüne kaydet ve kapat.",            L"Save (Ctrl+S)\nSave to Pictures folder and close." },
    { "TOOL_COPY",        L"Panoya Kopyala (Ctrl+C)\nPanoya PNG olarak kopyala ve kapat.",     L"Copy to Clipboard (Ctrl+C)\nCopy PNG to clipboard and close." },
    { "TOOL_CLOSE",       L"İptal (ESC)\nSeçim alanını kapat.",                                 L"Cancel (ESC)\nClose selection window." },

    // Context Menu
    { "MENU_ABOUT",       L"Uygulama Hakkında",                                                L"About ETDSelect" },
    { "MENU_HELP",        L"Nasıl Kullanılır?",                                                L"How to Use" },
    { "MENU_SETTINGS",    L"Ayarlar",                                                          L"Settings" },
    { "MENU_CLOSE_SEL",   L"Seçimi Kapat",                                                     L"Close Selection" },
    { "MENU_EXIT",        L"Uygulamayı Arka Plandan Kapat",                                   L"Exit Application" },

    // Settings Window
    { "SET_TITLE",        L"ETDSelect - Ayarlar",                                              L"ETDSelect - Settings" },
    { "SET_FIRST_TITLE",  L"ETDSelect - İlk Kurulum",                                          L"ETDSelect - Initial Setup" },
    { "SET_WELCOME",      L"ETDSelect'e Hoş Geldiniz! Lütfen kısayolu ve dili ayarlayın.",     L"Welcome to ETDSelect! Please configure shortcut and language." },
    { "SET_HOTKEY_LABEL", L"Global Ekran Alıntısı Kısayolu:",                                  L"Global Screenshot Hotkey:" },
    { "SET_KEY_LABEL",    L"Tuş:",                                                             L"Key:" },
    { "SET_LANG_LABEL",   L"Dil / Language:",                                                  L"Language / Dil:" },
    { "SET_AUTOSTART",    L"Windows açılışında otomatik başlat",                              L"Launch automatically on Windows startup" },
    { "SET_SAVE_BTN",     L"Kaydet",                                                           L"Save" },
    { "SET_CANCEL_BTN",   L"İptal",                                                            L"Cancel" },

    // Help Window
    { "HELP_TITLE",       L"ETDSelect - Kullanım Kılavuzu",                                    L"ETDSelect - User Guide" },

    // About Window
    { "ABOUT_TITLE",      L"ETDSelect - Hakkında",                                             L"ETDSelect - About" },

    // Notifications
    { "NOTIF_COPY",       L"Ekran alıntısı panoya kopyalandı!",                                L"Screenshot copied to clipboard!" },
    { "NOTIF_SAVE",       L"Ekran alıntısı başarıyla kaydedildi!",                            L"Screenshot saved successfully!" },

    // Language Options
    { "LANG_AUTO",        L"Otomatik (Auto OS)",                                              L"Auto (System OS)" },
    { "LANG_TR",          L"Türkçe (Turkish)",                                                L"Türkçe (Turkish)" },
    { "LANG_EN",          L"English (İngilizce)",                                             L"English (İngilizce)" },
};

const wchar_t* I18n::Get(const char* key) {
    int lang = App::GetInstance().GetSettings().GetEffectiveLanguage(); // 1 = TR, 2 = EN
    size_t count = sizeof(g_strings) / sizeof(g_strings[0]);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(g_strings[i].key, key) == 0) {
            return (lang == 1) ? g_strings[i].tr : g_strings[i].en;
        }
    }
    return L"";
}
