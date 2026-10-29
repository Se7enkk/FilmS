#include "FrameCanvas.hpp"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <algorithm>
#include <cmath>

namespace {class ImageNode final:public QSGSimpleTextureNode {public:quint64 revision=0;};}
FrameCanvas::FrameCanvas(QQuickItem* parent):QQuickItem(parent) {
    setFlag(ItemHasContents,true);setClip(true);setAcceptedMouseButtons(Qt::LeftButton);setAcceptHoverEvents(true);
}
void FrameCanvas::setImage(const QImage& image) {
    if(image.cacheKey()==image_.cacheKey())return;
    const bool changedSize=image.size()!=image_.size();image_=image;++revision_;
    if(changedSize)pan_={};clampPan();update();emit imageChanged();emit viewChanged();
}
void FrameCanvas::setFit(bool value) {if(value==fit_)return;fit_=value;pan_={};update();emit viewChanged();}
void FrameCanvas::resetView(bool value) {fit_=value;user_scale_=1.0;pan_={};update();emit viewChanged();}
double FrameCanvas::dpr() const {return window()?window()->effectiveDevicePixelRatio():1.0;}
double FrameCanvas::scale() const {
    if(image_.isNull()||width()<=0||height()<=0)return 1;
    return fit_?std::min(width()*dpr()/image_.width(),height()*dpr()/image_.height()):user_scale_;
}
double FrameCanvas::scalePercent() const {return image_.isNull()?0:scale()*100;}
QRectF FrameCanvas::imageRect() const {
    if(image_.isNull())return {};
    const QSizeF size(image_.width()*scale()/dpr(),image_.height()*scale()/dpr());
    auto position=[](double extent,double canvas,double pan){
        return extent<=canvas?(canvas-extent)*0.5:std::clamp((canvas-extent)*0.5+pan,canvas-extent,0.0);
    };
    double x=position(size.width(),width(),fit_?0:pan_.x());
    double y=position(size.height(),height(),fit_?0:pan_.y());
    // At 100%, align the image origin to the physical pixel grid as well as
    // preserving its physical size. Fractional centring must not shift grain.
    if(std::abs(scale()-1.0)<1e-12){x=std::round(x*dpr())/dpr();y=std::round(y*dpr())/dpr();}
    return {x,y,size.width(),size.height()};
}
void FrameCanvas::clampPan() {
    const auto rect=imageRect();
    pan_.setX(rect.width()>width()?rect.x()-(width()-rect.width())*0.5:0);
    pan_.setY(rect.height()>height()?rect.y()-(height()-rect.height())*0.5:0);
}
void FrameCanvas::panBy(double x,double y) {
    if(fit_||image_.isNull())return;
    pan_+=QPointF(x,y);clampPan();update();emit viewChanged();
}
void FrameCanvas::zoomAt(double factor, double cx, double cy) {
    if(image_.isNull())return;
    // If currently fitting, jump to 100% first so we have a stable zoom base.
    if(fit_){fit_=false;user_scale_=1.0;}
    const double oldScale=user_scale_;
    const double newScale=std::clamp(oldScale*factor, 0.1, 10.0);
    if(std::abs(newScale-oldScale)<1e-6)return;
    // Keep the point under the cursor fixed on screen: convert screen -> image
    // coords at old scale, then adjust pan so the same image point stays at the
    // same screen position at the new scale.
    const auto rect=imageRect();
    const double imgX=(cx-rect.x())/oldScale*dpr();
    const double imgY=(cy-rect.y())/oldScale*dpr();
    user_scale_=newScale;
    // After changing scale, recompute where the image would be, then pan so the
    // same image pixel lands under the cursor.
    const double newW=image_.width()*newScale/dpr();
    const double newH=image_.height()*newScale/dpr();
    const double newX=(width()-newW)*0.5;
    const double newY=(height()-newH)*0.5;
    pan_.setX(cx - newX - imgX*newScale/dpr());
    pan_.setY(cy - newY - imgY*newScale/dpr());
    clampPan();update();emit viewChanged();
}
void FrameCanvas::wheelEvent(QWheelEvent* e) {
    if(image_.isNull()||e->angleDelta().y()==0){e->ignore();return;}
    const double factor=std::pow(1.0015, e->angleDelta().y());
    const auto pos=e->position();
    zoomAt(factor, pos.x(), pos.y());
    e->accept();
}
QSGNode* FrameCanvas::updatePaintNode(QSGNode* old,UpdatePaintNodeData*) {
    auto* node=static_cast<ImageNode*>(old);
    if(image_.isNull()||!window()){delete node;return nullptr;}
    if(!node)node=new ImageNode;
    if(node->revision!=revision_) {
        auto* texture=window()->createTextureFromImage(image_);
        if(!texture){delete node;return nullptr;}
        node->setTexture(texture);node->setOwnsTexture(true);node->revision=revision_;
    }
    node->setFiltering(scale()>=1.0?QSGTexture::Nearest:QSGTexture::Linear);
    node->setRect(imageRect());return node;
}
void FrameCanvas::geometryChange(const QRectF& next,const QRectF& previous) {
    QQuickItem::geometryChange(next,previous);clampPan();update();emit viewChanged();
}
void FrameCanvas::itemChange(ItemChange change,const ItemChangeData& data) {
    QQuickItem::itemChange(change,data);
    if(change==ItemDevicePixelRatioHasChanged){clampPan();update();emit viewChanged();}
}
void FrameCanvas::mousePressEvent(QMouseEvent* e) {
    if(fit_||image_.isNull()){e->ignore();return;}dragging_=true;last_mouse_=e->position();e->accept();
}
void FrameCanvas::mouseMoveEvent(QMouseEvent* e) {if(dragging_){const auto delta=e->position()-last_mouse_;last_mouse_=e->position();panBy(delta.x(),delta.y());e->accept();}else e->ignore();}
void FrameCanvas::mouseReleaseEvent(QMouseEvent* e) {dragging_=false;e->accept();}
void FrameCanvas::mouseUngrabEvent(){dragging_=false;}
void FrameCanvas::mouseDoubleClickEvent(QMouseEvent* e){resetView(!fit_);e->accept();}
