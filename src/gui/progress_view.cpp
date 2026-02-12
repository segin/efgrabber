/*
 * progress_view.cpp - Implementation of the GTK progress view
 * Copyright © 2026 Kirn Gill II <segin2005@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "efgrabber/gui/progress_view.h"
#include <sstream>
#include <iomanip>

namespace efgrabber {

ProgressView::ProgressView() {
    frame_ = gtk_frame_new("Progress");

    box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(box_, 8);
    gtk_widget_set_margin_end(box_, 8);
    gtk_widget_set_margin_top(box_, 8);
    gtk_widget_set_margin_bottom(box_, 8);

    progress_bar_ = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress_bar_), TRUE);
    gtk_box_append(GTK_BOX(box_), progress_bar_);

    label_ = gtk_label_new("Ready");
    gtk_label_set_xalign(GTK_LABEL(label_), 0);
    gtk_box_append(GTK_BOX(box_), label_);

    gtk_frame_set_child(GTK_FRAME(frame_), box_);
}

void ProgressView::update(const DownloadStats& stats) {
    int64_t total = stats.files_completed + stats.files_failed +
                   stats.files_pending + stats.files_in_progress +
                   stats.files_not_found;

    if (total == 0) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar_), 0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress_bar_), "0%");
        gtk_label_set_text(GTK_LABEL(label_), "Waiting for files...");
        return;
    }

    double progress = static_cast<double>(stats.files_completed) / total;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar_), progress);

    std::ostringstream text;
    text << std::fixed << std::setprecision(1) << (progress * 100) << "%";
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress_bar_), text.str().c_str());

    std::ostringstream label_text;
    label_text << stats.files_completed << " completed, "
               << stats.files_in_progress << " in progress, "
               << stats.files_pending << " pending";
    gtk_label_set_text(GTK_LABEL(label_), label_text.str().c_str());
}

void ProgressView::reset() {
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar_), 0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress_bar_), "0%");
    gtk_label_set_text(GTK_LABEL(label_), "Ready");
}

} // namespace efgrabber
