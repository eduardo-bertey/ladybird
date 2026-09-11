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

## 10. JNI WebView con APIs viejas (07-browser, lib ladybird)

- **Síntoma:** `WebViewImplementationNative.cpp`: `no viable overloaded '='`
  (on_load_start), `no member on_web_content_process_crash`, `MUST(uuid)` sin
  ErrorOr, `front_bitmap.bitmap` + `m_backup_bitmap` inexistentes,
  `Core::System` sin include, `make_ref_counted` sin match.
- **Causa raíz:** el modelo de `ViewImplementation` cambió
  (`NavigationListener`, uuid devuelve `String`, `SharedImageBuffer` en vez de
  `Bitmap` directo, ctor `WebContentClient` con 4 args) y el JNI quedó viejo.
  El PR #8504 solo cubre parte (include System, sacar el callback de crash,
  un arg de MouseEvent): se porteó el resto contra el árbol actual.
- **Fix:** listener de navegación (is_redirect=false + FIXME Java),
  uuid directo, pintado desde `shared_image_buffer`/`m_backup_shared_image_buffer`
  (protected, acceso OK), ctor cliente con `(No, 0, CrossProcessId{})`.

## 9. ANGLE sin display Android + CrashReport sin impl (07-browser)

- **Síntoma:** `undefined symbol: rx::DisplayAndroid::DisplayAndroid(...)` en
  `libexec/Compositor` + `undefined symbol: WebView::CrashReport::is_supported`
  (y `show_directory`, `symbolicate_frame`) en `libwebcontentservice.so`.
- **Causa A:** el overlay ANGLE de vcpkg no seteaba `is_android`, así que
  ANGLE no compilaba su código de display Android. Fix del PR #8504,
  verificado: `set(is_android TRUE)` en el `elseif (ANDROID)` del overlay.
- **Causa B:** en Android `AK_OS_LINUX` está definido (los stubs de
  `CrashReport.cpp` quedan fuera por `#if !defined(AK_OS_MACOS) &&
  !defined(AK_OS_LINUX)`) pero el `LINUX` de CMake es falso para Android, así
  que `CrashReportPOSIX.cpp` tampoco se compilaba. Fix:
  `elseif (LINUX OR ANDROID)` en `LibWebView/CMakeLists.txt`.

## 8. Servicios JNI viejos contra APIs nuevas de Services (07-browser)

- **Síntoma:** `RequestServerService.cpp.o: ConnectionFromClient.h:44: no
  matching constructor` y lo mismo en `WebContentService.cpp.o:56`. Además
  antes: `unknown type name 'curl_slist'` (ya fixeado con el include).
- **Causa raíz:** los Services cambiaron sus constructores
  (`RequestTransferLeaseMap&` nuevo en RequestServer, `enable_test_mode` en
  WebContent) y `UI/Android` quedó desactualizado (el oficial no compila
  Android en CI, nadie lo notó). El PR #8504 trae rewrites grandes de esos
  archivos pero contra otro árbol: se adaptó lo mínimo.
- **Fix:** pasar `RequestTransferLeaseMap` + mantener orden de 7 args en
  RequestServer; pasar `is_test_mode` en WebContent (igual que desktop).
