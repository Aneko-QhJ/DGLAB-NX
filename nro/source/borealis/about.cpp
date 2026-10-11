#include "about.hpp"
extern "C" {
#include <dglab/ui/strings.h>
}
#include <borealis/core/application.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/label.hpp>
#include <cstdio>
#include <string>

namespace dglab {
void buildAboutContent(brls::Box* content, const DglabAboutState& state) {
    for (auto key : {DglabString_AboutLine1, DglabString_AboutLine2}) {
        auto* paragraph = new brls::Label();
        paragraph->setText(dglabString(key)); paragraph->setFontSize(DGLAB_TEXT_BODY);
        paragraph->setSingleLine(false); paragraph->setAutoAnimate(false);
        paragraph->setMarginBottom(12); content->addView(paragraph);
    }
    char version[48], language[96], theme[96];
    std::snprintf(version, sizeof(version), "%u.%u.%u", state.ipc_version.major,
        state.ipc_version.minor, state.ipc_version.patch);
    dglabAboutLanguageValue(&state, language, sizeof(language));
    dglabAboutThemeValue(&state, theme, sizeof(theme));
    const std::pair<DglabString, std::string> values[] = {
        {DglabString_AboutAppVersion, state.app_version ? state.app_version : ""},
        {DglabString_AboutIpcVersion, version},
        {DglabString_AboutBuild, state.build_id ? state.build_id : ""},
        {DglabString_AboutSource, state.github_url ? state.github_url : ""},
        {DglabString_AboutLanguage, language}, {DglabString_AboutTheme, theme},
    };
    auto* list = new brls::Box(brls::Axis::COLUMN); list->setId("dglab/about/list");
    for (const auto& value : values) {
        auto* row = new brls::DetailCell();
        row->setText(dglabString(value.first)); row->setDetailText(value.second);
        row->setFocusable(false); row->setHideHighlight(true);
        row->setLineBottom(1);
        row->setLineColor(brls::Application::getTheme().getColor("brls/sidebar/separator"));
        row->detail->setMaxWidth(700); row->detail->setFontSize(DGLAB_TEXT_VALUE);
        row->detail->setAutoAnimate(false); row->title->setAutoAnimate(false);
        row->setDetailTextColor(brls::Application::getTheme().getColor("brls/text"));
        list->addView(row);
    }
    content->addView(list);
}
}
