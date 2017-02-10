/* This file is part of the KDE project
   Copyright (C) 2011 Thorsten Zachmann <zachmann@kde.org>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
*/

#include "KoMarker.h"

#include <KoXmlReader.h>
#include <KoXmlNS.h>
#include <KoGenStyle.h>
#include <KoGenStyles.h>
#include "KoPathShape.h"
#include "KoPathShapeLoader.h"
#include "KoShapeLoadingContext.h"
#include "KoShapeSavingContext.h"
#include "KoOdfWorkaround.h"
#include "KoShapePainter.h"
#include "KoViewConverter.h"

#include <QString>
#include <QUrl>
#include <QPainterPath>
#include <QPainter>

#include "kis_global.h"

class Q_DECL_HIDDEN KoMarker::Private
{
public:
    Private()
        : coordinateSystem(StrokeWidth),
          referenceSize(3,3),
          hasAutoOrientation(false),
          explicitOrientation(0)
    {}

    QString name;
    QString d;
    QPainterPath path;
    QRect viewBox;
    MarkerCoordinateSystem coordinateSystem;
    QPointF referencePoint;
    QSizeF referenceSize;

    bool hasAutoOrientation;
    qreal explicitOrientation;

    QList<KoShape*> shapes;
};

KoMarker::KoMarker()
: d(new Private())
{
}

KoMarker::~KoMarker()
{
    delete d;
}

bool KoMarker::loadOdf(const KoXmlElement &element, KoShapeLoadingContext &context)
{
    Q_UNUSED(context);
    // A shape uses draw:marker-end="Arrow" draw:marker-end-width="0.686cm" draw:marker-end-center="true" which marker and how the marker is used

    //<draw:marker draw:name="Arrow" svg:viewBox="0 0 20 30" svg:d="m10 0-10 30h20z"/>
    //<draw:marker draw:name="Arrowheads_20_1" draw:display-name="Arrowheads 1" svg:viewBox="0 0 10 10" svg:d="m0 0h10v10h-10z"/>

    d->d =element.attributeNS(KoXmlNS::svg, "d");
    if (d->d.isEmpty()) {
        return false;
    }

#ifndef NWORKAROUND_ODF_BUGS
    KoOdfWorkaround::fixMarkerPath(d->d);
#endif

    KoPathShape pathShape;
    KoPathShapeLoader loader(&pathShape);
    loader.parseSvg(d->d, true);

    d->path = pathShape.outline();
    d->viewBox = KoPathShape::loadOdfViewbox(element);

    QString displayName(element.attributeNS(KoXmlNS::draw, "display-name"));
    if (displayName.isEmpty()) {
        displayName = element.attributeNS(KoXmlNS::draw, "name");
    }
    d->name = displayName;
    return true;
}

QString KoMarker::saveOdf(KoShapeSavingContext &context) const
{
    KoGenStyle style(KoGenStyle::MarkerStyle);
    style.addAttribute("draw:display-name", d->name);
    style.addAttribute("svg:d", d->d);
    const QString viewBox = QString::fromLatin1("%1 %2 %3 %4")
        .arg(d->viewBox.x()).arg(d->viewBox.y())
        .arg(d->viewBox.width()).arg(d->viewBox.height());
    style.addAttribute(QLatin1String("svg:viewBox"), viewBox);
    QString name = QString(QUrl::toPercentEncoding(d->name, "", " ")).replace('%', '_');
    return context.mainStyles().insert(style, name, KoGenStyles::DontAddNumberToName);
}

QString KoMarker::name() const
{
    return d->name;
}

QPainterPath KoMarker::path(qreal width) const
{
    if (!d->viewBox.isValid() || width == 0) {
        return QPainterPath();
    }

    // TODO: currently the <min-x>, <min-y> properties of viewbox are ignored, why? OOo-compat?
    qreal height = width * d->viewBox.height() / d->viewBox.width();

    QTransform transform;
    transform.scale(width / d->viewBox.width(), height / d->viewBox.height());
    return transform.map(d->path);
}

bool KoMarker::operator==(const KoMarker &other) const
{
    return (d->d == other.d->d && d->viewBox ==other.d->viewBox);
}

void KoMarker::setCoordinateSystem(KoMarker::MarkerCoordinateSystem value)
{
    d->coordinateSystem = value;
}

KoMarker::MarkerCoordinateSystem KoMarker::coordinateSystem() const
{
    return d->coordinateSystem;
}

KoMarker::MarkerCoordinateSystem KoMarker::coordinateSystemFromString(const QString &value)
{
    MarkerCoordinateSystem result = StrokeWidth;

    if (value == "userSpaceOnUse") {
        result = UserSpaceOnUse;
    }

    return result;
}

QString KoMarker::coordinateSystemToString(KoMarker::MarkerCoordinateSystem value)
{
    return
        value == StrokeWidth ?
        "strokeWidth" :
                "userSpaceOnUse";
}

void KoMarker::setReferencePoint(const QPointF &value)
{
    d->referencePoint = value;
}

QPointF KoMarker::referencePoint() const
{
    return d->referencePoint;
}

void KoMarker::setReferenceSize(const QSizeF &size)
{
    d->referenceSize = size;
}

QSizeF KoMarker::referenceSize() const
{
    return d->referenceSize;
}

bool KoMarker::hasAutoOtientation() const
{
    return d->hasAutoOrientation;
}

void KoMarker::setAutoOrientation(bool value)
{
    d->hasAutoOrientation = value;
}

qreal KoMarker::explicitOrientation() const
{
    return d->explicitOrientation;
}

void KoMarker::setExplicitOrientation(qreal value)
{
    d->explicitOrientation = value;
}

void KoMarker::setShapes(const QList<KoShape *> &shapes)
{
    d->shapes = shapes;
}

QList<KoShape *> KoMarker::shapes() const
{
    return d->shapes;
}

void KoMarker::paintAtPosition(QPainter *painter, const QPointF &pos, qreal strokeWidth, qreal nodeAngle)
{
    QTransform oldTransform = painter->transform();

    KoViewConverter converter;
    KoShapePainter p;
    p.setShapes(d->shapes);

    painter->translate(pos);

    const qreal angle = d->hasAutoOrientation ? nodeAngle : d->explicitOrientation;
    painter->rotate(kisRadiansToDegrees(angle));

    if (d->coordinateSystem == StrokeWidth) {
        painter->scale(strokeWidth, strokeWidth);
    }

    painter->translate(-d->referencePoint);

    p.paint(*painter, converter);

    painter->setTransform(oldTransform);
}

qreal KoMarker::maxInset(qreal strokeWidth) const
{
    QRectF shapesBounds;

    Q_FOREACH (KoShape *shape, d->shapes) {
        shapesBounds |= shape->boundingRect();
    }

    qreal result = 0.0;

    result = qMax(kisDistance(shapesBounds.topLeft(), d->referencePoint), result);
    result = qMax(kisDistance(shapesBounds.topRight(), d->referencePoint), result);
    result = qMax(kisDistance(shapesBounds.bottomLeft(), d->referencePoint), result);
    result = qMax(kisDistance(shapesBounds.bottomRight(), d->referencePoint), result);

    if (d->coordinateSystem == StrokeWidth) {
        result *= strokeWidth;
    }

    return result;
}
