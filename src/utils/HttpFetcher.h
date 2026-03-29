#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

#include <curl/curl.h>
#include <glog/logging.h>

// Simple synchronous HTTP GET using libcurl.
// Designed for fetching small binary payloads (vector tiles, etc.).

namespace HttpFetcher
{

namespace detail
{
	inline size_t writeCallback(void *contents, size_t size, size_t nmemb, void *userp)
	{
		auto *buf = static_cast<std::vector<uint8_t> *>(userp);
		size_t totalBytes = size * nmemb;
		buf->insert(buf->end(),
			static_cast<uint8_t *>(contents),
			static_cast<uint8_t *>(contents) + totalBytes);
		return totalBytes;
	}
} // namespace detail

struct Response
{
	long httpCode = 0;
	std::vector<uint8_t> data;
	std::string error;
	bool ok() const { return httpCode == 200 && error.empty(); }
};

// Fetch a URL synchronously. Returns the response body as bytes.
// Timeout is in seconds.
inline Response get(const std::string &url, int timeoutSec = 15)
{
	Response resp;

	CURL *curl = curl_easy_init();
	if (!curl)
	{
		resp.error = "curl_easy_init failed";
		return resp;
	}

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, detail::writeCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.data);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, long(timeoutSec));
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Minotaur/1.0");
	// Accept gzip if available
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK)
	{
		resp.error = curl_easy_strerror(res);
		LOG(ERROR) << "HTTP GET failed: " << resp.error << " url=" << url;
	}
	else
	{
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.httpCode);
		if (resp.httpCode != 200)
		{
			resp.error = "HTTP " + std::to_string(resp.httpCode);
			LOG(WARNING) << "HTTP GET " << url << " -> " << resp.httpCode;
		}
	}

	curl_easy_cleanup(curl);
	return resp;
}

} // namespace HttpFetcher
