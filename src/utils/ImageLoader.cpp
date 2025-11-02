#include "ImageLoader.h"

#include <fstream>
#include <sstream>
#include <vector>
#include <cctype>
#include <windows.h>
#include <wincodec.h>

namespace ImageLoader
{
    // Simple PGM (P5 binary) parser. Ignores comments and supports maxval up to 65535.
    static bool readToken(std::istream &is, std::string &tok)
    {
        tok.clear();
        char ch;
        // skip whitespace and comments
        while (is.get(ch))
        {
            if (std::isspace(static_cast<unsigned char>(ch)))
                continue;
            if (ch == '#')
            {
                std::string dummy;
                std::getline(is, dummy);
                continue;
            }
            tok.push_back(ch);
            break;
        }
        if (tok.empty()) return false;
        // read the rest of the token
        while (is.get(ch))
        {
            if (std::isspace(static_cast<unsigned char>(ch)))
                break;
            tok.push_back(ch);
        }
        return true;
    }

    bool loadPGM(const std::string &filePath, Bitmap &out, std::string *errorOut, float pixel_size_mm)
    {
        std::ifstream ifs(filePath, std::ios::binary);
        if (!ifs)
        {
            if (errorOut) *errorOut = "Failed to open file";
            return false;
        }

        std::string tok;
        if (!readToken(ifs, tok) || tok != "P5")
        {
            if (errorOut) *errorOut = "Not a binary PGM (P5) file";
            return false;
        }
        if (!readToken(ifs, tok)) { if (errorOut) *errorOut = "Missing width"; return false; }
        int w = std::stoi(tok);
        if (!readToken(ifs, tok)) { if (errorOut) *errorOut = "Missing height"; return false; }
        int h = std::stoi(tok);
        if (!readToken(ifs, tok)) { if (errorOut) *errorOut = "Missing maxval"; return false; }
        int maxval = std::stoi(tok);
        if (maxval <= 0 || maxval > 65535)
        {
            if (errorOut) *errorOut = "Unsupported maxval";
            return false;
        }
        // consume single whitespace char after header
        ifs.get();

        std::vector<unsigned char> data;
        if (maxval < 256)
        {
            data.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
            ifs.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()));
        }
        else
        {
            // 16-bit, read and downscale to 8-bit
            std::vector<unsigned short> tmp;
            tmp.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
            ifs.read(reinterpret_cast<char *>(tmp.data()), static_cast<std::streamsize>(tmp.size() * sizeof(unsigned short)));
            data.resize(tmp.size());
            for (size_t i = 0; i < tmp.size(); ++i)
            {
                data[i] = static_cast<unsigned char>((static_cast<unsigned int>(tmp[i]) * 255u) / static_cast<unsigned int>(maxval));
            }
        }
        if (!ifs)
        {
            if (errorOut) *errorOut = "Failed to read pixel data";
            return false;
        }

        out.width_px = w;
        out.height_px = h;
        out.pixel_size_mm = pixel_size_mm;
        out.pixels = std::move(data);
        return true;
    }

    // Helper: UTF-8 to UTF-16
    static std::wstring utf8ToWide(const std::string &s)
    {
        if (s.empty()) return std::wstring();
        int count = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w;
        w.resize(static_cast<size_t>(count));
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], count);
        return w;
    }

    bool loadImage(const std::string &filePath, Bitmap &out, std::string *errorOut, float pixel_size_mm)
    {
        HRESULT hr = S_OK;
        bool needUninit = false;
        // Initialize COM if needed
        hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr)) needUninit = true;

        IWICImagingFactory *factory = nullptr;
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr))
        {
            if (errorOut) *errorOut = "WIC factory creation failed";
            if (needUninit) CoUninitialize();
            return false;
        }

        std::wstring wpath = utf8ToWide(filePath);
        IWICBitmapDecoder *decoder = nullptr;
        hr = factory->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
        if (FAILED(hr))
        {
            if (errorOut) *errorOut = "Failed to decode image";
            factory->Release();
            if (needUninit) CoUninitialize();
            return false;
        }

        IWICBitmapFrameDecode *frame = nullptr;
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr))
        {
            if (errorOut) *errorOut = "Failed to get image frame";
            decoder->Release();
            factory->Release();
            if (needUninit) CoUninitialize();
            return false;
        }

        UINT w = 0, h = 0;
        frame->GetSize(&w, &h);

        // Try direct convert to 8bppGray
        IWICFormatConverter *conv = nullptr;
        hr = factory->CreateFormatConverter(&conv);
        if (SUCCEEDED(hr))
        {
            hr = conv->Initialize(frame, GUID_WICPixelFormat8bppGray, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut);
        }

        if (SUCCEEDED(hr))
        {
            const size_t stride = static_cast<size_t>(w);
            std::vector<BYTE> buffer;
            buffer.resize(static_cast<size_t>(h) * stride);
            WICRect rect{0, 0, static_cast<INT>(w), static_cast<INT>(h)};
            hr = conv->CopyPixels(&rect, static_cast<UINT>(stride), static_cast<UINT>(buffer.size()), buffer.data());
            if (SUCCEEDED(hr))
            {
                out.width_px = static_cast<int>(w);
                out.height_px = static_cast<int>(h);
                out.pixel_size_mm = pixel_size_mm;
                out.pixels.assign(buffer.begin(), buffer.end());
                conv->Release();
                frame->Release();
                decoder->Release();
                factory->Release();
                if (needUninit) CoUninitialize();
                return true;
            }
        }

        if (conv) { conv->Release(); conv = nullptr; }

        // Fallback: convert to 32bppRGBA then compute grayscale
        hr = factory->CreateFormatConverter(&conv);
        if (FAILED(hr))
        {
            if (errorOut) *errorOut = "CreateFormatConverter failed";
            frame->Release(); decoder->Release(); factory->Release(); if (needUninit) CoUninitialize();
            return false;
        }
        hr = conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut);
        if (FAILED(hr))
        {
            if (errorOut) *errorOut = "RGBA conversion failed";
            conv->Release(); frame->Release(); decoder->Release(); factory->Release(); if (needUninit) CoUninitialize();
            return false;
        }

        const size_t strideRGBA = static_cast<size_t>(w) * 4u;
        std::vector<BYTE> rgba;
        rgba.resize(static_cast<size_t>(h) * strideRGBA);
        WICRect rect{0, 0, static_cast<INT>(w), static_cast<INT>(h)};
        hr = conv->CopyPixels(&rect, static_cast<UINT>(strideRGBA), static_cast<UINT>(rgba.size()), rgba.data());
        if (FAILED(hr))
        {
            if (errorOut) *errorOut = "CopyPixels failed";
            conv->Release(); frame->Release(); decoder->Release(); factory->Release(); if (needUninit) CoUninitialize();
            return false;
        }

        std::vector<unsigned char> gray;
        gray.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
        for (UINT y = 0; y < h; ++y)
        {
            const BYTE *row = rgba.data() + static_cast<size_t>(y) * strideRGBA;
            for (UINT x = 0; x < w; ++x)
            {
                BYTE r = row[x * 4 + 0];
                BYTE g = row[x * 4 + 1];
                BYTE b = row[x * 4 + 2];
                // Rec. 709 luma approximation
                float gf = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                gray[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = static_cast<unsigned char>(gf + 0.5f);
            }
        }

        out.width_px = static_cast<int>(w);
        out.height_px = static_cast<int>(h);
        out.pixel_size_mm = pixel_size_mm;
        out.pixels = std::move(gray);

        conv->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        if (needUninit) CoUninitialize();
        return true;
    }
}


