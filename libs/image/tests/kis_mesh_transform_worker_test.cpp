/*
 *  Copyright (c) 2014,2020 Dmitry Kazakov <dimula73@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "kis_mesh_transform_worker_test.h"

#include <QTest>

#include <KoColor.h>
#include <KoProgressUpdater.h>
#include <KoUpdater.h>

#include "testutil.h"
#include <kis_algebra_2d.h>

#include <boost/iterator/iterator_facade.hpp>

#include <tuple>

template <typename Point>
Point lerp(const Point &pt1, const Point &pt2, qreal t)
{
    return pt1 + (pt2 - pt1) * t;
}

QColor lerp(const QColor &c1, const QColor &c2, qreal t) {
    return QColor::fromRgbF(lerp(c1.redF(), c2.redF(), t),
                            lerp(c1.greenF(), c2.greenF(), t),
                            lerp(c1.blueF(), c2.blueF(), t),
                            lerp(c1.alphaF(), c2.alphaF(), t));
}

QPointF bezierCurve(const QPointF p0,
                    const QPointF p1,
                    const QPointF p2,
                    const QPointF p3,
                    qreal t)
{
    const qreal t_2 = pow2(t);
    const qreal t_3 = t_2 * t;
    const qreal t_inv = 1.0 - t;
    const qreal t_inv_2 = pow2(t_inv);
    const qreal t_inv_3 = t_inv_2 * t_inv;

    return
        t_inv_3 * p0 +
        3 * t_inv_2 * t * p1 +
        3 * t_inv * t_2 * p2 +
        t_3 * p3;

}

QPointF bezierCurveDeriv(const QPointF p0,
                         const QPointF p1,
                         const QPointF p2,
                         const QPointF p3,
                         qreal t)
{
    const qreal t_2 = pow2(t);
    const qreal t_inv = 1.0 - t;
    const qreal t_inv_2 = pow2(t_inv);

    return
        3 * t_inv_2 * (p1 - p0) +
        6 * t_inv * t * (p2 - p1) +
        3 * t_2 * (p3 - p2);
}

QPointF bezierCurveDeriv2(const QPointF p0,
                          const QPointF p1,
                          const QPointF p2,
                          const QPointF p3,
                          qreal t)
{
    const qreal t_inv = 1.0 - t;

    return
        6 * t_inv * (p2 - 2 * p1 + p0) +
        6 * t * (p3 - 2 * p2 + p1);
}


void deCasteljau(const QPointF &q0,
                 const QPointF &q1,
                 const QPointF &q2,
                 const QPointF &q3,
                 qreal t,
                 QPointF *p0,
                 QPointF *p1,
                 QPointF *p2,
                 QPointF *p3,
                 QPointF *p4)
{
    QPointF q[4];

    q[0] = q0;
    q[1] = q1;
    q[2] = q2;
    q[3] = q3;

    // points of the new segment after the split point
    QPointF p[3];

    // the De Casteljau algorithm
    for (unsigned short j = 1; j <= 3; ++j) {
        for (unsigned short i = 0; i <= 3 - j; ++i) {
            q[i] = (1.0 - t) * q[i] + t * q[i + 1];
        }
        p[j - 1] = q[0];
    }

    *p0 = p[0];
    *p1 = p[1];
    *p2 = p[2];
    *p3 = q[1];
    *p4 = q[2];
}

struct Node {
    Node() {}
    Node(const QPointF &_node)
        : leftControl(_node),
          topControl(_node),
          node(_node),
          rightControl(_node),
          bottomControl(_node)
    {
    }

    QPointF leftControl;
    QPointF topControl;
    QPointF node;
    QPointF rightControl;
    QPointF bottomControl;
};

void lerpNodeData(const Node &left, const Node &right, qreal t, Node &dst)
{
    Q_UNUSED(left);
    Q_UNUSED(right);
    Q_UNUSED(t);
    Q_UNUSED(dst);
}

QDebug operator<<(QDebug dbg, const Node &n) {
    dbg.nospace() << "Node " << n.node << " "
                  << "(lC: " << n.leftControl << " "
                  << "tC: " << n.topControl << " "
                  << "rC: " << n.rightControl << " "
                  << "bC: " << n.bottomControl << ") ";
    return dbg.nospace();
}


struct BezierPatch
{
    enum ControlPointType {
        TL = 0,
        TL_HC,
        TL_VC,
        TR,
        TR_HC,
        TR_VC,
        BL,
        BL_HC,
        BL_VC,
        BR,
        BR_HC,
        BR_VC
    };

    QRectF originalRect;
    std::array<QPointF, 12> points;

    QRectF dstBoundingRect() const {
        QRectF result;

        for (auto it = points.begin(); it != points.end(); ++it) {
            KisAlgebra2D::accumulateBounds(*it, &result);
        }

        return result;
    }

    QRectF srcBoundingRect() const {
        return originalRect;
    }

    static bool isLinearSegment(const QPointF &p0, const QPointF &d0,
                         const QPointF &p1, const QPointF &d1)
    {
        const QPointF diff = p1 - p0;
        const qreal dist = KisAlgebra2D::norm(diff);

        const qreal normCoeff = 1.0 / 3.0 / dist;

        // TODO: handle negative projection case

        const qreal offset1 =
            normCoeff * KisAlgebra2D::crossProduct(diff, d0);
        if (offset1 > 1.0) return false;

        const qreal offset2 =
            normCoeff * KisAlgebra2D::crossProduct(diff, d1);
        if (offset2 > 1.0) return false;

        return true;
    }

    static QVector<qreal> linearizeCurve(const QPointF p0,
                                         const QPointF p1,
                                         const QPointF p2,
                                         const QPointF p3)
    {
        const qreal minStepSize = 2.0 / kisDistance(p0, p3);

        QVector<qreal> steps;
        steps << 0.0;


        QStack<std::tuple<QPointF, QPointF, qreal>> stackedPoints;
        stackedPoints.push(std::make_tuple(p3, 3 * (p3 - p2), 1.0));

        QPointF lastP = p0;
        QPointF lastD = 3 * (p1 - p0);
        qreal lastT = 0.0;

        while (!stackedPoints.isEmpty()) {
            QPointF p = std::get<0>(stackedPoints.top());
            QPointF d = std::get<1>(stackedPoints.top());
            qreal t = std::get<2>(stackedPoints.top());

            if (t - lastT < minStepSize ||
                isLinearSegment(lastP, lastD, p, d)) {
                lastP = p;
                lastD = d;
                lastT = t;
                steps << t;
                stackedPoints.pop();
            } else {
                t = 0.5 * (lastT + t);
                p = bezierCurve(p0, p1, p2, p3, t);
                d = bezierCurveDeriv(p0, p1, p2, p3, t);

                stackedPoints.push(std::make_tuple(p, d, t));
            }
        }

//        qDebug() << ppVar(kisDistance(p0, p3)) << ppVar(steps.size());
//        Q_FOREACH(const qreal t, steps) {
//            qDebug() << ppVar(t);
//        }

        return steps;
    }

    static QVector<qreal> mergeSteps(const QVector<qreal> &a, const QVector<qreal> &b) {
        QVector<qreal> result;

        std::merge(a.constBegin(), a.constEnd(),
                   b.constBegin(), b.constEnd(),
                   std::back_inserter(result));
        result.erase(
            std::unique(result.begin(), result.end(),
                        [] (qreal x, qreal y) { return qFuzzyCompare(x, y); }),
            result.end());

        return result;
    }

    void sampleIrregularGrid(QSize &gridSize,
                           QVector<QPointF> &origPoints,
                           QVector<QPointF> &transfPoints) const {

        const QVector<qreal> topSteps = linearizeCurve(points[TL], points[TL_HC], points[TR_HC], points[TR]);
        const QVector<qreal> bottomSteps = linearizeCurve(points[BL], points[BL_HC], points[BR_HC], points[BR]);
        const QVector<qreal> horizontalSteps = mergeSteps(topSteps, bottomSteps);

        const QVector<qreal> leftSteps = linearizeCurve(points[TL], points[TL_VC], points[BL_VC], points[BL]);
        const QVector<qreal> rightSteps = linearizeCurve(points[TR], points[TR_VC], points[BR_VC], points[BR]);
        const QVector<qreal> verticalSteps = mergeSteps(leftSteps, rightSteps);

        gridSize.rwidth() = horizontalSteps.size();
        gridSize.rheight() = verticalSteps.size();

        ENTER_FUNCTION() << ppVar(gridSize);

        for (int y = 0; y < gridSize.height(); y++) {
            const qreal yProportion = verticalSteps[y];

            for (int x = 0; x < gridSize.width(); x++) {
                const qreal xProportion = horizontalSteps[x];

                const QPointF orig = KisAlgebra2D::relativeToAbsolute(
                            QPointF(xProportion, yProportion), originalRect);

                const QPointF Sc =
                    lerp(bezierCurve(points[TL], points[TL_HC], points[TR_HC], points[TR], xProportion),
                         bezierCurve(points[BL], points[BL_HC], points[BR_HC], points[BR], xProportion),
                         yProportion);

                const QPointF Sd =
                    lerp(bezierCurve(points[TL], points[TL_VC], points[BL_VC], points[BL], yProportion),
                         bezierCurve(points[TR], points[TR_VC], points[BR_VC], points[BR], yProportion),
                         xProportion);

                const QPointF Sb =
                     lerp(lerp(points[TL], points[TR], xProportion),
                          lerp(points[BL], points[BR], xProportion),
                          yProportion);

                const QPointF transf = Sc + Sd - Sb;

                origPoints.append(orig);
                transfPoints.append(transf);
            }
        }

    }

    void sampleRegularGrid(QSize &gridSize,
                           QVector<QPointF> &origPoints,
                           QVector<QPointF> &transfPoints,
                           const QPointF &dstStep) const {

        const QRectF bounds = dstBoundingRect();
        gridSize.rwidth() = qCeil(bounds.width() / dstStep.x());
        gridSize.rheight() = qCeil(bounds.height() / dstStep.y());

        for (int y = 0; y < gridSize.height(); y++) {
            const qreal yProportion = qreal(y) / (gridSize.height() - 1);

            for (int x = 0; x < gridSize.width(); x++) {
                const qreal xProportion = qreal(x) / (gridSize.width() - 1);

                const QPointF orig = KisAlgebra2D::relativeToAbsolute(
                            QPointF(xProportion, yProportion), originalRect);

                const QPointF Sc =
                    lerp(bezierCurve(points[TL], points[TL_HC], points[TR_HC], points[TR], xProportion),
                         bezierCurve(points[BL], points[BL_HC], points[BR_HC], points[BR], xProportion),
                         yProportion);

                const QPointF Sd =
                    lerp(bezierCurve(points[TL], points[TL_VC], points[BL_VC], points[BL], yProportion),
                         bezierCurve(points[TR], points[TR_VC], points[BR_VC], points[BR], yProportion),
                         xProportion);

                const QPointF Sb =
                     lerp(lerp(points[TL], points[TR], xProportion),
                          lerp(points[BL], points[BR], xProportion),
                          yProportion);

                const QPointF transf = Sc + Sd - Sb;

                origPoints.append(orig);
                transfPoints.append(transf);
            }
        }

    }
};

QDebug operator<<(QDebug dbg, const BezierPatch &p) {
    dbg.nospace() << "Patch " << p.srcBoundingRect() << " -> " << p.dstBoundingRect() << "\n";
    dbg.nospace() << "  ( " << p.points[BezierPatch::TL] << " "<< p.points[BezierPatch::TR] << " " << p.points[BezierPatch::BL] << " " << p.points[BezierPatch::BR] << ") ";
    return dbg.nospace();
}

namespace KisAlgebra2D
{
inline QRectF relativeToAbsolute(const QRectF &rel, const QRectF &rc) {
    return QRectF(relativeToAbsolute(rel.topLeft(), rc), relativeToAbsolute(rel.bottomRight(), rc));
}
}

struct BezierMesh
{
    BezierMesh(const QRectF &mapRect, const QSize &size = QSize(2,2))
        : m_size(size),
          m_originalRect(mapRect)
    {
        for (int row = 0; row < m_size.height(); row++) {
            const qreal yPos = qreal(row) / (size.height() - 1) * mapRect.height() + mapRect.y();

            for (int col = 0; col < m_size.width(); col++) {
                const qreal xPos = qreal(col) / (size.width() - 1) * mapRect.width() + mapRect.x();

                m_nodes.push_back(Node(QPointF(xPos, yPos)));
            }
        }

        for (int col = 0; col < m_size.width(); col++) {
            m_columns.push_back(qreal(col) / (size.width() - 1));
        }

        for (int row = 0; row < m_size.height(); row++) {
            m_rows.push_back(qreal(row) / (size.height() - 1));
        }
    }

    void splitCurveHorizontally(Node &left, Node &right, qreal t, Node &newNode) {
        QPointF p1, p2, p3, q1, q2;

        deCasteljau(left.node, left.rightControl, right.leftControl, right.node, t,
                    &p1, &p2, &p3, &q1, &q2);

        left.rightControl = p1;
        newNode.leftControl = p2;
        newNode.node = p3;
        newNode.rightControl = q1;
        right.leftControl = q2;

        newNode.topControl = newNode.node + lerp(left.topControl - left.node, right.topControl - right.node, t);
        newNode.bottomControl = newNode.node + lerp(left.bottomControl - left.node, right.bottomControl - right.node, t);

        lerpNodeData(left, right, t, newNode);
    }

    Node& node(int col, int row) {
        return m_nodes[row * m_size.width() + col];
    }

    const Node& node(int col, int row) const {
        return m_nodes[row * m_size.width() + col];
    }


    void splitCurveVertically(Node &top, Node &bottom, qreal t, Node &newNode) {
        QPointF p1, p2, p3, q1, q2;

        deCasteljau(top.node, top.bottomControl, bottom.topControl, bottom.node, t,
                    &p1, &p2, &p3, &q1, &q2);

        top.bottomControl = p1;
        newNode.topControl = p2;
        newNode.node = p3;
        newNode.bottomControl = q1;
        bottom.topControl = q2;

        newNode.leftControl = newNode.node + lerp(top.leftControl - top.node, bottom.leftControl - bottom.node, t);
        newNode.rightControl = newNode.node + lerp(top.rightControl - top.node, bottom.rightControl - bottom.node, t);

        lerpNodeData(top, bottom, t, newNode);
    }

    void subdivideRow(qreal t) {
        if (qFuzzyCompare(t, 0.0) || qFuzzyCompare(t, 1.0)) return;

        KIS_SAFE_ASSERT_RECOVER_RETURN(t > 0.0 && t < 1.0);

        const auto it = prev(upper_bound(m_rows.begin(), m_rows.end(), t));
        const int topRow = distance(m_rows.begin(), it);
        const int bottomRow = topRow + 1;


        const qreal relT = (t - *it) / (*next(it) - *it);

        std::vector<Node> newRow;
        newRow.resize(m_size.width());
        for (int col = 0; col < m_size.width(); col++) {
            splitCurveVertically(node(col, topRow), node(col, bottomRow), relT, newRow[col]);
        }

        m_nodes.insert(m_nodes.begin() + bottomRow * m_size.width(),
                       newRow.begin(), newRow.end());

        m_size.rheight()++;
        m_rows.insert(next(it), t);
    }

    void subdivideColumn(qreal t) {
        if (qFuzzyCompare(t, 0.0) || qFuzzyCompare(t, 1.0)) return;

        KIS_SAFE_ASSERT_RECOVER_RETURN(t > 0.0 && t < 1.0);

        const auto it = prev(upper_bound(m_columns.begin(), m_columns.end(), t));
        const int leftColumn = distance(m_columns.begin(), it);
        const int rightColumn = leftColumn + 1;

        const qreal relT = (t - *it) / (*next(it) - *it);

        std::vector<Node> newColumn;
        newColumn.resize(m_size.height());
        for (int row = 0; row < m_size.height(); row++) {
            splitCurveHorizontally(node(leftColumn, row), node(rightColumn, row), relT, newColumn[row]);
        }

        auto dstIt = m_nodes.begin() + rightColumn;
        for (auto columnIt = newColumn.begin(); columnIt != newColumn.end(); ++columnIt) {
            dstIt = m_nodes.insert(dstIt, *columnIt);
            dstIt += m_size.width() + 1;
        }

        m_size.rwidth()++;
        m_columns.insert(next(it), t);
    }

    BezierPatch makePatch(int col, int row) const
    {
        const Node &tl = node(col, row);
        const Node &tr = node(col + 1, row);
        const Node &bl = node(col, row + 1);
        const Node &br = node(col + 1, row + 1);

        BezierPatch patch;

        patch.points[BezierPatch::TL] = tl.node;
        patch.points[BezierPatch::TL_HC] = tl.rightControl;
        patch.points[BezierPatch::TL_VC] = tl.bottomControl;

        patch.points[BezierPatch::TR] = tr.node;
        patch.points[BezierPatch::TR_HC] = tr.leftControl;
        patch.points[BezierPatch::TR_VC] = tr.bottomControl;

        patch.points[BezierPatch::BL] = bl.node;
        patch.points[BezierPatch::BL_HC] = bl.rightControl;
        patch.points[BezierPatch::BL_VC] = bl.topControl;

        patch.points[BezierPatch::BR] = br.node;
        patch.points[BezierPatch::BR_HC] = br.leftControl;
        patch.points[BezierPatch::BR_VC] = br.topControl;

        const QRectF relRect(m_columns[col],
                             m_rows[row],
                             m_columns[col + 1] - m_columns[col],
                             m_rows[row + 1] - m_rows[row]);

        patch.originalRect = KisAlgebra2D::relativeToAbsolute(relRect, m_originalRect);

        return patch;
    }

    class iterator :
        public boost::iterator_facade <iterator,
                                       BezierPatch,
                                       boost::random_access_traversal_tag,
                                       BezierPatch>
    {
    public:
        iterator()
            : m_mesh(0),
              m_col(0),
              m_row(0) {}

        iterator(const BezierMesh* mesh, int col, int row)
            : m_mesh(mesh),
              m_col(col),
              m_row(row)
        {
        }

    private:
        friend class boost::iterator_core_access;

        void increment() {
            m_col++;
            if (m_col >= m_mesh->m_size.width() - 1) {
                m_col = 0;
                m_row++;
            }
        }

        void decrement() {
            m_col--;
            if (m_col < 0) {
                m_col = m_mesh->m_size.width() - 2;
                m_row--;
            }
        }

        void advance(int n) {
            const int index = m_row * (m_mesh->m_size.width() - 1) + m_col + n;

            m_row = index / (m_mesh->m_size.width() - 1);
            m_col = index % (m_mesh->m_size.width() - 1);

            KIS_SAFE_ASSERT_RECOVER_NOOP(m_row < m_mesh->m_size.height() - 1);
        }

        int distance_to(const iterator &z) const {
            const int index = m_row * (m_mesh->m_size.width() - 1) + m_col;
            const int otherIndex = z.m_row * (m_mesh->m_size.width() - 1) + z.m_col;

            return otherIndex - index;
        }

        bool equal(iterator const& other) const {
            return m_row == other.m_row &&
                    m_col == other.m_col &&
                m_mesh == other.m_mesh;
        }

        BezierPatch dereference() const {
            return m_mesh->makePatch(m_col, m_row);
        }

    private:

        const BezierMesh* m_mesh;
        int m_col;
        int m_row;
    };


    iterator begin() const {
        return iterator(this, 0, 0);
    }

    iterator end() const {
        return iterator(this, 0, m_size.height() - 1);
    }

    std::vector<Node> m_nodes;
    std::vector<qreal> m_rows;
    std::vector<qreal> m_columns;

    QSize m_size;
    QRectF m_originalRect;
};

QDebug operator<<(QDebug dbg, const BezierMesh &mesh) {
    dbg.nospace() << "Mesh: \n";

    int i = 0;
    for (auto it = mesh.m_nodes.begin(); it != mesh.m_nodes.end(); ++it, ++i) {
        dbg.nospace() << "  " << *it << "\n";
    }

    return dbg.nospace();
}

#include "kis_grid_interpolation_tools.h"

namespace GridIterationTools {
struct RegularGridIndexesOp {

    RegularGridIndexesOp(const QSize &gridSize)
        : m_gridSize(gridSize)
    {
    }

    inline QVector<int> calculateMappedIndexes(int col, int row,
                                               int *numExistingPoints) const {

        *numExistingPoints = 4;
        QVector<int> cellIndexes =
            GridIterationTools::calculateCellIndexes(col, row, m_gridSize);

        return cellIndexes;
    }

    inline int tryGetValidIndex(const QPoint &cellPt) const {
        Q_UNUSED(cellPt);

        KIS_ASSERT_RECOVER_NOOP(0 && "Not applicable");
        return -1;
    }

    inline QPointF getSrcPointForce(const QPoint &cellPt) const {
        Q_UNUSED(cellPt);

        KIS_ASSERT_RECOVER_NOOP(0 && "Not applicable");
        return QPointF();
    }

    inline const QPolygonF srcCropPolygon() const {
        KIS_ASSERT_RECOVER_NOOP(0 && "Not applicable");
        return QPolygonF();
    }

    QSize m_gridSize;
};

struct QImageGradientOp
{
    QImageGradientOp(const std::array<QColor, 4> &colors, QImage &dstImage,
                    const QPointF &dstImageOffset)
        : m_colors(colors), m_dstImage(dstImage),
          m_dstImageOffset(dstImageOffset),
          m_dstImageRect(m_dstImage.rect())
    {
    }

    void operator() (const QPolygonF &srcPolygon, const QPolygonF &dstPolygon) {
        this->operator() (srcPolygon, dstPolygon, dstPolygon);
    }

    void operator() (const QPolygonF &srcPolygon, const QPolygonF &dstPolygon, const QPolygonF &clipDstPolygon) {
        QRect boundRect = clipDstPolygon.boundingRect().toAlignedRect();
        KisFourPointInterpolatorBackward interp(srcPolygon, dstPolygon);

        for (int y = boundRect.top(); y <= boundRect.bottom(); y++) {
            interp.setY(y);
            for (int x = boundRect.left(); x <= boundRect.right(); x++) {

                QPointF srcPoint(x, y);
                if (clipDstPolygon.containsPoint(srcPoint, Qt::OddEvenFill)) {

                    interp.setX(srcPoint.x());
                    QPointF dstPoint = interp.getValue();

                    // about srcPoint/dstPoint hell please see a
                    // comment in PaintDevicePolygonOp::operator() ()

                    srcPoint -= m_dstImageOffset;

                    QPoint srcPointI = srcPoint.toPoint();

                    if (!m_dstImageRect.contains(srcPointI)) continue;

                    // TODO: move vertical calculation into the upper loop
                    const QColor c1 = lerp(m_colors[0], m_colors[1], qBound(0.0, dstPoint.x(), 1.0));
                    const QColor c2 = lerp(m_colors[2], m_colors[3], qBound(0.0, dstPoint.x(), 1.0));

                    m_dstImage.setPixelColor(srcPointI, lerp(c1, c2, qBound(0.0, dstPoint.y(), 1.0)));
                }
            }
        }
    }

    const std::array<QColor, 4> &m_colors;
    QImage &m_dstImage;
    QPointF m_dstImageOffset;
    QRect m_dstImageRect;
};

}

void KisMeshTransformWorkerTest::testPointsQImage()
{
    TestUtil::TestProgressBar bar;
    KoProgressUpdater pu(&bar);
    KoUpdaterPtr updater = pu.startSubtask();

    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    QImage srcImage(TestUtil::fetchDataFileLazy("test_transform_quality_second.png"));

    KisPaintDeviceSP srcDev = new KisPaintDevice(cs);
    srcDev->convertFromQImage(srcImage, 0);

    const QRect initialRect(srcImage.rect());
    ENTER_FUNCTION() << ppVar(initialRect);


    BezierPatch patch;
    patch.originalRect = initialRect;

    patch.points[0] = initialRect.topLeft();
    patch.points[1] = initialRect.topLeft() + QPointF(300, 30);
    patch.points[2] = initialRect.topLeft() + QPointF(20, 300);
    patch.points[3] = initialRect.topRight();
    patch.points[4] = initialRect.topRight() + QPointF(-300, 30);
    patch.points[5] = initialRect.topRight() + QPointF(-20, 300);
    patch.points[6] = initialRect.bottomLeft();
    patch.points[7] = initialRect.bottomLeft() + QPointF(300, 30);
    patch.points[8] = initialRect.bottomLeft() + QPointF(20, -300);
    patch.points[9] = initialRect.bottomRight();
    patch.points[10] = initialRect.bottomRight() + QPointF(-300, 30);
    patch.points[11] = initialRect.bottomRight() + QPointF(-20, -300);


    QVector<QPointF> originalPointsLocal;
    QVector<QPointF> transformedPointsLocal;
    QSize gridSize;

    QElapsedTimer t; t.start();


    //patch.sampleRegularGrid(gridSize, originalPointsLocal, transformedPointsLocal, QPointF(8,8));


    patch.sampleIrregularGrid(gridSize, originalPointsLocal, transformedPointsLocal);

    ENTER_FUNCTION() << "sample time" << t.restart();


    const QRect dstBoundsI = patch.dstBoundingRect().toAlignedRect();

    {
        QImage dstImage(dstBoundsI.size(), srcImage.format());
        dstImage.fill(0);

        t.start();

        const QPoint srcImageOffset;
        const QPoint dstQImageOffset;

        GridIterationTools::QImagePolygonOp polygonOp(srcImage, dstImage, srcImageOffset, dstQImageOffset);


        GridIterationTools::RegularGridIndexesOp indexesOp(gridSize);
        GridIterationTools::iterateThroughGrid
                <GridIterationTools::AlwaysCompletePolygonPolicy>(polygonOp, indexesOp,
                                                                  gridSize,
                                                                  originalPointsLocal,
                                                                  transformedPointsLocal);

        ENTER_FUNCTION() << "process qimage time" << t.restart();


        dstImage.save("dd_mesh_result.png");
    }

    {
        KisPaintDeviceSP dstDev = new KisPaintDevice(srcDev->colorSpace());
        dstDev->prepareClone(srcDev);

        t.start();

        GridIterationTools::PaintDevicePolygonOp polygonOp(srcDev, dstDev);
        GridIterationTools::RegularGridIndexesOp indexesOp(gridSize);
        GridIterationTools::iterateThroughGrid
                <GridIterationTools::AlwaysCompletePolygonPolicy>(polygonOp, indexesOp,
                                                                  gridSize,
                                                                  originalPointsLocal,
                                                                  transformedPointsLocal);
        ENTER_FUNCTION() << "process device time" << t.restart();


        dstDev->convertToQImage(0, dstBoundsI).save("dd_mesh_result_dev.png");
    }
}

void KisMeshTransformWorkerTest::testGradient()
{
    TestUtil::TestProgressBar bar;
    KoProgressUpdater pu(&bar);
    KoUpdaterPtr updater = pu.startSubtask();

    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    KisPaintDeviceSP srcDev = new KisPaintDevice(cs);

    const QRect initialRect(0,0,1600, 1200);
    ENTER_FUNCTION() << ppVar(initialRect);


    std::array<QColor, 4> colors;
    colors[0] = Qt::white;
    colors[1] = Qt::red;
    colors[2] = Qt::green;
    colors[3] = Qt::yellow;

    BezierPatch patch;
    patch.originalRect = QRectF(0, 0, 1.0, 1.0);

    patch.points[0] = initialRect.topLeft();
    patch.points[1] = initialRect.topLeft() + QPointF(300, 30);
    patch.points[2] = initialRect.topLeft() + QPointF(20, 300);
    patch.points[3] = initialRect.topRight();
    patch.points[4] = initialRect.topRight() + QPointF(-300, 30);
    patch.points[5] = initialRect.topRight() + QPointF(-20, 300);
    patch.points[6] = initialRect.bottomLeft();
    patch.points[7] = initialRect.bottomLeft() + QPointF(300, 30);
    patch.points[8] = initialRect.bottomLeft() + QPointF(20, -300);
    patch.points[9] = initialRect.bottomRight();
    patch.points[10] = initialRect.bottomRight() + QPointF(-300, 30);
    patch.points[11] = initialRect.bottomRight() + QPointF(-20, -300);


    QVector<QPointF> originalPointsLocal;
    QVector<QPointF> transformedPointsLocal;
    QSize gridSize;

    //patch.sampleRegularGrid(gridSize, originalPointsLocal, transformedPointsLocal, QPointF(16,16));
    patch.sampleIrregularGrid(gridSize, originalPointsLocal, transformedPointsLocal);

    const QRect dstBoundsI = patch.dstBoundingRect().toAlignedRect();

    {
        QImage dstImage(dstBoundsI.size(), QImage::Format_ARGB32);
        dstImage.fill(255);

        QElapsedTimer t; t.start();

        const QPoint srcImageOffset;
        const QPoint dstQImageOffset;

        GridIterationTools::QImageGradientOp polygonOp(colors, dstImage, dstQImageOffset);


        GridIterationTools::RegularGridIndexesOp indexesOp(gridSize);
        GridIterationTools::iterateThroughGrid
                <GridIterationTools::AlwaysCompletePolygonPolicy>(polygonOp, indexesOp,
                                                                  gridSize,
                                                                  originalPointsLocal,
                                                                  transformedPointsLocal);

        ENTER_FUNCTION() << "gradient fill" << t.elapsed();
        dstImage.save("dd_mesh_result_grad.png");
    }

#if 0

    {
        KisPaintDeviceSP dstDev = new KisPaintDevice(srcDev->colorSpace());
        dstDev->prepareClone(srcDev);

        GridIterationTools::PaintDevicePolygonOp polygonOp(srcDev, dstDev);
        GridIterationTools::RegularGridIndexesOp indexesOp(gridSize);
        GridIterationTools::iterateThroughGrid
                <GridIterationTools::AlwaysCompletePolygonPolicy>(polygonOp, indexesOp,
                                                                  gridSize,
                                                                  originalPointsLocal,
                                                                  transformedPointsLocal);
        dstDev->convertToQImage(0, dstBoundsI).save("dd_mesh_result_dev.png");
    }
#endif
}

#include "KisCppQuirks.h"

void KisMeshTransformWorkerTest::testMesh()
{

    {
        BezierMesh mesh(QRectF(0,0,100,100));

        mesh.subdivideRow(0.5);

        qDebug() << mesh;

        mesh.subdivideColumn(0.5);

        qDebug() << mesh;
    }

    {
        BezierMesh mesh(QRectF(0,0,100,100), QSize(5,5));

        qDebug() << mesh;

        mesh.subdivideRow(0.125);

        qDebug() << mesh;

        mesh.subdivideColumn(0.125);

        qDebug() << mesh;
    }

    {
        BezierMesh mesh(QRectF(0,0,100,100), QSize(3,3));


        for (auto it = mesh.begin(); it != mesh.end(); ++it) {
            qDebug() << *it;
        }

        qDebug() << ppVar(std::distance(mesh.begin(), mesh.end()));
        qDebug() << ppVar(std::distance(mesh.begin()+2, mesh.end()));

        for (auto it = std::make_reverse_iterator(mesh.end()); it != std::make_reverse_iterator(mesh.begin()); ++it) {
            qDebug() << *it;
        }
    }

}

#include <config-gsl.h>

#ifdef HAVE_GSL
#include <gsl/gsl_multimin.h>
#endif

namespace {

struct Params2D {
    QPointF p0, p1, p2, p3;
    QPointF q0, q1, q2, q3;
    QPointF r0, r1, r2, r3;
    QPointF s0, s1, s2, s3;

    QPointF dstPoint;
};

template<typename T>
inline T pow3(const T& x) {
    return x * x * x;
}

QPointF meshForwardMapping(qreal u, qreal v, const Params2D &p) {
    return p.r0 + pow3(u)*v*(p.p0 - 3*p.p1 + 3*p.p2 - p.p3 - p.q0 + 3*p.q1 - 3*p.q2 + p.q3) + pow3(u)*(-p.p0 + 3*p.p1 - 3*p.p2 + p.p3) + pow2(u)*v*(-3*p.p0 + 6*p.p1 - 3*p.p2 + 3*p.q0 - 6*p.q1 + 3*p.q2) + pow2(u)*(3*p.p0 - 6*p.p1 + 3*p.p2) + u*pow3(v)*(p.r0 - 3*p.r1 + 3*p.r2 - p.r3 - p.s0 + 3*p.s1 - 3*p.s2 + p.s3) + u*pow2(v)*(-3*p.r0 + 6*p.r1 - 3*p.r2 + 3*p.s0 - 6*p.s1+ 3*p.s2) + u*v*(2*p.p0 - 3*p.p1 + p.p3 - 2*p.q0 + 3*p.q1 - p.q3 + 3*p.r0 - 3*p.r1 - 3*p.s0 + 3*p.s1) + u*(-2*p.p0 + 3*p.p1 - p.p3 - p.r0 + p.s0) + pow3(v)*(-p.r0 + 3*p.r1 - 3*p.r2 + p.r3) + pow2(v)*(3*p.r0 - 6*p.r1 + 3*p.r2) + v*(-3*p.r0 + 3*p.r1);
}

QPointF meshForwardMappingDiffU(qreal u, qreal v, const Params2D &p) {
    return -2*p.p0 + 3*p.p1 - p.p3 - p.r0 + p.s0 + pow2(u)*v*(3*p.p0 - 9*p.p1 + 9*p.p2 - 3*p.p3 - 3*p.q0 + 9*p.q1 - 9*p.q2 + 3*p.q3) + pow2(u)*(-3*p.p0 + 9*p.p1 - 9*p.p2 + 3*p.p3) + u*v*(-6*p.p0 + 12*p.p1 - 6*p.p2 + 6*p.q0 - 12*p.q1 + 6*p.q2) + u*(6*p.p0 - 12*p.p1 + 6*p.p2) + pow3(v)*(p.r0 - 3*p.r1 + 3*p.r2 - p.r3 - p.s0 + 3*p.s1 - 3*p.s2 + p.s3) + pow2(v)*(-3*p.r0 + 6*p.r1 - 3*p.r2 + 3*p.s0 - 6*p.s1 + 3*p.s2) + v*(2*p.p0 - 3*p.p1 + p.p3 - 2*p.q0 + 3*p.q1 - p.q3 + 3*p.r0 - 3*p.r1 - 3*p.s0 + 3*p.s1);
}

QPointF meshForwardMappingDiffV(qreal u, qreal v, const Params2D &p) {
    return -3*p.r0 + 3*p.r1 + pow3(u)*(p.p0 - 3*p.p1 + 3*p.p2 - p.p3 - p.q0 + 3*p.q1 - 3*p.q2 + p.q3) + pow2(u)*(-3*p.p0 + 6*p.p1 - 3*p.p2 + 3*p.q0 - 6*p.q1 + 3*p.q2) + u*pow2(v)*(3*p.r0 - 9*p.r1 + 9*p.r2 - 3*p.r3 - 3*p.s0 + 9*p.s1 - 9*p.s2 + 3*p.s3) + u*v*(-6*p.r0 + 12*p.r1 - 6*p.r2 + 6*p.s0 - 12*p.s1 + 6*p.s2) + u*(2*p.p0 - 3*p.p1 + p.p3 - 2*p.q0 + 3*p.q1 - p.q3 + 3*p.r0 - 3*p.r1 - 3*p.s0 + 3*p.s1) + pow2(v)*(-3*p.r0 + 9*p.r1 - 9*p.r2 + 3*p.r3) + v*(6*p.r0 - 12*p.r1 + 6*p.r2);
}

double my_f(const gsl_vector * x, void *paramsPtr)
{
    const Params2D *params = static_cast<const Params2D*>(paramsPtr);
    const QPointF pos(gsl_vector_get(x, 0), gsl_vector_get(x, 1));

    const QPointF S = meshForwardMapping(pos.x(), pos.y(), *params);

    return kisSquareDistance(S, params->dstPoint);
}

void my_fdf (const gsl_vector *x, void *paramsPtr, double *f, gsl_vector *df)
{
    const Params2D *params = static_cast<const Params2D*>(paramsPtr);
    const QPointF pos(gsl_vector_get(x, 0), gsl_vector_get(x, 1));

    const QPointF S = meshForwardMapping(pos.x(), pos.y(), *params);
    const QPointF dU = meshForwardMappingDiffU(pos.x(), pos.y(), *params);
    const QPointF dV = meshForwardMappingDiffV(pos.x(), pos.y(), *params);

    *f = kisSquareDistance(S, params->dstPoint);

    gsl_vector_set(df, 0,
                   2 * (S.x() - params->dstPoint.x()) * dU.x() +
                   2 * (S.y() - params->dstPoint.y()) * dU.y());
    gsl_vector_set(df, 1,
                   2 * (S.x() - params->dstPoint.x()) * dV.x() +
                   2 * (S.y() - params->dstPoint.y()) * dV.y());
}

void my_df (const gsl_vector *x, void *paramsPtr,
            gsl_vector *df)
{
    const Params2D *params = static_cast<const Params2D*>(paramsPtr);
    const QPointF pos(gsl_vector_get(x, 0), gsl_vector_get(x, 1));

    const QPointF S = meshForwardMapping(pos.x(), pos.y(), *params);
    const QPointF dU = meshForwardMappingDiffU(pos.x(), pos.y(), *params);
    const QPointF dV = meshForwardMappingDiffV(pos.x(), pos.y(), *params);

    gsl_vector_set(df, 0,
                   2 * (S.x() - params->dstPoint.x()) * dU.x() +
                   2 * (S.y() - params->dstPoint.y()) * dU.y());
    gsl_vector_set(df, 1,
                   2 * (S.x() - params->dstPoint.x()) * dV.x() +
                   2 * (S.y() - params->dstPoint.y()) * dV.y());
}

QPointF calculateLocalPos(const BezierPatch &patch, const QPointF &dstPoint)
{
    const gsl_multimin_fdfminimizer_type *T =
        gsl_multimin_fdfminimizer_vector_bfgs2;
    gsl_multimin_fdfminimizer *s = 0;
    gsl_vector *x;
    gsl_multimin_function_fdf minex_func;

    size_t iter = 0;
    int status;

    /* Starting point */
    x = gsl_vector_alloc (2);
    gsl_vector_set (x, 0, 0.5);
    gsl_vector_set (x, 1, 0.5);

    Params2D p;

    p.p0 = patch.points[BezierPatch::TL];
    p.p1 = patch.points[BezierPatch::TL_HC];
    p.p2 = patch.points[BezierPatch::TR_HC];
    p.p3 = patch.points[BezierPatch::TR];

    p.q0 = patch.points[BezierPatch::BL];
    p.q1 = patch.points[BezierPatch::BL_HC];
    p.q2 = patch.points[BezierPatch::BR_HC];
    p.q3 = patch.points[BezierPatch::BR];

    p.r0 = patch.points[BezierPatch::TL];
    p.r1 = patch.points[BezierPatch::TL_VC];
    p.r2 = patch.points[BezierPatch::BL_VC];
    p.r3 = patch.points[BezierPatch::BL];

    p.s0 = patch.points[BezierPatch::TR];
    p.s1 = patch.points[BezierPatch::TR_VC];
    p.s2 = patch.points[BezierPatch::BR_VC];
    p.s3 = patch.points[BezierPatch::BR];

    p.dstPoint = dstPoint;

    /* Initialize method and iterate */
    minex_func.n = 2;
    minex_func.f = my_f;
    minex_func.params = (void*)&p;
    minex_func.df = my_df;
    minex_func.fdf = my_fdf;

    s = gsl_multimin_fdfminimizer_alloc (T, 2);
    gsl_multimin_fdfminimizer_set (s, &minex_func, x, 0.01, 0.1);

    QPointF result;


    result.rx() = gsl_vector_get (s->x, 0);
    result.rx() = gsl_vector_get (s->x, 1);

    do
    {
        iter++;
        status = gsl_multimin_fdfminimizer_iterate(s);

        if (status)
            break;

        status = gsl_multimin_test_gradient (s->gradient, 1e-4);

        result.rx() = gsl_vector_get (s->x, 0);
        result.ry() = gsl_vector_get (s->x, 1);

        qDebug() << result << gsl_vector_get (s->gradient, 0) << gsl_vector_get (s->gradient, 1) << s->f;

        if (status == GSL_SUCCESS)
        {
            result.rx() = gsl_vector_get (s->x, 0);
            result.ry() = gsl_vector_get (s->x, 1);
            qDebug() << "******* Converged to minimum" << ppVar(result);

        }
    }
    while (status == GSL_CONTINUE && iter < 10000);

    ENTER_FUNCTION()<< ppVar(iter) << ppVar(dstPoint) << ppVar(result);
    ENTER_FUNCTION() << ppVar(meshForwardMapping(result.x(), result.y(), p));

    gsl_vector_free(x);
    gsl_multimin_fdfminimizer_free (s);

    return result;
}
}

