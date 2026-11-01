#include <mod/amlmod.h>
#include <mod/logger.h>
#include <stdint.h>

MYMOD(net.kamu.gta6hud, GTA6 HUD, 1.0, KamuNama)

// Struct GTA SA
struct CRect { float left, bottom, right, top; };
struct CRGBA { uint8_t r, g, b, a; };

uintptr_t pGTASA = 0;
void* hGTASA = nullptr;

// Fungsi game
void (*DrawRect)(const CRect&, const CRGBA&) = nullptr;  // CSprite2d::DrawRect

// Hook CHud::Draw
void (*CHud_Draw_orig)() = nullptr;
void CHud_Draw_hook()
{
    // Gambar HUD asli dulu
    CHud_Draw_orig();

    // Contoh elemen custom: kotak semi-transparan gaya GTA 6
    if (DrawRect)
    {
        CRect rect = { 20.0f, 120.0f, 220.0f, 90.0f };
        CRGBA color = { 0, 0, 0, 120 };
        DrawRect(rect, color);
    }
}

ON_MOD_PRELOAD()
{
    logger->SetTag("GTA6HUD");
    logger->Info("Loading...");

    pGTASA = aml->GetLib("libGTASA.so");
    hGTASA = aml->GetLibHandle("libGTASA.so");

    if (!pGTASA || !hGTASA)
    {
        logger->Error("libGTASA tidak ditemukan");
        return;
    }

    DrawRect = (void(*)(const CRect&, const CRGBA&))
        aml->GetSym(hGTASA, "_ZN9CSprite2d8DrawRectERK5CRectRK5CRGBA");

    void* chudDraw = aml->GetSym(hGTASA, "_ZN4CHud4DrawEv");
    if (chudDraw)
        aml->Hook(chudDraw, (void*)CHud_Draw_hook, (void**)&CHud_Draw_orig);
    else
        logger->Error("CHud::Draw tidak ketemu");
}
