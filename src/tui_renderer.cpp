#include "tui_renderer.h"
#include "utils.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <string>
#include <thread>
#include <vector>

namespace ftx = ftxui;

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------
namespace {
    struct tui_theme {
        ftx::Color title;
        ftx::Color primary;
        ftx::Color secondary;
        ftx::Color text;
        ftx::Color ok;
        ftx::Color warn;
        ftx::Color error;
        ftx::Color muted;
        ftx::Color header_bg;
        ftx::Color status_bg;
        ftx::Color hints_bg;
    };

    const tui_theme k_themes[] = {
        // Nord
        { ftx::Color::RGB(136,192,208), ftx::Color::RGB(129,161,193), ftx::Color::RGB(180,142,173),
          ftx::Color::RGB(236,239,244), ftx::Color::RGB(163,190,140), ftx::Color::RGB(235,203,139),
          ftx::Color::RGB(191,97,106),  ftx::Color::RGB(129,161,193),
          ftx::Color::RGB(46,52,64),    ftx::Color::RGB(59,66,82),    ftx::Color::RGB(67,76,94) },
        // Dracula
        { ftx::Color::RGB(139,233,253), ftx::Color::RGB(80,250,123),  ftx::Color::RGB(189,147,249),
          ftx::Color::RGB(248,248,242), ftx::Color::RGB(80,250,123),  ftx::Color::RGB(241,250,140),
          ftx::Color::RGB(255,85,85),   ftx::Color::RGB(98,114,164),
          ftx::Color::RGB(40,42,54),    ftx::Color::RGB(68,71,90),    ftx::Color::RGB(68,71,90) },
        // Gruvbox
        { ftx::Color::RGB(250,189,47),  ftx::Color::RGB(131,165,152), ftx::Color::RGB(211,134,155),
          ftx::Color::RGB(235,219,178), ftx::Color::RGB(184,187,38),  ftx::Color::RGB(250,189,47),
          ftx::Color::RGB(204,36,29),   ftx::Color::RGB(146,131,116),
          ftx::Color::RGB(40,40,40),    ftx::Color::RGB(50,48,47),    ftx::Color::RGB(80,73,69) },
        // Slate
        { ftx::Color::RGB(143,188,187), ftx::Color::RGB(94,129,172),  ftx::Color::RGB(180,142,173),
          ftx::Color::RGB(242,244,248), ftx::Color::RGB(163,190,140), ftx::Color::RGB(235,203,139),
          ftx::Color::RGB(191,97,106),  ftx::Color::RGB(129,161,193),
          ftx::Color::RGB(36,41,51),    ftx::Color::RGB(47,54,64),    ftx::Color::RGB(59,66,82) },
    };

    constexpr int k_theme_count = static_cast<int>(sizeof(k_themes) / sizeof(k_themes[0]));
    const char* k_theme_names[] = { "Nord", "Dracula", "Gruvbox", "Slate" };

    const tui_theme& current_theme(int idx) {
        return k_themes[idx % k_theme_count];
    }

    bool contains_any(const std::string& value, const std::vector<std::string>& needles) {
        return std::any_of(needles.begin(), needles.end(),
                           [&](const std::string& n) { return value.find(n) != std::string::npos; });
    }

    // Format a stored UTC time string as an age: "42s" or "7m"
    std::string format_age(const std::string& iso_time) {
        if (iso_time.empty()) return "--";

        std::tm tm = {};
        std::istringstream ss(iso_time);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%SZ");
        if (ss.fail()) {
            ss.clear(); ss.str(iso_time);
            ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        }
        if (ss.fail()) {
            const int age = utils::compute_age_minutes(iso_time);
            return age >= 0 ? std::to_string(age) + "m" : "--";
        }

#ifdef _WIN32
        const time_t parsed = _mkgmtime(&tm);
#else
        const time_t parsed = timegm(&tm);
#endif
        if (parsed == -1) return "--";

        const auto tp  = std::chrono::system_clock::from_time_t(parsed);
        const auto now = std::chrono::system_clock::now();
        auto age_s = std::chrono::duration_cast<std::chrono::seconds>(now - tp).count();
        if (age_s < 0) age_s = 0;
        if (age_s < 120) return std::to_string(static_cast<int>(age_s)) + "s";
        return std::to_string(static_cast<int>(age_s / 60)) + "m";
    }
} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
tui_renderer::tui_renderer(std::shared_ptr<data_aggregator> aggregator)
    : aggregator_(std::move(aggregator))
    , screen_(ftx::ScreenInteractive::Fullscreen()) {

    aggregator_->set_on_change([this]() {
        {
            std::lock_guard<std::mutex> lock(repaint_mutex_);
            repaint_pending_ = true;
        }
        repaint_cv_.notify_one();
        screen_.PostEvent(ftx::Event::Custom);
    });
}

