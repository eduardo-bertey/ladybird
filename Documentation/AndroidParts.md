# Android por partes (arm64-v8a)

Compilar el navegador entero de una sola vez esconde qué parte falla.
Estos workflows compilan **una librería por vez** para Android arm64,
con error rápido: si una parte falla, el log muestra solo esa parte.
Cuando 01..06 están verdes, 07 compila la lib nativa completa
y recién ahí se arma el APK con `android-build.yml`.

> Nota: GitHub solo ejecuta workflows que están directamente en
> `.github/workflows/`. Por eso las partes se llaman `android-0X-*.yml`
> en vez de vivir en una subcarpeta. La lógica compartida está en
> `.github/actions/android-part/action.yml`.

## Orden (correr a mano, uno por vez)

| # | Workflow | Target | Qué valida |
|---|----------|--------|------------|
| 01 | `android-01-core.yml` | `LibCore` | AK + LibUnicode + LibCore |
| 02 | `android-02-gfx.yml` | `LibGfx` | Gráficos 2D, decodificadores de imagen |
| 03 | `android-03-js.yml` | `LibJS` | Motor JS (con host tools, ver #8672) |
| 04 | `android-04-media.yml` | `LibMedia` | Audio + FFmpeg de vcpkg (ver #421) |
| 05 | `android-05-web.yml` | `LibWeb` | Motor web (la más pesada) |
| 06 | `android-06-webview.yml` | `LibWebView` | Vista web + IPC + procesos |
| 07 | `android-07-browser.yml` | `ladybird` | Lib JNI nativa completa (puerta al APK) |

Cada parte reusa ccache/vcpkg de las anteriores, así que solo compila
lo nuevo. Todos son `workflow_dispatch`: Actions → elegir workflow → Run.

## Cómo funciona

Sin Gradle: se configura el `CMakeLists.txt` raíz con los mismos flags que
`UI/Android/build.gradle.kts` le pasa (`ANDROID_STL=c++_shared`,
`VCPKG_TARGET_ANDROID=ON`, `ENABLE_CRANELIFT_JIT=OFF`,
`LADYBIRD_HOST_LAYOUT_GENERATOR`), pero solo ABI `arm64-v8a`
(el APK oficial compila además `x86_64`) y se pide un solo target:
`cmake --build Build/android-arm64 --target <parte>`.
Las dependencias ya verdes salen de la caché.

## Problemas de Android documentados (repo oficial)

- `#8672` LibJS: el intérprete ASM **no es cross-compilable** (ASMIntGen
  necesita ejecutar herramientas del host). En Android se usa el intérprete
  C++ + `generate_interpreter_layout` compilado para el host. Las partes
  03/05/06/07 lo construyen solas (`need-host-tools`).
- `#421` Android: sin decodificación de video del sistema. En Android
  LibMedia enlaza el FFmpeg de **vcpkg**, no el del sistema; usar la
  Media NDK del sistema es trabajo pendiente. La parte 04 valida que
  LibMedia compile, no que el video funcione.
- `#484` Android: loop infinito en `EventLoopImplementationUnix`.
  Es de runtime, no frena la compilación por partes.
- El runner se queda sin memoria con LibWeb/LibJS en paralelo: el
  `CMakeLists.txt` raíz limita a 2 compilaciones (`CMAKE_JOB_POOL_COMPILE`).
- `build.gradle.kts` usa NDK `29.0.13599879` con FIXME: es una beta r29,
  reemplazar por una estable cuando exista.
- Historial oficial relevante: `ac28b52` (variables de entorno desde
  CMakeLists), `4be9c6d` (wrapper architecture en LibWeb),
  `2dc3e3d` (se eliminó el build viejo de host-tools Lagom),
  `96bad93` (LibWeb usa adblock-rust: es crate Rust, ojo cross-compile
  aarch64 si algún día falla la parte 05 por el lado Rust).
