#include "efgrabber/scraper.h"
#include <iostream>
#include <cassert>
#include <vector>

using namespace efgrabber;

void test_relative_url_resolution() {
    DataSetConfig config = get_data_set_config(11);
    Scraper scraper(config);

    // 1. Absolute URL
    std::string html1 = "<a href=\"https://www.justice.gov/epstein/files/DataSet%2011/EFTA02205655.pdf\">Link</a>";
    auto links1 = scraper.extract_pdf_links(html1);
    assert(links1.size() == 1);
    assert(links1[0].url == "https://www.justice.gov/epstein/files/DataSet%2011/EFTA02205655.pdf");
    assert(links1[0].file_id == "EFTA02205655");

    // 2. Root-relative path
    std::string html2 = "<a href=\"/epstein/files/DataSet%2011/EFTA02205655.pdf\">Link</a>";
    auto links2 = scraper.extract_pdf_links(html2);
    assert(links2.size() == 1);
    assert(links2[0].url == "https://www.justice.gov/epstein/files/DataSet%2011/EFTA02205655.pdf");

    // 3. Current-relative path (not starting with /)
    std::string html3 = "<a href=\"epstein/files/DataSet%2011/EFTA02205655.pdf\">Link</a>";
    auto links3 = scraper.extract_pdf_links(html3);
    assert(links3.size() == 1);
    assert(links3[0].url == "https://www.justice.gov/epstein/files/DataSet%2011/EFTA02205655.pdf");

    // 4. DataSet with space instead of %20
    std::string html4 = "<a href=\"/epstein/files/DataSet 11/EFTA02205655.pdf\">Link</a>";
    auto links4 = scraper.extract_pdf_links(html4);
    assert(links4.size() == 1);
    assert(links4[0].url == "https://www.justice.gov/epstein/files/DataSet 11/EFTA02205655.pdf");

    // 5. Mixed absolute and relative in one page
    std::string html5 =
        "<a href=\"https://www.justice.gov/epstein/files/DataSet%2011/EFTA02205655.pdf\">L1</a>"
        "<a href=\"/epstein/files/DataSet%2011/EFTA02205656.pdf\">L2</a>";
    auto links5 = scraper.extract_pdf_links(html5);
    assert(links5.size() == 2);
    assert(links5[0].file_id == "EFTA02205655");
    assert(links5[1].file_id == "EFTA02205656");
    assert(links5[1].url == "https://www.justice.gov/epstein/files/DataSet%2011/EFTA02205656.pdf");

    std::cout << "test_relative_url_resolution passed!" << std::endl;
}

void test_dataset_filtering() {
    DataSetConfig config11 = get_data_set_config(11);
    Scraper scraper11(config11);

    // Should match DataSet 11
    std::string html = "<a href=\"/epstein/files/DataSet%2011/EFTA02205655.pdf\">Link 11</a>"
                       "<a href=\"/epstein/files/DataSet%2012/EFTA02730265.pdf\">Link 12</a>";

    auto links = scraper11.extract_pdf_links(html);
    assert(links.size() == 1);
    assert(links[0].file_id == "EFTA02205655");

    std::cout << "test_dataset_filtering passed!" << std::endl;
}

void test_duplicate_removal() {
    DataSetConfig config = get_data_set_config(11);
    Scraper scraper(config);

    std::string html = "<a href=\"/epstein/files/DataSet%2011/EFTA02205655.pdf\">Link 1</a>"
                       "<a href=\"https://www.justice.gov/epstein/files/DataSet%2011/EFTA02205655.pdf\">Link 1 again</a>";

    auto links = scraper.extract_pdf_links(html);
    assert(links.size() == 1);
    assert(links[0].file_id == "EFTA02205655");

    std::cout << "test_duplicate_removal passed!" << std::endl;
}

int main() {
    try {
        test_relative_url_resolution();
        test_dataset_filtering();
        test_duplicate_removal();
        std::cout << "All tests passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Test failed with unknown exception" << std::endl;
        return 1;
    }
    return 0;
}