// ---------------------------------------------------------------------------
// run()
// ---------------------------------------------------------------------------
void tui_renderer::run() {
    auto renderer = ftx::Renderer([this] { return render(); });

    // Repaint thread: wakes on CV or 5-second fallback timer
    std::atomic<bool> repaint_running{true};
    std::thread repaint_thread([this, &repaint_running]() {
        while (repaint_running.load()) {
            std::unique_lock<std::mutex> lock(repaint_mutex_);
            repaint_cv_.wait_for(lock, std::chrono::seconds(5),
                                 [this] { return repaint_pending_; });
            repaint_pending_ = false;
            lock.unlock();
            if (repaint_running.load()) {
                screen_.PostEvent(ftx::Event::Custom);
            }
        }
    });

    auto component = ftx::CatchEvent(renderer, [this](ftx::Event event) -> bool {
        if (event == ftx::Event::Character('q') || event == ftx::Event::Character('Q')) {
            screen_.Exit();
            return true;
        }
        if (event == ftx::Event::ArrowUp) {
            metar_scroll_ = std::max(0, metar_scroll_ - 1);
            return true;
        }
        if (event == ftx::Event::ArrowDown) {
            ++metar_scroll_;
            return true;
        }
        if (event == ftx::Event::Character('r') || event == ftx::Event::Character('R')) {
            aggregator_->force_refresh();
            return true;
        }
        if (event == ftx::Event::Character('t') || event == ftx::Event::Character('T')) {
            theme_index_ = (theme_index_ + 1) % k_theme_count;
            return true;
        }
        return false;
    });

    screen_.Loop(component);

    repaint_running = false;
    repaint_cv_.notify_all();
    repaint_thread.join();
}

// ---------------------------------------------------------------------------
// render()
// ---------------------------------------------------------------------------
ftx::Element tui_renderer::render() {
    const auto& th = current_theme(theme_index_);
    return ftx::vbox({
        render_header(),
        render_nwp_panel()      | ftx::flex_shrink,
        render_metar_panel()    | ftx::flex_shrink,
        ftx::hbox({
            ftx::hbox({ ftx::filler(), render_hourly_panel() | ftx::flex_shrink, ftx::filler() }) | ftx::flex,
            ftx::separator() | ftx::color(th.muted),
            ftx::vbox({
                render_primary_info_panel()    | ftx::flex_shrink,
                render_tmax_consensus()        | ftx::flex_shrink,
                render_aviation_alerts_panel() | ftx::flex,
            }) | ftx::size(ftx::WIDTH, ftx::GREATER_THAN, 54) | ftx::flex_shrink,
        }) | ftx::flex,
        render_status_bar(),
        render_key_hints(),
    });
}

// ---------------------------------------------------------------------------
// render_header()
// ---------------------------------------------------------------------------
ftx::Element tui_renderer::render_header() {
    const auto& th = current_theme(theme_index_);
    const std::string station = aggregator_->get_primary_station_id();
    return ftx::hbox({
        ftx::text("TORONTO WEATHER DASH") | ftx::bold | ftx::color(th.title),
        ftx::text(" | ")                  | ftx::color(th.muted),
        ftx::text(station)                | ftx::bold | ftx::color(th.secondary),
        ftx::text(" | ")                  | ftx::color(th.muted),
        ftx::filler(),
        ftx::text(utils::current_utc_time_string()) | ftx::bold | ftx::color(th.text),
    }) | ftx::bgcolor(th.header_bg) | ftx::bold;
}

