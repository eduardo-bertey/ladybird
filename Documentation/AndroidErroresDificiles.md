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

## 21. platform_init pisaba el resource root: LA causa raiz (runtime)

- **Síntoma:** `stat ENOENT` en site-compat con los archivos PRESENTES.
  El DIAG (`uri=... root=/system/share/Lagom`) lo probo: el root NO era
  `files/` sino `/system/share/Lagom`.
- **Causa raíz:** `initNativeCode` pone bien el root (`files/`), pero
  `Application::create` → ctor → `platform_init()` lo SOBREESCRIBE con el
  calculo desktop (sale del path de `app_process` → `/system/share/Lagom`)
  e instala `ResourceImplementation` con eso. Todos los `resource://` rotos.
  El log "Set resource dir" mentia (se imprime antes del pisoton).
- **Fix:** `#if !defined(AK_OS_ANDROID)` alrededor de la asignacion en
  `platform_init` (`Libraries/LibWebView/Utilities.cpp`): en Android manda
  el root de `initNativeCode`, pero igual se instala el impl.

## 22. Profile moria en mkdir /run/user (XDG incompleto)

- **Síntoma:** `VERIFICATION FAILED: !is_error()` en `initNativeCode`,
  sin ningun warning previo.
- **Causa raíz:** el JNI solo ponia `XDG_CONFIG_HOME`+`XDG_DATA_HOME`.
  `runtime_directory()` caia a `/run/user/UID` (`AK_OS_LINUX` tambien esta
  definido en Android) y `Profile::create_legacy` → `mkdir /run/user/UID`
  → `EACCES`. El cache (`~/.cache`, sin `HOME`) era el siguiente cadaver.
- **Fix:** en `initNativeCode` poner `XDG_CACHE_HOME`, `XDG_RUNTIME_DIR`,
  `XDG_STATE_HOME`, `HOME` y `TMPDIR` bajo `user_dir`, y pre-crear los
  6 subdirs. Kotlin: `getExternalFilesDir` con fallback a `filesDir`.

## 23. Segundo EventLoop en el mismo thread

- **Síntoma:** `VERIFICATION FAILED: !current_event_loop()` en
  `LibCore/EventLoop.cpp:33` (el ctor, uno por thread).
- **Causa raíz:** el JNI crea `s_main_event_loop`, y como en Android
  `coordinate_browser_process=false`, `m_event_loop` llegaba nulo a
  `Application.cpp:715` y `create_platform_event_loop()` hacia `new EventLoop`
  → segundo loop en el thread → SIGTRAP.
- **Fix:** si `Core::EventLoop::is_running()`, reusar `current()` en vez de
  crear otro (desktop intacto: ahi no hay loop previo).

## 20. Android restauraba datos viejos en cada reinstall (BackupManager)

- **Síntoma:** "clean installs" que no eran limpios: en el log
  `BackupManagerService: restoreAtInstall pkg=org.serenityos.ladybird` —
  el sistema restauraba `files/` (layouts viejos) en cada reinstall.
- **Mecanismo exacto (probado con DIAG):** el restore corre EN PARALELO con
  el primer arranque: Java ve `testFile exists=true`, el restore borra/reescribe
  el árbol, y 2ms después el `open()` nativo da ENOENT. Carrera imposible de
  ganar desde la app.
- **Fix:** `allowBackup="false"` (más extracción versionada del error 19:
  doble defensa). Sin restore no hay carrera.

## 19. Updates con datos viejos no se curaban (pm install -r)

- **Síntoma:** tras update (`pm install -r` conserva datos), mezcla de layouts
  viejos/nuevos y crashes confusos; imposible distinguir qué APK tiene
  instalado (todos se llaman igual, mismo versionCode).
- **Fix:** extracción versionada en `LadybirdActivity.kt`: prefs
  `assets_version` (2 = plano). Si cambia o falta el testigo, re-extrae y
  guarda la versión. Los updates se autocuran sin desinstalar.

## 18. Runtime: VERIFY por site-compatibility faltante (initNativeCode)

- **Síntoma (en el celu):** `Unable to load site compatibility data: stat: No
  such file or directory` + `VERIFICATION FAILED: !is_error()` (SIGTRAP) en
  `initNativeCode`. Todo lo anterior ya anda (nativo, SDL, assets).
