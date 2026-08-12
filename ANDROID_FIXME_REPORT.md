# Reporte de FIXMEs del shell Android (UI/Android)

Estado de las limitaciones pendientes del puente Android -> Ladybird, relevadas del código.

## 1. Entrada táctil y scroll

| # | FIXME | Archivo |
|---|-------|---------|
| 1 | El WebView debería implementar `NestedScrollingChild3` y `ScrollingView` (scrolling nativo anidado). Hoy es un `View` plano. | `UI/Android/src/main/java/org/serenityos/ladybird/WebView.kt:16` |
| 2 | Los `MotionEvent` se pasan como **mouse events**; deberían tratarse como eventos táctiles reales (gestos, multitouch). Hoy solo se aceptan `ACTION_DOWN/MOVE/UP` con botón primario. | `WebView.kt:43` |
| 3 | No se tiene en cuenta el scroll offset cuando la vista soporte scroll (el `drawIntoBitmap` asume la vista sin desplazamiento). | `WebView.kt:58` |

**Efecto:** no hay scroll por gesto (solo arrastrando la scrollbar de la página), ni multitouch, ni scroll inercial.

## 2. Entrada de teclado / IME

No existe ningún FIXME marcado, pero el problema es directo: **no hay puente de teclado**.
- El único uso de teclado es la barra de URL (`LadybirdActivity.kt:84`, `IME_ACTION_GO`).
- No hay `onKeyDown`, conexión de `InputMethodManager`/IME ni `nativeKeyEvent` en la capa JNI (`WebViewImplementationNativeJNI.cpp` solo define `nativeMouseEvent`).

**Efecto:** no se puede escribir en `<input>`/`<textarea>` de la página; el teclado de Android ni siquiera se muestra al tocar un campo.

## 3. Ciclo de vida de procesos y servicios

| # | FIXME | Archivo |
|---|-------|---------|
| 4 | No se notifica a la implementación cuando el servicio WebContent muere y debe reiniciarse. | `WebContentService.kt:27`, `WebContentService.kt:42`, `WebViewImplementation.kt:61` |
| 5 | Los servicios se deberían des-vincular (`unbindService`) en algún momento. | `WebContentService.kt:30`, `WebContentService.kt:45`, `WebViewImplementation.kt:64` |
| 6 | "Handle garbage messages from weird clients" — no se validan mensajes de clientes corruptos. | `LadybirdServiceBase.kt:45` |
| 7 | Manejar el caso de "ya seteado / no presente" al obtener el IPC socket. | `LadybirdServiceBase.kt:51` |
| 8 | Verificar que el Intent que llega es legítimo antes de aceptar el socket. | `LadybirdServiceBase.kt:58` |
| 9 | Tras un crash de WebContent: "launch a new client" (hoy solo loguea). | `WebViewImplementationNative.cpp:58` |

## 4. Motor / recursos

| # | FIXME | Archivo |
|---|-------|---------|
| 10 | No se hace `update_palette` ni `update system fonts` (colores y fuentes del sistema no sincronizados). | `WebViewImplementationNative.cpp:69` |
| 11 | No usar el `Application` (Ladybird) para trackear procesos. | `LadybirdActivity.cpp:100` |
| 12 | No setear `s_ladybird_resource_root` en cada servicio para evitar linkear LibWebView. | `LadybirdServiceBaseJNI.cpp:45` |
| 13 | Usar una versión Android con `AssetManager` para cargar recursos en vez de filesystem. | `LadybirdServiceBaseJNI.cpp:48` |

## 5. Event loop

| # | FIXME | Archivo |
|---|-------|---------|
| 14 | Posible race condition en timers: tomar un lock. | `ALooperEventLoopImplementation.cpp:98` |
| 15 | Las APIs que solo existen para casos oscuros de SerenityOS deberían eliminarse. | `ALooperEventLoopImplementation.h:36` |

## Resumen

- **Crítico para uso real:** teclado/IME en página (inexistente) y scroll táctil (solo mouse-like). Sin esto, el navegador solo sirve para navegar y leer.
- **Media prioridad:** reinicio de WebContent tras crash (hoy deja la app sin contenido hasta reiniciar).
- **Baja/deuda:** validación de Intents, unbind de servicios, race en timers, recursos vía AssetManager.