void KisMeshTransformWorkerTest::testGlobalToLocal()
{
    const QRect initialRect(0,0,1000,1000);

    BezierPatch patch;
    patch.originalRect = initialRect;

#if 0

    patch.points[0] = initialRect.topLeft();
    patch.points[1] = initialRect.topLeft();
    patch.points[2] = initialRect.topLeft();
    patch.points[3] = initialRect.topRight();
    patch.points[4] = initialRect.topRight();
    patch.points[5] = initialRect.topRight();
    patch.points[6] = initialRect.bottomLeft();
    patch.points[7] = initialRect.bottomLeft();
    patch.points[8] = initialRect.bottomLeft();
    patch.points[9] = initialRect.bottomRight();
    patch.points[10] = initialRect.bottomRight();
    patch.points[11] = initialRect.bottomRight();

#else
    patch.points[0] = initialRect.topLeft();
    patch.points[1] = initialRect.topLeft() + QPointF(300, 30);
    patch.points[2] = initialRect.topLeft() + QPointF(20, 300);
    patch.points[3] = initialRect.topRight();
    patch.points[4] = initialRect.topRight() + QPointF(-300, 30);
    patch.points[5] = initialRect.topRight() + QPointF(-20, 300);
    patch.points[6] = initialRect.bottomLeft();
    patch.points[7] = initialRect.bottomLeft() + QPointF(300, 30);
    patch.points[8] = initialRect.bottomLeft() + QPointF(20, -300);
    patch.points[9] = initialRect.bottomRight();
    patch.points[10] = initialRect.bottomRight() + QPointF(-300, 30);
    patch.points[11] = initialRect.bottomRight() + QPointF(-20, -300);
#endif

    QPointF local;

    local = calculateLocalPos(patch, QPointF(500,500));
    qDebug() << ppVar(local);

    local = calculateLocalPos(patch, QPointF(0,500));
    qDebug() << ppVar(local);

    local = calculateLocalPos(patch, QPointF(0,1000));
    qDebug() << ppVar(local);

    local = calculateLocalPos(patch, QPointF(1000,1000));
    qDebug() << ppVar(local);
}

QTEST_MAIN(KisMeshTransformWorkerTest)
