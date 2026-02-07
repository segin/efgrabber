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
    std::string filename;   // Just the filename
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
    DataSetConfig config_;
    std::regex pdf_link_regex_;
    std::regex file_id_regex_;
};

} // namespace efgrabber
