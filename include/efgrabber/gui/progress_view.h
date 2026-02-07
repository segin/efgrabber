#pragma once

#include <gtk/gtk.h>
#include "efgrabber/common.h"

namespace efgrabber {

// Progress view widget for displaying download progress
class ProgressView {
public:
    ProgressView();

    GtkWidget* get_widget() const { return frame_; }

    void update(const DownloadStats& stats);
    void reset();

private:
    GtkWidget* frame_ = nullptr;
    GtkWidget* box_ = nullptr;
    GtkWidget* progress_bar_ = nullptr;
    GtkWidget* label_ = nullptr;
};

} // namespace efgrabber
