#include <vespera/core/application.hpp>
#include <vespera/core/game.hpp>
#include <vespera/core/log.hpp>
#include <vespera/core/version.hpp>
#include <vespera/render/render_backend.hpp>
#include <vespera/ui/rmlui_surface.hpp>

#include <algorithm>
#include <format>
#include <string>

namespace {

class RmlUiSpikeGame final : public vespera::Game {
public:
    void on_start(vespera::GameContext&) override {
        const auto result = ui_.initialize("ui/guild_showcase.rml", 1280, 720);
        if (!result) {
            vespera::log::error("RmlUi showcase failed to initialize: " + result.message);
            failed_ = true;
            return;
        }
        vespera::log::info("RmlUi 6.2 showcase renderer: " + ui_.renderer_mode());
        vespera::log::info("RmlUi showcase controls: mouse | Tab focus | Enter activate | type into Guild Name | Escape quit");
    }

    void on_update(vespera::GameContext& context, double delta_seconds) override {
        if (failed_) return;
        ui_.process_input(context.input);

        if (ui_.consume_clicks("start-mission") > 0 && !mission_active_) {
            mission_active_ = true;
            mission_elapsed_ = 0.0;
            ++mission_serial_;
            (void)ui_.set_text("mission-status", "Expedition underway - Moonwell Escort");
            (void)ui_.set_text("event-log", "RECENT EVENTS\nAldric and Mira departed for the Moonwell Escort.");
            (void)ui_.set_text("start-mission", "Expedition in Progress");
        }
        if (ui_.consume_clicks("upgrade") > 0) {
            ++upgrade_tier_;
            (void)ui_.set_text("upgrade", std::format("War Room / Tier {}", upgrade_tier_));
            (void)ui_.set_text("event-log", std::format("RECENT EVENTS\nWar Room upgraded to tier {}. The guild feels a little more dangerous.", upgrade_tier_));
        }

        if (mission_active_) {
            mission_elapsed_ += std::max(delta_seconds, 0.0);
            const double progress = std::clamp(mission_elapsed_ / 5.0, 0.0, 1.0);
            (void)ui_.set_property("progress-fill", "width", std::format("{:.1f}%", progress * 100.0));
            (void)ui_.set_text("mission-status", std::format("Expedition underway - {:.1f}s remaining", std::max(0.0, 5.0 - mission_elapsed_)));
            if (mission_elapsed_ >= 5.0) {
                mission_active_ = false;
                const bool victory = (mission_serial_ % 2) != 0;
                (void)ui_.set_property("progress-fill", "width", "0%");
                (void)ui_.set_text("start-mission", "Start Expedition");
                (void)ui_.set_text("mission-status", victory ? "Victory - party returned safely." : "Setback - one adventurer needs recovery.");
                (void)ui_.set_text("event-log", victory
                    ? "RECENT EVENTS\nVictory at Moonwell Escort / +70 gold / +12 renown."
                    : "RECENT EVENTS\nThe escort returned bruised, but the guild survived the contract.");
            }
        }
    }

    void on_render(vespera::GameContext&, vespera::RenderBackend& renderer, double) override {
        if (failed_) return;
        const int width = std::max(renderer.target_width(), 1);
        const int height = std::max(renderer.target_height(), 1);
        ui_.resize(width, height);
        vespera::UiRenderPacket packet = ui_.build_packet();
        renderer.render_ui(packet);
        if (!reported_warnings_ && !packet.warnings.empty()) {
            reported_warnings_ = true;
            for (const std::string& warning : packet.warnings) vespera::log::warn("RmlUi showcase: " + warning);
        }
    }

    void on_stop(vespera::GameContext&) override { ui_.shutdown(); }

private:
    vespera::RmlUiSurface ui_;
    bool failed_ = false;
    bool reported_warnings_ = false;
    bool mission_active_ = false;
    double mission_elapsed_ = 0.0;
    int mission_serial_ = 0;
    int upgrade_tier_ = 0;
};

} // namespace

int main() {
    vespera::Application application;
    RmlUiSpikeGame game;
    vespera::ApplicationConfig config;
    config.title = std::string("Vespera Engine ") + std::string(vespera::kEngineVersion) + " - RmlUi Showcase";
    config.width = 1280;
    config.height = 720;
    config.resizable = true;
    config.relative_mouse = false;
    config.escape_quits = true;
    config.icon_path = "branding/vespera_icon_window.png";
    return application.run(game, config);
}
