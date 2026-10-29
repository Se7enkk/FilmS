#include "preview_host.hpp"
#include "io/image_writer.hpp"
#include "json.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "../../third_party/stb/stb_image.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace spk::desktop {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using EnginePtr = std::unique_ptr<spk_engine, decltype(&spk_engine_destroy)>;
using SessionPtr = std::unique_ptr<spk_session, decltype(&spk_session_release)>;

double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string utf8(const fs::path& path) {
    const auto s = path.u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}

Json read_json(const fs::path& path) {
    std::vector<char> text;
    if (FILE* f = _wfopen(path.wstring().c_str(), L"rb")) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0 && sz <= 16*1024*1024) {
            text.resize(sz);
            fread(text.data(), 1, sz, f);
        }
        fclose(f);
    }
    require(!text.empty(), "Cannot read bundled stock profile: " + utf8(path));
    Json value;
    std::string error;
    const bool ok = Json::parse(std::string(text.data(), text.size()), value, error);
    require(ok, "Bundled stock profile: " + error);
    return value;
}

Catalog read_catalog(const fs::path& resources) {
    Catalog catalog;
    std::set<std::string> ids;
    for (const auto& entry : fs::directory_iterator(resources / "profiles")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        const Json profile = read_json(entry.path());
        const auto& info = profile.at("info");
        Stock stock{info.at("stock").as_string(), info.at("name").as_string()};
        const auto& stage = info.at("stage").as_string();
        const auto& type = info.at("type").as_string();
        require(!stock.id.empty() && stock.id == utf8(entry.path().stem()) &&
                    std::all_of(stock.id.begin(), stock.id.end(), [](unsigned char c) {
                        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                               (c >= '0' && c <= '9') || c == '_';
                    }), "Invalid stock identifier in bundled profile");
        require(!stock.label.empty() && ids.insert(stock.id).second,
                "Missing or duplicate bundled stock metadata");
        require(type == "negative" || type == "positive", "Unknown bundled stock type: " + type);
        stock.is_positive = type == "positive";
        if (stage == "filming") catalog.films.push_back(std::move(stock));
        else if (stage == "printing") catalog.papers.push_back(std::move(stock));
        else throw std::runtime_error("Unknown bundled stock stage: " + stage);
    }
    const auto by_label = [](const Stock& a, const Stock& b) { return a.label < b.label; };
    std::sort(catalog.films.begin(), catalog.films.end(), by_label);
    std::sort(catalog.papers.begin(), catalog.papers.end(), by_label);
    require(!catalog.films.empty() && !catalog.papers.empty(), "Bundled film/paper catalog is empty");
    return catalog;
}

bool validate_settings(const RenderSettings& settings, const Catalog& catalog) {
    const auto contains = [](const std::vector<Stock>& stocks, const std::string& id) {
        return std::any_of(stocks.begin(), stocks.end(), [&](const Stock& s) { return s.id == id; });
    };
    const auto film = std::find_if(catalog.films.begin(), catalog.films.end(),
        [&](const Stock& stock) { return stock.id == settings.film_stock; });
    require(film != catalog.films.end(), "Unknown film stock: " + settings.film_stock);
    require(contains(catalog.papers, settings.print_stock), "Unknown paper stock: " + settings.print_stock);
    require(std::isfinite(settings.print_exposure) && settings.print_exposure >= 0.05 &&
                settings.print_exposure <= 20.0, "Print exposure must be between 0.05 and 20");
    const auto range = [](double value, double low, double high, const char* label) {
        require(std::isfinite(value) && value >= low && value <= high,
                std::string(label) + " must be between " + std::to_string(low) + " and " + std::to_string(high));
    };
    // Reject before touching the live session, including NaN/Inf values
    // that cannot be represented faithfully in the JSON parameter delta.
    range(settings.film_exposure_ev, -8.0, 8.0, "Film exposure (EV)");
    range(settings.film_format_mm, 4.0, 200.0, "Film format (mm)");
    range(settings.grain_amount, 0.0, 2.0, "Grain amount");
    range(settings.halation_amount, 0.0, 4.0, "Halation amount");
    range(settings.glare_amount, 0.0, 30.0, "Glare amount");
    range(settings.y_filter_shift, -1.0, 1.0, "Yellow filter shift");
    range(settings.m_filter_shift, -1.0, 1.0, "Magenta filter shift");
    range(settings.brightness, -100.0, 100.0, "Brightness");
    range(settings.contrast, 0.5, 2.0, "Contrast");
    range(settings.saturation, 0.0, 2.0, "Saturation");
    range(settings.r_gain, 0.5, 1.5, "Red gain");
    range(settings.g_gain, 0.5, 1.5, "Green gain");
    range(settings.b_gain, 0.5, 1.5, "Blue gain");
    range(settings.black_point, 0.0, 30.0, "Black point");
    range(settings.white_point, 225.0, 255.0, "White point");
    range(settings.curve_strength, 0.0, 1.0, "Curve strength");
    range(settings.temperature, -100.0, 100.0, "Temperature");
    range(settings.tint, -100.0, 100.0, "Tint");
    range(settings.highlights, -100.0, 100.0, "Highlights");
    range(settings.shadows, -100.0, 100.0, "Shadows");
    range(settings.vignette_amount, -100.0, 100.0, "Vignette amount");
    range(settings.vignette_midpoint, 0.0, 100.0, "Vignette midpoint");
    return film->is_positive;
}