- **Segunda vuelta (link):** `undefined symbol:
  RequestServer::g_default_certificate_path` y `g_resource_substitution_map`.
  En el árbol nuevo el path es `static` en `Resolver.cpp` (se usa
  `set_default_certificate_path`) y el mapa lo DEFINE el main del servicio
  (desktop lo hace en `Services/RequestServer/main.cpp:28`). El servicio
  Android hacía lo viejo: fix = definir el `OwnPtr` + llamar al setter
  (igual que el PR #8504, verificado contra nuestro árbol).

## 7. Dos Rust crates duplican shims + falta include de curl (07-browser)

- **Síntoma A (link):** `ld.lld: error: duplicate symbol:
  __rustc::__rust_alloc` (+dealloc/realloc/alloc_zeroed) entre
  `web_content_blocker_rust` (adblock, en `liblagom-web.a`) e
  `imagedecoders_rust`. Las partes 01-06 pasan porque `ar` no valida; solo el
  link del `.so` lo expone. En desktop no pasa porque esos crates caen en
  binarios distintos; en Android los servicios empaquetan más cosas juntas.
- **Fix A:** `-Wl,--allow-multiple-definition` solo-Andro
  en `CMakeLists.txt` (los shims son idénticos, gana el primero).
- **Síntoma B (compile):** `RequestServerService.cpp` (JNI Android):
  `ConnectionFromClient.h:38: unknown type name 'curl_slist'`. El header
  usaba tipos curl sin incluir el header (en desktop llegaba transitivo).
- **Fix B:** `#include <curl/curl.h>` en
  `Services/RequestServer/ConnectionFromClient.h` (el target ya linkeaba
  `CURL::libcurl`).

## 6. Rust compilaba para el host y envenenaba el link (adblock-rust)

- **Síntoma:** 05-web verde pero 07-browser roja en el link:
  `ld.lld: error: lib/liblagom-web.a: archive member
  '42c5d46f287ea510-lse_cas16_acq.o' is neither ET_REL nor LLVM bitcode`
  (decenas de `lse_cas*`). El `ar` no valida formato, por eso la 05 pasó y
  la 07 explotó.
- **Causa raíz:** objetos con hash (`XXXX-*.o`) = `compiler_builtins` de Rust.
  `rust_crate.cmake` fijaba `RUST_TARGET_TRIPLE` al triple del **host**
  siempre, así que adblock-rust salía Mach-O (macOS) dentro de un link ELF.
  Referencia: PR `LadybirdBrowser/ladybird#8504` ("Fix Android Compilation",
  cerrado sin mergear) que mapea ABIs Android→Rust (`CMAKE_ANDROID_ARCH_ABI`).
  Ojo: ese PR no distingue binarios host (flapc DEBE seguir en host porque se
  ejecuta en build time); nuestro fix sí.
- **Fix:** en `_rust_crate_common_setup`, flag `HOST_TOOL`: `build_rust_binary`
  (flapc) y `test_rust_crate` usan host; `import_rust_crate` usa el triple del
  device según ABI (4 ABIs como el PR). Más `rustup target add
  aarch64-linux-android` (y `x86_64` para el APK por Gradle) en CI.

## 5. El interprete ASM en ARM64 (lo que rompia el compilador anterior)

- **Contexto:** en Android arm64 el ASM **sí** está activo
  (`FLAP_ARCH=aarch64` en `Libraries/LibJS/CMakeLists.txt`): flapc (binario
  Rust compilado para el host) genera `interpreter_aarch64.S` y
  `generate_interpreter_layout` (host) produce `layout.conf` con los offsets.
  Sin esa cadena el cross-compile muere (issue #8672).
- **Test de regresión:** `android-08-js-asm.yml` compila LibJS y verifica que
  existan `interpreter_aarch64.S` + `layout.conf`, que el `.S` se ensambló
  (`.S.o`) y que ese objeto está dentro de la estática LibJS (vía `llvm-ar`).
  Si alguien rompe flapc/layout/ASM, da rojo aunque el resto compile.

## 4. LibJS corta TODO el configure sin generador del host

- **Síntoma:** `CMake Error at Libraries/LibJS/CMakeLists.txt:358:
  Cross-compiling requires a host-built generate_interpreter_layout`,
  aunque el target pedido fuera LibGfx/LibCore (nada que ver con JS).
- **Causa raíz:** el `cmake -S .` configura el proyecto entero,
  incondicionalmente. El intento de optimizar salteando host tools para las
  partes que "no lo necesitaban" estaba mal: sin
  `-DLADYBIRD_HOST_LAYOUT_GENERATOR` el configure muere antes de compilar
  nada.
- **Fix:** host tools SIEMPRE en la acción compartida (se sacó el flag
  `need-host-tools`). Cuestan ~10 min por parte pero son obligatorias.
