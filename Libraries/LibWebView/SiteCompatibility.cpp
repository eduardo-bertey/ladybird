/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/JsonArray.h>
#include <AK/QuickSort.h>
#include <LibCore/Resource.h>
#include <LibWeb/Loader/SiteCompatibility.h>
#include <LibWebView/SiteCompatibility.h>
#include <LibWebView/Utilities.h>

namespace WebView {

ErrorOr<JsonValue> load_site_compatibility_data(StringView directory_uri)
{
    auto directory_or_error = Core::Resource::load_from_uri(directory_uri);
    if (directory_or_error.is_error()) {
        // DIAG-ANDROID: loguear el path real que falla (root + uri + error).
        warnln("site-compat DIAG: uri={} root={} error={}", directory_uri, s_ladybird_resource_root, directory_or_error.error());
        return directory_or_error.release_error();
    }
    auto directory = directory_or_error.release_value();
    auto children = directory->children();
    quick_sort(children);

    JsonArray rules;
    for (auto const& child : children) {
        if (!child.bytes_as_string_view().ends_with(".json"sv))
            continue;

        auto resource = TRY(Core::Resource::load_from_uri(TRY(String::formatted("{}/{}", directory->uri(), child))));
        auto rule = TRY(JsonValue::from_string(resource->data()));
        TRY(rules.append(move(rule)));
    }

    JsonValue data { move(rules) };
    TRY(Web::SiteCompatibilityData::from_json(data));
    return data;
}

}
