# Reporte de compilación Android por partes (arm64-v8a)

Estrategia: una parte por vez, error rápido. Cada parte compila un solo
target CMake con el NDK (sin Gradle). Orden: 01-core → 02-gfx → 03-js →
04-media → 05-web → 06-webview → 07-browser (navegador entero, solo cuando
todo lo anterior esté verde). Todo `workflow_dispatch` (nada automático).

## Estado por parte

| # | Workflow | Target | Estado | Run | Nota |
|---|----------|--------|--------|-----|------|
| 01 | `android-01-core.yml` | `LibCore` | 🟢 verde | `34538245769` | AK + Unicode + Core |
| 02 | `android-02-gfx.yml` | `LibGfx` | 🟢 verde | `34539005924` | gráficos + decodificadores |
| 03 | `android-03-js.yml` | `LibJS` | 🟢 verde | `34539009088` | falló 1 vez por host-tools, ver errores difíciles |
| 04 | `android-04-media.yml` | `LibMedia` | 🟢 verde | `34539012121` | FFmpeg de vcpkg (video sist. pendiente #421) |
| 05 | `android-05-web.yml` | `LibWeb` | 🟢 verde | `34539014759` | la más pesada, pool compile=2 |
| 06 | `android-06-webview.yml` | `LibWebView` | 🟢 verde | `34539018540` | vista web + IPC |
| 07 | `android-07-browser.yml` | `ladybird` | 🟢 verde | `34620597237` | lib JNI nativa completa arm64 + sube las 4 .so |
| 08 | `android-08-js-asm.yml` | `LibJS` | 🟢 verde | `34548215391` | test regresión: ASM aarch64 verificado ELF (no Mach-O) |
| 09 | `android-09-sos.yml` | todas | ⬜ reserva | — | junta .so (la 07 ya sube las 4, 09 redundante por ahora) |
| APK | `android-build.yml` | apk debug | 🟢 verde | `34654059649` | `ladybird-apk` con assets locales (site-compat+about) |

Leyenda: 🟢 verde / 🟡 en curso / 🔴 rojo (ver causa + fix) / ⬜ no lanzado.

## Fixes del harness ya aplicados

1. `yes | sdkmanager` + `pipefail` volteaba el paso aunque las licencias se
   aceptaran (SIGPIPE en `yes`, exit 141). Ahora sdkmanager se verifica por
   efecto (mensaje `accepted` + carpetas instaladas), no por exit code.
2. `VCPKG_ROOT` apuntaba a `Build/vcpkg`, que el setup nunca crea (el preset
   Release crea `Build/vcpkg-release`). Se corrigió en `android-part` y en
   `android-build.yml`. El oficial siempre falló por esto.
3. Host manifest pedía simdutf 9.0.0 (ya no existe en la DB). Se llevó a
   simdutf 9.1.0 + baseline `7f3781e1`, igual que el `vcpkg.json` raíz.

Detalle de los errores difíciles: `Documentation/AndroidErroresDificiles.md`.