Json opening_params(const RenderSettings& settings, bool scan_film, bool decode_srgb) {
    // Unexposed effects keep their engine defaults. The exposed settings
    // start at those same defaults, so opening an unedited image retains
    // the existing look, including the physical 35 mm grain/halation scale.
    Json params = Json::object();
    // RAW decodes to a linear ProPhoto RGB scene; JPG/PNG/etc. are sRGB images.
    params.set("input_color_space", Json(std::string(decode_srgb ? "sRGB" : "ProPhoto RGB")));
    params.set("input_cctf_decoding", Json(decode_srgb));
    params.set("output_color_space", Json(std::string("sRGB")));
    params.set("output_cctf_encoding", Json(true));
    params.set("extended_dynamic_range", Json(false));
    params.set("film_stock", Json(settings.film_stock));
    // Desktop policy matches the macOS session: a slide is already a
    // positive and must bypass negative-to-paper printing. The generic
    // engine API still permits deliberate positive-to-paper experiments.
    params.set("scan_film", Json(scan_film));
    params.set("print_stock", Json(settings.print_stock));
    params.set("print_exposure", Json(settings.print_exposure));
    params.set("exposure_compensation_ev", Json(settings.film_exposure_ev));
    params.set("film_format_mm", Json(settings.film_format_mm));
    params.set("grain_active", Json(settings.grain_active));
    params.set("grain_amount", Json(settings.grain_amount));
    params.set("halation_active", Json(settings.halation_active));
    params.set("halation_amount", Json(settings.halation_amount));
    params.set("glare_active", Json(settings.glare_active));
    params.set("glare_amount", Json(settings.glare_amount));
    params.set("y_filter_shift", Json(settings.y_filter_shift));
    params.set("m_filter_shift", Json(settings.m_filter_shift));
    params.set("preflash_exposure", Json(settings.preflash_exposure));
    // Overscan (film edge).
    params.set("overscan_active", Json(settings.overscan_active));
    params.set("overscan_format", Json(settings.overscan_format));
    params.set("overscan_gate", Json(settings.overscan_gate));
    params.set("overscan_holes", Json(settings.overscan_holes));
    params.set("overscan_carrier", Json(settings.overscan_carrier));
    params.set("overscan_fog", Json(settings.overscan_fog));
    params.set("overscan_leaks", Json(settings.overscan_leaks));
    // Auto edge text: "kodak_portra_400" -> "KODAK PORTRA 400".
    {
        std::string edge = settings.film_stock;
        for (char& c : edge) c = char(std::toupper(c));
        for (char& c : edge) if (c == '_') c = ' ';
        params.set("overscan_edge_text", Json(edge));
    }
    // Date imprint: LCD seven-segment style, warm orange glow (like Fuji date backs).
    params.set("date_imprint_active", Json(settings.date_back_active));
    params.set("date_imprint_text", Json(settings.date_back_text));
    params.set("date_imprint_style", Json(std::string("lcd")));
    params.set("date_imprint_corner", Json(std::string("br")));
    params.set("date_imprint_placement", Json(std::string("frame")));
    params.set("date_imprint_size", Json(0.8));
    params.set("date_imprint_data_text", Json(std::string("")));
    return params;
}

