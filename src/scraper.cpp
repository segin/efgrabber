/*
 * Copyright (c) 2026 Kirn Gill II
 * SPDX-License-Identifier: MIT
 * See LICENSE file for full license text.
 */

#include "efgrabber/scraper.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

namespace efgrabber {

Scraper::Scraper(const DataSetConfig& config)
    : config_(config),
      // Match PDF links like href="/epstein/files/DataSet%2011/EFTA02205655.pdf"
      // or href="https://www.justice.gov/epstein/files/DataSet%2011/EFTA02205655.pdf"
      // Dynamic pattern based on current data set ID
      pdf_link_regex_(build_pdf_regex(config.id), std::regex::icase | std::regex::optimize),
      // Match EFTA followed by digits
      file_id_regex_(R"(EFTA(\d{8}))", std::regex::optimize) {
}

std::string Scraper::build_pdf_regex(int data_set_id) {
    // Build a regex that matches this specific data set's PDF links
    // Handles both "DataSet%20X" and "DataSet X" formats
    std::string id_str = std::to_string(data_set_id);
    // Match href="...DataSet%20X/EFTA....pdf" or href="...DataSet X/EFTA....pdf"
    return R"(href\s*=\s*["']([^"']*(?:DataSet(?:%20|\s))" + id_str + R"()[^"']*\.pdf)["'])";
}

std::vector<PdfLink> Scraper::extract_pdf_links(const std::string& html_content) {
    std::vector<PdfLink> links;

    std::sregex_iterator it(html_content.begin(), html_content.end(), pdf_link_regex_);
    std::sregex_iterator end;

    while (it != end) {
        std::smatch match = *it;
        std::string href = match[1].str();

        // Extract file ID from the URL
        std::string file_id = extract_file_id(href);

        if (!file_id.empty()) {
            PdfLink link;
            link.file_id = file_id;

            // Build full URL if it's a relative path
            if (href.find("http") != 0) {
                if (href[0] == '/') {
                    link.url = "https://www.justice.gov" + href;
                } else {
                    link.url = "https://www.justice.gov/" + href;
                }
            } else {
                link.url = href;
            }

            // Decode URL-encoded spaces
            std::string::size_type pos = 0;
            while ((pos = link.url.find("%20", pos)) != std::string::npos) {
                // Keep %20 in URLs for proper requests
                pos += 3;
            }

            link.filename = file_id + ".pdf";
            links.push_back(std::move(link));
        }

        ++it;
    }

    // Remove duplicates
    std::sort(links.begin(), links.end(),
              [](const PdfLink& a, const PdfLink& b) { return a.file_id < b.file_id; });
    links.erase(std::unique(links.begin(), links.end(),
                [](const PdfLink& a, const PdfLink& b) { return a.file_id == b.file_id; }),
                links.end());

    return links;
}

std::string Scraper::build_page_url(int page_number) const {
    if (page_number == 0) {
        return config_.base_url;
    }
    return config_.base_url + "?page=" + std::to_string(page_number);
}

std::string Scraper::build_file_url(uint64_t file_id) const {
    return build_file_url(format_file_id(file_id));
}

std::string Scraper::build_file_url(const std::string& file_id) const {
    return config_.file_url_base + file_id + ".pdf";
}

std::string Scraper::extract_file_id(const std::string& url_or_filename) const {
    std::smatch match;
    if (std::regex_search(url_or_filename, match, file_id_regex_)) {
        return config_.file_prefix + match[1].str();
    }
    return "";
}

uint64_t Scraper::parse_file_id_number(const std::string& file_id) const {
    std::smatch match;
    if (std::regex_search(file_id, match, file_id_regex_)) {
        return std::stoull(match[1].str());
    }
    return 0;
}

std::string Scraper::format_file_id(uint64_t number) const {
    std::ostringstream oss;
    oss << config_.file_prefix << std::setw(8) << std::setfill('0') << number;
    return oss.str();
}

bool Scraper::is_valid_file_id(const std::string& file_id) const {
    return std::regex_match(file_id, file_id_regex_);
}

} // namespace efgrabber
