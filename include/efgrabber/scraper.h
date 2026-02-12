/*
 * scraper.h - HTML scraper for extracting PDF links from DOJ index pages
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

#pragma once

#include <string>
#include <vector>
#include <regex>
#include "efgrabber/common.h"

namespace efgrabber {

// Extracted PDF link info
struct PdfLink {
    std::string file_id;    // e.g., "EFTA02205655"
    std::string url;        // Full URL
};

class Scraper {
public:
    explicit Scraper(const DataSetConfig& config);

    // Parse HTML page for PDF links
    std::vector<PdfLink> extract_pdf_links(const std::string& html_content);

    // Build URL for index page
    std::string build_page_url(int page_number) const;

    // Build URL for a specific file ID
    std::string build_file_url(uint64_t file_id) const;
    std::string build_file_url(const std::string& file_id) const;

    // Extract file ID from URL or filename
    std::string extract_file_id(const std::string& url_or_filename) const;

    // Parse file ID to numeric value
    uint64_t parse_file_id_number(const std::string& file_id) const;

    // Format numeric ID to file ID string
    std::string format_file_id(uint64_t number) const;

    // Validate file ID format
    bool is_valid_file_id(const std::string& file_id) const;

    // Get the data set config
    const DataSetConfig& config() const { return config_; }

private:
    static std::string build_pdf_regex(int data_set_id);

    DataSetConfig config_;
    std::regex pdf_link_regex_;
    std::regex file_id_regex_;
};

} // namespace efgrabber