Json changed_params(const RenderSettings& before, const RenderSettings& after, bool scan_film) {
    Json delta = Json::object();
    // Sending an unchanged film_stock still invalidates the negative in the
    // C API. A print-only edit must never include it incidentally.
    if (before.film_stock != after.film_stock) {
        delta.set("film_stock", Json(after.film_stock));
        // Send false too when returning to negative film, otherwise the
        // previous slide's direct-scan mode would survive the stock change.
        delta.set("scan_film", Json(scan_film));
    }
    if (before.print_stock != after.print_stock) delta.set("print_stock", Json(after.print_stock));
    if (before.print_exposure != after.print_exposure) delta.set("print_exposure", Json(after.print_exposure));
    // The C API retains carried user controls when loading another stock.
    // Send only changed fields: an unchanged SHOOT field in a paper-only
    // delta would unnecessarily invalidate the cached grain realization.
    if (before.film_exposure_ev != after.film_exposure_ev) delta.set("exposure_compensation_ev", Json(after.film_exposure_ev));
    if (before.film_format_mm != after.film_format_mm) delta.set("film_format_mm", Json(after.film_format_mm));
    if (before.grain_active != after.grain_active) delta.set("grain_active", Json(after.grain_active));
    if (before.grain_amount != after.grain_amount) delta.set("grain_amount", Json(after.grain_amount));
    if (before.halation_active != after.halation_active) delta.set("halation_active", Json(after.halation_active));
    if (before.halation_amount != after.halation_amount) delta.set("halation_amount", Json(after.halation_amount));
    if (before.glare_active != after.glare_active) delta.set("glare_active", Json(after.glare_active));
    if (before.glare_amount != after.glare_amount) delta.set("glare_amount", Json(after.glare_amount));
    if (before.y_filter_shift != after.y_filter_shift) delta.set("y_filter_shift", Json(after.y_filter_shift));
    if (before.m_filter_shift != after.m_filter_shift) delta.set("m_filter_shift", Json(after.m_filter_shift));
    if (before.preflash_exposure != after.preflash_exposure) delta.set("preflash_exposure", Json(after.preflash_exposure));
    if (before.overscan_active != after.overscan_active) delta.set("overscan_active", Json(after.overscan_active));
    if (before.overscan_format != after.overscan_format) delta.set("overscan_format", Json(after.overscan_format));
    if (before.overscan_gate != after.overscan_gate) delta.set("overscan_gate", Json(after.overscan_gate));
    if (before.overscan_holes != after.overscan_holes) delta.set("overscan_holes", Json(after.overscan_holes));
    if (before.overscan_carrier != after.overscan_carrier) delta.set("overscan_carrier", Json(after.overscan_carrier));
    if (before.overscan_fog != after.overscan_fog) delta.set("overscan_fog", Json(after.overscan_fog));
    if (before.overscan_leaks != after.overscan_leaks) delta.set("overscan_leaks", Json(after.overscan_leaks));
    if (before.film_stock != after.film_stock) {
        std::string edge = after.film_stock;
        for (char& c : edge) c = char(std::toupper(c));
        for (char& c : edge) if (c == '_') c = ' ';
        delta.set("overscan_edge_text", Json(edge));
    }
    if (before.date_back_active != after.date_back_active) delta.set("date_imprint_active", Json(after.date_back_active));
    if (before.date_back_text != after.date_back_text) delta.set("date_imprint_text", Json(after.date_back_text));
    if (before.date_back_placement != after.date_back_placement) delta.set("date_imprint_placement", Json(after.date_back_placement));
    return delta;
}

