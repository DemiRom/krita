/*
 *  SPDX-FileCopyrightText: 2022 Dmitry Kazakov <dimula73@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef KISCURVEOPTIONDATA_H
#define KISCURVEOPTIONDATA_H

#include <optional>
#include <boost/operators.hpp>

#include "kis_paintop_option.h"
#include "kis_dynamic_sensor.h"


struct PAINTOP_EXPORT KisSensorData : public boost::equality_comparable<KisSensorData>
{
    KisSensorData(const KoID &sensorId);
    virtual ~KisSensorData();

    inline friend bool operator==(const KisSensorData &lhs, const KisSensorData &rhs) {
        return lhs.id == rhs.id &&
                lhs.curve == rhs.curve &&
                lhs.isActive == rhs.isActive;
    }

    virtual void write(QDomDocument& doc, QDomElement &e) const;
    virtual void read(const QDomElement &e);
    virtual void reset();

    KoID id;
    QString curve;

    // not a part of XML data, managed by the curve option
    bool isActive = false;
};

struct PAINTOP_EXPORT KisSensorWithLengthData : public KisSensorData, public boost::equality_comparable<KisSensorWithLengthData>
{
    KisSensorWithLengthData(const KoID &sensorId, const QLatin1String &lengthTag = {});

    inline friend bool operator==(const KisSensorWithLengthData &lhs, const KisSensorWithLengthData &rhs) {
        return *static_cast<const KisSensorData*>(&lhs) == *static_cast<const KisSensorData*>(&rhs) &&
                lhs.length == rhs.length &&
                lhs.isPeriodic == rhs.isPeriodic &&
                lhs.m_lengthTag == rhs.m_lengthTag;
    }

    void write(QDomDocument& doc, QDomElement &e) const override;
    void read(const QDomElement &e) override;
    void reset() override;

    int length = 30;
    bool isPeriodic = false;
private:
    QLatin1String m_lengthTag;
};

struct PAINTOP_EXPORT KisDrawingAngleSensorData : public KisSensorData, public boost::equality_comparable<KisDrawingAngleSensorData>
{
    KisDrawingAngleSensorData();

    inline friend bool operator==(const KisDrawingAngleSensorData &lhs, const KisDrawingAngleSensorData &rhs) {
        return *static_cast<const KisSensorData*>(&lhs) == *static_cast<const KisSensorData*>(&rhs) &&
                lhs.fanCornersEnabled == rhs.fanCornersEnabled &&
                lhs.fanCornersStep == rhs.fanCornersStep &&
                lhs.angleOffset == rhs.angleOffset &&
                lhs.lockedAngleMode == rhs.lockedAngleMode;
    }

    void write(QDomDocument& doc, QDomElement &e) const override;
    void read(const QDomElement &e) override;
    void reset() override;

    bool fanCornersEnabled = false;
    int fanCornersStep = 30;
    int angleOffset = 0; // in degrees
    bool lockedAngleMode = false;
};



struct PAINTOP_EXPORT KisCurveOptionData : boost::equality_comparable<KisCurveOptionData>
{
    static constexpr bool supports_prefix = true;

    KisCurveOptionData(const QString &prefix,
                       const KoID id,
                       bool isCheckable = true,
                       bool isChecked = false,
                       bool separateCurveValue = false,
                       qreal minValue = 0.0,
                       qreal maxValue = 1.0);

    KisCurveOptionData(const KoID id,
                       bool isCheckable = true,
                       bool isChecked = false,
                       bool separateCurveValue = false,
                       qreal minValue = 0.0,
                       qreal maxValue = 1.0);

    inline friend bool operator==(const KisCurveOptionData &lhs, const KisCurveOptionData &rhs) {
        return lhs.id == rhs.id &&
                lhs.prefix == rhs.prefix &&
                lhs.isCheckable == rhs.isCheckable &&
                lhs.isChecked == rhs.isChecked &&
                lhs.useCurve == rhs.useCurve &&
                lhs.useSameCurve == rhs.useSameCurve &&
                lhs.separateCurveValue == rhs.separateCurveValue &&
                lhs.curveMode == rhs.curveMode &&
                lhs.commonCurve == rhs.commonCurve &&
                lhs.strengthValue == rhs.strengthValue &&
                lhs.strengthMinValue == rhs.strengthMinValue &&
                lhs.strengthMaxValue == rhs.strengthMaxValue &&
                lhs.sensorPressure == rhs.sensorPressure &&
                lhs.sensorPressureIn == rhs.sensorPressureIn &&
                lhs.sensorXTilt == rhs.sensorXTilt &&
                lhs.sensorYTilt == rhs.sensorYTilt &&
                lhs.sensorTiltDirection == rhs.sensorTiltDirection &&
                lhs.sensorTiltElevation == rhs.sensorTiltElevation &&
                lhs.sensorSpeed == rhs.sensorSpeed &&
                lhs.sensorDrawingAngle == rhs.sensorDrawingAngle &&
                lhs.sensorRotation == rhs.sensorRotation &&
                lhs.sensorDistance == rhs.sensorDistance &&
                lhs.sensorTime == rhs.sensorTime &&
                lhs.sensorFuzzyPerDab == rhs.sensorFuzzyPerDab &&
                lhs.sensorFuzzyPerStroke == rhs.sensorFuzzyPerStroke &&
                lhs.sensorFade == rhs.sensorFade &&
                lhs.sensorPerspective == rhs.sensorPerspective &&
                lhs.sensorTangentialPressure == rhs.sensorTangentialPressure;
    }

    KoID id;
    QString prefix;
    bool isCheckable = true;
    bool separateCurveValue = false;
    qreal strengthMinValue = 0.0;
    qreal strengthMaxValue = 1.0;

    bool isChecked = true;
    bool useCurve = true;
    bool useSameCurve = true;

    int curveMode = 0; // TODO: to enum!
    QString commonCurve = DEFAULT_CURVE_STRING;
    qreal strengthValue = 1.0;

    KisSensorData sensorPressure;
    KisSensorData sensorPressureIn;
    KisSensorData sensorXTilt;
    KisSensorData sensorYTilt;
    KisSensorData sensorTiltDirection;
    KisSensorData sensorTiltElevation;
    KisSensorData sensorSpeed;
    KisDrawingAngleSensorData sensorDrawingAngle;
    KisSensorData sensorRotation;
    KisSensorWithLengthData sensorDistance;
    KisSensorWithLengthData sensorTime;
    KisSensorData sensorFuzzyPerDab;
    KisSensorData sensorFuzzyPerStroke;
    KisSensorWithLengthData sensorFade;
    KisSensorData sensorPerspective;
    KisSensorData sensorTangentialPressure;

    std::vector<const KisSensorData *> sensors() const;
    std::vector<KisSensorData*> sensors();

    bool read(const KisPropertiesConfiguration *setting);
    void write(KisPropertiesConfiguration *setting) const;

private:
    bool readPrefixed(const KisPropertiesConfiguration *setting);
    void writePrefixed(KisPropertiesConfiguration *setting) const;
};


#endif // KISCURVEOPTIONDATA_H
