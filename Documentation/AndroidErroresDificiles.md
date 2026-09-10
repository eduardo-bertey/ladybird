# Errores difíciles de Android (y cómo se resolvieron)

## 1. `yes | sdkmanager` fallaba con licencias aceptadas

- **Síntoma:** `All SDK package licenses accepted` y acto seguido
  `Process completed with exit code 1`. Los 6 workflows fallaban en ~3 min.
- **Causa raíz:** `yes` escribe "y" infinitas; cuando sdkmanager termina y
  cierra stdin, `yes` muere con SIGPIPE (exit 141). Con `pipefail` activado
  (el runner de GitHub lo trae prendido por defecto, y mi `set -euxo pipefail`
  lo reforzaba) ese 141 voltea todo el paso aunque el trabajo esté hecho.
  Quitar `pipefail` de mi script no alcanzó porque el default del runner
  sigue vigente.
- **Fix:** no confiar en el exit code de sdkmanager; verificar por efecto:
  `grep "All SDK package licenses accepted"` + `test -d platform-tools` +
  `test -d ndk/<versión>`. Si el efecto está, se sigue; si no, se muestra el
  log y se falla de verdad. (`2>/dev/null` en `yes` para tapar el Broken pipe.)

## 2. `Could not find toolchain file: .../Build/vcpkg/...`

- **Síntoma:** el configure de CMake moría al instante con ese error (+ en
  cascada `Ninja not found`, compiladores no seteados).
- **Causa raíz:** el `setup` corre `./Meta/ladybird.py vcpkg --preset Release`
  y eso clona+compila vcpkg en `Build/vcpkg-release` (ver `VCPKG_PRESETS` en
  `Meta/ladybird.py`). `Build/vcpkg` no lo crea nadie: era una ruta vieja,
  anterior a los presets. El `android-build.yml` oficial la arrastraba, por
  eso **el oficial daba error siempre**.
- **Fix:** `VCPKG_ROOT` → `Build/vcpkg-release` en la acción `android-part`
  y en `android-build.yml`. Verificado en `Meta/Utils/build_vcpkg.py`: clona
  y bootstrappea el binario (con reintentos).

## 3. Host manifest pedía simdutf 9.0.0 inexistente

- **Síntoma:** partes con host tools (03-js, 05-web, 06-webview) fallaban en
  `vcpkg install --triplet arm64-osx` con
  `error: no version database entry for simdutf at 9.0.0`.
- **Causa raíz:** el manifest standalone de host tools tenía pins viejos
  (simdutf 9.0.0, baseline `40f3c709...`) y un comentario que mandaba al
  overlay-port del repo, que ya no trae simdutf. El `vcpkg.json` raíz actual
  fija simdutf **9.1.0** con baseline `7f3781e1`.
- **Fix:** pins del host iguales a los del raíz (simdutf 9.1.0, mimalloc
  2.2.7, fmt 12.2.0#1, fast-float 8.2.10#1, baseline `7f3781e1...`).

## Pendientes de documentar aquí

- El primer error *de compilación* real de cada librería (cuando aparezca).
- Issues oficiales que limitan Android: #8672 (ASM no cross-compilable),
  #421 (sin video del sistema), #484 (EventLoop).
