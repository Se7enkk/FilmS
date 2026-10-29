#include "PreviewController.hpp"
#include <QColorSpace>
#include <QMetaObject>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QImage>
#include <QFile>
#include <QDataStream>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace {
namespace desktop=spk::desktop;
namespace output=desktop::output;
namespace editing=desktop::editing;
QString text(const std::string& s) { return QString::fromUtf8(s.data(),qsizetype(s.size())); }
std::filesystem::path path(const QUrl& u) { return std::filesystem::path(u.toLocalFile().toStdWString()); }

// Minimal JPEG EXIF DateTime reader: returns "YYYY-MM-DD HH:MM:SS" or empty.
QString readJpegExifDateTime(const QString& filePath) {
    QFile f(filePath);
    if(!f.open(QIODevice::ReadOnly)) return {};
    QByteArray data = f.readAll();
    f.close();
    // JPEG starts with FFD8
    if(data.size() < 4 || (uchar)data[0] != 0xFF || (uchar)data[1] != 0xD8) return {};
    int pos = 2;
    while(pos + 4 < data.size()) {
        if((uchar)data[pos] != 0xFF) break;
        uchar marker = (uchar)data[pos+1];
        pos += 2;
        if(marker == 0xDA) break; // SOS
        if(marker == 0xD9) break; // EOI
        if(pos + 2 > data.size()) break;
        uint len = (uchar)data[pos] << 8 | (uchar)data[pos+1];
        if(marker == 0xE1) { // APP1 = EXIF
            if(pos + 8 < data.size() &&
               data[pos+2]=='E' && data[pos+3]=='x' && data[pos+4]=='i' && data[pos+5]=='f') {
                int exifStart = pos + 8; // TIFF header right after "Exif\0\0"
                // TIFF header: byte order + 42
                bool little = data[exifStart] == 'I';
                if(little || data[exifStart] == 'M') {
                    auto rd16 = [&](int off) -> uint {
                        return little ? (uchar)data[off] | ((uchar)data[off+1] << 8)
                                      : ((uchar)data[off] << 8) | (uchar)data[off+1];
                    };
                    auto rd32 = [&](int off) -> uint {
                        return little ? (uchar)data[off] | ((uchar)data[off+1] << 8) | ((uchar)data[off+2] << 16) | ((uchar)data[off+3] << 24)
                                      : ((uchar)data[off] << 24) | ((uchar)data[off+1] << 16) | ((uchar)data[off+2] << 8) | (uchar)data[off+3];
                    };
                    uint ifdOff = exifStart + rd32(exifStart+4);
                    // Walk IFD entries looking for tag 0x0132 (DateTime)
                    uint n = rd16(ifdOff);
                    for(uint i = 0; i < n; i++) {
                        uint e = ifdOff + 2 + i * 12;
                        if(e + 12 > data.size()) break;
                        uint tag = rd16(e);
                        if(tag == 0x0132) {
                            uint valOff = exifStart + rd32(e+8);
                            if(valOff + 19 <= data.size()) {
                                // "YYYY:MM:DD HH:MM:SS" -> "YYYY-MM-DD HH:MM:SS"
                                QByteArray raw = data.mid(valOff, 19);
                                raw[4] = '-'; raw[7] = '-';
                                return QString::fromLatin1(raw);
                            }
                        }
                    }
                }
            }
        }
        pos += len;
    }
    return {};
}
QVariantList list(const std::vector<desktop::Stock>& stocks) {
    QVariantList result;
    for(const auto& s:stocks) result.push_back(QVariantMap{{"id",text(s.id)},{"name",text(s.label)},{"positive",s.is_positive}});
    return result;
}
bool sameSettings(desktop::RenderSettings a, desktop::RenderSettings b) {
    const bool exposure=std::abs(a.print_exposure-b.print_exposure)<1e-12;
    a.print_exposure=b.print_exposure;
    return exposure&&a==b;
}
bool sameState(const editing::State& a,const editing::State& b) {
    return sameSettings(a.settings,b.settings)&&a.mode==b.mode&&a.geometry==b.geometry;
}
QImage visibleImage(const desktop::FramePtr& frame,const QImage& full,const output::Geometry& geometry) {
    if(geometry.quarterTurns==0&&!geometry.flipHorizontal&&!geometry.flipVertical&&geometry.straightenDegrees==0)
        return output::displayImage(full,geometry.crop);
    return output::displayFrame(frame,geometry);
}
output::ExportOptions exportOptions(int format,int quality,int maxEdge) {
    if(format<0||format>3||quality<1||quality>100||maxEdge<0||maxEdge>100000)
        throw std::runtime_error("Invalid export options");
    return {static_cast<output::Format>(format),quality,maxEdge};
}
}
QImage PreviewController::imageFromFrame(const desktop::FramePtr& frame) {
    // QImage is implicitly shared; the cleanup holder keeps the independently
    // owned engine result and its display bytes alive on the scene-graph side.
    auto held=std::make_unique<desktop::FramePtr>(frame);
    QImage image(frame->bgra8.data(),int(frame->width()),int(frame->height()),
                 qsizetype(frame->width())*4,QImage::Format_ARGB32,
                 [](void* p){delete static_cast<desktop::FramePtr*>(p);},held.get());
    if(image.isNull()) throw std::runtime_error("Cannot create the display image");
    held.release();
    image.setColorSpace(QColorSpace::SRgb);
    // Display-side post-processing on the 8-bit sRGB copy.
    const auto& s=frame->settings;
    const bool hasGrade=s.brightness!=0.0||s.contrast!=1.0||s.saturation!=1.0
        ||s.r_gain!=1.0||s.g_gain!=1.0||s.b_gain!=1.0
        ||s.black_point!=0.0||s.white_point!=255.0||s.curve_strength!=0.0
        ||s.temperature!=0.0||s.tint!=0.0||s.highlights!=0.0||s.shadows!=0.0
        ||s.vignette_amount!=0.0||s.overscan_active;
    if(hasGrade){
        image=image.copy();
        const double b=s.brightness,c=s.contrast,sa=s.saturation;
        const double rg=s.r_gain,gg=s.g_gain,bg=s.b_gain;
        const double bp=s.black_point,wp=s.white_point,cs=s.curve_strength;
        const double temp=s.temperature,tint=s.tint,hl=s.highlights,sh=s.shadows;
        const double invRange=1.0/std::max(1.0,wp-bp);
        // Vignette precompute
        const double vAmt=s.vignette_amount/100.0;
        const double vMid=s.vignette_midpoint/100.0;
        const double cx=image.width()/2.0, cy=image.height()/2.0;
        const double maxDist=std::sqrt(cx*cx+cy*cy);
        // Temperature: +amber (r+, b-), tint: +magenta (g-)
        const double tR=temp*0.3, tB=-temp*0.3, tG=-tint*0.3;
        for(int y=0;y<image.height();++y){
            uchar* line=image.scanLine(y);
            const double dy=y-cy;
            for(int x=0;x<image.width();++x){
                uchar* p=line+x*4;
                double r=p[2],g=p[1],bl=p[0];
                // White balance (temperature/tint)
                r+=tR;g+=tG;bl+=tB;
                // Black/white point remap
                r=(r-bp)*invRange*255.0;
                g=(g-bp)*invRange*255.0;
                bl=(bl-bp)*invRange*255.0;
                // Per-channel RGB gain
                r*=rg;g*=gg;bl*=bg;
                // Highlights / shadows (luminance-weighted)
                double lum=0.299*r+0.587*g+0.114*bl;
                if(hl!=0.0){double w=std::clamp((lum-128.0)/127.0,0.0,1.0);r+=hl*w*0.5;g+=hl*w*0.5;bl+=hl*w*0.5;}
                if(sh!=0.0){double w=std::clamp((128.0-lum)/128.0,0.0,1.0);r+=sh*w*0.5;g+=sh*w*0.5;bl+=sh*w*0.5;}
                // S-curve
                if(cs>0.0){
                    for(double* v:{&r,&g,&bl}){
                        double t=std::clamp(*v/255.0,0.0,1.0);
                        double ss=t*t*(3.0-2.0*t);
                        *v=(t+cs*(ss-t))*255.0;
                    }
                }
                // Brightness / contrast / saturation
                r+=b;g+=b;bl+=b;
                r=(r-128.0)*c+128.0;g=(g-128.0)*c+128.0;bl=(bl-128.0)*c+128.0;
                double gray=0.299*r+0.587*g+0.114*bl;
                r=gray+(r-gray)*sa;g=gray+(g-gray)*sa;bl=gray+(bl-gray)*sa;
                // Vignette
                if(vAmt!=0.0){
                    double dx=x-cx, dist=std::sqrt(dx*dx+dy*dy)/maxDist;
                    double f=std::clamp((dist-vMid)/(1.0-vMid),0.0,1.0);
                    double mult=1.0-vAmt*f*f;
                    r*=mult;g*=mult;bl*=mult;
                }
                p[2]=uchar(std::clamp(int(std::lround(r)),0,255));
                p[1]=uchar(std::clamp(int(std::lround(g)),0,255));
                p[0]=uchar(std::clamp(int(std::lround(bl)),0,255));
            }
        }
    }
    // Film edge (overscan) is now rendered by the Vulkan engine; no CPU fallback.
    return image;
}

