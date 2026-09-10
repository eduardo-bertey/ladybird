# Reporte de auditoría de includes — UI/Android

Auditoría de los `#include` de los archivos de `UI/Android/src/main/cpp/` hecha durante la
resolución del build Android del fork.

## Resultado

### Includes colgados (apuntan a un archivo que ya no existe en el repo)

Solo **uno**:

| Archivo | Include | Estado |
|---|---|---|
| `WebContentService.cpp` | `<LibMedia/Audio/Loader.h>` | **Eliminado** (commit `defaa9f613`). La API de audio de LibMedia cambió y el header ya no existe; además era un include sin uso en el archivo. |

### Includes que "no existen" pero son correctos

Son headers del **NDK / sistema Android**, provistos por el toolchain (`-isystem` del NDK),
no por el repo:

- `android/log.h`, `android/looper.h`, `android/bitmap.h`
- `jni.h`
- `fcntl.h`

Estos NO son errores; son parte del SDK nativo de Android.

### Includes que existen pero parecen no usarse (falsos positivos)

Una heurística de "el token del header no aparece después del include" marca varios includes
como sin uso, pero son **falsos positivos**:

- `LadybirdServiceBase.h` → declara `service_main`, que el `.cpp` define.
- Otros se necesitan por cadenas de inclusión transitivas (macros, tipos) y los archivos
  compilan correctamente en CI.

**No se eliminaron** para evitar romper el build a ciegas.

## Contexto

Los archivos de servicio Android (`RequestServerService.cpp`, `ImageDecoderService.cpp`,
`WebContentService.cpp`) estaban desactualizados contra la API actual:

- `try_create(...)` no existe (la macro `C_OBJECT` solo genera `construct(...)`) → corregido
  en el commit `81a241bb43` replicando los `main` de escritorio.
- Include obsoleto de `LibMedia/Audio/Loader.h` → corregido en `defaa9f613`.

Este mismo estado de código existe en `upstream/master`: el build Android de upstream también
está roto en estos puntos.
