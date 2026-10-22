#include "window.hpp"
#include <iostream>
void Terminal::preview() {
    if (snapshot.empty())
        return;
    gtk_container_check_resize(GTK_CONTAINER(window));
    GtkAllocation allocation;
    gtk_widget_get_allocation(window, &allocation);
    gtk_widget_size_allocate(window, &allocation);
    while (gtk_events_pending())
        gtk_main_iteration();
    int width = gtk_widget_get_allocated_width(window),
        height = gtk_widget_get_allocated_height(window);
    auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    auto cr = cairo_create(surface);
    gtk_widget_draw(window, cr);
    cairo_destroy(cr);
    auto result = cairo_surface_write_to_png(surface, snapshot.c_str());
    cairo_surface_destroy(surface);
    if (result != CAIRO_STATUS_SUCCESS) {
        testFailed = true;
        std::cerr << "Cannot render native preview\n";
    }
}
int main(int argc, char **argv) {
    bool test = false;
    std::string image;
    int port = 8788;
    try {
        if (auto p = std::getenv("REACH_DESKTOP_PORT"))
            port = std::stoi(p);
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--self-test")
                test = true;
            else if (a == "--snapshot" && i + 1 < argc)
                image = argv[++i];
            else
                throw std::runtime_error(
                    "Usage: reachterminal-desktop [--self-test] [--snapshot FILE]");
        }
        if (port < 1 || port > 65535)
            throw std::runtime_error("Invalid local port");
        curl_global_init(CURL_GLOBAL_DEFAULT);
        int gtkArgc = 1;
        if (!gtk_init_check(&gtkArgc, &argv))
            throw std::runtime_error("无法连接图形桌面；请在本机桌面会话运行原生终端。");
        auto css = gtk_css_provider_new();
        gtk_css_provider_load_from_data(css, R"CSS(
 * { font-family: "Noto Sans", "DejaVu Sans", sans-serif; font-size: 13px; }
 window, viewport {background:#0b121a;color:#d6e2eb;} box {color:#d6e2eb;} .rail {background:#111b27;border-right:1px solid #283746;}
 .brand {color:#50ccba;font-size:24px;font-weight:bold;} .eyebrow {color:#53c6b7;font-size:11px;letter-spacing:1px;} .page-title {font-size:27px;font-weight:bold;}
 .section-title {font-size:15px;font-weight:bold;color:#e4edf4;} .muted {color:#94a6b8;font-size:12px;} .metric {font-size:23px;font-weight:bold;color:#e4edf4;}
 frame {border:1px solid #2a3948;border-radius:7px;background:#131e2a;} frame border {border:none;} label {padding:2px 0;}
 button {background:#192836;color:#dbe7ef;border:1px solid #314453;border-radius:5px;padding:9px 13px;box-shadow:none;text-shadow:none;}
 button:hover,button.selected {background:#1b4945;color:#77e3d3;border-color:#388d81;} button:disabled {color:#526675;background:#15202a;}
 entry,spinbutton,combobox button,textview text {background:#0c1721;color:#dce7ef;border-color:#304252;} entry {padding:7px;} spinbutton entry {padding:7px;}
 treeview {background:#111d28;color:#dbe7ef;} treeview:selected {background:#24564f;color:#ecfffa;} treeview header button {background:#182b3c;color:#a7c0d0;}
 scrolledwindow {border:none;} .connection {color:#56cdbb;border-left:3px solid #56cdbb;padding:12px;} scrollbar slider {background:#3b5367;min-width:7px;min-height:24px;border-radius:5px;}
)CSS",
                                        -1, nullptr);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
                                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css);
        Terminal terminal(port, test, image);
        terminal.run();
        if (test) {
            if (terminal.failed())
                return 1;
            std::cout << "Native desktop: all 9 workspaces and hedge scenario verified in isolated "
                         "test environment\n";
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "ReachTerminal Desktop: " << e.what() << '\n';
        return 1;
    }
}
