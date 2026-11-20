#if CONSUME_JSON
#include <rateweaver/providers.hpp>
#endif
#if CONSUME_ECB
#include <rateweaver/providers/ecb.hpp>
#endif
#if CONSUME_KURLYK
#include <rateweaver/runtime/kurlyk_refresh.hpp>
#endif

int main() {
#if CONSUME_KURLYK
    kurlyk::init(false);
    {
        rateweaver::QuoteBook book;
        kurlyk::HttpClient client;
        rateweaver::Provider provider;
        provider.source_id = "installed";
        provider.url = "http://127.0.0.1/";
        rateweaver::KurlykRefresh refresh(book, client, provider);
        refresh.stop();
        if (refresh.start() != rateweaver::RefreshStatus::Stopped) return 4;
    }
    kurlyk::deinit();
#endif
#if CONSUME_JSON
    const auto cdn = rateweaver::cdn_provider().parse(
        R"({"date":"2024-01-02","usd":{"eur":0.8}})", 1704153600100LL);
    if (!cdn || cdn.quotes.front().rate != 0.8) return 1;
    const auto cbr = rateweaver::cbr_provider().parse(
        R"({"Date":"2024-01-02T11:30:00+03:00","Valute":{"a":{"CharCode":"USD","Nominal":1,"Value":90}}})", 1704184200100LL);
    if (!cbr || cbr.quotes.front().rate != 90) return 2;
#endif
#if CONSUME_ECB
    const auto ecb = rateweaver::ecb_provider().parse(
        R"(<gesmes:Envelope xmlns:gesmes="http://www.gesmes.org/xml/2002-08-01" xmlns="http://www.ecb.int/vocabulary/2002-08-01/eurofxref"><Cube><Cube time="2024-01-02"><Cube currency="USD" rate="1.25"/></Cube></Cube></gesmes:Envelope>)", 1704153600100LL);
    if (!ecb || ecb.quotes.front().rate != 1.25) return 3;
#endif
    return 0;
}
