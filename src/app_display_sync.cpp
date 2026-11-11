// app_display_sync.cpp -- start an emulator in the bootloader's display mode.
//
// The emulators built on pico_shared keep their own display mode (HDMI or DVI)
// in their own settings file, /settings_<TAG>.dat, as flags.useDVIModeForHDMI,
// and start in it. Before the bootloader flashes or starts one, it sets that
// flag to its own mode, so the emulator comes up in the mode the bootloader
// runs in. The native ports (Doom, Duke Nukem 3D, Quake II, Diablo) are not
// handled here: they read DISPLAY= from /boot.txt themselves when the
// bootloader started them.
//
// The file is a raw struct settings, which pico_shared's loadsettings() only
// accepts with exactly sizeof(struct settings) bytes and its own
// SETTINGS_VERSION. The bootloader touches only a file it understands in the
// same way, i.e. one written by an emulator built from the same settings
// layout as the bootloader's pico_shared. Anything else is logged and left
// alone: the emulator resets a damaged file itself, and one written with a
// newer layout is not this bootloader's to rewrite.
//
// What is changed, and why:
//   - useDVIModeForHDMI, and useExtAudio with it, exactly as the emulator's
//     own Display Mode toggle does (pico_shared menu.cpp): DVI carries no
//     audio, so the sound goes to the board's audio output instead. Only when
//     the mode actually differs, so a file already in HDMI keeps whatever
//     audio output the user picked.
//   - useExtAudio, for a file already in DVI that has it off. The emulators
//     send their sound to the cable unless it is set (or headphones are
//     plugged in), and in DVI the cable carries none: the emulator would
//     start silent.
//   - currentDir, when it no longer opens. loadsettings() resets the whole
//     struct in RAM in that case, which would throw the display mode away
//     again for that session.
//   - A missing file (a fresh card: the emulators create theirs only when a
//     setting is saved) is created when the emulator would otherwise start in
//     the other mode than the bootloader's, from the same defaults the
//     emulator itself would use.

#include <cstdio>
#include <cstring>
#include <strings.h>

#include "ff.h"
#include "FrensHelpers.h"
#include "settings.h"

#include "app_display_sync.h"

#if HSTX

#define LOG(fmt, ...) printf("[bootLoader] " fmt "\n", ##__VA_ARGS__)

// The layout that the emulators of this release write (pico_shared 5bdd883).
// If this fails after a pico_shared update, struct settings has changed: check
// that the useDVIModeForHDMI / useExtAudio / currentDir handling below still
// matches what the emulators built from that pico_shared expect.
static_assert(sizeof(struct settings) == 280, "struct settings changed, see above");