// ---------------------------------------------------------------------------
// render_nwp_panel()
// ---------------------------------------------------------------------------
ftx::Element tui_renderer::render_nwp_panel() {
    const auto& th  = current_theme(theme_index_);
    const auto nwps     = aggregator_->get_nwp_forecasts();
    const auto ensemble = aggregator_->get_ensemble_forecasts();

    if (nwps.empty()) {
        return ftx::window(ftx::text(" NWP ENSEMBLE ") | ftx::bold | ftx::color(th.title),
                           ftx::text("  Fetching...")  | ftx::color(th.muted));
    }

    std::vector<std::string> dates;
    for (const auto& e : ensemble) {
        dates.push_back(e.date);
        if (dates.size() >= 5) break;
    }

    std::vector<std::vector<std::string>> rows;
    {
        std::vector<std::string> header = {"Model"};
        for (const auto& d : dates) header.push_back(d);
        header.push_back("Run");
        header.push_back("Age");
        rows.push_back(std::move(header));
    }

    for (const auto& m : nwps) {
        std::vector<std::string> row;
        std::string name = m.model;
        for (auto& c : name) c = static_cast<char>(std::toupper(c));
        row.push_back(name);
        for (const auto& date : dates) {
            auto it = m.tmax_by_date.find(date);
            row.push_back(it != m.tmax_by_date.end()
                ? utils::format_float(it->second, 1) + "\xC2\xB0C"
                : "--");
        }
        row.push_back(m.run_time.size() > 16 ? m.run_time.substr(0, 16) : m.run_time);
        row.push_back(format_age(m.run_time));
        rows.push_back(std::move(row));
    }

    // Footer: ensemble row
    {
        std::vector<std::string> footer = {"ENSEMBLE"};
        for (size_t i = 0; i < dates.size() && i < ensemble.size(); ++i) {
            const auto& e = ensemble[i];
            footer.push_back(
                utils::format_float(e.weighted_tmax, 1) + "\xC2\xB0C \xC2\xB1" +
                utils::format_float(e.sigma, 1) + " " +
                (e.models_agree ? "\xE2\x9C\x93" : "\xE2\x9C\x97"));
        }
        footer.push_back("spread");
        std::string spreads;
        for (size_t i = 0; i < dates.size() && i < ensemble.size(); ++i) {
            if (!spreads.empty()) spreads += '/';
            spreads += utils::format_float(ensemble[i].spread, 1);
        }
        footer.push_back(spreads.empty() ? "--" : spreads + "\xC2\xB0C");
        rows.push_back(std::move(footer));
    }

    auto table = ftx::Table(rows);
    table.SelectAll().Border(ftx::LIGHT);
    table.SelectRow(0).Decorate(ftx::bold);
    table.SelectRow(0).SeparatorVertical(ftx::LIGHT);
    table.SelectRow(static_cast<int>(rows.size()) - 1).Decorate(ftx::bold | ftx::color(th.ok));

    // Highlight outlier cells
    for (size_t mi = 0; mi < nwps.size(); ++mi) {
        for (size_t di = 0; di < dates.size() && di < ensemble.size(); ++di) {
            const auto& e = ensemble[di];
            auto it = e.model_tmax.find(nwps[mi].model);
            if (it != e.model_tmax.end() && std::abs(it->second - e.weighted_tmax) > 1.5f) {
                table.SelectCell(static_cast<int>(mi + 1),
                                 static_cast<int>(di + 1)).Decorate(ftx::color(th.error));
            }
        }
    }

    return ftx::window(ftx::text(" NWP ENSEMBLE ") | ftx::bold | ftx::color(th.title),
                       table.Render());
}