- **Causa raíz (doble):** 1) `resource://ladybird/site-compatibility`
  (WebCompat/*.json) no viajaba en Android (solo 4 carpetas). 2) El zip
  Android tenía un nivel `res/` de más: desktop instala plano
  (`share/Lagom/{ladybird,fonts...}`) pero el asset-bundle espejaba
  `Base/res/`, así que `resource://ladybird/X` buscaba en `files/ladybird/X`
  y estaba en `files/res/ladybird/X`.
- **Fix:** layout plano como desktop en los dos lados (zip local del APK y
  `AndroidExtras.cmake`: copiar a `asset-bundle/` + WebCompat a
  `asset-bundle/ladybird/site-compatibility/`), y checks
  `res/icons/` → `icons/` en `LadybirdActivity.cpp/.kt`.

## 17. Runtime: falta ladybird-assets.zip (NoSuchFileException)

- **Síntoma (en el celu):**
  `NoSuchFileException: Invalid Assets, this won't work/ladybird-assets.zip`
  en `LadybirdActivity.onCreate`.
- **Causa raíz:** el zip lo genera CMake (`copy-assets`, dependencia del
  target `ladybird`) en el source tree. Con prebuilt Gradle no corre CMake y
  el zip nunca llega al APK.
- **Fix:** la 07 lo sube como artifact `ladybird-android-assets` y el
  workflow del APK lo baja a `src/main/assets/`. Más `.gitignore` para no
  commitearlo.

## 16. Runtime: falta SDLActivity java (SIGABRT en el celu)

- **Síntoma (en el celu):**
  `ClassNotFoundException: Didn't find class "org.libsdl.app.SDLActivity"`
  → `JNI DETECTED ERROR` → SIGABRT en `LadybirdActivity.<clinit>` (línea 120,
  `System.loadLibrary`).
- **Causa raíz:** `LibWeb` linkea SDL3 estático; su `JNI_OnLoad` (código C de
  SDL para Android) hace `FindClass` de su `SDLActivity` para el contexto.
  La clase Java no existía en la app (nadie la vendoreó; el oficial no corre
  Android y no lo notó).
- **Fix:** vendorear los 11 `.java` de `org.libsdl.app` de SDL **3.2.28**
  (misma versión del vcpkg) en `UI/Android/src/main/java/`. No hace falta
  declararla en el manifest (no se lanza, solo debe existir la clase).

## 15. Runtime: falta libc++_shared.so en el APK (UnsatisfiedLinkError)

- **Síntoma (en el celu):**
  `dlopen failed: library "libc++_shared.so" not found: needed by
  libLadybird.so` en `LadybirdActivity.<clinit>`.
- **Causa raíz:** con `ANDROID_STL=c++_shared` hay que empaquetar el STL.
  Antes lo copiaba Gradle solo vía externalNativeBuild; con prebuilt nadie lo
  copia.
- **Fix:** `cp` de `libc++_shared.so` del NDK
  (`toolchains/llvm/prebuilt/*/sysroot/usr/lib/aarch64-linux-android/`) a
  `jniLibs/arm64-v8a/` en el workflow, junto al strip.

## 14. APK muere por OOM compilando 2 ABIs a la vez (Android Build)

- **Síntoma:** `buildCMakeDebug[arm64-v8a] FAILED` con `libc++abi:` vacío
  compilando TUs pesados de LibWeb (`[2414/2862]`). Es el OOM que describe el
  comentario del pool en `CMakeLists.txt`.
- **Causa raíz:** el pool `compile=2` vale POR invocación ninja, pero Gradle
  corre arm64-v8a + x86_64 en paralelo (2 pools = 4 clang pesados + cargo +
  vcpkg + Gradle en 14 GB) → OOM. Las partes (1 ABI) no lo sufren.
- **Fix/estrategia:** no pelear el OOM: el APK pasa a prebuilt
  (`android-09-sos.yml` junta las .so, `android-build.yml` solo empaqueta
  Java + `.so`, flag `-PusePrebuiltNative`, solo arm64-v8a por ahora).
  Antes también murió por disco lleno (vcpkg+nativo x2 en un runner):
  cachés compartidas + `--clean-after-build` mitigan, pero prebuilt lo evita.

## 13. Gradle híbrido roto por el merge (Android Build/APK)

- **Síntoma:** `assembleDebug` muere en 1 min:
  `build.gradle.kts:25: Unresolved reference: cacheDir` (+ `sourceDir`).
- **Causa raíz:** el oficial (`ac28b52`) eliminó `var buildDir/cacheDir/sourceDir`
  y los args `-DLADYBIRD_CACHE_DIR`/`-DVCPKG_ROOT`, reemplazados por
  `-DLADYBIRD_VCPKG_TYPE=release` (CMake deriva todo en `environment.cmake`).
  El merge con la rama eduardo dejó un híbrido: usa las vars pero sin
  definirlas.
- **Fix:** alinear con el oficial (`LADYBIRD_VCPKG_TYPE=release`, se mantiene
  `ENABLE_CRANELIFT_JIT=OFF` explícito). De paso se elimina el
  `VCPKG_ROOT=Build/vcpkg` viejo del path Gradle.
- **Mejora:** la 07 ahora sube `libladybird.so` como artifact
  (`libladybird-arm64-so`), antes se perdía con el runner.

## 12. Timer JNI posteaba al impl viejo (07-browser, lib ladybird)

- **Síntoma:** al borrar `post_event` (error 11),
  `TimerExecutorService.cpp:28: no member named 'post_event'` + tipos
  incompletos `EventReceiver`.
- **Causa raíz:** el timer corre en otro hilo Java y posteaba al impl. Modelo
  nuevo: `ThreadEventQueue::post_event(receiver*, type)` con mutex + aviso
  `did_post_event` (wake por pipe, ya cableado).
- **Fix:** postear a `thread_data().thread_queue` (la cola del hilo del loop,
  capturada en el ctor del impl) + `#include <LibCore/EventReceiver.h>`.

## 11. EventLoop Android con modelo viejo (07-browser, lib ladybird)

- **Síntoma:** `ALooperEventLoopImplementation.cpp:200: out-of-line definition
  of 'post_event' does not match` + conversión `EventReceiver&`→`*`.
- **Causa raíz:** el modelo de eventos cambió: se encola con
  `ThreadEventQueue::post_event(receiver*, type)` y se avisa con
  `EventLoopManager::did_post_event()`. El manager Android ya estaba porteado
  (`did_post_event` + `on_did_post_event`); solo sobraba el `post_event` del
  implementation, que la base ya no declara.
- **Fix:** borrar `ALooperEventLoopImplementation::post_event`. Nota: esto
  conecta con el issue #484 (EventLoop Android) a nivel runtime, pendiente.

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
