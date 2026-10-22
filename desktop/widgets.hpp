#pragma once
#include <cmath>
#include <functional>
#include <gtk/gtk.h>
#include <iomanip>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <vector>
using json = nlohmann::json;
inline std::string text(const json &v) {
    if (v.is_null())
        return "—";
    if (v.is_string())
        return v.get<std::string>();
    if (v.is_number()) {
        std::ostringstream s;
        s << std::fixed << std::setprecision(2) << v.get<double>();
        return s.str();
    }
    return v.dump();
}
inline std::string field(const json &v, const char *k) {
    return v.contains(k) ? text(v[k]) : "—";
}
inline std::string epochText(const json &v) {
    if (!v.is_number_integer() || v.get<int64_t>() <= 0)
        return "—";
    auto t = g_date_time_new_from_unix_utc(v.get<int64_t>() / 1000);
    if (!t)
        return "—";
    auto p = g_date_time_format(t, "%Y-%m-%d %H:%M:%S UTC");
    std::string out = p ? p : "—";
    g_free(p);
    g_date_time_unref(t);
    return out;
}
inline GtkWidget *box(bool horizontal = false, int spacing = 12) {
    return gtk_box_new(horizontal ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL, spacing);
}
inline void add(GtkWidget *b, GtkWidget *w, bool expand = false) {
    gtk_box_pack_start(GTK_BOX(b), w, expand, expand, 0);
}
inline GtkWidget *label(const std::string &s, const char *klass = "") {
    auto w = gtk_label_new(s.c_str());
    gtk_label_set_xalign(GTK_LABEL(w), 0);
    gtk_label_set_line_wrap(GTK_LABEL(w), true);
    gtk_label_set_line_wrap_mode(GTK_LABEL(w), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(w), 120);
    if (*klass)
        gtk_style_context_add_class(gtk_widget_get_style_context(w), klass);
    return w;
}
inline GtkWidget *entry(const std::string &s) {
    auto w = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(w), s.c_str());
    return w;
}
inline std::string value(GtkWidget *w) {
    return gtk_entry_get_text(GTK_ENTRY(w));
}
inline GtkWidget *number(double v, double low = -1e18, double high = 1e18, double step = .1) {
    auto w = gtk_spin_button_new_with_range(low, high, step);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(w), 4);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), v);
    return w;
}
inline double numeric(GtkWidget *w) {
    gtk_spin_button_update(GTK_SPIN_BUTTON(w));
    return gtk_spin_button_get_value(GTK_SPIN_BUTTON(w));
}
inline GtkWidget *combo(const std::vector<std::pair<std::string, std::string>> &options) {
    auto w = gtk_combo_box_text_new();
    for (auto &[id, title] : options)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w), id.c_str(), title.c_str());
    if (!options.empty())
        gtk_combo_box_set_active(GTK_COMBO_BOX(w), 0);
    return w;
}
inline std::string chosen(GtkWidget *w) {
    auto id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(w));
    return id ? id : "";
}
inline void on(GtkWidget *w, const char *signal, std::function<void()> f) {
    auto p = new std::function<void()>(std::move(f));
    g_signal_connect_data(
        w, signal, G_CALLBACK((+[](GtkWidget *, gpointer p) {
            auto f = *static_cast<std::function<void()> *>(p);
            f();
        })),
        p, +[](gpointer p, GClosure *) { delete static_cast<std::function<void()> *>(p); },
        G_CONNECT_DEFAULT);
}
inline GtkWidget *button(const std::string &s, std::function<void()> f) {
    auto w = gtk_button_new_with_label(s.c_str());
    on(w, "clicked", std::move(f));
    return w;
}
inline void clear(GtkWidget *w) {
    auto list = gtk_container_get_children(GTK_CONTAINER(w));
    for (auto *p = list; p; p = p->next)
        gtk_widget_destroy(GTK_WIDGET(p->data));
    g_list_free(list);
}
inline GtkWidget *panel(GtkWidget *parent, const std::string &title) {
    auto frame = gtk_frame_new(nullptr);
    auto content = box();
    gtk_container_set_border_width(GTK_CONTAINER(content), 18);
    add(content, label(title, "section-title"));
    gtk_container_add(GTK_CONTAINER(frame), content);
    add(parent, frame);
    return content;
}
inline GtkWidget *form(GtkWidget *parent,
                       const std::vector<std::pair<std::string, GtkWidget *>> &fields) {
    auto grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    int i = 0;
    for (auto &[name, w] : fields) {
        int row = i / 2, col = (i % 2) * 2;
        gtk_grid_attach(GTK_GRID(grid), label(name), col, row, 1, 1);
        gtk_widget_set_hexpand(w, true);
        gtk_grid_attach(GTK_GRID(grid), w, col + 1, row, 1, 1);
        ++i;
    }
    add(parent, grid);
    return grid;
}
inline GtkWidget *table(GtkWidget *parent, const std::vector<std::string> &headers,
                        const std::vector<std::vector<std::string>> &rows, int height = 240) {
    std::vector<GType> types(headers.size(), G_TYPE_STRING);
    auto store = gtk_list_store_newv(static_cast<int>(types.size()), types.data());
    for (auto &r : rows) {
        GtkTreeIter it;
        gtk_list_store_append(store, &it);
        for (size_t i = 0; i < headers.size(); ++i)
            gtk_list_store_set(store, &it, static_cast<int>(i), i < r.size() ? r[i].c_str() : "",
                               -1);
    }
    auto view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    for (size_t i = 0; i < headers.size(); ++i) {
        auto cell = gtk_cell_renderer_text_new();
        auto col = gtk_tree_view_column_new_with_attributes(headers[i].c_str(), cell, "text",
                                                            static_cast<int>(i), nullptr);
        gtk_tree_view_column_set_resizable(col, true);
        gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
    }
    auto scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_widget_set_size_request(scroll, -1, height);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    add(parent, scroll);
    return view;
}
inline void activateRow(GtkWidget *view, std::function<void(int)> f) {
    auto p = new std::function<void(int)>(std::move(f));
    g_signal_connect_data(
        view, "row-activated",
        G_CALLBACK((+[](GtkTreeView *, GtkTreePath *path, GtkTreeViewColumn *, gpointer p) {
            auto index = gtk_tree_path_get_indices(path);
            if (index)
                (*static_cast<std::function<void(int)> *>(p))(index[0]);
        })),
        p, +[](gpointer p, GClosure *) { delete static_cast<std::function<void(int)> *>(p); },
        G_CONNECT_DEFAULT);
}
inline void keyValues(GtkWidget *parent, const json &v) {
    std::vector<std::vector<std::string>> rows;
    if (v.is_object())
        for (auto &[k, n] : v.items())
            if (!n.is_structured())
                rows.push_back({k, text(n)});
    table(parent, {"指标", "值"}, rows, 200);
}
inline double plotNumber(const json &v) {
    if (v.is_number()) {
        auto n = v.get<double>();
        return std::isfinite(n) ? n : NAN;
    }
    if (v.is_string())
        try {
            auto str = v.get<std::string>();
            size_t used = 0;
            auto n = std::stod(str, &used);
            if (used == str.size() && std::isfinite(n))
                return n;
        } catch (const std::exception &) {
        }
    return NAN;
}
struct Plot {
    std::vector<std::string> dates;
    std::vector<double> values;
    std::string title;
};
inline GtkWidget *chart(GtkWidget *parent, const json &rows, const std::string &key,
                        const std::string &title, double divisor = 1) {
    auto model = new Plot;
    model->title = title;
    for (auto &r : rows) {
        model->dates.push_back(r.value("date", r.value("period_end", std::string())));
        if (model->dates.back().empty() && r.contains("timestamp"))
            model->dates.back() = epochText(r["timestamp"]).substr(0, 10);
        model->values.push_back(r.contains(key) ? plotNumber(r[key]) / divisor : NAN);
    }
    auto area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, -1, 270);
    g_object_set_data_full(
        G_OBJECT(area), "plot", model, +[](gpointer p) { delete static_cast<Plot *>(p); });
    g_signal_connect(
        area, "draw", G_CALLBACK((+[](GtkWidget *w, cairo_t *c, gpointer) -> gboolean {
            auto m = static_cast<Plot *>(g_object_get_data(G_OBJECT(w), "plot"));
            double width = gtk_widget_get_allocated_width(w),
                   height = gtk_widget_get_allocated_height(w), lo = INFINITY, hi = -INFINITY;
            for (double v : m->values)
                if (std::isfinite(v)) {
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
            cairo_set_source_rgb(c, .05, .075, .10);
            cairo_paint(c);
            cairo_select_font_face(c, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(c, 11);
            cairo_set_source_rgb(c, .55, .65, .70);
            cairo_move_to(c, 16, 22);
            cairo_show_text(c, m->title.c_str());
            if (!std::isfinite(lo)) {
                cairo_move_to(c, 20, height / 2);
                cairo_show_text(c, "No observations available");
                return FALSE;
            }
            if (lo == hi) {
                lo -= 1;
                hi += 1;
            }
            double pad = (hi - lo) * .12;
            lo -= pad;
            hi += pad;
            auto x = [&](size_t n) {
                return 18. + static_cast<double>(n) /
                                 static_cast<double>(std::max<size_t>(1, m->values.size() - 1)) *
                                 (width - 115);
            };
            auto y = [&](double v) { return 40 + (hi - v) / (hi - lo) * (height - 75); };
            for (int i = 0; i < 5; ++i) {
                double v = lo + (hi - lo) * i / 4, py = y(v);
                cairo_set_source_rgb(c, .14, .19, .23);
                cairo_move_to(c, 16, py);
                cairo_line_to(c, width - 90, py);
                cairo_stroke(c);
                cairo_set_source_rgb(c, .55, .65, .7);
                cairo_move_to(c, width - 85, py + 3);
                cairo_show_text(c, text(v).c_str());
            }
            cairo_set_source_rgb(c, .28, .80, .72);
            cairo_set_line_width(c, 2);
            bool opened = false;
            for (size_t i = 0; i < m->values.size(); ++i) {
                if (!std::isfinite(m->values[i])) {
                    opened = false;
                    continue;
                }
                if (opened)
                    cairo_line_to(c, x(i), y(m->values[i]));
                else
                    cairo_move_to(c, x(i), y(m->values[i]));
                opened = true;
            }
            cairo_stroke(c);
            cairo_set_source_rgb(c, .55, .65, .7);
            if (!m->dates.empty()) {
                cairo_move_to(c, 18, height - 12);
                cairo_show_text(c, m->dates.front().c_str());
                cairo_move_to(c, width - 170, height - 12);
                cairo_show_text(c, m->dates.back().c_str());
            }
            return FALSE;
        })),
        nullptr);
    add(parent, area);
    return area;
}