// ---------------------------------------------------------------------------
// render_metar_panel()
// ---------------------------------------------------------------------------
ftx::Element tui_renderer::render_metar_panel() {
    const auto& th  = current_theme(theme_index_);
    const auto metars = aggregator_->get_metar_reports();

    if (metars.empty()) {
        return ftx::window(ftx::text(" METAR ") | ftx::bold | ftx::color(th.title),
                           ftx::text("  Fetching...") | ftx::color(th.muted));
    }

    std::vector<std::vector<std::string>> rows;
    rows.push_back({"Station", "Time", "Temp", "Dew", "Wind", "Vis", "Sky", "WX", "Age"});

    for (const auto& m : metars) {
        std::string time_short = m.obs_time;
        if (time_short.size() > 16) time_short = time_short.substr(11, 5);
        rows.push_back({
            m.station_id,
            time_short,
            utils::format_float(m.temp_c,     1) + "\xC2\xB0C",
            utils::format_float(m.dewpoint_c, 1) + "\xC2\xB0C",
            m.wind_dir_deg + "/" + std::to_string(m.wind_speed_kt) + "kt",
            m.visibility,
            m.sky_condition,
            m.wx_string.empty() ? "--" : m.wx_string,
            format_age(m.obs_time)
        });
    }

    const int total_data  = static_cast<int>(rows.size()) - 1;
    const int max_visible = 8;
    metar_scroll_ = std::clamp(metar_scroll_, 0, std::max(0, total_data - max_visible));

    std::vector<std::vector<std::string>> visible;
    visible.push_back(rows[0]);
    const int start = metar_scroll_ + 1;
    const int end   = std::min(start + max_visible, static_cast<int>(rows.size()));
    for (int i = start; i < end; ++i) visible.push_back(rows[i]);

    auto table = ftx::Table(visible);
    table.SelectAll().Border(ftx::LIGHT);
    table.SelectRow(0).Decorate(ftx::bold);
    table.SelectRow(0).SeparatorVertical(ftx::LIGHT);

    for (int ri = 1; ri < static_cast<int>(visible.size()); ++ri) {
        const int orig_idx = metar_scroll_ + ri - 1;
        if (orig_idx < static_cast<int>(metars.size())) {
            const int age = utils::compute_age_minutes(metars[orig_idx].obs_time);
            if (age > 90) {
                table.SelectRow(ri).Decorate(ftx::color(th.error));
            } else if (age > 30) {
                table.SelectRow(ri).Decorate(ftx::color(th.warn));
            }
        }
    }

    std::string title = " METAR (" + std::to_string(total_data) + " reports) ";
    if (total_data > max_visible) {
        title += "[" + std::to_string(metar_scroll_ + 1) + "-" +
                 std::to_string(std::min(metar_scroll_ + max_visible, total_data)) + "] ";
    }

    return ftx::window(ftx::text(title) | ftx::bold | ftx::color(th.title),
                       table.Render());
}

// ---------------------------------------------------------------------------
// render_primary_info_panel()
// ---------------------------------------------------------------------------
ftx::Element tui_renderer::render_primary_info_panel() {
    const auto& th       = current_theme(theme_index_);
    const auto  ensemble = aggregator_->get_ensemble_forecasts();
    const std::string station   = aggregator_->get_primary_station_id();
    const std::string panel_title = " " + station + " INFO ";

    if (!aggregator_->has_primary_metar()) {
        return ftx::window(ftx::text(panel_title) | ftx::bold | ftx::color(th.title),
                           ftx::text("  Waiting for primary station METAR...") | ftx::color(th.muted));
    }

    const auto  pm        = aggregator_->get_primary_metar();
    const bool  has_high  = aggregator_->has_primary_station_day_high();
    const float day_high  = has_high ? aggregator_->get_primary_station_day_high() : pm.temp_c;
    const std::string today = utils::current_utc_date_string();

    auto today_it = std::find_if(ensemble.begin(), ensemble.end(),
                                 [&](const ensemble_forecast& e) { return e.date == today; });

    std::vector<ftx::Element> lines;

    // Current obs line
    std::string obs_line = station + " latest " +
        utils::format_float(pm.temp_c, 1) + "\xC2\xB0C / dew " +
        utils::format_float(pm.dewpoint_c, 1) + "\xC2\xB0C / wind " +
        pm.wind_dir_deg + "/" + std::to_string(pm.wind_speed_kt) + "kt";
    if (!pm.wx_string.empty())     obs_line += " / wx "  + pm.wx_string;
    if (!pm.sky_condition.empty()) obs_line += " / sky " + pm.sky_condition;
    lines.push_back(ftx::text(obs_line) | ftx::color(th.text));

    // Day high vs ensemble line
    std::string high_line = station + " high so far " + utils::format_float(day_high, 1) + "\xC2\xB0C";
    if (today_it != ensemble.end()) {
        const float gap = today_it->weighted_tmax - day_high;
        high_line += " / ensemble " + utils::format_float(today_it->weighted_tmax, 1) + "\xC2\xB0C";
        high_line += " / gap "      + utils::format_float(gap, 1) + "\xC2\xB0C";
    }
    lines.push_back(ftx::text(high_line) | ftx::color(th.secondary));

    // Intraday bias signal
    if (today_it != ensemble.end()) {
        const bool wet_signal       = contains_any(pm.wx_string,     {"RA", "DZ", "SH", "TS"});
        const bool low_cloud_signal = contains_any(pm.sky_condition, {"OVC", "BKN00", "BKN01", "BKN02"});
        const bool supportive       = !wet_signal && !low_cloud_signal;
        const float cur_gap  = today_it->weighted_tmax - pm.temp_c;
        const float high_gap = today_it->weighted_tmax - day_high;

        std::string bias  = "Balanced intraday";
        ftx::Color  bias_color = th.secondary;

        if (day_high >= today_it->weighted_tmax + 0.2f) {
            bias = "Target already exceeded"; bias_color = th.ok;
        } else if (high_gap <= 0.3f || (cur_gap <= 0.7f && supportive)) {
            bias = "Upside risk elevated";    bias_color = th.warn;
        } else if (cur_gap >= 2.0f && (wet_signal || low_cloud_signal)) {
            bias = "Upside risk limited";     bias_color = th.muted;
        }

        lines.push_back(ftx::text("Signal: " + bias) | ftx::bold | ftx::color(bias_color));
    }

    return ftx::window(ftx::text(panel_title) | ftx::bold | ftx::color(th.title),
                       ftx::vbox(lines));
}

