DemonVoice - Kompilieren (Windows)
1. Installiere Visual Studio 2022 (Workload "Desktopentwicklung mit C++") und CMake.
2. Im Ordner DemonVoice ein Terminal öffnen:
     cmake -B build
     cmake --build build --config Release
3. Das VST3 landet automatisch in C:\Program Files\Common Files\VST3\
   (ggf. Terminal als Administrator starten, sonst manuell aus
   build\DemonVoice_artefacts\Release\VST3 kopieren).
4. FL Studio: Optionen > Manage plugins > Find installed plugins.
5. Plugin auf einen Mixer-Insert laden und Stimme darüber schicken.