PreviewController::PreviewController(std::filesystem::path resources,QObject* parent,ImageFactory factory,QString editDirectory)
    :QObject(parent),resources_(resources),
     edit_store_(editDirectory.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/Edits":editDirectory),
     image_factory_(factory?std::move(factory):ImageFactory(imageFromFrame)) {
    auto_apply_.setSingleShot(true);auto_apply_.setInterval(180);
    connect(&auto_apply_,&QTimer::timeout,this,[this]{if(dirty())apply();});
    save_timer_.setSingleShot(true);save_timer_.setInterval(350);
    connect(&save_timer_,&QTimer::timeout,this,&PreviewController::flushEdits);
    worker_=std::thread([this,resources=std::move(resources)]{workerLoop(resources);});
}
PreviewController::~PreviewController() {
    auto_apply_.stop();
    endEditGesture();flushEdits();
    if(cancel_batch_)cancel_batch_->store(true);
    ++generation_;
    {std::lock_guard lock(mutex_);stopping_=true;jobs_.clear();}
    wake_.notify_one();
    if(worker_.joinable()) worker_.join();
}
void PreviewController::workerLoop(std::filesystem::path resources) {
    std::unique_ptr<desktop::PreviewHost> host;
    Reply init;init.operation="init";
    try {host=std::make_unique<desktop::PreviewHost>(resources);init.catalog=host->catalog();}
    catch(const std::exception& e){init.error=text(e.what());}
    deliver(std::move(init));
    if(!host)return;
    for(;;) {
        Job job;
        {std::unique_lock lock(mutex_);wake_.wait(lock,[&]{return stopping_||!jobs_.empty();});
         if(stopping_)break;job=std::move(jobs_.front());jobs_.pop_front();}
        deliver(job(*host));
    }
}
void PreviewController::deliver(Reply reply) {
    QMetaObject::invokeMethod(this,[this,reply=std::move(reply)]() mutable {accept(std::move(reply));},Qt::QueuedConnection);
}
void PreviewController::submit(Job job,QString status,QString operation) {
    auto_apply_.stop();active_operation_=std::move(operation);
    busy_=true;failed_=false;status_=std::move(status);emit stateChanged();emit historyChanged();
    {std::lock_guard lock(mutex_);jobs_.push_back(std::move(job));}
    wake_.notify_one();
}
void PreviewController::accept(Reply reply) {
    if(reply.generation!=generation_)return; // a retired generation never replaces the canvas
    const bool superseded=reply.operation=="render"&&reply.settings_revision!=settings_revision_;
    busy_=false;active_operation_.clear();failed_=!superseded&&!reply.error.isEmpty();
    if(superseded) {
        // The worker may finish an earlier choice after the next click. Keep
        // both the displayed frame and the newest desired controls; never flash
        // the retired result or restore its settings over the user's last edit.
        status_=dirty()?QStringLiteral("正在更新预览…"):QStringLiteral("预览已是当前选择");
    } else if(failed_) {
        status_=reply.error;
        if(frame_&&reply.operation!="export"&&reply.operation!="batch-export")restoreSettings();
    } else if(reply.operation=="init") {
        ready_=true;catalog_=std::move(reply.catalog);films_=list(catalog_.films);papers_=list(catalog_.papers);
        restoreSettings();emit catalogChanged();status_=QStringLiteral("就绪 · 打开 RAW 开始");
    } else if(reply.frame) {
        // Allocate the presentation wrapper before publishing any new UI state.
        try {
            auto next=image_factory_(reply.frame);
            if(next.isNull())throw std::runtime_error("Cannot create the display image");
            // Save the original image for before/after comparison.
            if(reply.operation=="open") {
                jpeg_datetime_ = reply.jpeg_datetime;
                if(!reply.original_qimage.isNull()) {
                    reply.original_qimage.setColorSpace(QColorSpace::SRgb);
                    original_full_image_=reply.original_qimage;
                } else {
                    const auto& fr=*reply.frame;
                    // Use raw decoded pixels (pre-film) for true before comparison.
                    if(!fr.raw_bgra8.empty() && fr.raw_width>0 && fr.raw_height>0) {
                        QImage temp(fr.raw_bgra8.data(), int(fr.raw_width), int(fr.raw_height),
                                    qsizetype(fr.raw_width)*4, QImage::Format_ARGB32);
                        QImage orig=temp.copy();
                        orig.setColorSpace(QColorSpace::SRgb);
                        original_full_image_=std::move(orig);
                    } else {
                        QImage temp(fr.bgra8.data(),int(fr.width()),int(fr.height()),
                                    qsizetype(fr.width())*4,QImage::Format_ARGB32);
                        QImage orig=temp.copy();
                        orig.setColorSpace(QColorSpace::SRgb);
                        original_full_image_=std::move(orig);
                    }
                }
            }
            const auto geometry=reply.operation=="open"?reply.geometry:geometry_;
            auto visible=visibleImage(reply.frame,next,geometry);
            frame_=std::move(reply.frame);full_image_=std::move(next);image_=std::move(visible);
            geometry_=geometry;
            if(reply.operation=="open")active_index_=reply.library_index;
        }
        catch(const std::exception& e){failed_=true;status_=text(e.what());if(frame_)restoreSettings();}
        if(!failed_) {
            restoreSettings();
            updateThumbnail();
            status_=QStringLiteral("渲染完成 %1 秒%2")
                .arg(frame_->timings.render_ms/1000,0,'f',2)
                .arg(frame_->reprint()?QStringLiteral(" · 已复用胶片"):QString());
            emit frameChanged();emit geometryChanged();if(reply.operation=="open")emit sourceOpened();
        }
    } else if(reply.operation=="export") status_=QStringLiteral("已导出当前画面");
    else if(reply.operation=="batch-export")status_=reply.message;
    if(failed_&&reply.operation=="open"&&reply.library_index>=0&&reply.library_index<int(library_.size()))
        library_[reply.library_index].error=status_;
    if(failed_&&reply.operation!="export"&&reply.operation!="batch-export"&&active_index_>=0){
        auto& history=library_[active_index_].undo;
        while(!history.empty()&&sameState(history.back(),currentState()))history.pop_back();
    }
    if(reply.operation=="batch-export")cancel_batch_.reset();
    emit libraryChanged();
    emit historyChanged();
    emit stateChanged();
    if(reply.operation=="init")emit initialized(!failed_);
    else emit operationFinished(reply.operation,!failed_);
    // Export holds an immutable displayed frame. An edit made just before it
    // began still needs applying, including after an overwrite refusal.
    if((superseded||reply.operation=="export"||reply.operation=="batch-export")&&!busy_)schedulePreview();
}
desktop::RenderSettings PreviewController::settings() const {
    desktop::RenderSettings s=extra_settings_;
    if(film_index_>=0&&std::size_t(film_index_)<catalog_.films.size())s.film_stock=catalog_.films[film_index_].id;
    if(paper_index_>=0&&std::size_t(paper_index_)<catalog_.papers.size())s.print_stock=catalog_.papers[paper_index_].id;
    s.print_exposure=std::exp2(-exposure_ev_);return s;
}
void PreviewController::restoreSettings() {
    const auto s=frame_?frame_->settings:desktop::RenderSettings{};
    loadSettings(s,frame_?frame_->decode_mode:desktop::DecodeMode::compatible16);
    saveActiveSettings();
}
void PreviewController::loadSettings(const desktop::RenderSettings& s,desktop::DecodeMode mode) {
    extra_settings_=s;
    for(std::size_t i=0;i<catalog_.films.size();++i)if(catalog_.films[i].id==s.film_stock)film_index_=int(i);
    for(std::size_t i=0;i<catalog_.papers.size();++i)if(catalog_.papers[i].id==s.print_stock)paper_index_=int(i);
    exposure_ev_=-std::log2(s.print_exposure);
    decode_mode_=mode==desktop::DecodeMode::headroom?1:0;
    emit settingsChanged();
}
bool PreviewController::dirty() const {
    if(!frame_)return false;
    return !sameSettings(settings(),frame_->settings)||
        decode_mode_!=(frame_->decode_mode==desktop::DecodeMode::headroom?1:0);
}
bool PreviewController::filmIsPositive() const {
    return film_index_>=0&&std::size_t(film_index_)<catalog_.films.size()&&catalog_.films[film_index_].is_positive;
}
QString PreviewController::fileName() const {return frame_?QString::fromStdWString(frame_->source.filename().wstring()):QString();}
QUrl PreviewController::sourceFolder() const {return frame_?QUrl::fromLocalFile(QString::fromStdWString(frame_->source.parent_path().wstring())):QUrl();}
QVariantMap PreviewController::metadata() const {
    if(!frame_)return {};
    const auto& m = frame_->metadata;
    QStringList exif;
    if(!m.lens.empty()) exif << QString::fromStdString(m.lens);
    if(m.iso > 0) exif << "ISO " + QString::number(int(m.iso));
    if(m.shutter > 0) {
        QString s = m.shutter >= 1.0 ? QString::number(m.shutter,'f',1) + "s" : "1/" + QString::number(int(0.5/m.shutter)) + "s";
        exif << s;
    }
    if(m.aperture > 0) exif << "f/" + QString::number(m.aperture,'f',1);
    if(m.focal_length > 0) exif << QString::number(int(m.focal_length)) + "mm";
    if(!m.datetime.empty()) exif << QString::fromStdString(m.datetime);
    else if(!jpeg_datetime_.isEmpty()) exif << jpeg_datetime_;
    return {{"width",image_.width()},{"height",image_.height()},
        {"sourceWidth",frame_->width()},{"sourceHeight",frame_->height()},
        {"camera",text(m.make+" "+m.model)},
        {"exif",exif.join("  ·  ")},
        {"datetime",!m.datetime.empty()?QString::fromStdString(m.datetime):jpeg_datetime_},
        {"decodeMs",frame_->timings.decode_ms},{"renderMs",frame_->timings.render_ms},
        {"totalMs",frame_->timings.total_ms},{"cached",frame_->reprint()},{"scanFilm",frame_->scan_film},
        {"mode",frame_->decode_mode==desktop::DecodeMode::headroom?"headroom":"compatible16"}};
}
void PreviewController::schedulePreview(){if(!dialog_open_&&gesture_key_!="straightenDegrees"&&frame_&&dirty()&&!auto_apply_.isActive())auto_apply_.start();}
void PreviewController::setDialogOpen(bool open){
    if(dialog_open_==open)return;
    dialog_open_=open;
    if(open){endEditGesture();auto_apply_.stop();}else schedulePreview();
    emit stateChanged();emit historyChanged();
}
void PreviewController::changed(const QString& editKey){
    if(gesture_index_>=0&&gesture_key_!=editKey)endEditGesture();
    saveActiveSettings(true);
    ++settings_revision_;auto_apply_.stop();schedulePreview();
    if(frame_)status_=QStringLiteral("正在更新预览…");
    failed_=false;emit settingsChanged();emit stateChanged();
}
void PreviewController::setFilmIndex(int v){if(!settingsEditable()||v<0||v>=films_.size()||v==film_index_)return;film_index_=v;changed("filmIndex");}
void PreviewController::setPaperIndex(int v){if(!settingsEditable()||filmIsPositive()||v<0||v>=papers_.size()||v==paper_index_)return;paper_index_=v;changed("paperIndex");}
void PreviewController::setDecodeMode(int v){if(!settingsEditable()||v<0||v>1||v==decode_mode_)return;decode_mode_=v;changed("decodeMode");}
void PreviewController::setExposureEv(double v){if(!settingsEditable()||filmIsPositive()||!std::isfinite(v)||v< -3||v>3||v==exposure_ev_)return;exposure_ev_=v;changed("exposureEv");}
void PreviewController::resetSettings(){if(!settingsEditable())return;loadSettings({},desktop::DecodeMode::compatible16);changed();}
void PreviewController::openRaw(const QUrl& url) {
    // Changing sources and exporting remain single flight. Parameter edits
    // can be coalesced during a render, but cannot retarget an in-flight open.
    if(!ready_||busy_||!url.isLocalFile())return;
    saveActiveSettings();
    const int index=addFile(url);if(index<0)return;
    // A known photo restores its own saved settings. A genuinely new photo
    // still starts with the current preset; opening never overwrites a sidecar.
    openItem(index);
}
void PreviewController::openItem(int index) {
    endEditGesture();
    const auto item=library_.at(index);const auto p=path(item.url);
    const auto gen=++generation_;
    submit([p,item,gen,index](auto& host){Reply r;r.generation=gen;r.operation="open";r.library_index=index;r.geometry=item.geometry;
        try{
            r.frame=host.open_raw(p,item.mode,item.settings);
            // For JPG/PNG, read the original file directly for true "before" comparison.
            std::string ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if(ext==".jpg"||ext==".jpeg"||ext==".png"||ext==".bmp"||ext==".tif"||ext==".tiff"||ext==".webp") {
                QImage orig(QString::fromStdWString(p.wstring()));
                if(!orig.isNull()) r.original_qimage = orig.convertToFormat(QImage::Format_RGBA8888);
                // Read EXIF DateTime from JPEG.
                if(ext==".jpg"||ext==".jpeg") {
                    r.jpeg_datetime = readJpegExifDateTime(QString::fromStdWString(p.wstring()));
                }
            }
        }catch(const std::exception& e){r.error=text(e.what());}return r;},QStringLiteral("正在解码并渲染 RAW…"),"open");
}
void PreviewController::apply() {
    if(!frame_||busy_||dialog_open_)return;
    const auto s=settings();const auto shown=frame_;const auto gen=++generation_;const auto revision=settings_revision_;
    const auto mode=decode_mode_?desktop::DecodeMode::headroom:desktop::DecodeMode::compatible16;
    submit([s,shown,gen,revision,mode](auto& host){Reply r;r.generation=gen;r.settings_revision=revision;r.operation="render";
        try {
            // Publication can fail after a successful worker open (for example,
            // a display allocation failure). Reconcile against the image the
            // user is actually editing before reusing the worker's session.
            const auto active=host.current();
            r.frame=active&&active->source==shown->source&&active->decode_mode==mode
                ?host.render(s):host.open_raw(shown->source,mode,s);
        }catch(const std::exception& e){r.error=text(e.what());}return r;},QStringLiteral("正在更新胶片效果…"),"render");
}
void PreviewController::exportTiff(const QUrl& url) {
    exportImage(url,0,95,0);
}
void PreviewController::exportImage(const QUrl& url,int format,int quality,int maxLongEdge) {
    if(!frame_||busy_||!url.isLocalFile())return;
    const auto p=path(url);const auto held=frame_;const auto gen=++generation_;const auto geometry=geometry_;const auto resources=resources_;
    // Apply the same display-side post-grade (temperature, brightness, etc.) to
    // the export image so the exported file matches what the user sees.
    QImage graded = imageFromFrame(held).copy();
    // Convert ARGB32 -> RGBA64 so the exporter (especially TIFF16) reads correct 16-bit pixels.
    QImage graded64 = graded.convertToFormat(QImage::Format_RGBA64);
    submit([p,held,gen,geometry,resources,format,quality,maxLongEdge,graded64=std::move(graded64)](auto&){Reply r;r.generation=gen;r.operation="export";
        try{
            output::exportImageFile(graded64, p, geometry, exportOptions(format,quality,maxLongEdge), resources);
        }catch(const std::exception& e){r.error=text(e.what());}return r;},QStringLiteral("正在导出当前画面…"),"export");
}

QVariantMap PreviewController::adjustments() const {
    const auto& s=extra_settings_;
    return {{"filmExposureEv",s.film_exposure_ev},{"filmFormatMm",s.film_format_mm},
        {"grainActive",s.grain_active},{"grainAmount",s.grain_amount},
        {"halationActive",s.halation_active},{"halationAmount",s.halation_amount},
        {"glareActive",s.glare_active},{"glareAmount",s.glare_amount},
        {"yFilterShift",s.y_filter_shift},{"mFilterShift",s.m_filter_shift},
        {"preflash",s.preflash_exposure*100.0},
        {"brightness",s.brightness},{"contrast",s.contrast},{"saturation",s.saturation},
        {"rGain",s.r_gain},{"gGain",s.g_gain},{"bGain",s.b_gain},
        {"blackPoint",s.black_point},{"whitePoint",s.white_point},{"curveStrength",s.curve_strength},
        {"temperature",s.temperature},{"tint",s.tint},{"highlights",s.highlights},
        {"shadows",s.shadows},{"vignetteAmount",s.vignette_amount},{"vignetteMidpoint",s.vignette_midpoint},
        {"overscanActive",s.overscan_active},{"overscanFormat",QString::fromStdString(s.overscan_format)},
        {"overscanGate",QString::fromStdString(s.overscan_gate)},
        {"overscanHoles",QString::fromStdString(s.overscan_holes)},
        {"overscanCarrier",QString::fromStdString(s.overscan_carrier)},
        {"overscanFog",s.overscan_fog},{"overscanLeaks",s.overscan_leaks},
        {"dateBack",s.date_back_active}};
}
void PreviewController::setAdjustment(const QString& name,const QVariant& value) {
    if(!settingsEditable())return;
    auto next=extra_settings_;
    if(name=="grainActive")next.grain_active=value.toBool();
    else if(name=="halationActive")next.halation_active=value.toBool();
    else if(name=="glareActive")next.glare_active=value.toBool();
    else if(name=="overscanActive")next.overscan_active=value.toBool();
    else if(name=="overscanFormat"){next.overscan_format=value.toString().toStdString();}
    else if(name=="overscanGate"){next.overscan_gate=value.toString().toStdString();}
    else if(name=="overscanHoles"){next.overscan_holes=value.toString().toStdString();}
    else if(name=="overscanCarrier"){next.overscan_carrier=value.toString().toStdString();}
    else if(name=="dateBack"){
        next.date_back_active=value.toBool();
        if(next.date_back_active && next.date_back_text.empty()) {
            // Use RAW metadata first, then JPG EXIF read on the main thread.
            std::string dt;
            if(frame_ && !frame_->metadata.datetime.empty()) dt = frame_->metadata.datetime;
            else if(!jpeg_datetime_.isEmpty()) dt = jpeg_datetime_.toStdString();
            // Accept both "2026-10-29 ..." and "2026:10:29 ..."
            if(dt.size() >= 10 && (dt[4]=='-' || dt[4]==':') && (dt[7]=='-' || dt[7]==':')) {
                next.date_back_text = "'" + dt.substr(2,2) + " " + dt.substr(5,2) + " " + dt.substr(8,2);
            }
        }
    }
    else if(name=="dateBackText"){next.date_back_text=value.toString().toStdString();}
    else if(name=="dateBackPlacement"){next.date_back_placement=value.toString().toStdString();}
    else {
        bool ok=false;const double v=value.toDouble(&ok);if(!ok||!std::isfinite(v))return;
        double* field=nullptr;double low=0,high=0;
        if(name=="filmExposureEv"){field=&next.film_exposure_ev;low=-8;high=8;}
        else if(name=="filmFormatMm"){field=&next.film_format_mm;low=4;high=200;}
        else if(name=="grainAmount"){field=&next.grain_amount;high=2;}
        else if(name=="halationAmount"){field=&next.halation_amount;high=4;}
        else if(name=="glareAmount"){field=&next.glare_amount;high=30;}
        else if(name=="yFilterShift"){field=&next.y_filter_shift;low=-1;high=1;}
        else if(name=="mFilterShift"){field=&next.m_filter_shift;low=-1;high=1;}
        else if(name=="preflash"){next.preflash_exposure=v/100.0;if(next.preflash_exposure<0||next.preflash_exposure>0.03)return;changed(name);return;}
        else if(name=="brightness"){field=&next.brightness;low=-100;high=100;}
        else if(name=="contrast"){field=&next.contrast;low=0.5;high=2;}
        else if(name=="saturation"){field=&next.saturation;low=0;high=2;}
        else if(name=="rGain"){field=&next.r_gain;low=0.5;high=1.5;}
        else if(name=="gGain"){field=&next.g_gain;low=0.5;high=1.5;}
        else if(name=="bGain"){field=&next.b_gain;low=0.5;high=1.5;}
        else if(name=="blackPoint"){field=&next.black_point;low=0;high=30;}
        else if(name=="whitePoint"){field=&next.white_point;low=225;high=255;}
        else if(name=="curveStrength"){field=&next.curve_strength;low=0;high=1;}
        else if(name=="temperature"){field=&next.temperature;low=-100;high=100;}
        else if(name=="tint"){field=&next.tint;low=-100;high=100;}
        else if(name=="highlights"){field=&next.highlights;low=-100;high=100;}
        else if(name=="shadows"){field=&next.shadows;low=-100;high=100;}
        else if(name=="vignetteAmount"){field=&next.vignette_amount;low=-100;high=100;}
        else if(name=="vignetteMidpoint"){field=&next.vignette_midpoint;low=0;high=100;}
        else if(name=="overscanFog"){field=&next.overscan_fog;low=0;high=4;}
        else if(name=="overscanLeaks"){field=&next.overscan_leaks;low=0;high=4;}
        else return;
        if(v<low||v>high)return;*field=v;
    }
    if(next==extra_settings_)return;
    extra_settings_=next;changed(name);
}
QVariantList PreviewController::libraryItems() const {
    QVariantList result;
    for(int i=0;i<int(library_.size());++i) {
        const auto& item=library_[i];
        result.push_back(QVariantMap{{"name",QFileInfo(item.url.toLocalFile()).fileName()},
            {"url",item.url},{"thumbnail",item.thumbnail},{"active",i==active_index_},
            {"selected",item.selected},{"error",item.error}});
    }
    return result;
}
int PreviewController::selectedCount() const {
    int count=0;for(const auto& item:library_)count+=item.selected;return count;
}
void PreviewController::saveActiveSettings(bool recordHistory) {
    if(active_index_<0||active_index_>=int(library_.size()))return;
    auto& item=library_[active_index_];const auto desired=settings();
    const auto mode=decode_mode_?desktop::DecodeMode::headroom:desktop::DecodeMode::compatible16;
    // A saved pending edit has no rendered thumbnail yet. Do not let the
    // strip present an older look as if it represented the new settings.
    const bool changed=!sameSettings(item.settings,desired)||item.mode!=mode||item.geometry!=geometry_;
    if(changed){
        if(recordHistory)recordEdit(item,itemState(item));
        item.thumbnail={};item.pending_save=true;
    }
    item.settings=desired;item.mode=mode;
    item.geometry=geometry_;
    if(changed){scheduleSave();emit libraryChanged();emit historyChanged();}
}
void PreviewController::updateThumbnail() {
    if(active_index_<0||active_index_>=int(library_.size()))return;
    auto& item=library_[active_index_];
    item.thumbnail=dirty()?QImage{}:image_.scaled(180,120,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    item.error.clear();emit libraryChanged();
}
int PreviewController::addFile(const QUrl& url) {
    if(!url.isLocalFile())return -1;
    const QFileInfo info(url.toLocalFile());
    if(!info.isFile())return -1;
    const auto local=QUrl::fromLocalFile(info.canonicalFilePath());
    for(int i=0;i<int(library_.size());++i)
        if(library_[i].url.toLocalFile().compare(local.toLocalFile(),Qt::CaseInsensitive)==0)return i;
    LibraryItem item;item.url=local;item.settings=desktop::RenderSettings{};
    item.mode=decode_mode_?desktop::DecodeMode::headroom:desktop::DecodeMode::compatible16;
    const auto stored=edit_store_.load(local,catalog_);
    item.save_blocked=stored.blocked;item.save_warning=stored.warning;
    // Do not restore saved settings; always start fresh with defaults.
    library_.push_back(std::move(item));emit libraryChanged();return int(library_.size())-1;
}
void PreviewController::openFiles(const QVariantList& urls) {
    if(!ready_||busy_)return;
    saveActiveSettings();int first=-1;
    for(const auto& url:urls){const int index=addFile(url.toUrl());if(first<0&&index>=0)first=index;}
    if(first>=0)openItem(first);
}
void PreviewController::selectImage(int index) {
    if(!ready_||busy_||dialog_open_||index<0||index>=int(library_.size())||index==active_index_)return;
    saveActiveSettings();openItem(index);
}
void PreviewController::setItemSelected(int index,bool selected) {
    if(busy_||dialog_open_||index<0||index>=int(library_.size()))return;
    library_[index].selected=selected;emit libraryChanged();
}
void PreviewController::selectAllPhotos(bool selected) {
    if(busy_||dialog_open_)return;
    for(auto& item:library_)item.selected=selected;emit libraryChanged();
}
void PreviewController::syncSelectedSettings(bool includeCrop) {
    if(!frame_||busy_||dialog_open_)return;
    saveActiveSettings();int count=0;
    for(int i=0;i<int(library_.size());++i)if(i!=active_index_&&library_[i].selected){
        auto& item=library_[i];auto next=currentState();
        if(!includeCrop)next.geometry=item.geometry;
        if(sameState(itemState(item),next))continue;
        // Each recipient owns its own undo history, just like its settings.
        item.undo.push_back(itemState(item));if(item.undo.size()>64)item.undo.erase(item.undo.begin());item.redo.clear();
        item.settings=next.settings;item.mode=next.mode;item.geometry=next.geometry;item.pending_save=true;
        item.thumbnail={};item.error.clear();++count;
    }
    status_=QStringLiteral("已将当前参数同步至 %1 张照片%2").arg(count)
        .arg(includeCrop?QStringLiteral("（含裁切）"):QStringLiteral("（各自保留裁切）"));
    scheduleSave();emit libraryChanged();emit historyChanged();emit stateChanged();
}
void PreviewController::setCropRect(const QRectF& crop) {
    auto next=geometry_;next.crop=crop;setGeometry(next,"cropRect");
}
void PreviewController::setGeometry(const output::Geometry& geometry,const QString& editKey) {
    if(!frame_||busy_||dialog_open_||geometry==geometry_)return;
    try {
        auto next=visibleImage(frame_,full_image_,geometry);
        if(gesture_index_>=0&&gesture_key_!=editKey)endEditGesture();
        image_=std::move(next);geometry_=geometry;saveActiveSettings(true);updateThumbnail();
        failed_=false;emit geometryChanged();emit frameChanged();emit stateChanged();
    } catch(const std::exception& e){failed_=true;status_=text(e.what());emit stateChanged();}
}
void PreviewController::resetCrop(){setCropRect(QRectF(0,0,1,1));}
void PreviewController::setCropAspect(double ratio) {
    if(!frame_||!std::isfinite(ratio)||ratio<0)return;
    if(ratio==0){resetCrop();return;}
    // Ratios describe the displayed result, even after a quarter turn.
    if(geometry_.quarterTurns%2)ratio=1/ratio;
    const double original=double(frame_->width())/frame_->height();
    double w=1,h=1;
    if(original>ratio)w=ratio/original;else h=original/ratio;
    setCropRect(QRectF((1-w)/2,(1-h)/2,w,h));
}
void PreviewController::cancelExport(){if(cancel_batch_)cancel_batch_->store(true);}

editing::State PreviewController::itemState(const LibraryItem& item) {
    return {item.settings,item.mode,item.geometry};
}
editing::State PreviewController::currentState() const {
    return {settings(),decode_mode_?desktop::DecodeMode::headroom:desktop::DecodeMode::compatible16,geometry_};
}
bool PreviewController::canUndo() const {
    return settingsEditable()&&active_index_>=0&&!library_[active_index_].undo.empty();
}
bool PreviewController::canRedo() const {
    return settingsEditable()&&active_index_>=0&&!library_[active_index_].redo.empty();
}
void PreviewController::recordEdit(LibraryItem& item,const editing::State& before) {
    if(gesture_index_!=active_index_||!gesture_recorded_) {
        item.undo.push_back(before);
        if(item.undo.size()>64)item.undo.erase(item.undo.begin());
        item.redo.clear();
        if(gesture_index_==active_index_)gesture_recorded_=true;
    }
}
void PreviewController::beginEditGesture(const QString& key) {
    if(!settingsEditable()||active_index_<0||key.isEmpty())return;
    endEditGesture();gesture_index_=active_index_;gesture_key_=key;gesture_recorded_=false;gesture_redo_=library_[active_index_].redo;
    if(key=="straightenDegrees")auto_apply_.stop();
}
void PreviewController::endEditGesture() {
    const bool heldGeometry=gesture_key_=="straightenDegrees";
    if(gesture_index_>=0&&gesture_index_<int(library_.size())&&gesture_recorded_) {
        auto& item=library_[gesture_index_];
        // A drag returning to its starting value is not an undo step.
        if(!item.undo.empty()&&sameState(item.undo.back(),itemState(item))){item.undo.pop_back();item.redo=std::move(gesture_redo_);}
    }
    gesture_index_=-1;gesture_key_.clear();gesture_recorded_=false;gesture_redo_.clear();emit historyChanged();
    if(heldGeometry)schedulePreview();
}
void PreviewController::undo(){restoreHistory(false);}
void PreviewController::redo(){restoreHistory(true);}
void PreviewController::restoreHistory(bool redo) {
    if(!frame_||!settingsEditable()||active_index_<0)return;
    endEditGesture();
    auto& item=library_[active_index_];auto& from=redo?item.redo:item.undo;
    auto& to=redo?item.undo:item.redo;
    if(from.empty())return;
    const auto target=from.back();
    try {
        // Prepare geometry before moving the history cursor. Allocation or
        // validation failure leaves the frame and both stacks intact.
        const bool geometryChangedNow=target.geometry!=geometry_;
        auto next=geometryChangedNow?visibleImage(frame_,full_image_,target.geometry):image_;
        to.push_back(currentState());if(to.size()>64)to.erase(to.begin());from.pop_back();
        geometry_=target.geometry;image_=std::move(next);
        loadSettings(target.settings,target.mode);saveActiveSettings();
        ++settings_revision_;auto_apply_.stop();schedulePreview();updateThumbnail();
        failed_=false;status_=redo?QStringLiteral("已重做当前照片调整"):QStringLiteral("已撤销当前照片调整");
        if(geometryChangedNow){emit geometryChanged();emit frameChanged();}
        emit settingsChanged();emit stateChanged();emit historyChanged();
    }catch(const std::exception& e){failed_=true;status_=text(e.what());emit stateChanged();}
}
void PreviewController::scheduleSave() {
    save_timer_.start();emit stateChanged();
}
void PreviewController::flushEdits() {
    save_timer_.stop();
    for(auto& item:library_)if(item.pending_save&&!item.save_blocked) {
        try {edit_store_.save(item.url,itemState(item));item.pending_save=false;item.has_stored=true;item.save_warning.clear();}
        catch(const std::exception& e){item.save_warning=text(e.what());}
    }
    emit stateChanged();
}
QString PreviewController::persistenceWarning() const {
    QStringList warnings;
    for(const auto& item:library_)if(!item.save_warning.isEmpty())
        warnings.append(QFileInfo(item.url.toLocalFile()).fileName()+": "+item.save_warning);
    return warnings.join("\n");
}
QString PreviewController::savedStatus() const {
    if(!persistenceWarning().isEmpty())return QStringLiteral("部分设置未保存，请查看下方提示");
    for(const auto& item:library_)if(item.pending_save)return QStringLiteral("正在自动保存设置…");
    if(active_index_>=0&&library_[active_index_].has_stored)return QStringLiteral("当前照片设置已自动保存");
    return QStringLiteral("调整将自动保存，重新打开照片时恢复");
}
// Flips are stored in output axes. Rotating the displayed image also swaps
// those axes; without this a mirrored photo would turn the opposite way.
void PreviewController::rotateClockwise(){auto next=geometry_;next.quarterTurns=(next.quarterTurns+1)%4;std::swap(next.flipHorizontal,next.flipVertical);setGeometry(next,"quarterTurns");}
void PreviewController::rotateCounterClockwise(){auto next=geometry_;next.quarterTurns=(next.quarterTurns+3)%4;std::swap(next.flipHorizontal,next.flipVertical);setGeometry(next,"quarterTurns");}
void PreviewController::setFlipHorizontal(bool value){auto next=geometry_;next.flipHorizontal=value;setGeometry(next,"flipHorizontal");}
void PreviewController::setFlipVertical(bool value){auto next=geometry_;next.flipVertical=value;setGeometry(next,"flipVertical");}
void PreviewController::setStraightenDegrees(double value){auto next=geometry_;next.straightenDegrees=value;setGeometry(next,"straightenDegrees");}
void PreviewController::resetGeometry(){setGeometry(output::Geometry{});}

void PreviewController::exportBatch(const QUrl& folder,int format,int quality,int maxLongEdge) {
    if(!ready_||busy_||!folder.isLocalFile()||!selectedCount())return;
    saveActiveSettings();
    std::vector<LibraryItem> items;
    for(const auto& item:library_)if(item.selected)items.push_back(item);
    // Strip thumbnails from the job snapshot. Only one developed frame is
    // retained by the worker, regardless of the number of imported photos.
    for(auto& item:items)item.thumbnail={};
    const auto held=frame_;const auto resources=resources_;const auto destination=path(folder);
    const auto gen=++generation_;cancel_batch_=std::make_shared<std::atomic_bool>(false);
    const auto cancel=cancel_batch_;batch_progress_=0;batch_total_=int(items.size());
    submit([this,items=std::move(items),held,resources,destination,gen,cancel,format,quality,maxLongEdge](auto& host){
        Reply r;r.generation=gen;r.operation="batch-export";
        int done=0,skipped=0,errors=0,processed=0;QStringList details;
        try {
            const auto options=exportOptions(format,quality,maxLongEdge);
            if(!std::filesystem::is_directory(destination))throw std::runtime_error("Export destination is not a folder");
            const QString extension=format==0?".tif":format==3?".jpg":".png";
            QSet<QString> names;
            for(const auto& item:items) {
                if(cancel->load())break;
                const auto source=path(item.url);
                const QString stem=QFileInfo(item.url.toLocalFile()).completeBaseName();
                QString name=stem+extension;int suffix=2;
                while(names.contains(name.toCaseFolded()))name=stem+"-"+QString::number(suffix++)+extension;
                names.insert(name.toCaseFolded());
                const auto target=destination/std::filesystem::path(name.toStdWString());
                try {
                    if(std::filesystem::exists(target))++skipped;
                    else {
                        const bool useShown=held&&held->source==source&&held->decode_mode==item.mode&&sameSettings(held->settings,item.settings);
                        const auto developed=useShown?held:host.open_raw(source,item.mode,item.settings);
                        output::exportFrame(developed,target,item.geometry,options,resources);++done;
                    }
                }catch(const std::exception& e){++errors;details.append(name+": "+text(e.what()));}
                ++processed;
                QMetaObject::invokeMethod(this,[this,gen,processed]{if(generation_==gen){batch_progress_=processed;emit stateChanged();}},Qt::QueuedConnection);
            }
            r.message=QStringLiteral("%1：导出 %2 张，跳过已有 %3 张，失败 %4 张")
                .arg(cancel->load()?QStringLiteral("批量导出已取消"):QStringLiteral("批量导出完成")).arg(done).arg(skipped).arg(errors);
            if(errors)r.error=r.message+"\n"+details.join("\n");
        }catch(const std::exception& e){r.error=text(e.what());}
        return r;
    },QStringLiteral("正在批量导出…"),"batch-export");
}
