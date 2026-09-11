# Copyright (c) 2022, Andrew Kaster <akaster@serenityos.org>
#
# SPDX-License-Identifier: BSD-2-Clause
#

#
# Copy resources into tarball for inclusion in /assets of APK
#
set(LADYBIRD_RESOURCE_ROOT "${LADYBIRD_SOURCE_DIR}/Base/res")
macro(copy_res_folder folder)
    add_custom_target(copy-${folder}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${LADYBIRD_RESOURCE_ROOT}/${folder}"
            "asset-bundle/res/${folder}"
    )
    add_dependencies(archive-assets copy-${folder})
endmacro()
add_custom_target(archive-assets COMMAND ${CMAKE_COMMAND} -E chdir asset-bundle zip -r ../ladybird-assets.zip ./ )
copy_res_folder(ladybird)
copy_res_folder(fonts)
copy_res_folder(icons)
copy_res_folder(themes)
# site-compatibility (WebCompat/*.json) y about-pages no estan en esas 4
# carpetas pero el arranque los necesita (resource://ladybird/...). Sin ellos
# el init muere. Se copian igual que desktop (ver ResourceFiles.cmake).
add_custom_target(copy-android-extra-res
    COMMAND ${CMAKE_COMMAND} -E make_directory asset-bundle/res/ladybird/site-compatibility asset-bundle/res/ladybird/about-pages
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${LADYBIRD_SOURCE_DIR}/WebCompat" asset-bundle/res/ladybird/site-compatibility
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${LADYBIRD_SOURCE_DIR}/Base/res/ladybird/about-pages" asset-bundle/res/ladybird/about-pages
)
add_dependencies(archive-assets copy-android-extra-res)
add_custom_target(copy-assets COMMAND ${CMAKE_COMMAND} -E copy_if_different ladybird-assets.zip "${CMAKE_SOURCE_DIR}/UI/Android/src/main/assets/")
add_dependencies(copy-assets archive-assets)
add_dependencies(ladybird copy-assets)
