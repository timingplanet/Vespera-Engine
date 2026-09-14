#include <vespera/assets/rml_asset_references.hpp>

#include <algorithm>
#include <cctype>
#include <optional>

namespace vespera {
namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 'a');
        return static_cast<char>(c);
    });
    return value;
}

struct ReferenceValueParts {
    std::string path;
    std::string suffix;
    bool root_relative = false;
    bool explicit_dot_prefix = false;
};

ReferenceValueParts split_reference_value(std::string_view value) {
    ReferenceValueParts parts;
    std::size_t suffix = value.size();
    if (const auto pos = value.find('?'); pos != std::string_view::npos) suffix = (std::min)(suffix, pos);
    if (const auto pos = value.find('#'); pos != std::string_view::npos) suffix = (std::min)(suffix, pos);
    parts.path = std::string(value.substr(0, suffix));
    parts.suffix = std::string(value.substr(suffix));
    parts.root_relative = !parts.path.empty() && parts.path.front() == '/';
    parts.explicit_dot_prefix = parts.path.starts_with("./");
    return parts;
}

bool is_local_rml_asset_reference(std::string_view value) {
    if (value.empty() || value.front() == '#' || value.starts_with("data:") || value.find("://") != std::string_view::npos) return false;
    const auto parts = split_reference_value(value);
    std::string path = parts.path;
    if (parts.root_relative && !path.empty()) path.erase(path.begin());
    const auto ext = lowercase(std::filesystem::path(path).extension().string());
    return ext == ".rml" || ext == ".rcss" || ext == ".bmp" || ext == ".png" || ext == ".jpg"
        || ext == ".jpeg" || ext == ".tga" || ext == ".ttf" || ext == ".otf";
}

bool is_attribute_name_boundary(std::string_view text, std::size_t cursor) {
    if (cursor == 0) return true;
    const unsigned char previous = static_cast<unsigned char>(text[cursor - 1]);
    return std::isspace(previous) || text[cursor - 1] == '<';
}

void add_reference(
    std::vector<RmlAssetPathReference>& out,
    std::string_view text,
    std::size_t value_begin,
    std::size_t value_end,
    const std::filesystem::path& source_project_path,
    std::string reason
) {
    while (value_begin < value_end && std::isspace(static_cast<unsigned char>(text[value_begin]))) ++value_begin;
    while (value_end > value_begin && std::isspace(static_cast<unsigned char>(text[value_end - 1]))) --value_end;
    if (value_begin == value_end) return;

    const std::string raw(text.substr(value_begin, value_end - value_begin));
    if (!is_local_rml_asset_reference(raw)) return;

    const auto parts = split_reference_value(raw);
    std::filesystem::path authored_path(parts.path);
    // Preserve the 0.9.8 catalog's resolution behavior for leading-slash paths.
    // They remain detectable, but controlled move rewriting deliberately leaves
    // them alone because Vespera has not yet defined a public project-root URI
    // semantic for RmlUi.
    if (authored_path.is_absolute()) authored_path = authored_path.relative_path();
    const auto project_path = (source_project_path.parent_path() / authored_path).lexically_normal();

    out.push_back({value_begin, value_end - value_begin, raw, project_path, std::move(reason), parts.root_relative});
}

std::string replacement_value(
    const RmlAssetPathReference& reference,
    const std::filesystem::path& target_project_path,
    const std::filesystem::path& new_source_project_path
) {
    const auto parts = split_reference_value(reference.raw_value);
    std::string rewritten_path;
    if (reference.root_relative) {
        return reference.raw_value;
    } else {
        auto relative = target_project_path.lexically_normal().lexically_relative(new_source_project_path.parent_path().lexically_normal());
        if (relative.empty()) relative = target_project_path.filename();
        rewritten_path = relative.generic_string();
        if (parts.explicit_dot_prefix && !rewritten_path.starts_with(".") && !rewritten_path.starts_with("/")) {
            rewritten_path = "./" + rewritten_path;
        }
    }
    return rewritten_path + parts.suffix;
}

} // namespace

std::vector<RmlAssetPathReference> scan_rml_asset_path_references(
    std::string_view text,
    const std::filesystem::path& source_project_path,
    bool stylesheet
) {
    std::vector<RmlAssetPathReference> result;

    if (!stylesheet) {
        for (const auto token : {std::string_view("href"), std::string_view("src")}) {
            std::size_t cursor = 0;
            while ((cursor = text.find(token, cursor)) != std::string_view::npos) {
                if (!is_attribute_name_boundary(text, cursor)) {
                    cursor += token.size();
                    continue;
                }
                std::size_t pos = cursor + token.size();
                while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
                if (pos >= text.size() || text[pos] != '=') {
                    cursor = pos;
                    continue;
                }
                ++pos;
                while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
                if (pos >= text.size() || (text[pos] != '\'' && text[pos] != '"')) {
                    cursor = pos;
                    continue;
                }
                const char quote = text[pos++];
                const auto end = text.find(quote, pos);
                if (end == std::string_view::npos) break;
                add_reference(result, text, pos, end, source_project_path,
                    token == "href" ? "RmlUi linked asset" : "RmlUi source asset");
                cursor = end + 1;
            }
        }
    }

    std::size_t cursor = 0;
    while ((cursor = text.find("url(", cursor)) != std::string_view::npos) {
        std::size_t pos = cursor + 4;
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
        char quote = 0;
        if (pos < text.size() && (text[pos] == '\'' || text[pos] == '"')) quote = text[pos++];
        const auto end = quote ? text.find(quote, pos) : text.find(')', pos);
        if (end == std::string_view::npos) break;
        add_reference(result, text, pos, end, source_project_path, "RmlUi stylesheet resource");
        cursor = end + 1;
    }

    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.value_offset < rhs.value_offset;
    });
    return result;
}

RmlAssetRewriteResult rewrite_rml_asset_path_references(
    std::string_view text,
    const std::filesystem::path& old_source_project_path,
    const std::filesystem::path& new_source_project_path,
    std::span<const RmlAssetPathMove> moved_assets,
    bool stylesheet
) {
    RmlAssetRewriteResult result;
    result.text.reserve(text.size());
    const bool source_moved = old_source_project_path.lexically_normal() != new_source_project_path.lexically_normal();
    const auto references = scan_rml_asset_path_references(text, old_source_project_path, stylesheet);

    std::size_t cursor = 0;
    for (const auto& reference : references) {
        std::filesystem::path target = reference.project_path.lexically_normal();
        bool target_moved = false;
        for (const auto& move : moved_assets) {
            if (target == move.old_project_path.lexically_normal()) {
                target = move.new_project_path.lexically_normal();
                target_moved = true;
                break;
            }
        }

        if (!source_moved && !target_moved) continue;
        const std::string replacement = replacement_value(reference, target, new_source_project_path);
        if (replacement == reference.raw_value) continue;

        result.text.append(text.substr(cursor, reference.value_offset - cursor));
        result.text.append(replacement);
        cursor = reference.value_offset + reference.value_length;
        ++result.references_rewritten;
    }

    if (result.references_rewritten == 0) {
        result.text.assign(text);
    } else {
        result.text.append(text.substr(cursor));
    }
    return result;
}

} // namespace vespera