SessionPtr open_session(spk_engine* engine, const io::DecodedRaw& decoded,
                        const RenderSettings& settings, bool scan_film, bool decode_srgb) {
    const spk_image input{decoded.rgb.data(), decoded.width, decoded.height, 3};
    Json params = opening_params(settings, scan_film, decode_srgb);
    // Default date-back text: use EXIF datetime if the user left it blank.
    // EXIF format "2026:10:29 12:34:56" -> "'26 10 29".
    if (settings.date_back_text.empty() && !decoded.metadata.datetime.empty()) {
        const std::string& dt = decoded.metadata.datetime;
        // Accept both "2026-10-29 ..." and "2026:10:29 ..."
        if (dt.size() >= 10 && (dt[4]=='-' || dt[4]==':') && (dt[7]=='-' || dt[7]==':')) {
            std::string yy = dt.substr(2, 2);
            std::string mm = dt.substr(5, 2);
            std::string dd = dt.substr(8, 2);
            params.set("date_imprint_text", Json("'" + yy + " " + mm + " " + dd));
        }
    }
    const auto params_str = params.dump();
    // No reply is needed here; the host has no allocator or JSON lifetime to
    // share with the UI, and the C API validates the complete delta itself.
    SessionPtr session(spk_open(engine, &input, params_str.c_str(), nullptr), &spk_session_release);
    require(bool(session), "Open image: " + std::string(spk_last_error(engine)));
    return session;
}
}  // namespace

Frame::~Frame() { spk_result_free(&result_); }

std::vector<std::uint8_t> rgba16_to_bgra8(const spk_result& result) {
    require(result.rgba16 && result.width && result.height && result.row_stride_px >= result.width,
            "Invalid RGBA16 display dimensions or row pitch");
    const std::size_t count = std::size_t(result.width) * result.height * 4;
    std::vector<std::uint8_t> bgra(count);
    // Precompute 16-bit -> 8-bit lookup table (faster than per-pixel divide).
    static uint8_t conv[65536];
    static bool ready = [](){
        for(int i=0;i<65536;i++) conv[i]=(uint8_t)(((uint32_t)i+128u)/257u);
        return true;
    }();
    (void)ready;
    for (std::size_t y = 0; y < result.height; ++y) {
        const auto* src = result.rgba16 + y * result.row_stride_px * 4;
        auto* dst = bgra.data() + y * result.width * 4;
        for (std::size_t x = 0; x < result.width; ++x) {
            dst[4*x]   = conv[src[4*x+2]];
            dst[4*x+1] = conv[src[4*x+1]];
            dst[4*x+2] = conv[src[4*x]];
            dst[4*x+3] = 255;
        }
    }
    return bgra;
}

struct PreviewHost::Impl {
    fs::path resources;
    Catalog catalog;
    double engine_create_ms = 0.0;
    EnginePtr engine{nullptr, &spk_engine_destroy};
    // Declaration order makes session release precede engine destruction.
    SessionPtr session{nullptr, &spk_session_release};
    // Retained only for reopening after a failed settings/render operation.
    // Ordinary parameter edits reuse the current GPU source and negative.
    io::DecodedRaw decoded;
    FramePtr frame;
};

PreviewHost::PreviewHost(const fs::path& resources) : impl_(std::make_unique<Impl>()) {
    impl_->resources = fs::absolute(resources);
    const auto path = utf8(impl_->resources);
    require(fs::is_directory(impl_->resources), "Engine resources directory is missing");
    impl_->catalog = read_catalog(impl_->resources);
    const auto started = Clock::now();
    impl_->engine.reset(spk_engine_create(path.c_str(), nullptr));
    impl_->engine_create_ms = elapsed(started);
    require(bool(impl_->engine), "Engine creation failed: " + std::string(spk_last_error(nullptr)));
}

PreviewHost::~PreviewHost() = default;
const Catalog& PreviewHost::catalog() const noexcept { return impl_->catalog; }
double PreviewHost::engine_create_ms() const noexcept { return impl_->engine_create_ms; }
FramePtr PreviewHost::current() const noexcept { return impl_->frame; }