// ---------------------------------------------------------------------------
// render_aviation_alerts_panel()
// ---------------------------------------------------------------------------
ftx::Element tui_renderer::render_aviation_alerts_panel() {
    const auto& th    = current_theme(theme_index_);
    const auto metars = aggregator_->get_metar_reports();
    const auto tafs   = aggregator_->get_taf_forecasts();
    const auto taf_err = aggregator_->get_taf_error();

    std::vector<std::vector<std::string>> rows;
    rows.push_back({"Type", "Station", "Time", "Alert"});

    for (const auto& m : metars) {
        if (!m.is_speci) continue;
        std::string time_short = m.obs_time;
        if (time_short.size() > 16) time_short = time_short.substr(11, 5);
        rows.push_back({"SPECI", m.station_id, time_short, m.raw_text});
    }

    for (const auto& taf : tafs) {
        if (taf.alerts.empty()) continue;
        std::string issue_short = taf.issue_time;
        if (issue_short.size() > 16) issue_short = issue_short.substr(11, 5);
        for (const auto& alert : taf.alerts) {
            rows.push_back({"TAF", taf.station_id, issue_short, alert});
        }
    }

    if (rows.size() == 1) {
        const std::string msg = taf_err.empty() ? "No active TAF/SPECI alerts"
                                                : "Error: " + taf_err;
        return ftx::window(ftx::text(" TAF / SPECI ALERTS ") | ftx::bold | ftx::color(th.title),
                           ftx::text("  " + msg) | ftx::color(taf_err.empty() ? th.muted : th.error));
    }

    auto table = ftx::Table(rows);
    table.SelectAll().Border(ftx::LIGHT);
    table.SelectRow(0).Decorate(ftx::bold);
    table.SelectRow(0).SeparatorVertical(ftx::LIGHT);
    for (int ri = 1; ri < static_cast<int>(rows.size()); ++ri) {
        table.SelectRow(ri).Decorate(
            ftx::color(rows[ri][0] == "SPECI" ? th.error : th.warn));
    }

    return ftx::window(ftx::text(" TAF / SPECI ALERTS ") | ftx::bold | ftx::color(th.title),
                       table.Render());
}

