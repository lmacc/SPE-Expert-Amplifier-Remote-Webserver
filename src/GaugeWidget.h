#pragma once

#include <QString>
#include <QWidget>

// Round gauge with a colored arc (green→yellow→red), a numeric readout and
// a label underneath. Mirrors the justgage widgets used in the original
// web UI.
class GaugeWidget : public QWidget {
    Q_OBJECT
public:
    explicit GaugeWidget(QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void setRange(double min, double max);
    void setUnit(const QString& unit);
    void setValue(double value);
    void setValueFormat(char format, int precision);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_title;
    QString m_unit;
    double m_min = 0.0;
    double m_max = 1.0;
    double m_value = 0.0;
    char m_format = 'f';
    int m_precision = 1;
};