FramePtr PreviewHost::open_raw(const fs::path& source, DecodeMode mode, const RenderSettings& settings) {
    const auto started = Clock::now();
    const bool scan_film = validate_settings(settings, impl_->catalog);
    require(mode == DecodeMode::compatible16 || mode == DecodeMode::headroom, "Unknown RAW decode mode");
    const auto path = fs::absolute(source);
    require(fs::is_regular_file(path), "RAW input is missing: " + utf8(path));
    auto next = std::shared_ptr<Frame>(new Frame());
    next->source = path;
    next->decode_mode = mode;
    next->settings = settings;
    next->scan_film = scan_film;
    io::DecodedRaw decoded;
    std::string error;
    auto t = Clock::now();
    // Detect ordinary (non-RAW) images and load them with stb_image.
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    const bool is_srgb_image = (ext == ".jpg" || ext == ".jpeg" || ext == ".png" ||
                                ext == ".bmp" || ext == ".tif" || ext == ".tiff" || ext == ".webp");
    bool decoded_ok;
    if (is_srgb_image) {
        // stbi_load() uses ANSI fopen on Windows, which cannot open non-ASCII
        // paths. Read the whole file into memory with the wide-char API first,
        // then decode from memory -- this supports any path Windows allows.
        std::vector<unsigned char> file_bytes;
        if (FILE* f = _wfopen(path.wstring().c_str(), L"rb")) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz > 0) {
                file_bytes.resize(static_cast<size_t>(sz));
                size_t got = fread(file_bytes.data(), 1, static_cast<size_t>(sz), f);
                if (got != static_cast<size_t>(sz)) file_bytes.clear();
            }
            fclose(f);
        }
        if (file_bytes.empty()) {
            decoded_ok = false;
            error = "Cannot read file (locked, missing, or permission denied)";
        } else {
            int w = 0, h = 0, ch = 0;
            unsigned char* pixels = stbi_load_from_memory(file_bytes.data(), static_cast<int>(file_bytes.size()),
                                    &w, &h, &ch, 3);
            if (!pixels) {
                decoded_ok = false;
                error = std::string("Cannot decode image (") + stbi_failure_reason() +
                        "). This may be a CMYK or unsupported JPEG; try converting to sRGB JPEG.";
            } else {
            decoded.width = static_cast<std::uint32_t>(w);
            decoded.height = static_cast<std::uint32_t>(h);
            decoded.rgb.resize(static_cast<std::size_t>(w) * h * 3);
            for (std::size_t i = 0; i < decoded.rgb.size(); ++i)
                decoded.rgb[i] = pixels[i] / 255.0f;
            stbi_image_free(pixels);
            decoded_ok = true;
            }
        }
        next->decode_srgb = true;
    } else {
        decoded_ok = mode == DecodeMode::headroom
            ? io::decode_raw_headroom(path, decoded, error)
            : io::decode_raw_compatible(path, decoded, error);
        next->decode_srgb = false;
    }
    require(decoded_ok, "Image decode: " + error);
    next->timings.decode_ms = elapsed(t);
    next->metadata = decoded.metadata;
    next->decode_timings = decoded.timings;
    // Save raw decoded pixels (pre-film) for before/after comparison.
    next->raw_width = decoded.width;
    next->raw_height = decoded.height;
    next->raw_bgra8.resize(decoded.width * decoded.height * 4);
    if (next->decode_srgb) {
        // JPG/PNG: already sRGB, just pack BGRA. No gamma needed.
        for(uint32_t i=0;i<decoded.width*decoded.height;i++) {
            next->raw_bgra8[i*4+0]=(uint8_t)(decoded.rgb[i*3+2]*255.0f+0.5f);
            next->raw_bgra8[i*4+1]=(uint8_t)(decoded.rgb[i*3+1]*255.0f+0.5f);
            next->raw_bgra8[i*4+2]=(uint8_t)(decoded.rgb[i*3+0]*255.0f+0.5f);
            next->raw_bgra8[i*4+3]=255;
        }
    } else {
        // RAW: linear -> sRGB via lookup table (much faster than per-pixel pow).
        static uint8_t gamma_table[256];
        static bool table_ready = [](){
            for(int i=0;i<256;i++) {
                float c = i/255.0f;
                float v = c <= 0.0031308f ? c*12.92f : 1.055f*std::pow(c, 1.0f/2.4f) - 0.055f;
                gamma_table[i] = (uint8_t)(std::min(1.0f,std::max(0.0f,v))*255.0f+0.5f);
            }
            return true;
        }();
        (void)table_ready;
        for(uint32_t i=0;i<decoded.width*decoded.height;i++) {
            int ri=(int)(std::min(1.0f,std::max(0.0f,decoded.rgb[i*3+0]))*255.0f+0.5f);
            int gi=(int)(std::min(1.0f,std::max(0.0f,decoded.rgb[i*3+1]))*255.0f+0.5f);
            int bi=(int)(std::min(1.0f,std::max(0.0f,decoded.rgb[i*3+2]))*255.0f+0.5f);
            next->raw_bgra8[i*4+0]=gamma_table[bi];
            next->raw_bgra8[i*4+1]=gamma_table[gi];
            next->raw_bgra8[i*4+2]=gamma_table[ri];
            next->raw_bgra8[i*4+3]=255;
        }
    }
    t = Clock::now();
    auto session = open_session(impl_->engine.get(), decoded, settings, scan_film, next->decode_srgb);
    next->timings.open_ms = elapsed(t);
    t = Clock::now();
    const auto status = spk_render(session.get(), "full", &next->result_);
    require(status == SPK_OK, "Render: " + std::string(spk_last_error(impl_->engine.get())));
    next->timings.render_ms = elapsed(t);
    next->timings.engine_render_ms = next->result_.elapsed_ms;
    t = Clock::now();
    next->bgra8 = rgba16_to_bgra8(next->result_);
    next->timings.display_ms = elapsed(t);
    // Commit only after all allocations and GPU/CPU work succeeded.
    impl_->session = std::move(session);
    impl_->decoded = std::move(decoded);
    impl_->frame = next;
    next->timings.total_ms = elapsed(started);
    return next;
}