// ---------------------------------------------------------------------------
// render_hourly_panel()
// ---------------------------------------------------------------------------
ftx::Element tui_renderer::render_hourly_panel() {
    const auto& th    = current_theme(theme_index_);
    const auto  hours = aggregator_->get_hourly_obs();

    if (hours.empty()) {
        return ftx::window(ftx::text(" HOURLY TIMELINE ") | ftx::bold | ftx::color(th.title),
                           ftx::text("  Fetching...")     | ftx::color(th.muted));
    }

    // Find NOW row by matching current date+hour prefix
    const std::string now_date = utils::current_utc_date_string();
    const std::string now_hour = utils::current_utc_hour_string(); // "HH:00"
    int current_idx = -1;
    for (size_t i = 0; i < hours.size(); ++i) {
        // hour_label is "YYYY-MM-DD HH:MM"
        const auto& lbl = hours[i].hour_label;
        if (lbl.size() >= 16 &&
            lbl.substr(0, 10)  == now_date &&
            lbl.substr(11, 5) == now_hour) {
            current_idx = static_cast<int>(i);
            break;
        }
    }

    constexpr int k_day_block = 24;
    const int total = static_cast<int>(hours.size());

    auto build_table = [&](int from, int to) -> ftx::Element {
        std::vector<std::vector<std::string>> rows;
        rows.push_back({"Day+HH:MM", "Temp\xC2\xB0C", "Precip", "Wind", "Cloud%", ""});
        for (int i = from; i < to; ++i) {
            const auto& h = hours[i];
            // Show "MM-DD HH:MM" to fit within column
            std::string lbl = h.hour_label;
            if (lbl.size() >= 16) lbl = lbl.substr(5, 5) + ' ' + lbl.substr(11, 5);
            rows.push_back({
                lbl,
                utils::format_float(h.temp_c, 1),
                utils::format_float(h.precip_mm, 1),
                utils::format_float(h.wind_kmh, 0),
                std::to_string(h.cloud_pct),
                i == current_idx ? "NOW" : ""
            });
        }
        auto table = ftx::Table(rows);
        table.SelectAll().Border(ftx::LIGHT);
        table.SelectRow(0).Decorate(ftx::bold);
        table.SelectRow(0).SeparatorVertical(ftx::LIGHT);
        if (current_idx >= from && current_idx < to) {
            table.SelectRow(current_idx - from + 1)
                 .Decorate(ftx::bold | ftx::color(th.primary));
        }
        return table.Render() | ftx::flex_shrink;
    };

    const int d1s = 0,              d1e = std::min(total, k_day_block);
    const int d2s = d1e,            d2e = std::min(total, d2s + k_day_block);
    const int d3s = d2e,            d3e = std::min(total, d3s + k_day_block);

    auto day1 = build_table(d1s, d1e);
    auto day2 = d2s < d2e ? build_table(d2s, d2e)
                          : ftx::text("No day-2 hours") | ftx::color(th.muted) | ftx::flex_shrink;
    auto day3 = d3s < d3e ? build_table(d3s, d3e)
                          : ftx::text("No day-3 hours") | ftx::color(th.muted) | ftx::flex_shrink;

    auto trio = ftx::hbox({
        std::move(day1),
        ftx::separator() | ftx::color(th.muted),
        std::move(day2),
        ftx::separator() | ftx::color(th.muted),
        std::move(day3),
    }) | ftx::flex_shrink;

    return ftx::window(ftx::text(" HOURLY TIMELINE (3 DAYS) ") | ftx::bold | ftx::color(th.title),
                       ftx::hbox({ ftx::filler(), std::move(trio), ftx::filler() }) | ftx::flex);
}

