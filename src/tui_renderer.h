#pragma once
#include "data_aggregator.h"
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <mutex>
#include <condition_variable>

class tui_renderer {
public:
    explicit tui_renderer(std::shared_ptr<data_aggregator> aggregator);
    void run(); // Blocks until the user quits

private:
    std::shared_ptr<data_aggregator> aggregator_;
    ftxui::ScreenInteractive screen_;
    int metar_scroll_ = 0;
    int theme_index_  = 0;

    // Repaint CV — woken by on_change or timer
    std::mutex              repaint_mutex_;
    std::condition_variable repaint_cv_;
    bool                    repaint_pending_ = false;

    ftxui::Element render();
    ftxui::Element render_header();
    ftxui::Element render_nwp_panel();
    ftxui::Element render_metar_panel();
    ftxui::Element render_primary_info_panel();
    ftxui::Element render_aviation_alerts_panel();
    ftxui::Element render_hourly_panel();
    ftxui::Element render_tmax_consensus();
    ftxui::Element render_status_bar();
    ftxui::Element render_key_hints();
};
