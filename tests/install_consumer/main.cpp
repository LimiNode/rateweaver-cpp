#include <rateweaver.hpp>

#include <iostream>

int main() {
    rateweaver::QuoteBook book;
    const rateweaver::Quote quote{"EUR", "USD", 1.25, "fixture",
        rateweaver::QuoteOrigin::Observed, 1700000000000LL, 1700000000100LL};
    if (book.replace_source("fixture", {quote}) != rateweaver::QuotePublishStatus::Success) {
        return 1;
    }
    const rateweaver::QuoteGraph graph(book.snapshot());
    rateweaver::RouterOptions options;
    options.aggregation.reference_time = 1700000000200LL;
    options.aggregation.max_age_ms = 1000;
    const rateweaver::RateRouter router(graph, options);
    const auto result = router.convert(-8.0, "eur", "usd");
    if (!result || *result.amount != -10.0 || result.route.size() != 1 ||
        result.route.front().contributors.front().quote.source_id != "fixture") {
        return 2;
    }
    const auto unavailable = router.convert(0.0, "EUR", "JPY");
    if (unavailable || unavailable.amount) {
        return 3;
    }
    std::cout << "Installed consumer: conversion, provenance and failure semantics passed\n";
    return 0;
}