FramePtr PreviewHost::render(const RenderSettings& settings) {
    const auto started = Clock::now();
    require(bool(impl_->frame), "Open a RAW image before rendering");
    const bool scan_film = validate_settings(settings, impl_->catalog);
    auto next = std::shared_ptr<Frame>(new Frame());
    next->source = impl_->frame->source;
    next->decode_mode = impl_->frame->decode_mode;
    next->settings = settings;
    next->scan_film = scan_film;
    next->metadata = impl_->frame->metadata;
    next->decode_timings = impl_->frame->decode_timings;
    const auto delta = changed_params(impl_->frame->settings, settings, scan_film);
    try {
        auto t = Clock::now();
        if (!impl_->session) {
            // C API settings changes may fail after applying part of their
            // state. Never reuse such a session; rebuild from the retained
            // decoded input on the next request, without decoding the file.
            impl_->session = open_session(impl_->engine.get(), impl_->decoded, settings, scan_film, impl_->frame->decode_srgb);
            next->timings.open_ms = elapsed(t);
        } else if (!delta.fields().empty()) {
            const auto text = delta.dump();
            const auto status = spk_set_params(impl_->session.get(), text.c_str(), nullptr);
            require(status == SPK_OK, "Apply settings: " + std::string(spk_last_error(impl_->engine.get())));
            next->timings.params_ms = elapsed(t);
        }
        t = Clock::now();
        const auto status = spk_reprint(impl_->session.get(), "full", &next->result_);
        require(status == SPK_OK, "Render: " + std::string(spk_last_error(impl_->engine.get())));
        next->timings.render_ms = elapsed(t);
        next->timings.engine_render_ms = next->result_.elapsed_ms;
        t = Clock::now();
        next->bgra8 = rgba16_to_bgra8(next->result_);
        next->timings.display_ms = elapsed(t);
    } catch (...) {
        impl_->session.reset();
        throw;
    }
    impl_->frame = next;
    next->timings.total_ms = elapsed(started);
    return next;
}

void PreviewHost::export_tiff(const Frame& frame, const fs::path& destination) const {
    const auto& pixels = frame.pixels();
    std::string error;
    const bool written = io::write_srgb_tiff(destination, pixels.rgba16, pixels.width, pixels.height,
                                            pixels.row_stride_px, impl_->resources / "io" / "sRGB.icc", error);
    require(written, "TIFF export: " + error);
}

}  // namespace spk::desktop