namespace {

struct AppTag {
    const char *prog_name;   // binary_info program name (CMakeLists projectname)
    const char *tag;         // /settings_<tag>.dat, pico_shared settings.cpp
    const char *rom_dir;     // the currentDir pico_shared's resetsettings() gives it
    bool nes_screen_mode;    // resetsettings(): NES starts with scanlines, 8:7
};

const AppTag kApps[] = {
    {"piconesPlus",     "NES",  "/roms/NES",    true},
    {"picogenesisPlus", "MD",   "/roms/MD",     false},
    {"picosnesPlus",    "SNES", "/roms/SNES",   false},
    {"picosmsPlus",     "SMS",  "/roms/SMS",    false},
    {"picopcePlus",     "PCE",  "/roms/PCE",    false},
    {"PicoPeanutGB",    "GB",   "/roms/GB",     false},
    {"picoPacPlus",     "O2E",  "/roms/O2E",    false},
    {"pico994A",        "TI99", "/roms/TI99",   false},
    {"picoC64",         "C64",  "/roms/C64",    false},
    // The arcade games share one settings file.
    {"picoOutRun",      "ARC",  "/roms/arcade", false},
    {"picoPhoenix",     "ARC",  "/roms/arcade", false},
    {"picoMoonCresta",  "ARC",  "/roms/arcade", false},
    {"picoGalagino",    "ARC",  "/roms/arcade", false},
    {"picoDragonsLair", "ARC",  "/roms/arcade", false},
    {"pico1943",        "ARC",  "/roms/arcade", false},
    {"picoSalamander",  "ARC",  "/roms/arcade", false},
};

// What the emulators start in without a settings file (pico_shared
// resetsettings(); every emulator builds with ENABLEDVI=1).
constexpr bool kEmulatorDefaultDvi = (HW_CONFIG == 13);

// FatFs objects off the stack: FIL is ~600 bytes.
struct Scratch {
    FIL fil;
    DIR dir;
    struct settings s;
};

const AppTag *findApp(const char *prog_name)
{
    if (!prog_name || !prog_name[0]) return nullptr;
    for (const AppTag &a : kApps) {
        if (strcasecmp(a.prog_name, prog_name) == 0) return &a;
    }
    return nullptr;
}

bool dirOpens(DIR *dir, const char *path)
{
    if (f_opendir(dir, path) != FR_OK) return false;
    f_closedir(dir);
    return true;
}

// currentDir as loadsettings() will accept it: the emulator's own ROM folder
// when there is one, else the root.
void setRomDir(Scratch *sc, const AppTag *app)
{
    const char *dir = dirOpens(&sc->dir, app->rom_dir) ? app->rom_dir : "/";
    snprintf(sc->s.currentDir, sizeof(sc->s.currentDir), "%s", dir);
}

void setDisplay(struct settings *s, bool dvi)
{
    s->flags.useDVIModeForHDMI = dvi;
    s->flags.useExtAudio = dvi;   // as the emulator's own Display Mode toggle
}

bool writeAll(FIL *fil, const struct settings *s)
{
    UINT bw = 0;
    FRESULT fr = f_write(fil, s, sizeof(*s), &bw);
    FRESULT fc = f_close(fil);
    return fr == FR_OK && fc == FR_OK && bw == sizeof(*s);
}

void syncExisting(Scratch *sc, const AppTag *app, const char *path, bool dvi)
{
    UINT br = 0;
    if (f_size(&sc->fil) != sizeof(sc->s)
            || f_read(&sc->fil, &sc->s, sizeof(sc->s), &br) != FR_OK
            || br != sizeof(sc->s) || sc->s.version != SETTINGS_VERSION) {
        LOG("display sync: %s is not a version %d settings file of %u bytes, left alone",
            path, SETTINGS_VERSION, (unsigned)sizeof(sc->s));
        f_close(&sc->fil);
        return;
    }

    bool changed = false;
    if ((bool)sc->s.flags.useDVIModeForHDMI != dvi) {
        setDisplay(&sc->s, dvi);
        changed = true;
    } else if (dvi && !sc->s.flags.useExtAudio) {
        LOG("display sync: %s: DVI without external audio, switching it on", path);
        sc->s.flags.useExtAudio = 1;
        changed = true;
    }
    if (!dirOpens(&sc->dir, sc->s.currentDir)) {
        LOG("display sync: %s: folder %s is gone", path, sc->s.currentDir);
        setRomDir(sc, app);
        changed = true;
    }
    if (!changed) {
        f_close(&sc->fil);
        LOG("display sync: %s already %s", path, dvi ? "DVI" : "HDMI");
        return;
    }
    // In place, same size: a power cut cannot leave a short file behind.
    if (f_lseek(&sc->fil, 0) != FR_OK || !writeAll(&sc->fil, &sc->s)) {
        f_close(&sc->fil);
        LOG("display sync: writing %s FAILED", path);
        return;
    }
    LOG("display sync: %s written: %s, external audio %s (folder %s)", path,
        dvi ? "DVI" : "HDMI", sc->s.flags.useExtAudio ? "on" : "off", sc->s.currentDir);
}

void createFile(Scratch *sc, const AppTag *app, const char *path, bool dvi)
{
    if (dvi == kEmulatorDefaultDvi) {
        LOG("display sync: no %s, the emulator starts in %s by default",
            path, dvi ? "DVI" : "HDMI");
        return;
    }
    // The emulator's own defaults. resetsettings() gives the NES values here
    // (the bootloader's settings type is MULTI, which counts as NES), so the
    // per-emulator ones are set afterwards.
    memset(&sc->s, 0, sizeof(sc->s));
    FrensSettings::resetsettings(&sc->s);
    sc->s.screenMode = app->nes_screen_mode ? ScreenMode::SCANLINE_8_7 : ScreenMode::NOSCANLINE_1_1;
    setRomDir(sc, app);
    setDisplay(&sc->s, dvi);
    if (f_open(&sc->fil, path, FA_WRITE | FA_CREATE_NEW) != FR_OK || !writeAll(&sc->fil, &sc->s)) {
        LOG("display sync: creating %s FAILED", path);
        return;
    }
    LOG("display sync: %s created, %s (folder %s)", path, dvi ? "DVI" : "HDMI",
        sc->s.currentDir);
}

} // namespace

void appDisplaySync(const char *prog_name, bool dvi)
{
    const AppTag *app = findApp(prog_name);
    if (!app) return;   // a native port, ColecoJam, or not a pico_shared emulator

    char path[24];
    snprintf(path, sizeof(path), "/settings_%s.dat", app->tag);

    Scratch *sc = (Scratch *)Frens::f_malloc(sizeof(Scratch));
    if (!sc) {
        LOG("display sync: out of memory, %s left alone", path);
        return;
    }
    FRESULT fr = f_open(&sc->fil, path, FA_READ | FA_WRITE | FA_OPEN_EXISTING);
    if (fr == FR_OK) {
        syncExisting(sc, app, path, dvi);
    } else if (fr == FR_NO_FILE) {
        createFile(sc, app, path, dvi);
    } else {
        LOG("display sync: cannot open %s (FatFs error %d)", path, (int)fr);
    }
    Frens::f_free(sc);
}

#else

void appDisplaySync(const char *prog_name, bool dvi)
{
    (void)prog_name;
    (void)dvi;
}

#endif // HSTX
