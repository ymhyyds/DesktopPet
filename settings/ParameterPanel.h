#pragma once

#include <QWidget>
#include <QVector>
#include <QLabel>
#include <QSlider>

class QVBoxLayout;
class QScrollArea;
class QPushButton;
class Live2DView;

/**
 * @brief ParameterPanel — Live2D 模型参数调节面板（嵌入 SettingsWindow 使用）。
 *
 * 所有有效参数以「名称 + 滑块 + 当前值」一行的形式平铺在可滚动列表中，
 * 无需下拉选择，一眼可见所有参数，直接拖拽滑块实时调节。
 */
class ParameterPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ParameterPanel(QWidget *parent = nullptr);

    void setLive2DView(Live2DView *view);
    void refreshParameterValues();

private:
    void buildUi();
    void rebuildParameterList();
    void addParamRow(const QVector<int> &indices, const QString &displayName);

    struct ParamRow {
        QVector<int> paramIndices;  // 一个或多个模型参数索引（合并参数用）
        QLabel *nameLabel   = nullptr;
        QSlider *slider     = nullptr;
        QLabel *valueLabel  = nullptr;
    };

    Live2DView      *m_live2DView  = nullptr;
    QVector<ParamRow> m_rows;
    QVBoxLayout     *m_rowsLayout  = nullptr;
    QScrollArea     *m_scrollArea  = nullptr;
    QPushButton     *m_resetAllBtn = nullptr;

    static constexpr int kSliderRange = 10000;

    float sliderToValue(int sliderPos, float min, float max) const;
    int   valueToSlider(float value, float min, float max) const;
};