// ---------------------------------------------------------------------------
// render_tmax_consensus()
// ---------------------------------------------------------------------------
ftx::Element tui_renderer::render_tmax_consensus() {
    const auto& th       = current_theme(theme_index_);
    const auto  ensemble = aggregator_->get_ensemble_forecasts();
    const bool  has_high = aggregator_->has_primary_station_day_high();
    const float obs_high = has_high ? aggregator_->get_primary_station_day_high() : 0.0f;
    const std::string station = aggregator_->get_primary_station_id();

    if (ensemble.empty()) {
        return ftx::window(ftx::text(" TMAX CONSENSUS ") | ftx::bold | ftx::color(th.title),
                           ftx::text("  Fetching...") | ftx::color(th.muted));
    }

    std::vector<ftx::Element> lines;
    for (size_t i = 0; i < ensemble.size() && i < 5; ++i) {
        const auto& e = ensemble[i];
        std::string line = e.date + "  " +
            utils::format_float(e.weighted_tmax, 1) + "\xC2\xB0C \xC2\xB1 " +
            utils::format_float(e.sigma, 1) + "\xC2\xB0C";
        if (i == 0 && has_high) {
            line += "  " + station + " obs high: " + utils::format_float(obs_high, 1) + "\xC2\xB0C";
        }
        if (e.has_previous && e.changed_since_previous) {
            const std::string prefix = e.delta_from_previous > 0 ? "+" : "";
            line += "  dT " + prefix + utils::format_float(e.delta_from_previous, 1) + "\xC2\xB0C";
        }
        lines.push_back(ftx::text(line) |
                        ftx::color(e.models_agree ? th.ok : th.warn));
    }

    return ftx::window(ftx::text(" TMAX CONSENSUS ") | ftx::bold | ftx::color(th.title),
                       ftx::vbox(lines) |
                       ftx::size(ftx::HEIGHT, ftx::EQUAL, static_cast<int>(lines.size())));
}

// ---------------------------------------------------------------------------
// render_status_bar()
// ---------------------------------------------------------------------------
ftx::Element tui_renderer::render_status_bar() {
    const auto& th = current_theme(theme_index_);

    const auto metar_refresh  = aggregator_->get_metar_last_refresh();
    const auto taf_refresh    = aggregator_->get_taf_last_refresh();
    const auto nwp_refresh    = aggregator_->get_nwp_last_refresh();
    const auto hourly_refresh = aggregator_->get_hourly_last_refresh();
    const auto nwp_change     = aggregator_->get_nwp_change_summary();

    const auto metar_err  = aggregator_->get_metar_error();
    const auto taf_err    = aggregator_->get_taf_error();
    const auto nwp_err    = aggregator_->get_nwp_error();
    const auto hourly_err = aggregator_->get_hourly_error();

    ftx::Elements parts;
    auto add_source = [&](const std::string& name, const std::string& refresh,
                          const std::string& err, const std::string& suffix = {}) {
        if (!parts.empty()) parts.push_back(ftx::text(" | ") | ftx::color(th.muted));
        if (!err.empty()) {
            parts.push_back(ftx::text(name + ": ERR") | ftx::color(th.error));
        } else if (refresh.empty()) {
            parts.push_back(ftx::text(name + ": --")  | ftx::color(th.muted));
        } else {
            parts.push_back(ftx::text(name + ": " + format_age(refresh) + suffix) |
                            ftx::color(th.text));
        }
    };

    add_source("METAR",  metar_refresh,  metar_err);
    add_source("TAF",    taf_refresh,    taf_err);
    add_source("NWP",    nwp_refresh,    nwp_err,
               nwp_change.empty() ? std::string{} : " " + nwp_change);
    add_source("HOURLY", hourly_refresh, hourly_err);

    return ftx::hbox(parts) | ftx::bgcolor(th.status_bg);
}

// ---------------------------------------------------------------------------
// render_key_hints()
// ---------------------------------------------------------------------------
ftx::Element tui_renderer::render_key_hints() {
    const auto& th = current_theme(theme_index_);
    const std::string name = k_theme_names[theme_index_ % k_theme_count];
    return ftx::hbox({
        ftx::text("[q]")                  | ftx::bold  | ftx::color(th.text),
        ftx::text(" quit  ")              | ftx::color(th.muted),
        ftx::text("[\xE2\x86\x91\xE2\x86\x93]") | ftx::bold | ftx::color(th.text),
        ftx::text(" scroll METAR  ")      | ftx::color(th.muted),
        ftx::text("[r]")                  | ftx::bold  | ftx::color(th.text),
        ftx::text(" refresh  ")           | ftx::color(th.muted),
        ftx::text("[t]")                  | ftx::bold  | ftx::color(th.text),
        ftx::text(" theme: ")             | ftx::color(th.muted),
        ftx::text(name)                   | ftx::bold  | ftx::color(th.primary),
    }) | ftx::bgcolor(th.hints_bg);
}
