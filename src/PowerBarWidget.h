#pragma once

#include <QWidget>

// Horizontal forward-power bar with band-specific tick marks
// (50 / 250 / 500 / 750 / 1000 / 1300 / 1500 W), matching the original UI.
class PowerBarWidget : public QWidget {
    Q_OBJECT
public:
    explicit PowerBarWidget(QWidget* parent = nullptr);

    void setWatts(double watts);
    void setMaxWatts(double maxWatts);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    double m_watts = 0.0;
    double m_maxWatts = 1500.0;
};
