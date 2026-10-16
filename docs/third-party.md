# Third-party code and data

ReachTerminal code: MIT, copyright ZeroMatrixTech.

Runtime/build dependencies are dynamically linked system packages, not copied into the
source tree: cpp-httplib (MIT), nlohmann/json (MIT), PostgreSQL/libpq (PostgreSQL License),
libcurl (curl license), OpenSSL (Apache-2.0 for 3.x), libxml2 (MIT), CMake and Ninja.
Consult the exact installed versions' license notices when distributing binaries.
`.local/deps/` on the initial development machine is ignored local package extraction.

`examples/demo.json` is synthetic project-created data and is covered by the project license.
Its company names, prices, filings and news are fictional. Real public sources and user imports
are not included in Git. Public accessibility alone does not authorize redistribution.

Reference documentation consulted:

- SEC CompanyFacts: https://www.sec.gov/search-filings/edgar-application-programming-interfaces
- IBKR Flex configuration: https://www.interactivebrokers.com/docs/web-api/flex-web-service/client-portal-configuration
- IBKR market subscriptions: https://www.interactivebrokers.com/docs/general/market-data-subscriptions/introduction

DeepSeek was used for independent accounting/fundamental contracts and source review.
Returned claims and numeric examples were independently checked; incorrect claims were
rejected. Core implementation, integration, fixtures and verification were authored and
performed by the primary agent. Private management journals and API responses are not
included in the repository. No user account data was sent to DeepSeek.

- World Bank WDI API: https://datahelpdesk.worldbank.org/knowledgebase/articles/898581-api-basic-call-structures
- Federal Reserve RSS: https://www.federalreserve.gov/feeds/feeds.htm
- IBKR portfolio summary: https://www.interactivebrokers.com/docs/web-api/api-reference/trading/trading-portfolio/get-portfolio-summary

Release archives include source and synthetic fixtures only; fetched public observations and account summaries remain outside the source package.
